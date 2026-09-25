#pragma once

#include <array>
#include <cfloat>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// The audio output: a fixed pool of voices mixed in software to a stereo
// device, the way Mods/Sound/SfxMixer.cs stands in for OpenAL -- linear
// clamped distance attenuation, equal-power panning against the listener,
// loop points -- with the music mixed in from a callback.
namespace fp {

struct PcmBuffer {
    std::vector<float> data; // interleaved when stereo
    int channels = 1;
    int frames = 0;
    int sampleRate = 22050;
    int loopStart = -1, loopEnd = -1; // frames; none: the whole buffer loops
};

struct VoiceParams {
    float gain = 1, pitch = 1;
    bool loop = false;
    bool relative = false; // position relative to the listener
    std::array<float, 3> position{};
    float referenceDistance = 1, maxDistance = FLT_MAX, rolloff = 1;
};

class Mixer {
public:
    static constexpr int VoiceCount = 129; // 128 for the sounds, one for the voice streams

    // The default output device; nullptr when there is none. Without a
    // device (`device` false) the mix is only produced by render().
    static std::unique_ptr<Mixer> open(bool device = true);
    ~Mixer();

    int sampleRate() const { return m_rate; }
    void play(int voice, std::shared_ptr<const PcmBuffer> buffer, const VoiceParams& params);
    void setParams(int voice, const VoiceParams& params);
    void stop(int voice);
    bool playing(int voice) const;
    void setListener(const std::array<float, 3>& position, const std::array<float, 3>& facing, const std::array<float, 3>& up);
    // Called from the audio thread for `frames` stereo frames at sampleRate(), added to the sounds.
    void setMusic(std::function<void(float* out, int frames)> music);
    // The next `frames` stereo frames, as the device would get them.
    void render(float* out, int frames);

private:
    Mixer() = default;
    struct Voice {
        std::shared_ptr<const PcmBuffer> buffer;
        VoiceParams params;
        double cursor = 0;
        bool playing = false;
    };
    void mixVoice(Voice& voice, float* out, int frames);

    struct Device;
    std::unique_ptr<Device> m_device;
    int m_rate = 48000;
    mutable std::mutex m_lock;
    std::array<Voice, VoiceCount> m_voices{};
    std::array<float, 3> m_listenerPosition{}, m_listenerFacing{0, 0, -1}, m_listenerUp{0, 1, 0};
    std::function<void(float*, int)> m_music;
    std::vector<float> m_musicScratch;
};

} // namespace fp
