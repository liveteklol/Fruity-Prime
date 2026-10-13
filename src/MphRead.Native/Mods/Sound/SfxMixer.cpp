#include "SfxMixer.hpp"
#include "../../Formats/Types.hpp"
#include "../../NativeRuntime/System/Managed.hpp"
#include "../../NativeRuntime/OpenTK/Mathematics.hpp"

using ::MphRead::NativeRuntime::MathClamp;
using ::MphRead::NativeRuntime::MathMax;
using ::MphRead::NativeRuntime::MathMin;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedIncrement;
using ::MphRead::NativeRuntime::UncheckedSubtract;
using ::OpenTK::Mathematics::Divide;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::Subtract;

#if defined(__ANDROID__)
#include "../../Sound/Music.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>




namespace MphRead::Mods::Sound
{
    using OpenTK::Audio::OpenAL::ALFormat;
    using OpenTK::Audio::OpenAL::ALGetSourcei;
    using OpenTK::Audio::OpenAL::ALListener3f;
    using OpenTK::Audio::OpenAL::ALSource3f;
    using OpenTK::Audio::OpenAL::ALSourceb;
    using OpenTK::Audio::OpenAL::ALSourcef;
    using OpenTK::Audio::OpenAL::ALSourcei;
    using OpenTK::Mathematics::Vector3;
    using SoundFlow::Components::SoundPlayer;
    using SoundFlow::Enums::SampleFormat;
    using SoundFlow::Providers::RawDataProvider;
    using SoundFlow::Structs::AudioFormat;

    namespace
    {
        enum class SourceState : std::int32_t
        {
            Initial = 0x1011,
            Playing = 0x1012,
            Paused = 0x1013,
            Stopped = 0x1014
        };

        std::int32_t DotNetDoubleToInt32(double value)
        {
            if (std::isnan(value) || value >= 2147483648.0 || value < -2147483648.0)
            {
                return std::numeric_limits<std::int32_t>::min();
            }
            return static_cast<std::int32_t>(value);
        }

    }

    struct SfxMixer::Buffer
    {
        std::vector<float> Data;
        std::int32_t Channels = 1;
        std::int32_t Frames = 0;
        std::int32_t SampleRate = 22050;
        std::int32_t LoopStart = -1;
        std::int32_t LoopEnd = -1;
    };

    struct SfxMixer::Voice
    {
        std::vector<std::int32_t> Queue;
        std::int32_t Index = 0;
        std::int32_t Processed = 0;
        double Cursor = 0.0;
        SourceState State = SourceState::Initial;
        float Gain = 1.0F;
        float Pitch = 1.0F;
        bool Looping = false;
        bool Relative = false;
        Vector3 Position;
        float ReferenceDistance = 1.0F;
        float MaxDistance = std::numeric_limits<float>::max();
        float RolloffFactor = 1.0F;

        void Reset()
        {
            Queue.clear();
            Index = 0;
            Processed = 0;
            Cursor = 0.0;
            State = SourceState::Initial;
        }
    };

    struct SfxMixer::VoiceEntry
    {
        std::int32_t Key = 0;
        std::shared_ptr<Voice> Value;
        std::ptrdiff_t NextFree = -1;
        bool Occupied = false;
    };

    class SfxMixer::FloatSpan
    {
    public:
        FloatSpan(std::uint8_t* data, std::size_t size) noexcept
            : _data(data), _size(size)
        {
        }

        std::size_t Size() const noexcept
        {
            return _size;
        }

        void Clear() const noexcept
        {
            for (std::size_t i = 0; i < _size; i++)
            {
                Set(i, 0.0F);
            }
        }

        float Get(std::size_t index) const noexcept
        {
            float value = 0.0F;
            std::memcpy(&value, _data + index * sizeof(float), sizeof(value));
            return value;
        }

        void Set(std::size_t index, float value) const noexcept
        {
            std::memcpy(_data + index * sizeof(float), &value, sizeof(value));
        }

        void Add(std::size_t index, float value) const noexcept
        {
            Set(index, Get(index) + value);
        }

    private:
        std::uint8_t* _data;
        std::size_t _size;
    };

