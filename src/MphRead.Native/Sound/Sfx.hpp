#pragma once

#include "../Formats/Types.hpp"
#include "../Metadata/SoundMeta.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <vector>

namespace MphRead
{
    class Scene;
    enum class SoundCapability : std::int32_t;
}

namespace MphRead::Formats::Sound
{
    class DgnFile;
    class DgnFileEntry;
    struct DgnData;
    class SfxScriptFile;
    class SoundData;
    class SoundSample;
    class SoundStream;
    struct Sound3dEntry;
}

namespace MphRead::Mods::Network
{
    class DialancheCombatCheck;
}

namespace MphRead::Sound
{
    class SfxInstanceBase;

    class SoundSource
    {
    public:
        OpenTK::Mathematics::Vector3 Position{};
        float ReferenceDistance = 1.0F;
        float MaxDistance = std::numeric_limits<float>::max();
        float RolloffFactor = 1.0F;
        float Volume = 1.0F;
        bool Self = false;

        void Update(OpenTK::Mathematics::Vector3 position, std::int32_t rangeIndex);

        void PlaySfx(MphRead::SfxId id,
            bool loop = false,
            bool noUpdate = false,
            float recency = -1.0F,
            bool sourceOnly = false,
            bool cancellable = false,
            float amountA = 0.0F,
            float amountB = 0.0F);
        std::int32_t PlayFreeSfx(MphRead::SfxId id);
        void PlaySfx(std::int32_t id,
            bool loop = false,
            bool noUpdate = false,
            float recency = -1.0F,
            bool sourceOnly = false,
            bool cancellable = false,
            float amountA = 0.0F,
            float amountB = 0.0F);
        std::int32_t PlayFreeSfx(std::int32_t id);
        void PlayEnvironmentSfx(std::int32_t id);
        bool CheckEnvironmentSfx(std::int32_t id);
        void StopAllSfx(bool force = false);
        void StopSfx(MphRead::SfxId id);
        void StopSfx(std::int32_t id);
        void StopFreeSfx(MphRead::SfxId id);
        void StopFreeSfx(std::int32_t id);
        void StopSfxByHandle(std::int32_t handle);
        void StopFreeSfxScripts();
        void SetPausedFreeSfxScripts(bool paused);
        bool IsHandlePlaying(std::int32_t handle);
        std::int32_t CountPlayingSfx(MphRead::SfxId id);
        std::int32_t CountPlayingSfx(std::int32_t id);
        std::int32_t CountSourcePlayingSfx(MphRead::SfxId id);
        std::int32_t CountSourcePlayingSfx(std::int32_t id);
        void QueueStream(MphRead::VoiceId id, float delay = 0.0F, float expiration = 0.0F);
    };

    class Sfx final
    {
        friend class ::MphRead::Mods::Network::DialancheCombatCheck;
    public:
        Sfx() = delete;

        static float Volume;
        static bool SfxMute;
        static std::int32_t ForceFieldSfxMute;
        static std::int32_t TimedSfxMute;
        static std::int32_t LongSfxMute;

        [[nodiscard]] static std::shared_ptr<SfxInstanceBase> Instance();
        [[nodiscard]] static MphRead::SoundCapability CheckAudioLoad();
        [[nodiscard]] static float CalculatePitchDiv(float pitchFac);
        static void Update(float time);
        static void QueueStream(MphRead::VoiceId id, float delay = 0.0F, float expiration = 0.0F);
        static void Load(MphRead::Scene& scene);
        static void ShutDown();

    private:
        static std::shared_ptr<SfxInstanceBase> _instance;
    };

    class SfxInstanceBase
    {
    public:
        SfxInstanceBase();
        virtual ~SfxInstanceBase() = default;

