#include "Mixer.h"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>

namespace fp {

struct Mixer::Device {
    ma_device device{};
    bool started = false;
};

namespace {

void deviceCallback(ma_device* device, void* output, const void*, ma_uint32 frames)
{
    static_cast<Mixer*>(device->pUserData)->render(static_cast<float*>(output), static_cast<int>(frames));
}

template <typename F>
void offThread(F&& work)
{
    std::thread thread(std::forward<F>(work));
    thread.join();
}

} // namespace

std::unique_ptr<Mixer> Mixer::open(bool device)
{
    std::unique_ptr<Mixer> mixer(new Mixer());
    if (!device) {
        return mixer;
    }
    mixer->m_device = std::make_unique<Device>();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 0; // the device's own
    config.dataCallback = deviceCallback;
    config.pUserData = mixer.get();
    // On a thread of its own: Qt puts the main thread in a single-threaded
    // COM apartment, and miniaudio's WASAPI device crashes when made there.
    ma_result result = MA_ERROR;
    offThread([&] {
        result = ma_device_init(nullptr, &config, &mixer->m_device->device);
        if (result == MA_SUCCESS && ma_device_start(&mixer->m_device->device) != MA_SUCCESS) {
            ma_device_uninit(&mixer->m_device->device);
            result = MA_ERROR;
        }
    });
    if (result != MA_SUCCESS) {
        std::fprintf(stderr, "[sound] no audio device; continuing without sound\n");
        return nullptr;
    }
    mixer->m_rate = static_cast<int>(mixer->m_device->device.sampleRate);
    std::fprintf(stderr, "[sound] %s, %d Hz\n", ma_get_backend_name(mixer->m_device->device.pContext->backend), mixer->m_rate);
    mixer->m_device->started = true;
    return mixer;
}

Mixer::~Mixer()
{
    if (m_device && m_device->started) {
        offThread([&] { ma_device_uninit(&m_device->device); });
    }
}

void Mixer::play(int voice, std::shared_ptr<const PcmBuffer> buffer, const VoiceParams& params)
{
    if (voice < 0 || voice >= VoiceCount) {
        return;
    }
    std::lock_guard lock(m_lock);
    Voice& v = m_voices[voice];
    v.buffer = std::move(buffer);
    v.params = params;
    v.params.pitch = std::clamp(params.pitch, 0.01f, 8.0f); // zero would hold one frame for ever
    v.cursor = 0;
    v.playing = v.buffer != nullptr && v.buffer->frames > 0;
}

void Mixer::setParams(int voice, const VoiceParams& params)
{
    if (voice < 0 || voice >= VoiceCount) {
        return;
    }
    std::lock_guard lock(m_lock);
    m_voices[voice].params = params;
    m_voices[voice].params.pitch = std::clamp(params.pitch, 0.01f, 8.0f);
}

void Mixer::stop(int voice)
{
    if (voice < 0 || voice >= VoiceCount) {
        return;
    }
    std::lock_guard lock(m_lock);
    m_voices[voice].playing = false;
    m_voices[voice].buffer.reset();
}

bool Mixer::playing(int voice) const
{
    if (voice < 0 || voice >= VoiceCount) {
        return false;
    }
    std::lock_guard lock(m_lock);
    return m_voices[voice].playing;
}

void Mixer::setListener(const std::array<float, 3>& position, const std::array<float, 3>& facing, const std::array<float, 3>& up)
{
    std::lock_guard lock(m_lock);
    m_listenerPosition = position;
    m_listenerFacing = facing;
    m_listenerUp = up;
}

void Mixer::setMusic(std::function<void(float*, int)> music)
{
    std::lock_guard lock(m_lock);
    m_music = std::move(music);
}

void Mixer::render(float* out, int frames)
{
    std::fill(out, out + frames * 2, 0.0f);
    std::lock_guard lock(m_lock);
    for (Voice& voice : m_voices) {
        if (voice.playing) {
            mixVoice(voice, out, frames);
        }
    }
    if (m_music) {
        m_musicScratch.assign(static_cast<size_t>(frames) * 2, 0.0f);
        m_music(m_musicScratch.data(), frames);
        for (int i = 0; i < frames * 2; i++) {
            out[i] += m_musicScratch[i];
        }
    }
    for (int i = 0; i < frames * 2; i++) {
        // A dozen voices at the volumes the game asks for overshoot now and
        // then; clipping quietly beats wrapping loudly.
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
    }
}

void Mixer::mixVoice(Voice& voice, float* out, int frames)
{
    const PcmBuffer& buffer = *voice.buffer;
    const VoiceParams& p = voice.params;
    // Placement: OpenAL's linear clamped distance model, then equal-power panning.
    std::array<float, 3> relative = p.position;
    if (!p.relative) {
        for (int i = 0; i < 3; i++) {
            relative[i] -= m_listenerPosition[i];
        }
    }
    const float distance = std::sqrt(relative[0] * relative[0] + relative[1] * relative[1] + relative[2] * relative[2]);
    float attenuation = 1;
    const float span = p.maxDistance - p.referenceDistance;
    // ReferenceDistance == MaxDistance == float max is the engine's "do not attenuate".
    if (p.rolloff > 0 && span > 0 && std::isfinite(span) && p.maxDistance < FLT_MAX) {
        const float clamped = std::clamp(distance, p.referenceDistance, p.maxDistance);
        attenuation = std::clamp(1 - p.rolloff * (clamped - p.referenceDistance) / span, 0.0f, 1.0f);
    }
    float pan = 0;
    if (distance > 0.0001f) {
        const std::array<float, 3> facing = p.relative ? std::array<float, 3>{0, 0, -1} : m_listenerFacing;
        const std::array<float, 3> up = p.relative ? std::array<float, 3>{0, 1, 0} : m_listenerUp;
        const std::array<float, 3> side{facing[1] * up[2] - facing[2] * up[1], facing[2] * up[0] - facing[0] * up[2],
            facing[0] * up[1] - facing[1] * up[0]};
        const float sideLength = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (sideLength > 0.01f) {
            pan = std::clamp((relative[0] * side[0] + relative[1] * side[1] + relative[2] * side[2]) / (distance * sideLength), -1.0f, 1.0f);
        }
    }
    // Scaled so a centred source is at full gain in both ears rather than at -3 dB.
    const float left = std::min(1.0f, std::sqrt(0.5f * (1 - pan)) * 1.41421356f) * attenuation * p.gain;
    const float right = std::min(1.0f, std::sqrt(0.5f * (1 + pan)) * 1.41421356f) * attenuation * p.gain;
    const double step = buffer.sampleRate / static_cast<double>(m_rate) * p.pitch;
    int loopStart = 0;
    int loopEnd = buffer.frames;
    if (buffer.loopEnd > buffer.loopStart && buffer.loopStart >= 0) {
        loopStart = std::min(buffer.loopStart, buffer.frames - 1);
        loopEnd = std::min(buffer.loopEnd, buffer.frames);
    }
    for (int i = 0; i < frames; i++) {
        if (voice.cursor >= (p.loop ? loopEnd : buffer.frames)) {
            if (p.loop && loopEnd > loopStart) {
                voice.cursor = loopStart + std::fmod(voice.cursor - loopEnd, static_cast<double>(loopEnd - loopStart));
            } else {
                voice.playing = false;
                voice.buffer.reset();
                return;
            }
        }
        const int frame = static_cast<int>(voice.cursor);
        const int next = std::min(frame + 1, buffer.frames - 1);
        const float blend = static_cast<float>(voice.cursor - frame);
        float l, r;
        if (buffer.channels == 2) {
            l = buffer.data[frame * 2] + (buffer.data[next * 2] - buffer.data[frame * 2]) * blend;
            r = buffer.data[frame * 2 + 1] + (buffer.data[next * 2 + 1] - buffer.data[frame * 2 + 1]) * blend;
        } else {
            l = r = buffer.data[frame] + (buffer.data[next] - buffer.data[frame]) * blend;
        }
        out[i * 2] += l * left;
        out[i * 2 + 1] += r * right;
        voice.cursor += step;
    }
}

} // namespace fp