    class SfxMixer::MixStream
    {
    public:
        bool CanRead() const noexcept
        {
            return true;
        }

        bool CanSeek() const noexcept
        {
            return false;
        }

        bool CanWrite() const noexcept
        {
            return false;
        }

        std::int64_t Length() const noexcept
        {
            return std::numeric_limits<std::int64_t>::max();
        }

        std::int64_t Position() const noexcept
        {
            return _position.load(std::memory_order_relaxed);
        }

        void Position(std::int64_t value) noexcept
        {
            static_cast<void>(value);
        }

        std::int32_t Read(std::span<std::uint8_t> buffer, std::int32_t offset, std::int32_t count)
        {
            const std::int32_t bytes = count / 8 * 8;
            if (bytes <= 0)
            {
                return 0;
            }
            if (offset < 0 || static_cast<std::size_t>(offset) > buffer.size()
                || static_cast<std::size_t>(bytes) > buffer.size() - static_cast<std::size_t>(offset))
            {
                throw std::out_of_range("Specified argument was out of the range of valid values.");
            }
            SfxMixer::Mix(FloatSpan(buffer.data() + offset,
                static_cast<std::size_t>(bytes) / sizeof(float)));
            const std::int64_t position = _position.load(std::memory_order_relaxed);
            _position.store(UncheckedAdd(position, bytes), std::memory_order_relaxed);
            return bytes;
        }

        void Flush() noexcept
        {
        }

        std::int64_t Seek(std::int64_t offset, std::int32_t origin) const noexcept
        {
            static_cast<void>(offset);
            static_cast<void>(origin);
            return _position.load(std::memory_order_relaxed);
        }

        void SetLength(std::int64_t value) noexcept
        {
            static_cast<void>(value);
        }

        void Write(std::span<const std::uint8_t> buffer, std::int32_t offset, std::int32_t count)
        {
            static_cast<void>(buffer);
            static_cast<void>(offset);
            static_cast<void>(count);
            throw std::logic_error("Specified method is not supported.");
        }

    private:
        std::atomic<std::int64_t> _position{0};
    };

    std::recursive_mutex SfxMixer::_lock;
    std::map<std::int32_t, std::shared_ptr<SfxMixer::Buffer>> SfxMixer::_buffers;
    std::vector<SfxMixer::VoiceEntry> SfxMixer::_voices;
    std::ptrdiff_t SfxMixer::_freeVoiceHead = -1;
    std::int32_t SfxMixer::_nextBuffer = 1;
    std::int32_t SfxMixer::_nextSource = 1;

    Vector3 SfxMixer::_listenerPosition;
    Vector3 SfxMixer::_listenerFacing(0.0F, 0.0F, -1.0F);
    Vector3 SfxMixer::_listenerUp(0.0F, 1.0F, 0.0F);

    std::shared_ptr<SfxMixer::MixStream> SfxMixer::_stream;
    std::shared_ptr<RawDataProvider> SfxMixer::_provider;
    std::shared_ptr<SoundPlayer> SfxMixer::_player;
    std::atomic<bool> SfxMixer::_unavailable{false};
    std::int32_t SfxMixer::_outputRate = 32728;

    bool SfxMixer::Open()
    {
        if (_unavailable.load(std::memory_order_relaxed))
        {
            return false;
        }
        auto device = MphRead::MusicPlayer::PlaybackDevice();
        auto engine = MphRead::MusicPlayer::Engine();
        if (!device || !engine)
        {
            _unavailable.store(true, std::memory_order_relaxed);
            return false;
        }
        try
        {
            {
                std::lock_guard<std::recursive_mutex> guard(_lock);
                if (!_player)
                {
                    AudioFormat format = MphRead::MusicPlayer::Format();
                    _outputRate = format.SampleRate;
                    _stream = std::make_shared<MixStream>();
                    _provider = std::make_shared<RawDataProvider>(_stream, SampleFormat::F32,
                        format.SampleRate);
                    _player = std::make_shared<SoundPlayer>(engine, format, _provider);
                    device->MasterMixer.AddComponent(_player);
                }
            }
            device->Start();
            _player->Play();
            return true;
        }
        catch (const std::exception& ex)
        {
            std::cout << "[sound] the SFX mixer could not start (" << ex.what()
                << "); continuing without SFX\n";
            _unavailable.store(true, std::memory_order_relaxed);
            return false;
        }
    }