        [[nodiscard]] virtual const std::vector<MphRead::Formats::Sound::Sound3dEntry>& RangeData() const;
        [[nodiscard]] virtual OpenTK::Mathematics::Vector3 GetListenerPosition() const;
        [[nodiscard]] virtual OpenTK::Mathematics::Vector3 GetListenerUp() const;
        [[nodiscard]] virtual OpenTK::Mathematics::Vector3 GetListenerFacing() const;
        virtual void QueueStream(std::int32_t id, float delay, float expiration);
        virtual void PlayFreeStream(std::int32_t id);
        virtual void StopSoundByHandle(std::int32_t handle);
        virtual void PlayDgn(std::int32_t id,
            SoundSource* source,
            bool loop,
            bool noUpdate,
            float recency,
            bool cancellable,
            float amountA,
            float amountB);
        virtual void PlayScript(std::int32_t id,
            SoundSource* source,
            bool noUpdate,
            float recency,
            bool sourceOnly,
            bool cancellable);
        virtual std::int32_t PlaySample(std::int32_t id,
            SoundSource* source,
            std::optional<bool> loop,
            bool noUpdate,
            float recency,
            bool sourceOnly,
            bool cancellable);
        virtual std::int32_t PlayFreeSfx(MphRead::SfxId id);
        virtual std::int32_t PlayFreeSfx(std::int32_t id);
        virtual void PlayEnvironmentSfx(std::int32_t index, SoundSource* source);
        virtual bool CheckEnvironmentSfx(std::int32_t index);
        virtual void StopSoundFromSource(SoundSource* source, bool force);
        virtual void StopSoundFromSource(SoundSource* source, std::int32_t id);
        virtual void StopEnvironmentSfx();
        virtual void StopAllSound(bool force = false);
        virtual void StopSoundById(std::int32_t id);
        virtual void StopFreeSfxScripts();
        virtual void SetPausedFreeSfxScripts(bool paused);
        virtual bool IsHandlePlaying(std::int32_t handle);
        virtual std::int32_t CountPlayingSfx(std::int32_t id);
        virtual std::int32_t CountSourcePlayingSfx(std::int32_t id, SoundSource* source);
        virtual void Update(float time);
        virtual void Load(MphRead::Scene& scene);
        virtual void ShutDown();
        // A translated Quake 3 level's looping sound (a target_speaker):
        // 16-bit mono samples, at a place, at full strength out to reference
        // and silent from maximum, as Quake's loops fall off. Nothing where
        // there is no sound.
        virtual void PlayAmbientLoop(const std::vector<std::uint8_t>& samples, std::int32_t sampleRate,
            OpenTK::Mathematics::Vector3 position, float volume, float reference, float maximum);
        virtual void StopAmbientLoops();

    private:
        std::shared_ptr<std::vector<MphRead::Formats::Sound::Sound3dEntry>> _baseRangeData;
    };

