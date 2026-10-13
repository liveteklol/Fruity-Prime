#include "Sfx.hpp"

#include "../Entities/Players/PlayerEntity.hpp"
#include "../Scene.hpp"

#include "../Features.hpp"
#include "../Formats/Sound.hpp"
#include "../Mods/Headless.hpp"
#include "../Mods/AudioMute.hpp"
#include "../Mods/ThumbnailMode.hpp"
#include "../Mods/Diagnostics/PlatformDiagnostics.hpp"
#include "Music.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../Formats/Types.hpp"

#if defined(__ANDROID__)
#include "../Mods/Sound/AlEs.hpp"
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <span>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <utility>

#if !defined(__ANDROID__)
extern "C"
{
    struct ALCdevice_struct;
    struct ALCcontext_struct;
    using ALCdevice = ALCdevice_struct;
    using ALCcontext = ALCcontext_struct;
    using ALboolean = char;
    using ALchar = char;
    using ALenum = int;
    using ALint = int;
    using ALsizei = int;
    using ALuint = unsigned int;
    using ALfloat = float;

    ALCdevice* alcOpenDevice(const char* devicename);
    ALCcontext* alcCreateContext(ALCdevice* device, const int* attrlist);
    ALboolean alcMakeContextCurrent(ALCcontext* context);
    void alcDestroyContext(ALCcontext* context);
    ALboolean alcCloseDevice(ALCdevice* device);

    void alGenBuffers(ALsizei n, ALuint* buffers);
    void alGenSources(ALsizei n, ALuint* sources);
    void alBufferData(ALuint buffer, ALenum format, const void* data, ALsizei size, ALsizei freq);
    void alBufferiv(ALuint buffer, ALenum param, const ALint* values);
    void alSourcei(ALuint source, ALenum param, ALint value);
    void alSourcef(ALuint source, ALenum param, ALfloat value);
    void alSource3f(ALuint source, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3);
    void alSourcePlay(ALuint source);
    void alSourceStop(ALuint source);
    void alGetSourcei(ALuint source, ALenum param, ALint* value);
    void alListener3f(ALenum param, ALfloat value1, ALfloat value2, ALfloat value3);
    void alListenerfv(ALenum param, const ALfloat* values);
    void alDistanceModel(ALenum distanceModel);
    ALboolean alIsExtensionPresent(const ALchar* extname);
}
#endif

using ::MphRead::NativeRuntime::UncheckedIncrement;
using ::OpenTK::Mathematics::DistanceSquared;
using ::OpenTK::Mathematics::Scale;

namespace
{
    // The audio device is closed on a thread of its own, as upstream's
    // Task.Run did, because closing can block and ShutDown runs between
    // scenes. A process that exits while that thread is still closing kills
    // it mid-close (ExitProcess stops every other thread first), and then
    // OpenAL's own static destructors find a device thread still running and
    // call std::terminate as the DLL unloads. So the close jobs are counted,
    // and the exit waits for them -- briefly: a close that hangs must not
    // keep the process alive.
    struct PendingDeviceCloses final
    {
        std::mutex Mutex;
        std::condition_variable Done;
        std::int32_t Running = 0;
        bool AtExitRegistered = false;
    };

    PendingDeviceCloses& DeviceCloses()
    {
        static PendingDeviceCloses* closes = new PendingDeviceCloses();
        return *closes;
    }

    void WaitForDeviceCloses() noexcept
    {
        try
        {
            PendingDeviceCloses& closes = DeviceCloses();
            std::unique_lock<std::mutex> lock(closes.Mutex);
            closes.Done.wait_for(lock, std::chrono::seconds(3), [&closes] { return closes.Running == 0; });
        }
        catch (...)
        {
        }
    }

    template <typename F>
    void CloseDeviceInBackground(F&& close)
    {
        PendingDeviceCloses& closes = DeviceCloses();
        {
            std::lock_guard<std::mutex> lock(closes.Mutex);
            if (!closes.AtExitRegistered)
            {
                closes.AtExitRegistered = std::atexit(WaitForDeviceCloses) == 0;
            }
            ++closes.Running;
        }
        std::thread([close = std::forward<F>(close)]() mutable
        {
            try
            {
                close();
            }
            catch (...)
            {
            }
            PendingDeviceCloses& closes = DeviceCloses();
            {
                std::lock_guard<std::mutex> lock(closes.Mutex);
                --closes.Running;
            }
            closes.Done.notify_all();
        }).detach();
    }
}

namespace MphRead::Sound
{
    using OpenTK::Mathematics::Vector3;
    using MphRead::Formats::Sound::DgnData;
    using MphRead::Formats::Sound::DgnFile;
    using MphRead::Formats::Sound::DgnFileEntry;
    using MphRead::Formats::Sound::SfxScriptEntry;
    using MphRead::Formats::Sound::SfxScriptFile;
    using MphRead::Formats::Sound::Sound3dEntry;
    using MphRead::Formats::Sound::SoundData;
    using MphRead::Formats::Sound::SoundRead;
    using MphRead::Formats::Sound::SoundSample;
    using MphRead::Formats::Sound::SoundStream;
    using MphRead::Formats::Sound::SoundTable;
    using MphRead::Formats::Sound::SoundTableEntry;
    using MphRead::WaveFormat;

    namespace
    {
        constexpr std::int32_t AlSourceRelative = 0x0202;
        constexpr std::int32_t AlLooping = 0x1007;
        constexpr std::int32_t AlPitch = 0x1003;
        constexpr std::int32_t AlPosition = 0x1004;
        constexpr std::int32_t AlGain = 0x100A;
        constexpr std::int32_t AlBuffer = 0x1009;
        constexpr std::int32_t AlSourceState = 0x1010;
        constexpr std::int32_t AlInitial = 0x1011;
        constexpr std::int32_t AlPlaying = 0x1012;
        constexpr std::int32_t AlReferenceDistance = 0x1020;
        constexpr std::int32_t AlRolloffFactor = 0x1021;
        constexpr std::int32_t AlMaxDistance = 0x1023;
        constexpr std::int32_t AlFormatMono8 = 0x1100;
        constexpr std::int32_t AlFormatMono16 = 0x1101;
        constexpr std::int32_t AlFormatStereo8 = 0x1102;
        constexpr std::int32_t AlFormatStereo16 = 0x1103;
        constexpr std::int32_t AlOrientation = 0x100F;
        constexpr std::int32_t AlLinearDistanceClamped = 0xD004;
        constexpr std::int32_t AlLoopPointsSoft = 0x2015;

        [[noreturn]] void NullReference()
        {
            throw System::NullReferenceException();
        }

        std::shared_ptr<SfxInstanceBase> RequireInstance()
        {
            std::shared_ptr<SfxInstanceBase> instance = Sfx::Instance();
            if (!instance)
            {
                NullReference();
            }
            return instance;
        }

        class Audio final
        {
        public:
            Audio() = delete;

            static std::intptr_t OpenDevice()
            {
#if defined(__ANDROID__)
                auto device = MphRead::Mods::Sound::AlcEs::OpenDevice(std::nullopt);
                return device.Handle;
#else
                return reinterpret_cast<std::intptr_t>(alcOpenDevice(nullptr));
#endif
            }

            static std::intptr_t CreateContext(std::intptr_t device)
            {
#if defined(__ANDROID__)
                OpenTK::Audio::OpenAL::ALDevice native(device);
                OpenTK::Audio::OpenAL::ALContextAttributes attributes;
                return MphRead::Mods::Sound::AlcEs::CreateContext(native, &attributes).Handle;
#else
                return reinterpret_cast<std::intptr_t>(
                    alcCreateContext(reinterpret_cast<ALCdevice*>(device), nullptr));
#endif
            }

            static void MakeContextCurrent(std::intptr_t context)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlcEs::MakeContextCurrent(
                    OpenTK::Audio::OpenAL::ALContext(context));
#else
                alcMakeContextCurrent(reinterpret_cast<ALCcontext*>(context));
#endif
            }