    void SfxMixer::Close()
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        for (VoiceEntry& entry : _voices)
        {
            if (entry.Occupied)
            {
                entry.Value->State = SourceState::Stopped;
            }
        }
    }

    std::int32_t SfxMixer::NewBuffer()
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        const std::int32_t id = _nextBuffer;
        _nextBuffer = UncheckedIncrement(_nextBuffer);
        _buffers[id] = std::make_shared<Buffer>();
        return id;
    }

    void SfxMixer::DeleteBuffer(std::int32_t id)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        _buffers.erase(id);
    }

    SfxMixer::Voice* SfxMixer::FindVoice(std::int32_t id) noexcept
    {
        for (VoiceEntry& entry : _voices)
        {
            if (entry.Occupied && entry.Key == id)
            {
                return entry.Value.get();
            }
        }
        return nullptr;
    }

    std::int32_t SfxMixer::NewSource()
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        const std::int32_t id = _nextSource;
        _nextSource = UncheckedIncrement(_nextSource);
        std::shared_ptr<Voice> voice = std::make_shared<Voice>();
        for (VoiceEntry& entry : _voices)
        {
            if (entry.Occupied && entry.Key == id)
            {
                entry.Value = std::move(voice);
                return id;
            }
        }
        if (_freeVoiceHead >= 0)
        {
            const std::size_t slot = static_cast<std::size_t>(_freeVoiceHead);
            VoiceEntry& entry = _voices[slot];
            _freeVoiceHead = entry.NextFree;
            entry.Key = id;
            entry.Value = std::move(voice);
            entry.NextFree = -1;
            entry.Occupied = true;
        }
        else
        {
            _voices.push_back(VoiceEntry{id, std::move(voice), -1, true});
        }
        return id;
    }

    void SfxMixer::DeleteSource(std::int32_t id)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        for (std::size_t i = 0; i < _voices.size(); i++)
        {
            VoiceEntry& entry = _voices[i];
            if (entry.Occupied && entry.Key == id)
            {
                entry.Key = 0;
                entry.Value.reset();
                entry.NextFree = _freeVoiceHead;
                entry.Occupied = false;
                _freeVoiceHead = static_cast<std::ptrdiff_t>(i);
                return;
            }
        }
    }

    void SfxMixer::FillBuffer(
        std::int32_t id, ALFormat format, std::span<const std::uint8_t> data, std::int32_t sampleRate)
    {
        const std::int32_t channels = format == ALFormat::Stereo8 || format == ALFormat::Stereo16 ? 2 : 1;
        const bool sixteen = format == ALFormat::Mono16 || format == ALFormat::Stereo16;
        const std::size_t framesSize = sixteen
            ? data.size() / (2U * static_cast<std::size_t>(channels))
            : data.size() / static_cast<std::size_t>(channels);
        const std::int32_t frames = static_cast<std::int32_t>(framesSize);
        std::vector<float> samples(framesSize * static_cast<std::size_t>(channels));
        if (sixteen)
        {
            const std::size_t sourceLength = data.size() / sizeof(std::int16_t);
            for (std::size_t i = 0; i < samples.size() && i < sourceLength; i++)
            {
                std::int16_t source = 0;
                std::memcpy(&source, data.data() + i * sizeof(std::int16_t), sizeof(source));
                samples[i] = static_cast<float>(source) / 32768.0F;
            }
        }
        else
        {
            for (std::size_t i = 0; i < samples.size() && i < data.size(); i++)
            {
                samples[i] = (static_cast<std::int32_t>(data[i]) - 128) / 128.0F;
            }
        }
        std::lock_guard<std::recursive_mutex> guard(_lock);
        const auto found = _buffers.find(id);
        if (found == _buffers.end())
        {
            return;
        }
        Buffer& buffer = *found->second;
        buffer.Data = std::move(samples);
        buffer.Channels = channels;
        buffer.Frames = frames;
        buffer.SampleRate = sampleRate > 0 ? sampleRate : 22050;
        buffer.LoopStart = -1;
        buffer.LoopEnd = -1;
    }

    void SfxMixer::SetLoopPoints(std::int32_t id, std::int32_t start, std::int32_t end)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        const auto found = _buffers.find(id);
        if (found != _buffers.end())
        {
            found->second->LoopStart = start;
            found->second->LoopEnd = end;
        }
    }

    void SfxMixer::SetSource(std::int32_t id, ALSourceb param, bool value)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        Voice* found = FindVoice(id);
        if (found == nullptr)
        {
            return;
        }
        Voice& voice = *found;
        if (param == ALSourceb::Looping)
        {
            voice.Looping = value;
        }
        else if (param == ALSourceb::SourceRelative)
        {
            voice.Relative = value;
        }
    }

    void SfxMixer::SetSource(std::int32_t id, ALSourcei param, std::int32_t value)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        Voice* found = FindVoice(id);
        if (found == nullptr || param != ALSourcei::Buffer)
        {
            return;
        }
        Voice& voice = *found;
        voice.Reset();
        if (value != 0)
        {
            voice.Queue.push_back(value);
        }
    }

    void SfxMixer::SetSource(std::int32_t id, ALSourcef param, float value)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        Voice* found = FindVoice(id);
        if (found == nullptr)
        {
            return;
        }
        Voice& voice = *found;
        switch (param)
        {
        case ALSourcef::Gain:
            voice.Gain = MathMax(0.0F, value);
            break;
        case ALSourcef::Pitch:
            voice.Pitch = MathClamp(value, 0.01F, 8.0F);
            break;
        case ALSourcef::ReferenceDistance:
            voice.ReferenceDistance = value;
            break;
        case ALSourcef::MaxDistance:
            voice.MaxDistance = value;
            break;
        case ALSourcef::RolloffFactor:
            voice.RolloffFactor = value;
            break;
        default:
            break;
        }
    }

    void SfxMixer::SetSource(std::int32_t id, ALSource3f param, Vector3 value)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        Voice* found = FindVoice(id);
        if (found != nullptr && param == ALSource3f::Position)
        {
            found->Position = value;
        }
    }

    void SfxMixer::Play(std::int32_t id)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        Voice* found = FindVoice(id);
        if (found == nullptr)
        {
            return;
        }
        Voice& voice = *found;
        if (voice.Queue.empty())
        {
            voice.State = SourceState::Stopped;
            return;
        }
        if (voice.State != SourceState::Paused)
        {
            voice.Index = 0;
            voice.Cursor = 0.0;
        }
        voice.State = SourceState::Playing;
    }

    void SfxMixer::Pause(std::int32_t id)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        Voice* found = FindVoice(id);
        if (found != nullptr && found->State == SourceState::Playing)
        {
            found->State = SourceState::Paused;
        }
    }

    void SfxMixer::StopSource(std::int32_t id)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        Voice* found = FindVoice(id);
        if (found != nullptr)
        {
            found->State = SourceState::Stopped;
            found->Cursor = 0.0;
            found->Index = 0;
        }
    }

    void SfxMixer::QueueBuffers(std::int32_t id, std::span<std::int32_t> buffers)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        Voice* found = FindVoice(id);
        if (found == nullptr)
        {
            return;
        }
        Voice& voice = *found;
        for (std::size_t i = 0; i < buffers.size(); i++)
        {
            voice.Queue.push_back(buffers[i]);
        }
    }

    void SfxMixer::UnqueueBuffers(std::int32_t id, std::span<std::int32_t> buffers)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        Voice* found = FindVoice(id);
        if (found == nullptr)
        {
            return;
        }
        Voice& voice = *found;
        const std::int32_t bufferLength = static_cast<std::int32_t>(buffers.size());
        const std::int32_t queueCount = static_cast<std::int32_t>(voice.Queue.size());
        const std::int32_t count = std::min(bufferLength, std::min(voice.Processed, queueCount));
        for (std::int32_t i = 0; i < count; i++)
        {
            buffers[static_cast<std::size_t>(i)] = voice.Queue.front();
            voice.Queue.erase(voice.Queue.begin());
        }
        voice.Processed = UncheckedSubtract(voice.Processed, count);
        voice.Index = std::max<std::int32_t>(0, UncheckedSubtract(voice.Index, count));
    }

    std::int32_t SfxMixer::GetSource(std::int32_t id, ALGetSourcei param)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        Voice* found = FindVoice(id);
        if (found == nullptr)
        {
            return static_cast<std::int32_t>(SourceState::Stopped);
        }
        const Voice& voice = *found;
        switch (param)
        {
        case ALGetSourcei::SourceState:
            return static_cast<std::int32_t>(voice.State);
        case ALGetSourcei::BuffersQueued:
            return static_cast<std::int32_t>(voice.Queue.size());
        case ALGetSourcei::BuffersProcessed:
            return voice.Processed;
        case ALGetSourcei::Buffer:
            if (voice.Index < static_cast<std::int32_t>(voice.Queue.size()))
            {
                return voice.Queue.at(static_cast<std::size_t>(voice.Index));
            }
            return 0;
        default:
            return 0;
        }
    }

    void SfxMixer::SetListener(ALListener3f param, Vector3 value)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        if (param == ALListener3f::Position)
        {
            _listenerPosition = value;
        }
    }

    void SfxMixer::SetListenerOrientation(Vector3 facing, Vector3 up)
    {
        std::lock_guard<std::recursive_mutex> guard(_lock);
        _listenerFacing = facing;
        _listenerUp = up;
    }

    void SfxMixer::Mix(FloatSpan output)
    {
        output.Clear();
        try
        {
            std::lock_guard<std::recursive_mutex> guard(_lock);
            for (VoiceEntry& entry : _voices)
            {
                if (entry.Occupied)
                {
                    Voice& voice = *entry.Value;
                    if (voice.State == SourceState::Playing)
                    {
                        MixVoice(voice, output);
                    }
                }
            }
        }
        catch (const std::exception& ex)
        {
            std::cout << "[sound] the SFX mixer faulted: " << ex.what() << '\n';
            output.Clear();
            return;
        }
        for (std::size_t i = 0; i < output.Size(); i++)
        {
            output.Set(i, MathClamp(output.Get(i), -1.0F, 1.0F));
        }
    }

    void SfxMixer::MixVoice(Voice& voice, FloatSpan output)
    {
        if (voice.Index >= static_cast<std::int32_t>(voice.Queue.size()))
        {
            voice.State = SourceState::Stopped;
            return;
        }
        auto bufferIt = _buffers.find(voice.Queue.at(static_cast<std::size_t>(voice.Index)));
        if (bufferIt == _buffers.end() || bufferIt->second->Frames <= 0)
        {
            voice.State = SourceState::Stopped;
            return;
        }
        std::shared_ptr<Buffer> buffer = bufferIt->second;
        float left = 0.0F;
        float right = 0.0F;
        Placement(voice, left, right);
        double step = buffer->SampleRate / static_cast<double>(_outputRate) * voice.Pitch;
        std::int32_t loopStart = 0;
        std::int32_t loopEnd = buffer->Frames;
        if (buffer->LoopEnd > buffer->LoopStart && buffer->LoopStart >= 0)
        {
            loopStart = std::min(buffer->LoopStart, buffer->Frames - 1);
            loopEnd = std::min(buffer->LoopEnd, buffer->Frames);
        }
        for (std::size_t i = 0; i + 1 < output.Size(); i += 2)
        {
            if (voice.Cursor >= (voice.Looping ? loopEnd : buffer->Frames))
            {
                if (voice.Looping && loopEnd > loopStart)
                {
                    voice.Cursor = loopStart + std::fmod(voice.Cursor - loopEnd,
                        static_cast<double>(loopEnd - loopStart));
                }
                else if (UncheckedAdd(voice.Index, 1) < static_cast<std::int32_t>(voice.Queue.size()))
                {
                    voice.Index = UncheckedIncrement(voice.Index);
                    voice.Processed = UncheckedIncrement(voice.Processed);
                    voice.Cursor = 0.0;
                    bufferIt = _buffers.find(voice.Queue[static_cast<std::size_t>(voice.Index)]);
                    if (bufferIt == _buffers.end() || bufferIt->second->Frames <= 0)
                    {
                        voice.State = SourceState::Stopped;
                        return;
                    }
                    buffer = bufferIt->second;
                    step = buffer->SampleRate / static_cast<double>(_outputRate) * voice.Pitch;
                    loopStart = 0;
                    loopEnd = buffer->Frames;
                }
                else
                {
                    voice.Processed = UncheckedIncrement(voice.Processed);
                    voice.Index = UncheckedIncrement(voice.Index);
                    voice.State = SourceState::Stopped;
                    return;
                }
            }
            float monoLeft = 0.0F;
            float monoRight = 0.0F;
            Sample(*buffer, voice.Cursor, monoLeft, monoRight);
            output.Add(i, monoLeft * left * voice.Gain);
            output.Add(i + 1, monoRight * right * voice.Gain);
            voice.Cursor += step;
        }
    }

    void SfxMixer::Sample(const Buffer& buffer, double cursor, float& left, float& right)
    {
        // Cubic (Catmull-Rom) through the four frames round the cursor: the
        // cartridge's samples are 8 to 22 kHz, and a straight line between
        // two of them -- what played them before -- is what made short
        // effects such as the Lockjaw's sound muffled and grainy beside the
        // rest.
        std::int32_t frame = DotNetDoubleToInt32(cursor);
        if (frame < 0)
        {
            frame = 0;
        }
        const std::int32_t last = buffer.Frames - 1;
        const std::int32_t before = std::max(frame - 1, 0);
        const std::int32_t next = std::min(frame + 1, last);
        const std::int32_t after = std::min(frame + 2, last);
        const float t = static_cast<float>(cursor - frame);
        const std::size_t channels = static_cast<std::size_t>(buffer.Channels == 2 ? 2 : 1);
        const auto cubic = [&](std::size_t channel)
        {
            const float p0 = buffer.Data[static_cast<std::size_t>(before) * channels + channel];
            const float p1 = buffer.Data[static_cast<std::size_t>(frame) * channels + channel];
            const float p2 = buffer.Data[static_cast<std::size_t>(next) * channels + channel];
            const float p3 = buffer.Data[static_cast<std::size_t>(after) * channels + channel];
            return p1 + 0.5F * t * (p2 - p0
                + t * (2.0F * p0 - 5.0F * p1 + 4.0F * p2 - p3
                + t * (3.0F * (p1 - p2) + p3 - p0)));
        };
        left = cubic(0);
        right = channels == 2 ? cubic(1) : left;
    }

    float SfxMixer::Lerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    void SfxMixer::Placement(const Voice& voice, float& left, float& right)
    {
        const Vector3 relative = voice.Relative ? voice.Position : Subtract(voice.Position, _listenerPosition);
        const float distance = Length(relative);
        float attenuation = 1.0F;
        const float span = voice.MaxDistance - voice.ReferenceDistance;
        if (voice.RolloffFactor > 0.0F && span > 0.0F && !std::isinf(span)
            && voice.MaxDistance < std::numeric_limits<float>::max())
        {
            const float clamped = MathClamp(distance, voice.ReferenceDistance, voice.MaxDistance);
            attenuation = MathClamp(1.0F - voice.RolloffFactor
                * (clamped - voice.ReferenceDistance) / span, 0.0F, 1.0F);
        }
        float pan = 0.0F;
        if (distance > 0.0001F)
        {
            const Vector3 facing = voice.Relative ? Vector3(0.0F, 0.0F, -1.0F) : _listenerFacing;
            const Vector3 up = voice.Relative ? Vector3(0.0F, 1.0F, 0.0F) : _listenerUp;
            const Vector3 side = Vector3::Cross(facing, up);
            if (LengthSquared(side) > 0.0001F)
            {
                pan = MathClamp(Vector3::Dot(Divide(relative, distance), side.Normalized()), -1.0F, 1.0F);
            }
        }
        left = MathMin(1.0F, std::sqrt(0.5F * (1.0F - pan)) * 1.41421356F) * attenuation;
        right = MathMin(1.0F, std::sqrt(0.5F * (1.0F + pan)) * 1.41421356F) * attenuation;
    }
}
#endif