    class SfxInstance final : public SfxInstanceBase,
                              public std::enable_shared_from_this<SfxInstance>
    {
    public:
        static constexpr std::int32_t MaxPerInst = 12;

        class SoundChannel;

        class SoundInstance
        {
        public:
            std::int32_t Count = 0;
            std::array<std::shared_ptr<MphRead::Formats::Sound::SoundSample>, MaxPerInst> Samples{};
            std::array<SoundChannel*, MaxPerInst> Channels{};
            std::shared_ptr<MphRead::Formats::Sound::DgnFile> DgnFile{};
            std::shared_ptr<MphRead::Formats::Sound::SfxScriptFile> ScriptFile{};
            std::int32_t ScriptIndex = -1;
            SoundSource* Source = nullptr;
            std::array<float, MaxPerInst> Volume{};
            std::array<float, MaxPerInst> Pitch{};
            bool Paused = false;
            float PlayTime = -1.0F;
            std::int32_t SfxId = -1;
            bool NoUpdate = false;
            std::array<bool, MaxPerInst> Loop{};
            bool Cancellable = false;
            std::int32_t Handle = -1;
            static std::int32_t NextHandle;

            SoundInstance();
            void PlayChannel(std::int32_t index);
            [[nodiscard]] bool IsLooping() const;
            void UpdatePosition();
            void UpdateParameters();
            void Stop();
        };

        class SoundChannel
        {
        public:
            const std::int32_t Id;
            bool InUse = false;
            std::int32_t BufferId = 0;

            explicit SoundChannel(std::int32_t id);
            void Stop();
        };

        class SoundBuffer
        {
        public:
            const std::int32_t Id;
            std::shared_ptr<MphRead::Formats::Sound::SoundSample> Sample{};

            explicit SoundBuffer(std::int32_t id);
        };

        [[nodiscard]] const std::vector<MphRead::Formats::Sound::Sound3dEntry>& RangeData() const override;
        [[nodiscard]] OpenTK::Mathematics::Vector3 GetListenerPosition() const override;
        [[nodiscard]] OpenTK::Mathematics::Vector3 GetListenerUp() const override;
        [[nodiscard]] OpenTK::Mathematics::Vector3 GetListenerFacing() const override;
        std::int32_t PlaySample(std::int32_t id,
            SoundSource* source,
            std::optional<bool> loop,
            bool noUpdate,
            float recency,
            bool sourceOnly,
            bool cancellable) override;
        std::int32_t PlayFreeSfx(MphRead::SfxId id) override;
        std::int32_t PlayFreeSfx(std::int32_t id) override;
        void PlayDgn(std::int32_t id,
            SoundSource* source,
            bool loop,
            bool noUpdate,
            float recency,
            bool cancellable,
            float amountA,
            float amountB) override;
        void PlayScript(std::int32_t id,
            SoundSource* source,
            bool noUpdate,
            float recency,
            bool sourceOnly,
            bool cancellable) override;
        void StopAllSound(bool force = true) override;
        void StopSoundFromSource(SoundSource* source, bool force) override;
        void StopSoundFromSource(SoundSource* source, std::int32_t id) override;
        void StopSoundById(std::int32_t id) override;
        void StopSoundByHandle(std::int32_t handle) override;
        void StopFreeSfxScripts() override;
        void SetPausedFreeSfxScripts(bool paused) override;
        bool IsHandlePlaying(std::int32_t handle) override;
        void Update(float time) override;
        std::int32_t CountPlayingSfx(std::int32_t id) override;
        std::int32_t CountSourcePlayingSfx(std::int32_t id, SoundSource* source) override;
        void PlayEnvironmentSfx(std::int32_t index, SoundSource* source) override;
        bool CheckEnvironmentSfx(std::int32_t index) override;
        void StopEnvironmentSfx() override;
        void QueueStream(std::int32_t id, float delay, float expiration) override;
        void PlayFreeStream(std::int32_t id) override;
        void Load(MphRead::Scene& scene) override;
        void ShutDown() override;
        void PlayAmbientLoop(const std::vector<std::uint8_t>& samples, std::int32_t sampleRate,
            OpenTK::Mathematics::Vector3 position, float volume, float reference, float maximum) override;
        void StopAmbientLoops() override;

        std::shared_ptr<MphRead::Formats::Sound::SoundData> _soundData{};

    private:
        struct EnvironmentItem
        {
            const std::int32_t SfxId;
            std::int32_t Instances = 0;
            std::int32_t Handle = -1;
            SoundSource Source{};
            float DistanceSquared = std::numeric_limits<float>::max();

            explicit EnvironmentItem(MphRead::SfxId sfxId);
            void Reset();
        };

        struct QueueItem
        {
            std::shared_ptr<MphRead::Formats::Sound::SoundStream> Stream{};
            float DelayTimer = 0.0F;
            float ExpirationTimer = 0.0F;
            bool Playing = false;
        };

        SoundInstance* PlaySampleGetInst(std::int32_t id,
            SoundSource* source,
            std::optional<bool> loop,
            bool noUpdate,
            float recency,
            bool sourceOnly,
            bool cancellable);
        bool SetUpInstance(std::int32_t id,
            SoundSource* source,
            bool loop,
            float recency,
            bool sourceOnly,
            bool cancellable,
            SoundInstance*& inst);
        bool SetUpSample(std::int32_t id, SoundInstance& inst, std::int32_t index);
        void UpdateInstance(SoundInstance& inst, bool noUpdate);
        void StartInstance(SoundInstance& inst, bool noUpdate);
        SoundInstance* FindRecentSamplePlay(std::int32_t id, float recency, SoundSource* source);
        void UpdateDgn(SoundInstance& inst, float amountA, float amountB);
        float GetDgnValue(const std::vector<MphRead::Formats::Sound::DgnData>& data, float amount) const;
        SoundInstance& FindInstance(SoundSource* source);
        SoundChannel* GetChannel(std::int32_t bufferId);
        void BufferData(const std::shared_ptr<MphRead::Formats::Sound::SoundSample>& sample);
        void UpdateScript(SoundInstance& inst, float time);
        std::int32_t CountPlayingSfx(std::int32_t id, SoundSource* source) const;
        void UpdateEnvironmentSfx();
        void UpdateStreams(float time);

        std::intptr_t _device = 0;
        std::intptr_t _context = 0;
        bool _loopPointSupport = false;
        std::array<std::unique_ptr<SoundBuffer>, 64> _buffers{};
        std::array<std::unique_ptr<SoundChannel>, 128> _channels{};
        std::array<std::unique_ptr<SoundInstance>, 128> _instances{};
        std::shared_ptr<const std::vector<std::shared_ptr<MphRead::Formats::Sound::SoundSample>>> _samples{};
        std::shared_ptr<const std::vector<std::shared_ptr<MphRead::Formats::Sound::DgnFile>>> _dgnFiles{};
        std::shared_ptr<const std::vector<std::shared_ptr<MphRead::Formats::Sound::SfxScriptFile>>> _sfxScripts{};
        std::shared_ptr<const std::vector<MphRead::Formats::Sound::Sound3dEntry>> _rangeData{};
        MphRead::Scene* _scene = nullptr;
        std::list<std::shared_ptr<QueueItem>> _activeQueue{};
        std::deque<std::shared_ptr<QueueItem>> _inactiveQueue{};
        std::int32_t _streamInstance = -1;
        std::int32_t _streamBuffer = -1;
        // PlayAmbientLoop's: a buffer and a source each, kept for the next
        // level once stopped, and how loud each is.
        struct AmbientLoop
        {
            std::int32_t Buffer = 0;
            std::int32_t Source = 0;
            float Volume = 0.0F;
            bool Playing = false;
        };
        std::vector<AmbientLoop> _ambient{};

        static std::array<EnvironmentItem, 10> _environmentItems;
    };
}