            static void DestroyContext(std::intptr_t context)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlcEs::DestroyContext(
                    OpenTK::Audio::OpenAL::ALContext(context));
#else
                alcDestroyContext(reinterpret_cast<ALCcontext*>(context));
#endif
            }

            static void CloseDevice(std::intptr_t device)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlcEs::CloseDevice(
                    OpenTK::Audio::OpenAL::ALDevice(device));
#else
                alcCloseDevice(reinterpret_cast<ALCdevice*>(device));
#endif
            }

            static bool LoopPointsPresent()
            {
#if defined(__ANDROID__)
                return MphRead::Mods::Sound::AlEs::LoopPoints::IsExtensionPresent();
#else
                return alIsExtensionPresent("AL_SOFT_loop_points") != 0;
#endif
            }

            static void GenBuffers(std::span<std::int32_t> ids)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::GenBuffers(ids);
#else
                std::vector<ALuint> native(ids.size());
                alGenBuffers(static_cast<ALsizei>(native.size()), native.data());
                for (std::size_t i = 0; i < ids.size(); ++i)
                {
                    ids[i] = static_cast<std::int32_t>(native[i]);
                }
#endif
            }

            static std::int32_t GenBuffer()
            {
                std::array<std::int32_t, 1> ids{};
                GenBuffers(ids);
                return ids[0];
            }

            static void GenSources(std::span<std::int32_t> ids)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::GenSources(ids);
#else
                std::vector<ALuint> native(ids.size());
                alGenSources(static_cast<ALsizei>(native.size()), native.data());
                for (std::size_t i = 0; i < ids.size(); ++i)
                {
                    ids[i] = static_cast<std::int32_t>(native[i]);
                }
#endif
            }

            static std::int32_t GenSource()
            {
                std::array<std::int32_t, 1> ids{};
                GenSources(ids);
                return ids[0];
            }

            static void BufferData(std::int32_t buffer, std::int32_t format,
                const std::vector<std::uint8_t>& data, std::int32_t sampleRate)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::BufferData(buffer,
                    static_cast<OpenTK::Audio::OpenAL::ALFormat>(format),
                    std::span<const std::uint8_t>(data), sampleRate);
#else
                alBufferData(static_cast<ALuint>(buffer), format, data.data(),
                    static_cast<ALsizei>(data.size()), sampleRate);
#endif
            }

            static void LoopPoints(std::int32_t buffer, std::int32_t start, std::int32_t end)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::LoopPoints::Buffer(buffer,
                    OpenTK::Audio::OpenAL::BufferLoopPoint::LoopPointsSOFT, start, end);
#else
                const ALint points[2] = { start, end };
                alBufferiv(static_cast<ALuint>(buffer), AlLoopPointsSoft, points);
#endif
            }

            static void SourceBool(std::int32_t source, std::int32_t param, bool value)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::Source(source,
                    static_cast<OpenTK::Audio::OpenAL::ALSourceb>(param), value);
#else
                alSourcei(static_cast<ALuint>(source), param, value ? 1 : 0);
#endif
            }

            static void SourceInt(std::int32_t source, std::int32_t param, std::int32_t value)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::Source(source,
                    static_cast<OpenTK::Audio::OpenAL::ALSourcei>(param), value);
#else
                alSourcei(static_cast<ALuint>(source), param, value);
#endif
            }

            static void SourceFloat(std::int32_t source, std::int32_t param, float value)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::Source(source,
                    static_cast<OpenTK::Audio::OpenAL::ALSourcef>(param), value);
#else
                alSourcef(static_cast<ALuint>(source), param, value);
#endif
            }

            static void SourcePosition(std::int32_t source, Vector3 value)
            {
#if defined(__ANDROID__)
                OpenTK::Audio::OpenAL::ALSource3f param =
                    OpenTK::Audio::OpenAL::ALSource3f::Position;
                MphRead::Mods::Sound::AlEs::Source(source, param, value.X, value.Y, value.Z);
#else
                alSource3f(static_cast<ALuint>(source), AlPosition, value.X, value.Y, value.Z);
#endif
            }

            static void SourcePlay(std::int32_t source)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::SourcePlay(source);
#else
                alSourcePlay(static_cast<ALuint>(source));
#endif
            }

            static void SourceStop(std::int32_t source)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::SourceStop(source);
#else
                alSourceStop(static_cast<ALuint>(source));
#endif
            }

            static std::int32_t GetSourceState(std::int32_t source)
            {
#if defined(__ANDROID__)
                return MphRead::Mods::Sound::AlEs::GetSource(source,
                    OpenTK::Audio::OpenAL::ALGetSourcei::SourceState);
#else
                ALint value = 0;
                alGetSourcei(static_cast<ALuint>(source), AlSourceState, &value);
                return value;
#endif
            }

            static void ListenerPosition(Vector3 value)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::Listener(
                    OpenTK::Audio::OpenAL::ALListener3f::Position, value);
#else
                alListener3f(AlPosition, value.X, value.Y, value.Z);
#endif
            }

            static void ListenerOrientation(Vector3 facing, Vector3 up)
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::Listener(
                    OpenTK::Audio::OpenAL::ALListenerfv::Orientation, facing, up);
#else
                const ALfloat values[6] = { facing.X, facing.Y, facing.Z, up.X, up.Y, up.Z };
                alListenerfv(AlOrientation, values);
