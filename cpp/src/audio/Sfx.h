#pragma once

#include "SoundData.h"
#include "SoundIds.h"

#include <array>
#include <cfloat>
#include <deque>
#include <filesystem>
#include <memory>

// Sound effects, ported from Sound/Sfx.cs: SoundSource (where an entity's
// sounds come from) and SfxInstance (128 playing sounds of up to 12 samples
// each, sound scripts, DGN sounds whose volume and pitch follow two amounts,
// the looping environment sounds, the queued voice streams). Until load()
// succeeds every call does nothing, like the C#'s SfxInstanceBase: the
// headless runs and the machines without a sound device play silently.
namespace fp {

class Mixer;

// SoundSource: an entity's position and range for its sounds.
class SoundSource {
public:
    SoundSource() = default;
    SoundSource(const SoundSource&) = delete;
    SoundSource& operator=(const SoundSource&) = delete;
    ~SoundSource(); // its sounds stop with it

    std::array<float, 3> position{};
    float referenceDistance = 1, maxDistance = FLT_MAX, rolloff = 1;
    float volume = 1;
    bool self = false; // at the listener, never attenuated

    // Update: the position, and the range of SND3DLIST.DAT (-1: the listener's own).
    void update(const std::array<float, 3>& position, int rangeIndex);

    void playSfx(int id, bool loop = false, bool noUpdate = false, float recency = -1, bool sourceOnly = false,
        bool cancellable = false, float amountA = 0, float amountB = 0);
    void playSfx(SfxId id, bool loop = false, bool noUpdate = false, float recency = -1, bool sourceOnly = false,
        bool cancellable = false, float amountA = 0, float amountB = 0)
    {
        playSfx(static_cast<int>(id), loop, noUpdate, recency, sourceOnly, cancellable, amountA, amountB);
    }
    void stopSfx(int id);
    void stopSfx(SfxId id) { stopSfx(static_cast<int>(id)); }
    void stopAllSfx(bool force = false);
    int countSourcePlayingSfx(int id) const;
    int countSourcePlayingSfx(SfxId id) const { return countSourcePlayingSfx(static_cast<int>(id)); }
    void playEnvironmentSfx(int index);
};

class Sfx {
public:
    static Sfx& instance();

    // Reads the sound files and starts playing through `mixer`; false (and silent) on failure.
    bool load(const std::filesystem::path& root, Mixer* mixer);
    void unload();
    bool loaded() const { return m_mixer != nullptr; }
    const SoundData& data() const { return m_data; }

    float volume = 0.35f;
    bool sfxMute = false;
    int timedSfxMute = 0, longSfxMute = 0;

    // PlayFreeSfx: at the listener; a sample's handle, -1 for a script or nothing.
    int playFreeSfx(int id);
    int playFreeSfx(SfxId id) { return playFreeSfx(static_cast<int>(id)); }
    int playSample(int id, SoundSource* source, int loop, bool noUpdate, float recency, bool sourceOnly, bool cancellable);
    void playScript(int id, SoundSource* source, bool noUpdate, float recency, bool sourceOnly, bool cancellable);
    void playDgn(int id, SoundSource* source, bool loop, bool noUpdate, float recency, bool cancellable, float amountA, float amountB);
    void stopSoundFromSource(const SoundSource* source, bool force);
    void stopSoundFromSource(const SoundSource* source, int id);
    // A source going away: its loops stop, its one-shots finish where they are.
    void detach(const SoundSource* source);
    void stopSoundById(int id);
    void stopSoundById(SfxId id) { stopSoundById(static_cast<int>(id)); }
    void stopSoundByHandle(int handle);
    void stopFreeSfxScripts();
    void setPausedFreeSfxScripts(bool paused);
    void stopAllSound(bool force);
    bool isHandlePlaying(int handle) const;
    int countPlayingSfx(int id, const SoundSource* source = nullptr) const;
    int countPlayingSfx(SfxId id) const { return countPlayingSfx(static_cast<int>(id)); }
    // The looping sounds a room's objects share (ELECTRIC_BARRIER...): the closest source of each plays once.
    void playEnvironmentSfx(int index, const SoundSource& source);
    bool checkEnvironmentSfx(int index) const;
    void stopEnvironmentSfx();
    // The voice streams (VoiceId): one at a time, `delay` before it starts,
    // dropped `expiration` seconds after it was queued if still waiting.
    void queueStream(VoiceId id, float delay = 0, float expiration = 0);
    void playFreeStream(VoiceId id);

    // One update of `time` seconds with the listener (the camera) where it is.
    void update(float time, const std::array<float, 3>& position, const std::array<float, 3>& facing,
        const std::array<float, 3>& up);
    const std::array<float, 3>& listenerPosition() const { return m_listenerPosition; }
    const std::vector<SoundRange>& ranges() const { return m_data.ranges; }

private:
    static constexpr int MaxPerInstance = 12;
    static constexpr int InstanceCount = 128;
    static constexpr int ChannelCount = 128;
    static constexpr int StreamVoice = 128;

    struct Instance {
        int count = 0;
        std::array<const SoundSample*, MaxPerInstance> samples{};
        std::array<int, MaxPerInstance> channels; // mixer voices, -1 for none
        std::array<float, MaxPerInstance> volume, pitch;
        std::array<bool, MaxPerInstance> loop{};
        std::array<bool, MaxPerInstance> panned{};
        std::array<float, MaxPerInstance> pan{};
        const DgnFile* dgn = nullptr;
        const SfxScriptFile* script = nullptr;
        int scriptIndex = -1;
        SoundSource* source = nullptr;
        bool paused = false;
        float playTime = -1;
        int sfxId = -1;
        bool noUpdate = false;
        bool cancellable = false;
        int handle = -1;
        // UpdatePosition: where it was last put, which a noUpdate sound keeps.
        std::array<float, 3> position{};
        float referenceDistance = FLT_MAX, maxDistance = FLT_MAX, rolloff = 1;

        Instance()
        {
            channels.fill(-1);
            volume.fill(1);
            pitch.fill(1);
        }
        bool looping() const;
    };
    struct EnvironmentItem {
        SfxId sfxId;
        int instances = 0;
        int handle = -1;
        SoundSource source;
        float distanceSquared = FLT_MAX;
    };
    struct QueueItem {
        const SoundStream* stream = nullptr;
        float delay = 0, expiration = 0;
        bool playing = false;
    };

    Sfx();
    ~Sfx() { m_mixer = nullptr; } // the environment's own sources detach as it goes
    void stop(Instance& inst);
    Instance* playSampleInstance(int id, SoundSource* source, int loop, bool noUpdate, float recency, bool sourceOnly, bool cancellable);
    bool setUpInstance(int id, SoundSource* source, bool loop, float recency, bool sourceOnly, bool cancellable, Instance*& inst);
    bool setUpSample(int id, Instance& inst, int index);
    Instance* findRecent(int id, float recency, const SoundSource* source);
    Instance& findInstance(const SoundSource* source);
    int freeChannel();
    void updatePosition(Instance& inst);
    void updateParameters(Instance& inst);
    void updateInstance(Instance& inst, bool noUpdate);
    void startInstance(Instance& inst, bool noUpdate);
    void playChannel(Instance& inst, int index);
    void updateDgn(Instance& inst, float amountA, float amountB);
    void updateScript(Instance& inst, float time);
    void updateEnvironmentSfx();
    void updateStreams(float time);

    SoundData m_data;
    Mixer* m_mixer = nullptr;
    std::array<Instance, InstanceCount> m_instances;
    std::array<bool, ChannelCount> m_channelInUse{};
    std::array<EnvironmentItem, 10> m_environment;
    std::deque<QueueItem> m_queue;
    int m_nextHandle = 0;
    std::array<float, 3> m_listenerPosition{}, m_listenerFacing{0, 0, -1}, m_listenerUp{0, 1, 0};
};

} // namespace fp