#endif
            }

            static void LinearDistanceClamped()
            {
#if defined(__ANDROID__)
                MphRead::Mods::Sound::AlEs::DistanceModel(
                    OpenTK::Audio::OpenAL::ALDistanceModel::LinearDistanceClamped);
#else
                alDistanceModel(AlLinearDistanceClamped);
#endif
            }
        };
    }

    // The current Native Scene surface does not yet expose the renderer-owned
    // camera/player listener data read by Sfx.cs. These three declarations are

    std::shared_ptr<SfxInstanceBase> Sfx::_instance{};
    float Sfx::Volume = 0.35F;
    bool Sfx::SfxMute = false;
    std::int32_t Sfx::ForceFieldSfxMute = 0;
    std::int32_t Sfx::TimedSfxMute = 0;
    std::int32_t Sfx::LongSfxMute = 0;
    std::int32_t SfxInstance::SoundInstance::NextHandle = 0;

    std::array<SfxInstance::EnvironmentItem, 10> SfxInstance::_environmentItems = {
        EnvironmentItem(MphRead::SfxId::ELECTRO_WAVE2),
        EnvironmentItem(MphRead::SfxId::ELECTRICITY),
        EnvironmentItem(MphRead::SfxId::ELECTRIC_BARRIER),
        EnvironmentItem(MphRead::SfxId::ENERGY_BALL),
        EnvironmentItem(MphRead::SfxId::BLUE_FLAME),
        EnvironmentItem(MphRead::SfxId::CYLINDER_BOSS_ATTACK),
        EnvironmentItem(MphRead::SfxId::CYLINDER_BOSS_SPIN),
        EnvironmentItem(MphRead::SfxId::BUBBLES),
        EnvironmentItem(MphRead::SfxId::ELEVATOR2_START),
        EnvironmentItem(MphRead::SfxId::GOREA_ATTACK3_LOOP)
    };

    void SoundSource::Update(Vector3 position, std::int32_t rangeIndex)
    {
        if (rangeIndex == -1)
        {
            Position = RequireInstance()->GetListenerPosition();
            ReferenceDistance = std::numeric_limits<float>::max();
            MaxDistance = std::numeric_limits<float>::max();
            Self = true;
        }
        else
        {
            Position = position;
            const auto& rangeData = RequireInstance()->RangeData();
            if (rangeIndex >= 0 && static_cast<std::size_t>(rangeIndex) < rangeData.size())
            {
                const Sound3dEntry& data = rangeData[static_cast<std::size_t>(rangeIndex)];
                ReferenceDistance = static_cast<float>(data.FalloffDistance) / 4096.0F;
                MaxDistance = static_cast<float>(data.MaxDistance) / 4096.0F;
            }
            Self = false;
        }
    }

    void SoundSource::PlaySfx(MphRead::SfxId id, bool loop, bool noUpdate,
        float recency, bool sourceOnly, bool cancellable, float amountA, float amountB)
    {
        PlaySfx(static_cast<std::int32_t>(id), loop, noUpdate, recency,
            sourceOnly, cancellable, amountA, amountB);
    }

    std::int32_t SoundSource::PlayFreeSfx(MphRead::SfxId id)
    {
        return RequireInstance()->PlayFreeSfx(id);
    }

    void SoundSource::PlaySfx(std::int32_t id, bool loop, bool noUpdate,
        float recency, bool sourceOnly, bool cancellable, float amountA, float amountB)
    {
        if (id >= 0)
        {
            if ((id & 0x8000) != 0)
            {
                RequireInstance()->PlayDgn(id, this, loop, noUpdate, recency,
                    cancellable, amountA, amountB);
            }
            else if ((id & 0x4000) != 0)
            {
                RequireInstance()->PlayScript(id, this, noUpdate, recency,
                    sourceOnly, cancellable);
            }
            else
            {
                RequireInstance()->PlaySample(id, this, loop, noUpdate, recency,
                    sourceOnly, cancellable);
            }
        }
    }

    std::int32_t SoundSource::PlayFreeSfx(std::int32_t id)
    {
        return RequireInstance()->PlayFreeSfx(id);
    }

    void SoundSource::PlayEnvironmentSfx(std::int32_t id)
    {
        RequireInstance()->PlayEnvironmentSfx(id, this);
    }

    bool SoundSource::CheckEnvironmentSfx(std::int32_t id)
    {
        return RequireInstance()->CheckEnvironmentSfx(id);
    }

    void SoundSource::StopAllSfx(bool force)
    {
        RequireInstance()->StopSoundFromSource(this, force);
    }

    void SoundSource::StopSfx(MphRead::SfxId id)
    {
        StopSfx(static_cast<std::int32_t>(id));
    }

    void SoundSource::StopSfx(std::int32_t id)
    {
        RequireInstance()->StopSoundFromSource(this, id);
    }

    void SoundSource::StopFreeSfx(MphRead::SfxId id)
    {
        StopFreeSfx(static_cast<std::int32_t>(id));
    }

    void SoundSource::StopFreeSfx(std::int32_t id)
    {
        RequireInstance()->StopSoundById(id);
    }

    void SoundSource::StopSfxByHandle(std::int32_t handle)
    {
        RequireInstance()->StopSoundByHandle(handle);
    }

    void SoundSource::StopFreeSfxScripts()
    {
        RequireInstance()->StopFreeSfxScripts();
    }

    void SoundSource::SetPausedFreeSfxScripts(bool paused)
    {
        RequireInstance()->SetPausedFreeSfxScripts(paused);
    }

    bool SoundSource::IsHandlePlaying(std::int32_t handle)
    {
        return RequireInstance()->IsHandlePlaying(handle);
    }

    std::int32_t SoundSource::CountPlayingSfx(MphRead::SfxId id)
    {
        return RequireInstance()->CountPlayingSfx(static_cast<std::int32_t>(id));
    }

    std::int32_t SoundSource::CountPlayingSfx(std::int32_t id)
    {
        return RequireInstance()->CountPlayingSfx(id);
    }

    std::int32_t SoundSource::CountSourcePlayingSfx(MphRead::SfxId id)
    {
        return RequireInstance()->CountSourcePlayingSfx(static_cast<std::int32_t>(id), this);
    }

    std::int32_t SoundSource::CountSourcePlayingSfx(std::int32_t id)
    {
        return RequireInstance()->CountSourcePlayingSfx(id, this);
    }

    void SoundSource::QueueStream(MphRead::VoiceId id, float delay, float expiration)
    {
        Sfx::QueueStream(id, delay, expiration);
    }

    std::shared_ptr<SfxInstanceBase> Sfx::Instance()
    {
        return _instance;
    }

    MphRead::SoundCapability Sfx::CheckAudioLoad()
    {
        bool loopPointsSupported = false;
#if defined(__ANDROID__)
        try
        {
#endif
            const std::intptr_t device = Audio::OpenDevice();
            const std::intptr_t context = Audio::CreateContext(device);
            Audio::MakeContextCurrent(context);
            loopPointsSupported = Audio::LoopPointsPresent();
            Audio::MakeContextCurrent(0);
            Audio::DestroyContext(context);
            Audio::CloseDevice(device);
#if defined(__ANDROID__)
        }
        catch (const System::DllNotFoundException& ex)
        {
            MphRead::Mods::Diagnostics::PlatformDiagnostics::Report("libopenal.1.dylib", ex);
            return static_cast<MphRead::SoundCapability>(0);
        }
#endif
        return static_cast<MphRead::SoundCapability>(loopPointsSupported ? 2 : 1);
    }

    float Sfx::CalculatePitchDiv(float pitchFac)
    {
        if (pitchFac == 0.0F)
        {
            pitchFac = 1.0F;
        }
        std::int32_t pitchInt = static_cast<std::int32_t>(pitchFac);
        if (pitchFac <= 0xFFF)
        {
            if (pitchInt == 0)
            {
                throw std::domain_error("Attempted to divide by zero.");
            }
            pitchInt = -((0x600000 / pitchInt) >> 1);
        }
        else if (pitchFac <= 0x1FFF)
        {
            pitchInt = (768 * (pitchInt - 0x2000)) >> 12;
        }
        else
        {
            pitchInt = (768 * (pitchInt - 0x2000)) >> 13;
        }
        const float semitones = static_cast<float>(pitchInt) / 64.0F;
        const float octaves = std::fabs(semitones / 12.0F);
        if (semitones >= 0.0F)
        {
            return std::pow(2.0F, octaves);
        }
        return std::pow(0.5F, octaves);
    }

    void Sfx::Update(float time)
    {
        if (_instance)
        {
            _instance->Update(time);
        }
    }

    void Sfx::QueueStream(MphRead::VoiceId id, float delay, float expiration)
    {
        RequireInstance()->QueueStream(static_cast<std::int32_t>(id), delay, expiration);
    }

    void Sfx::Load(MphRead::Scene& scene)
    {
        if (MphRead::Mods::ThumbnailMode::Active() || MphRead::Mods::Headless::Active()
            || MphRead::Mods::AudioMute::Active())
        {
            _instance = std::make_shared<SfxInstanceBase>();
            SfxMute = false;
            ForceFieldSfxMute = 0;
            TimedSfxMute = 0;
            LongSfxMute = 0;
            return;
        }
        _instance = std::make_shared<SfxInstance>();
        try
        {
            _instance->Load(scene);
        }
        catch (const std::exception& ex)
        {
            MphRead::Mods::Diagnostics::PlatformDiagnostics::Report("libopenal.1.dylib", ex);
            std::cout << "[sound] SFX device unavailable (" << ex.what()
                      << "); continuing without SFX\n";
            _instance = std::make_shared<SfxInstanceBase>();
        }
        SfxMute = false;
        ForceFieldSfxMute = 0;
        TimedSfxMute = 0;
        LongSfxMute = 0;
    }

    void Sfx::ShutDown()
    {
        if (_instance)
        {
            _instance->ShutDown();
            _instance.reset();
        }
    }

    SfxInstanceBase::SfxInstanceBase()
        : _baseRangeData(std::make_shared<std::vector<Sound3dEntry>>())
    {
    }

    const std::vector<Sound3dEntry>& SfxInstanceBase::RangeData() const
    {
        return *_baseRangeData;
    }

    Vector3 SfxInstanceBase::GetListenerPosition() const
    {
        return Vector3(0.0F, 0.0F, 0.0F);
    }

    Vector3 SfxInstanceBase::GetListenerUp() const
    {
        return Vector3(0.0F, 1.0F, 0.0F);
    }

    Vector3 SfxInstanceBase::GetListenerFacing() const
    {
        return Vector3(0.0F, 0.0F, -1.0F);
    }

    void SfxInstanceBase::QueueStream(std::int32_t, float, float) {}
    void SfxInstanceBase::PlayFreeStream(std::int32_t) {}
    void SfxInstanceBase::StopSoundByHandle(std::int32_t) {}
    void SfxInstanceBase::PlayDgn(std::int32_t, SoundSource*, bool, bool, float, bool, float, float) {}
    void SfxInstanceBase::PlayScript(std::int32_t, SoundSource*, bool, float, bool, bool) {}
    std::int32_t SfxInstanceBase::PlaySample(std::int32_t, SoundSource*, std::optional<bool>, bool, float, bool, bool) { return -1; }
    std::int32_t SfxInstanceBase::PlayFreeSfx(MphRead::SfxId) { return -1; }
    std::int32_t SfxInstanceBase::PlayFreeSfx(std::int32_t) { return -1; }
    void SfxInstanceBase::PlayEnvironmentSfx(std::int32_t, SoundSource*) {}
    bool SfxInstanceBase::CheckEnvironmentSfx(std::int32_t) { return false; }
    void SfxInstanceBase::StopSoundFromSource(SoundSource*, bool) {}
    void SfxInstanceBase::StopSoundFromSource(SoundSource*, std::int32_t) {}
    void SfxInstanceBase::StopEnvironmentSfx() {}
    void SfxInstanceBase::StopAllSound(bool) {}
    void SfxInstanceBase::PlayAmbientLoop(const std::vector<std::uint8_t>&, std::int32_t, Vector3, float, float, float) {}
    void SfxInstanceBase::StopAmbientLoops() {}
    void SfxInstanceBase::StopSoundById(std::int32_t) {}
    void SfxInstanceBase::StopFreeSfxScripts() {}
    void SfxInstanceBase::SetPausedFreeSfxScripts(bool) {}
    bool SfxInstanceBase::IsHandlePlaying(std::int32_t) { return false; }
    std::int32_t SfxInstanceBase::CountPlayingSfx(std::int32_t) { return 0; }
    std::int32_t SfxInstanceBase::CountSourcePlayingSfx(std::int32_t, SoundSource*) { return 0; }
    void SfxInstanceBase::Update(float) {}
    void SfxInstanceBase::Load(MphRead::Scene&) {}
    void SfxInstanceBase::ShutDown() {}

    SfxInstance::SoundInstance::SoundInstance()
    {
        Volume.fill(1.0F);
        Pitch.fill(1.0F);
    }

    void SfxInstance::SoundInstance::PlayChannel(std::int32_t index)
    {
        SoundChannel* channel = Channels.at(static_cast<std::size_t>(index));
        const auto& sample = Samples.at(static_cast<std::size_t>(index));
        if (!channel || !sample)
        {
            NullReference();
        }
        Audio::SourceBool(channel->Id, AlLooping, sample->BufferCount == 1 && Loop.at(static_cast<std::size_t>(index)));
        Audio::SourcePlay(channel->Id);
    }

    bool SfxInstance::SoundInstance::IsLooping() const
    {
        for (std::int32_t i = 0; i < MaxPerInst; ++i)
        {
            if (Loop[static_cast<std::size_t>(i)])
            {
                return true;
            }
        }
        return false;
    }

    void SfxInstance::SoundInstance::UpdatePosition()
    {
        if (Source == nullptr || Source->Self)
        {
            const Vector3 sourcePos = RequireInstance()->GetListenerPosition();
            for (std::int32_t i = 0; i < MaxPerInst; ++i)
            {
                SoundChannel* channel = Channels[static_cast<std::size_t>(i)];
                if (!channel)
                {
                    continue;
                }
                Audio::SourcePosition(channel->Id, sourcePos);
                Audio::SourceFloat(channel->Id, AlReferenceDistance, std::numeric_limits<float>::max());
                Audio::SourceFloat(channel->Id, AlMaxDistance, std::numeric_limits<float>::max());
                Audio::SourceFloat(channel->Id, AlRolloffFactor, 1.0F);
            }
        }
        else if (!NoUpdate)
        {
            const Vector3 sourcePos = Source->Position;
            for (std::int32_t i = 0; i < MaxPerInst; ++i)
            {
                SoundChannel* channel = Channels[static_cast<std::size_t>(i)];
                if (!channel)
                {
                    continue;
                }
                Audio::SourcePosition(channel->Id, sourcePos);
                Audio::SourceFloat(channel->Id, AlReferenceDistance, Source->ReferenceDistance);
                Audio::SourceFloat(channel->Id, AlMaxDistance, Source->MaxDistance);
                Audio::SourceFloat(channel->Id, AlRolloffFactor, Source->RolloffFactor);
            }
        }
    }

    void SfxInstance::SoundInstance::UpdateParameters()
    {
        for (std::int32_t i = 0; i < MaxPerInst; ++i)
        {
            SoundChannel* channel = Channels[static_cast<std::size_t>(i)];
            if (!channel)
            {
                continue;
            }
            const auto& sample = Samples[static_cast<std::size_t>(i)];
            if (!sample)
            {
                NullReference();
            }
            const float mute = Sfx::SfxMute && Source != nullptr ? 0.0F : 1.0F;
            const float sourceMute = Source ? Source->Volume : 1.0F;
            Audio::SourceFloat(channel->Id, AlGain,
                Sfx::Volume * Volume[static_cast<std::size_t>(i)] * sample->Volume * mute * sourceMute);
            Audio::SourceFloat(channel->Id, AlPitch, Pitch[static_cast<std::size_t>(i)]);
            Audio::SourceBool(channel->Id, AlSourceRelative, false);
            Audio::SourceFloat(channel->Id, AlRolloffFactor, 1.0F);
        }
    }

    void SfxInstance::SoundInstance::Stop()
    {
        for (std::int32_t i = 0; i < MaxPerInst; ++i)
        {
            SoundChannel*& channel = Channels[static_cast<std::size_t>(i)];
            if (channel)
            {
                channel->Stop();
                channel = nullptr;
            }
            auto& sample = Samples[static_cast<std::size_t>(i)];
            if (sample)
            {
                sample->References--;
                sample.reset();
            }
            Volume[static_cast<std::size_t>(i)] = 1.0F;
            Pitch[static_cast<std::size_t>(i)] = 1.0F;
            Loop[static_cast<std::size_t>(i)] = false;
        }
        Paused = false;
        PlayTime = -1.0F;
        SfxId = -1;
        DgnFile.reset();
        ScriptFile.reset();
        ScriptIndex = -1;
        Source = nullptr;
        NoUpdate = false;
        Cancellable = false;
        Handle = -1;
        Count = 0;
    }

    SfxInstance::SoundChannel::SoundChannel(std::int32_t id) : Id(id) {}

    void SfxInstance::SoundChannel::Stop()
    {
        Audio::SourceStop(Id);
        Audio::SourceInt(Id, AlBuffer, 0);
        InUse = false;
        BufferId = 0;
    }

    SfxInstance::SoundBuffer::SoundBuffer(std::int32_t id) : Id(id) {}

    const std::vector<Sound3dEntry>& SfxInstance::RangeData() const
    {
        if (!_rangeData)
        {
            NullReference();
        }
        return *_rangeData;
    }

    Vector3 SfxInstance::GetListenerPosition() const
    {
        if (!_scene)
        {
            return SfxInstanceBase::GetListenerPosition();
        }
        return _scene->CameraPosition();
    }

    Vector3 SfxInstance::GetListenerUp() const
    {
        if (!_scene)
        {
            return SfxInstanceBase::GetListenerUp();
        }
        if (_scene->CameraMode() == MphRead::CameraMode::Player)
        {
            return Entities::PlayerEntity::Main()->CameraInfo()->TrueUp;
        }
        return _scene->ViewMatrix().Row1().Xyz().Normalized();
    }

    Vector3 SfxInstance::GetListenerFacing() const
    {
        if (!_scene)
        {
            return SfxInstanceBase::GetListenerFacing();
        }
        if (_scene->CameraMode() == MphRead::CameraMode::Player)
        {
            return Entities::PlayerEntity::Main()->CameraInfo()->Facing;
        }
        return _scene->ViewMatrix().Row2().Xyz().Normalized();
    }

    std::int32_t SfxInstance::PlaySample(std::int32_t id, SoundSource* source,
        std::optional<bool> loop, bool noUpdate, float recency, bool sourceOnly, bool cancellable)
    {
        SoundInstance* inst = PlaySampleGetInst(id, source, loop, noUpdate, recency, sourceOnly, cancellable);
        return inst ? inst->Handle : -1;
    }

    std::int32_t SfxInstance::PlayFreeSfx(MphRead::SfxId id)
    {
        return PlayFreeSfx(static_cast<std::int32_t>(id));
    }

    std::int32_t SfxInstance::PlayFreeSfx(std::int32_t id)
    {
        if (id >= 0)
        {
            if ((id & 0x4000) != 0)
            {
                RequireInstance()->PlayScript(id, nullptr, false, -1.0F, false, false);
                return -1;
            }
            return RequireInstance()->PlaySample(id, nullptr, std::nullopt, false, -1.0F, false, false);
        }
        return -1;
    }

    SfxInstance::SoundInstance* SfxInstance::PlaySampleGetInst(std::int32_t id,
        SoundSource* source, std::optional<bool> loop, bool noUpdate, float recency,
        bool sourceOnly, bool cancellable)
    {
        SoundInstance* inst = nullptr;
        const bool setUp = SetUpInstance(id, source, loop.value_or(false), recency,
            sourceOnly, cancellable, inst);
        if (!setUp)
        {
            return nullptr;
        }
        if (!SetUpSample(id, *inst, 0))
        {
            return nullptr;
        }
        inst->Loop[0] = loop.has_value() ? *loop : inst->Samples[0]->Loop;
        StartInstance(*inst, noUpdate);
        return inst;
    }

    void SfxInstance::PlayDgn(std::int32_t id, SoundSource* source, bool loop,
        bool noUpdate, float recency, bool cancellable, float amountA, float amountB)
    {
        const std::int32_t dgnId = id & 0x3FFF;
        SoundInstance* inst = nullptr;
        const bool setUp = SetUpInstance(id, source, loop, recency, true, cancellable, inst);
        if (!setUp)
        {
            UpdateDgn(*inst, amountA, amountB);
            return;
        }
        if (!_dgnFiles)
        {
            NullReference();
        }
        auto dgnFile = _dgnFiles->at(static_cast<std::size_t>(dgnId));
        inst->DgnFile = dgnFile;
        for (std::size_t i = 0; i < dgnFile->Entries->size(); ++i)
        {
            const auto& entry = dgnFile->Entries->at(i);
            if (!SetUpSample(static_cast<std::int32_t>(entry->SfxId), *inst, static_cast<std::int32_t>(i)))
            {
                inst->Stop();
                return;
            }
            inst->Loop[i] = loop;
        }
        UpdateDgn(*inst, amountA, amountB);
        for (std::int32_t i = 0; i < inst->Count; ++i)
        {
            if (inst->Volume[static_cast<std::size_t>(i)] > 0.0F)
            {
                StartInstance(*inst, noUpdate);
                return;
            }
        }
        inst->Stop();
    }

    void SfxInstance::PlayScript(std::int32_t id, SoundSource* source, bool noUpdate,
        float recency, bool sourceOnly, bool cancellable)
    {
        const std::int32_t scriptId = id & 0x3FFF;
        if (!_sfxScripts)
        {
            NullReference();
        }
        auto script = _sfxScripts->at(static_cast<std::size_t>(scriptId));
        if (script->Entries->empty())
        {
            return;
        }
        SoundInstance* inst = nullptr;
        if (!SetUpInstance(id, source, false, recency, sourceOnly, cancellable, inst))
        {
            return;
        }
        inst->NoUpdate = noUpdate;
        inst->ScriptFile = script;
    }

    bool SfxInstance::SetUpInstance(std::int32_t id, SoundSource* source, bool loop,
        float recency, bool sourceOnly, bool cancellable, SoundInstance*& inst)
    {
        if (loop)
        {
            recency = std::numeric_limits<float>::max();
            sourceOnly = true;
        }
        if (recency >= 0.0F)
        {
            SoundInstance* recent = FindRecentSamplePlay(id, recency, sourceOnly ? source : nullptr);
            if (recent)
            {
                inst = recent;
                return false;
            }
        }
        inst = &FindInstance(source);
        inst->Source = source;
        inst->Paused = false;
        inst->PlayTime = 0.0F;
        inst->Cancellable = cancellable;
        inst->SfxId = id;
        inst->Handle = SoundInstance::NextHandle;
        SoundInstance::NextHandle = UncheckedIncrement(SoundInstance::NextHandle);
        return true;
    }

    bool SfxInstance::SetUpSample(std::int32_t id, SoundInstance& inst, std::int32_t index)
    {
        if (!_samples)
        {
            NullReference();
        }
        auto sample = _samples->at(static_cast<std::size_t>(id));
        auto& prevSample = inst.Samples.at(static_cast<std::size_t>(index));
        if (prevSample != sample)
        {
            if (prevSample)
            {
                prevSample->References--;
            }
            prevSample = sample;
            sample->References++;
        }
        SoundChannel*& channel = inst.Channels.at(static_cast<std::size_t>(index));
        if (!channel)
        {
            channel = GetChannel(sample->BufferId);
            if (!channel)
            {
                return false;
            }
            inst.Count++;
        }
        channel->InUse = true;
        if (sample->BufferId == 0)
        {
            BufferData(sample);
        }
        else
        {
            sample->BufferCount = sample->MaxBuffers;
        }
        if (channel->BufferId != sample->BufferId)
        {
            Audio::SourceInt(channel->Id, AlBuffer, sample->BufferId);
            channel->BufferId = sample->BufferId;
        }
        return true;
    }

    void SfxInstance::UpdateInstance(SoundInstance& inst, bool noUpdate)
    {
        inst.NoUpdate = false;
        inst.UpdatePosition();
        inst.NoUpdate = noUpdate;
        inst.UpdateParameters();
    }

    void SfxInstance::StartInstance(SoundInstance& inst, bool noUpdate)
    {
        UpdateInstance(inst, noUpdate);
        for (std::int32_t i = 0; i < inst.Count; ++i)
        {
            inst.PlayChannel(i);
        }
    }

    SfxInstance::SoundInstance* SfxInstance::FindRecentSamplePlay(
        std::int32_t id, float recency, SoundSource* source)
    {
        for (auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            SoundInstance& inst = *holder;
            if ((source == nullptr || inst.Source == source) && inst.SfxId == id && inst.PlayTime <= recency)
            {
                return &inst;
            }
        }
        return nullptr;
    }

    void SfxInstance::UpdateDgn(SoundInstance& inst, float amountA, float amountB)
    {
        if (!inst.DgnFile)
        {
            NullReference();
        }
        for (std::int32_t i = 0; i < inst.Count; ++i)
        {
            const auto& entry = inst.DgnFile->Entries->at(static_cast<std::size_t>(i));
            const float volumeA = GetDgnValue(*entry->Data1, amountA);
            const float volumeB = GetDgnValue(*entry->Data2, amountB);
            const float pitchA = GetDgnValue(*entry->Data3, amountA);
            const float pitchB = GetDgnValue(*entry->Data4, amountB);
            float volumeFac = volumeA / 127.0F * volumeB;
            volumeFac = volumeFac / 127.0F * static_cast<float>(inst.DgnFile->Header.InitialVolume) / 127.0F;
            if (volumeFac < 1.0F / 130.0F)
            {
                volumeFac = 0.0F;
            }
            inst.Volume[static_cast<std::size_t>(i)] = volumeFac;
            float pitchFac = pitchA / 0x2000 * pitchB;
            if (pitchFac >= 0x4000)
            {
                pitchFac = 0x3FFF;
            }
            inst.Pitch[static_cast<std::size_t>(i)] = Sfx::CalculatePitchDiv(pitchFac);
        }
    }

    float SfxInstance::GetDgnValue(const std::vector<DgnData>& data, float amount) const
    {
        const DgnData& first = data.at(0);
        if (amount <= first.Amount)
        {
            return static_cast<float>(first.Value & 0x3FFF);
        }
        const DgnData& last = data.at(data.size() - 1);
        if (amount >= last.Amount)
        {
            return static_cast<float>(last.Value & 0x3FFF);
        }
        if (data.size() == 1)
        {
            return 0.0F;
        }
        std::size_t i = 0;
        while (true)
        {
            const DgnData& data1 = data.at(i);
            const DgnData& data2 = data.at(i + 1);
            if (amount < data2.Amount)
            {
                const float diff = amount - data1.Amount;
                const float ratio = diff / static_cast<float>(data2.Amount - data1.Amount);
                const float value1 = static_cast<float>(data1.Value & 0x3FFF);
                const float value2 = static_cast<float>(data2.Value & 0x3FFF);
                const std::uint16_t flags2 = data2.Value & 0xC000;
                if (flags2 == 0x4000)
                {
                    return value1 + (value2 - value1) *
                        std::sin(static_cast<float>(3.14159265358979323846 / 2.0) * ratio);
                }
                if (flags2 == 0x8000)
                {
                    return 0.0F;
                }
                return value1 + (value2 - value1) * ratio;
            }
            if (++i >= data.size() - 1)
            {
                return 0.0F;
            }
        }
    }

    void SfxInstance::StopAllSound(bool force)
    {
        if (force)
        {
            StopAmbientLoops();
        }
        for (auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            SoundInstance& inst = *holder;
            if (inst.SfxId != -1 && (force || inst.Source != nullptr || inst.IsLooping()))
            {
                inst.Stop();
            }
        }
        Audio::SourceStop(_streamInstance);
    }

    void SfxInstance::StopSoundFromSource(SoundSource* source, bool force)
    {
        for (auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            SoundInstance& inst = *holder;
            if (inst.Source == source)
            {
                bool loop = false;
                for (std::int32_t j = 0; j < MaxPerInst; ++j)
                {
                    if (inst.Loop[static_cast<std::size_t>(j)])
                    {
                        loop = true;
                        break;
                    }
                }
                if ((force || loop || inst.Cancellable) && (!force || !inst.NoUpdate))
                {
                    inst.Stop();
                }
            }
        }
    }

    void SfxInstance::StopSoundFromSource(SoundSource* source, std::int32_t id)
    {
        for (auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            SoundInstance& inst = *holder;
            if (inst.Source == source && inst.SfxId == id)
            {
                inst.Stop();
            }
        }
    }

    void SfxInstance::StopSoundById(std::int32_t id)
    {
        for (auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            if (holder->SfxId == id)
            {
                holder->Stop();
            }
        }
    }

    void SfxInstance::StopSoundByHandle(std::int32_t handle)
    {
        if (handle >= 0)
        {
            for (auto& holder : _instances)
            {
                if (!holder)
                {
                    NullReference();
                }
                if (holder->Handle == handle)
                {
                    holder->Stop();
                }
            }
        }
    }

    void SfxInstance::StopFreeSfxScripts()
    {
        for (auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            if (holder->ScriptFile)
            {
                holder->Stop();
            }
        }
    }

    void SfxInstance::SetPausedFreeSfxScripts(bool paused)
    {
        for (auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            if (holder->ScriptFile)
            {
                holder->Paused = paused;
            }
        }
    }

    bool SfxInstance::IsHandlePlaying(std::int32_t handle)
    {
        if (handle >= 0)
        {
            for (const auto& holder : _instances)
            {
                if (!holder)
                {
                    NullReference();
                }
                if (holder->Handle == handle)
                {
                    return true;
                }
            }
        }
        return false;
    }

    SfxInstance::SoundInstance& SfxInstance::FindInstance(SoundSource* source)
    {
        float maxTime = 0.0F;
        SoundInstance* resultBySource = nullptr;
        SoundInstance* anyResult = nullptr;
        for (auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            SoundInstance& inst = *holder;
            if (inst.PlayTime < 0.0F)
            {
                return inst;
            }
            if (inst.PlayTime >= maxTime)
            {
                if (source != nullptr && inst.Source == source)
                {
                    resultBySource = &inst;
                }
                anyResult = &inst;
                maxTime = inst.PlayTime;
            }
        }
        if (resultBySource)
        {
            return *resultBySource;
        }
        if (!anyResult)
        {
            NullReference();
        }
        return *anyResult;
    }

    SfxInstance::SoundChannel* SfxInstance::GetChannel(std::int32_t bufferId)
    {
        if (bufferId > 0)
        {
            for (auto& holder : _channels)
            {
                if (!holder)
                {
                    NullReference();
                }
                if (holder->BufferId == bufferId && !holder->InUse)
                {
                    return holder.get();
                }
            }
        }
        for (auto& holder : _channels)
        {
            if (!holder)
            {
                NullReference();
            }
            if (!holder->InUse)
            {
                return holder.get();
            }
        }
        return nullptr;
    }

    void SfxInstance::BufferData(const std::shared_ptr<SoundSample>& sample)
    {
        SoundBuffer* dest = nullptr;
        auto doBuffer = [&]()
        {
            if (!dest)
            {
                NullReference();
            }
            if (dest->Sample)
            {
                dest->Sample->BufferId = 0;
            }
            dest->Sample = sample;
            const std::int32_t format = sample->Format == WaveFormat::ADPCM ? AlFormatMono16 : AlFormatMono8;
            auto data = sample->WaveData->Value();
            if (!data)
            {
                NullReference();
            }
            Audio::BufferData(dest->Id, format, *data, sample->SampleRate);
            if (_loopPointSupport)
            {
                Audio::LoopPoints(dest->Id, sample->LoopStart, sample->LoopStart + sample->LoopLength);
            }
            sample->BufferCount = sample->MaxBuffers = 1;
            sample->BufferId = dest->Id;
        };

        for (auto& holder : _buffers)
        {
            if (!holder)
            {
                NullReference();
            }
            if (!holder->Sample)
            {
                dest = holder.get();
                break;
            }
        }
        if (dest)
        {
            doBuffer();
            return;
        }
        for (auto& holder : _buffers)
        {
            if (holder->Sample && holder->Sample->References == 0)
            {
                dest = holder.get();
                break;
            }
        }
        if (dest)
        {
            doBuffer();
            return;
        }

        dest = _buffers[0].get();
        if (!dest || !dest->Sample)
        {
            NullReference();
        }
        for (auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            for (std::int32_t j = 0; j < MaxPerInst; ++j)
            {
                if (holder->Samples[static_cast<std::size_t>(j)] == dest->Sample)
                {
                    holder->Stop();
                    break;
                }
            }
        }
        doBuffer();
    }

    void SfxInstance::UpdateScript(SoundInstance& inst, float time)
    {
        if (!inst.ScriptFile)
        {
            NullReference();
        }
        if (inst.Paused)
        {
            return;
        }
        if (inst.PlayTime >= 0.0F)
        {
            inst.PlayTime += time;
        }
        else
        {
            inst.PlayTime = 0.0F;
        }
        std::int32_t playingCount = 0;
        for (std::int32_t i = 0; i < MaxPerInst; ++i)
        {
            SoundChannel*& channel = inst.Channels[static_cast<std::size_t>(i)];
            if (channel)
            {
                if (Audio::GetSourceState(channel->Id) == AlPlaying)
                {
                    playingCount++;
                }
                else
                {
                    channel->Stop();
                    channel = nullptr;
                    auto& sample = inst.Samples[static_cast<std::size_t>(i)];
                    if (!sample)
                    {
                        NullReference();
                    }
                    sample->References--;
                    sample.reset();
                    inst.Loop[static_cast<std::size_t>(i)] = false;
                }
            }
        }
        if (inst.ScriptIndex >= static_cast<std::int32_t>(inst.ScriptFile->Entries->size()) - 1)
        {
            if (playingCount == 0)
            {
                inst.Stop();
                return;
            }
        }
        for (std::int32_t i = inst.ScriptIndex + 1;
             i < static_cast<std::int32_t>(inst.ScriptFile->Entries->size()); ++i)
        {
            const auto& entry = inst.ScriptFile->Entries->at(static_cast<std::size_t>(i));
            if (entry->Delay > inst.PlayTime)
            {
                break;
            }
            inst.ScriptIndex = i;
            const std::int32_t sfxId = entry->SfxData & 0x3FFF;
            if ((entry->SfxData & 0x8000) != 0)
            {
                if (!inst.Source)
                {
                    StopSoundById(sfxId);
                }
                else
                {
                    StopSoundFromSource(inst.Source, sfxId);
                }
                continue;
            }
            std::int32_t index = -1;
            for (std::int32_t j = 0; j < MaxPerInst; ++j)
            {
                if (!inst.Channels[static_cast<std::size_t>(j)])
                {
                    index = j;
                    break;
                }
            }
            if (index == -1)
            {
                inst.Stop();
                return;
            }
            inst.Volume[static_cast<std::size_t>(index)] = entry->Volume;
            inst.Pitch[static_cast<std::size_t>(index)] = entry->Pitch;
            if (!SetUpSample(sfxId, inst, index))
            {
                inst.Stop();
                return;
            }
            UpdateInstance(inst, inst.NoUpdate);
            if (entry->Pan > -1.0F)
            {
                SoundChannel* channel = inst.Channels[static_cast<std::size_t>(index)];
                Audio::SourceBool(channel->Id, AlSourceRelative, true);
                Audio::SourceFloat(channel->Id, AlRolloffFactor, 0.0F);
                Vector3 position(0.0F, 0.0F, 0.0F);
                if (std::fabs(entry->Pan) > 1.0F / 128.0F)
                {
                    position = Vector3(entry->Pan, 0.0F,
                        -std::sqrt(1.0F - entry->Pan * entry->Pan));
                }
                Audio::SourcePosition(channel->Id, position);
            }
            inst.Loop[static_cast<std::size_t>(index)] = (entry->SfxData & 0x4000) != 0;
            inst.PlayChannel(index);
        }
    }

    void SfxInstance::Update(float time)
    {
        const Vector3 listenerPos = GetListenerPosition();
        const Vector3 listenerUp = GetListenerUp();
        const Vector3 listenerFacing = GetListenerFacing();
        Audio::ListenerPosition(listenerPos);
        Audio::ListenerOrientation(listenerFacing, listenerUp);
        Audio::SourcePosition(_streamInstance, listenerPos);
        Audio::SourceFloat(_streamInstance, AlReferenceDistance, std::numeric_limits<float>::max());
        Audio::SourceFloat(_streamInstance, AlMaxDistance, std::numeric_limits<float>::max());
        Audio::SourceFloat(_streamInstance, AlRolloffFactor, 1.0F);
        for (const AmbientLoop& loop : _ambient)
        {
            if (loop.Playing)
            {
                Audio::SourceFloat(loop.Source, AlGain, Sfx::SfxMute ? 0.0F : Sfx::Volume * loop.Volume);
            }
        }
        for (auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            SoundInstance& inst = *holder;
            if (inst.PlayTime >= 0.0F)
            {
                if (inst.ScriptFile)
                {
                    if (inst.Source == nullptr || !Sfx::SfxMute)
                    {
                        UpdateScript(inst, time);
                    }
                    continue;
                }
                bool playing = false;
                for (std::int32_t j = 0; j < inst.Count; ++j)
                {
                    SoundChannel* channel = inst.Channels[static_cast<std::size_t>(j)];
                    if (!channel)
                    {
                        NullReference();
                    }
                    if (Audio::GetSourceState(channel->Id) == AlPlaying)
                    {
                        if (inst.Volume[static_cast<std::size_t>(j)] == 0.0F)
                        {
                            Audio::SourceStop(channel->Id);
                            continue;
                        }
                        playing = true;
                        break;
                    }
                }
                if (playing)
                {
                    inst.PlayTime += time;
                    inst.UpdatePosition();
                    inst.UpdateParameters();
                }
                else
                {
                    inst.Stop();
                }
            }
        }
        if (Sfx::LongSfxMute == 0)
        {
            UpdateEnvironmentSfx();
        }
        UpdateStreams(time);
    }

    std::int32_t SfxInstance::CountPlayingSfx(std::int32_t id)
    {
        return CountPlayingSfx(id, nullptr);
    }

    std::int32_t SfxInstance::CountSourcePlayingSfx(std::int32_t id, SoundSource* source)
    {
        return CountPlayingSfx(id, source);
    }

    std::int32_t SfxInstance::CountPlayingSfx(std::int32_t id, SoundSource* source) const
    {
        std::int32_t count = 0;
        for (const auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            if (holder->Handle != -1 && holder->SfxId == id && (source == nullptr || holder->Source == source))
            {
                count++;
            }
        }
        return count;
    }

    SfxInstance::EnvironmentItem::EnvironmentItem(MphRead::SfxId sfxId)
        : SfxId(static_cast<std::int32_t>(sfxId))
    {
    }

    void SfxInstance::EnvironmentItem::Reset()
    {
        if (Handle != -1)
        {
            RequireInstance()->StopSoundByHandle(Handle);
        }
        Handle = -1;
        Instances = 0;
        DistanceSquared = std::numeric_limits<float>::max();
    }

    void SfxInstance::PlayEnvironmentSfx(std::int32_t index, SoundSource* source)
    {
        EnvironmentItem& item = _environmentItems.at(static_cast<std::size_t>(index));
        if (!source)
        {
            NullReference();
        }
        const Vector3 listenerPos = GetListenerPosition();
        const Vector3 listenerFacing = GetListenerFacing();
        const float distSqr = DistanceSquared(source->Position, listenerPos);
        if (distSqr < item.DistanceSquared)
        {
            item.DistanceSquared = distSqr;
            const float dist = std::sqrt(distSqr);
            item.Source.Position = listenerPos + Scale(listenerFacing, dist);
            item.Source.ReferenceDistance = source->ReferenceDistance;
            item.Source.MaxDistance = source->MaxDistance;
            item.Source.RolloffFactor = source->RolloffFactor;
        }
        item.Instances++;
    }

    bool SfxInstance::CheckEnvironmentSfx(std::int32_t index)
    {
        return _environmentItems.at(static_cast<std::size_t>(index)).Instances > 0;
    }

    void SfxInstance::UpdateEnvironmentSfx()
    {
        for (EnvironmentItem& item : _environmentItems)
        {
            if (item.Instances > 0)
            {
                SoundInstance* inst = PlaySampleGetInst(item.SfxId, &item.Source, true,
                    false, -1.0F, false, false);
                if (inst)
                {
                    item.Handle = inst->Handle;
                }
            }
            else if (item.Handle != -1)
            {
                StopSoundByHandle(item.Handle);
                item.Handle = -1;
            }
            item.Instances = 0;
            item.DistanceSquared = std::numeric_limits<float>::max();
        }
    }

    void SfxInstance::StopEnvironmentSfx()
    {
        for (EnvironmentItem& item : _environmentItems)
        {
            if (item.Instances > 0)
            {
                item.Reset();
            }
        }
    }

    void SfxInstance::QueueStream(std::int32_t id, float delay, float expiration)
    {
        if (_inactiveQueue.empty())
        {
            return;
        }
        if (!_soundData)
        {
            NullReference();
        }
        auto item = _inactiveQueue.front();
        _inactiveQueue.pop_front();
        item->Stream = _soundData->Streams->at(static_cast<std::size_t>(id));
        item->DelayTimer = delay;
        item->ExpirationTimer = expiration;
        item->Playing = false;
        _activeQueue.push_back(item);
    }

    void SfxInstance::PlayFreeStream(std::int32_t id)
    {
        while (!_activeQueue.empty())
        {
            auto item = _activeQueue.front();
            _activeQueue.pop_front();
            _inactiveQueue.push_back(item);
        }
        QueueStream(id, 0.0F, 0.0F);
        UpdateStreams(0.0F);
    }

    void SfxInstance::UpdateStreams(float time)
    {
        std::int32_t index = 0;
        for (auto it = _activeQueue.begin(); it != _activeQueue.end(); )
        {
            auto next = std::next(it);
            auto item = *it;
            if (!item->Stream)
            {
                NullReference();
            }
            if (index == 0 && item->Playing)
            {
                const std::int32_t state = Audio::GetSourceState(_streamInstance);
                if (state != AlInitial && state != AlPlaying)
                {
                    Audio::SourceStop(_streamInstance);
                    Audio::SourceInt(_streamInstance, AlBuffer, 0);
                    item->Stream.reset();
                    _activeQueue.erase(it);
                    _inactiveQueue.push_back(item);
                    it = next;
                    index++;
                    continue;
                }
            }
            else if (item->DelayTimer > 0.0F)
            {
                item->DelayTimer -= time;
                if (item->DelayTimer <= 0.0F)
                {
                    item->DelayTimer = 0.0F;
                }
            }
            if (item->DelayTimer == 0.0F && !item->Playing && index == 0)
            {
                std::int32_t format = 0;
                if (item->Stream->Format == WaveFormat::ADPCM)
                {
                    format = item->Stream->Channels->size() == 2 ? AlFormatStereo16 : AlFormatMono16;
                }
                else
                {
                    format = item->Stream->Channels->size() == 2 ? AlFormatStereo8 : AlFormatMono8;
                }
                Audio::SourceStop(_streamInstance);
                Audio::SourceInt(_streamInstance, AlBuffer, 0);
                auto data = item->Stream->BufferData->Value();
                if (!data)
                {
                    NullReference();
                }
                Audio::BufferData(_streamBuffer, format, *data, item->Stream->SampleRate);
                Audio::SourceInt(_streamInstance, AlBuffer, _streamBuffer);
                Audio::SourceBool(_streamInstance, AlLooping, item->Stream->Loop);
                Audio::SourceFloat(_streamInstance, AlGain, Sfx::Volume * item->Stream->Volume);
                Audio::SourcePlay(_streamInstance);
                item->Playing = true;
                it = next;
                index++;
                continue;
            }
            if (index > 0 && item->ExpirationTimer > 0.0F)
            {
                item->ExpirationTimer -= time;
                if (item->ExpirationTimer <= 0.0F)
                {
                    item->ExpirationTimer = 0.0F;
                    _activeQueue.erase(it);
                    _inactiveQueue.push_back(item);
                }
            }
            it = next;
            index++;
        }
    }

    void SfxInstance::Load(MphRead::Scene& scene)
    {
        _scene = &scene;
        _samples = SoundRead::ReadSoundSamples();
        std::shared_ptr<SoundTable> table = SoundRead::ReadSoundTables();
        if (!_samples || !table || !table->Entries)
        {
            NullReference();
        }
        for (std::size_t i = 0; i < _samples->size(); ++i)
        {
            auto sample = _samples->at(i);
            auto entry = table->Entries->at(i);
            sample->Volume = static_cast<float>(entry->InitialVolume) / 127.0F;
            sample->Name = entry->Name;
            (void)sample->WaveData->Value();
        }
        _rangeData = SoundRead::ReadSound3dList();
        _dgnFiles = SoundRead::ReadDgnFiles();
        _sfxScripts = SoundRead::ReadSfxScriptFiles();
        _soundData = SoundRead::ReadSdat();
        if (!_soundData || !_soundData->Streams)
        {
            NullReference();
        }
        for (const auto& stream : *_soundData->Streams)
        {
            (void)stream->BufferData->Value();
        }
        for (std::int32_t i = 0; i < 16; ++i)
        {
            _inactiveQueue.push_back(std::make_shared<QueueItem>());
        }
        _device = Audio::OpenDevice();
        _context = Audio::CreateContext(_device);
        Audio::MakeContextCurrent(_context);
        _loopPointSupport = Audio::LoopPointsPresent();
        std::array<std::int32_t, 128> bufferIds{};
        Audio::GenBuffers(bufferIds);
        for (std::size_t i = 0; i < _buffers.size(); ++i)
        {
            _buffers[i] = std::make_unique<SoundBuffer>(bufferIds[i * 2]);
        }
        std::array<std::int32_t, 128> channelIds{};
        Audio::GenSources(channelIds);
        for (std::size_t i = 0; i < _channels.size(); ++i)
        {
            _channels[i] = std::make_unique<SoundChannel>(channelIds[i]);
        }
        for (auto& instance : _instances)
        {
            instance = std::make_unique<SoundInstance>();
        }
        _streamBuffer = Audio::GenBuffer();
        _streamInstance = Audio::GenSource();
        if (!MphRead::Features::LogSpatialAudio())
        {
            Audio::LinearDistanceClamped();
        }
    }

    void SfxInstance::PlayAmbientLoop(const std::vector<std::uint8_t>& samples, std::int32_t sampleRate,
        Vector3 position, float volume, float reference, float maximum)
    {
        if (_context == 0 || samples.empty() || sampleRate <= 0)
        {
            return;
        }
        AmbientLoop* loop = nullptr;
        for (AmbientLoop& existing : _ambient)
        {
            if (!existing.Playing)
            {
                loop = &existing;
                break;
            }
        }
        if (loop == nullptr)
        {
            // Quake's levels have dozens; more than this many at once is
            // more than anyone hears.
            if (_ambient.size() >= 64)
            {
                return;
            }
            _ambient.push_back(AmbientLoop{Audio::GenBuffer(), Audio::GenSource(), 0.0F, false});
            loop = &_ambient.back();
        }
        Audio::SourceStop(loop->Source);
        Audio::SourceInt(loop->Source, AlBuffer, 0);
        Audio::BufferData(loop->Buffer, AlFormatMono16, samples, sampleRate);
        Audio::SourceInt(loop->Source, AlBuffer, loop->Buffer);
        Audio::SourceBool(loop->Source, AlSourceRelative, false);
        Audio::SourceBool(loop->Source, AlLooping, true);
        Audio::SourcePosition(loop->Source, position);
        Audio::SourceFloat(loop->Source, AlReferenceDistance, reference);
        Audio::SourceFloat(loop->Source, AlMaxDistance, maximum);
        Audio::SourceFloat(loop->Source, AlRolloffFactor, 1.0F);
        Audio::SourceFloat(loop->Source, AlPitch, 1.0F);
        Audio::SourceFloat(loop->Source, AlGain, Sfx::SfxMute ? 0.0F : Sfx::Volume * volume);
        Audio::SourcePlay(loop->Source);
        loop->Volume = volume;
        loop->Playing = true;
    }

    void SfxInstance::StopAmbientLoops()
    {
        for (AmbientLoop& loop : _ambient)
        {
            if (loop.Playing)
            {
                Audio::SourceStop(loop.Source);
                loop.Playing = false;
            }
        }
    }

    void SfxInstance::ShutDown()
    {
        StopAmbientLoops();
        MphRead::MusicPlayer::Remove(true);
        for (auto& holder : _instances)
        {
            if (!holder)
            {
                NullReference();
            }
            SoundInstance& inst = *holder;
            for (std::int32_t j = 0; j < MaxPerInst; ++j)
            {
                SoundChannel* channel = inst.Channels[static_cast<std::size_t>(j)];
                if (!channel)
                {
                    continue;
                }
                if (Audio::GetSourceState(channel->Id) == AlPlaying)
                {
                    inst.Stop();
                    break;
                }
            }
        }
        for (EnvironmentItem& item : _environmentItems)
        {
            item.Reset();
        }
        Audio::SourceStop(_streamInstance);
        Audio::MakeContextCurrent(0);
        std::shared_ptr<SfxInstance> self = shared_from_this();
        CloseDeviceInBackground([self]()
        {
            Audio::DestroyContext(self->_context);
            Audio::CloseDevice(self->_device);
            self->_context = 0;
            self->_device = 0;
        });
        _scene = nullptr;
    }
}
