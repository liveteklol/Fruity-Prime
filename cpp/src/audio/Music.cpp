#include "Music.h"

#include "Mixer.h"
#include "formats/Model.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace fp {

Music& Music::instance()
{
    static Music music;
    return music;
}

bool Music::load(const std::filesystem::path& root, Mixer* mixer)
{
    unload();
    if (mixer == nullptr) {
        return false;
    }
    try {
        m_archive = std::make_unique<SoundArchive>(readFile(resolveCaseInsensitive(root, "data/sound/sound_data.sdat")));
        const std::vector<uint8_t> info = readFile(resolveCaseInsensitive(root, "data/sound/INTERMUSICINFO.DAT"));
        const std::vector<uint8_t> assign = readFile(resolveCaseInsensitive(root, "data/sound/ASSIGNMUSIC.DAT"));
        auto u16 = [](const std::vector<uint8_t>& b, size_t at) { return static_cast<uint16_t>(b.at(at) | b.at(at + 1) << 8); };
        auto u32 = [](const std::vector<uint8_t>& b, size_t at) { return b.at(at) | b.at(at + 1) << 8 | b.at(at + 2) << 16 | b.at(at + 3) << 24; };
        const uint32_t infoCount = u32(info, 0);
        m_musicInfo.clear();
        for (uint32_t i = 0; i < infoCount; i++) {
            // RawMusicTrack: sequence, fade out frames, tracks, fade in frames.
            const size_t o = 4 + i * 8;
            const uint16_t seq = u16(info, o);
            m_musicInfo.push_back({seq == 0xFFFF ? -1 : seq, u16(info, o + 2), u16(info, o + 4), u16(info, o + 6)});
        }
        const uint32_t roomCount = u32(assign, 0);
        m_roomMusic.clear();
        for (uint32_t i = 0; i < roomCount; i++) {
            const size_t o = 4 + i * 8;
            m_roomMusic.push_back({u16(assign, o), {u16(assign, o + 2), u16(assign, o + 4), u16(assign, o + 6)}});
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[sound] cannot read the music (%s); continuing without it\n", e.what());
        m_archive.reset();
        return false;
    }
    m_mixer = mixer;
    m_mixer->setMusic([this](float* out, int frames) {
        std::lock_guard lock(m_lock);
        if (m_playerPlaying) {
            m_sequencer.render(out, frames, std::clamp(m_playerVolume, 0.0f, 1.0f));
        }
    });
    return true;
}

void Music::unload()
{
    if (m_mixer != nullptr) {
        m_mixer->setMusic({});
    }
    m_mixer = nullptr;
    std::lock_guard lock(m_lock);
    m_sequencer.stop();
    m_playerPlaying = false;
    m_playing = m_musicQueued = false;
    m_currentSeq = m_nextSeq = -1;
    m_currentMusicId = MusicId::None;
}

void Music::playerStop()
{
    std::lock_guard lock(m_lock);
    m_playerPlaying = false;
}

void Music::playMusic(MusicId id, int tracks, bool toggleOnTracks, bool toggleOffTracks)
{
    const int index = static_cast<int>(id);
    if (!loaded() || index < 0 || index >= static_cast<int>(m_musicInfo.size())) {
        return;
    }
    const MusicTrack& info = m_musicInfo[index];
    if (info.seqId < 0) {
        return;
    }
    const uint16_t mask = tracks < 0 ? info.tracks : static_cast<uint16_t>(tracks);
    if (toggleOnTracks) {
        m_pendingTracks |= mask;
    } else if (toggleOffTracks) {
        m_pendingTracks &= static_cast<uint16_t>(~mask);
    } else {
        m_pendingTracks = mask;
    }
    m_currentMusicId = id;
    playSeq(static_cast<SeqId>(info.seqId), m_pendingTracks, true, info.fadeOutFrames, info.fadeInFrames);
}

void Music::playRoomMusic(int roomId, int track)
{
    track = std::clamp(track, 0, 2);
    for (const RoomMusic& room : m_roomMusic) {
        if (room.roomId == roomId) {
            playMusic(static_cast<MusicId>(room.trackIds[track]));
            return;
        }
    }
}

void Music::playSeq(SeqId seq, uint16_t tracks, bool queue, int fadeOutFrames, int fadeInFrames)
{
    if (!loaded()) {
        return;
    }
    const int seqId = static_cast<int>(seq);
    if (!queue) {
        stop();
        m_activeTracks = tracks;
        m_currentSeq = seqId;
        m_mutedTracks = 0;
        m_fadingTracks = static_cast<uint16_t>(~m_activeTracks);
        m_musicVolume = 1;
        m_volumeFadeTarget = 1;
        m_volumeFading = false;
        for (int i = 0; i < 16; i++) {
            const uint8_t volume = (m_activeTracks & (1 << i)) ? 127 : 0;
            m_trackFaders[i] = {volume, volume, 0, 0, false};
        }
        {
            // MusicPlayer.Load, then Play: the tracks left out start silent.
            // Started here, then handed to the audio thread.
            Sequencer sequencer;
            SoundArchive::Sequence sequence;
            const bool found = seqId >= 0 && m_archive->sequence(seqId, sequence);
            if (found) {
                sequencer.start(sequence, static_cast<uint32_t>(m_mixer->sampleRate()));
                for (int i = 0; i < 16; i++) {
                    if (!(tracks & (1 << i))) {
                        sequencer.setTrackVolume(i, 0);
                        sequencer.setTrackMute(i, true);
                    }
                }
            }
            if (std::getenv("FP_DEBUG_SOUND") != nullptr) {
                std::fprintf(stderr, "[sound] music: sequence %d, tracks %04x%s\n", seqId, tracks, found ? "" : " (missing)");
            }
            std::lock_guard lock(m_lock);
            m_sequencer = std::move(sequencer);
            m_playerPlaying = found;
            m_playerVolume = userVolume * m_musicVolume;
        }
        m_playing = true;
        m_mutedTracks = 0;
        updateTrackVolume(m_fadingTracks, 0);
        m_fadingTracks = 0;
        updateTempo(256, 0);
        m_musicQueued = false;
        m_nextSeq = seqId;
        return;
    }
    if (!m_musicQueued) {
        if (m_nextSeq == seqId) {
            // The same sequence: only its tracks fade in or out.
            setTrackFaders(tracks, 127, fadeInFrames / 30.0f);
            setTrackFaders(static_cast<uint16_t>(tracks ^ 0xFFFF), 0, fadeOutFrames / 30.0f);
            return;
        }
        stop(fadeOutFrames / 30.0f);
        m_musicQueued = true;
    }
    m_nextSeq = seqId;
    m_nextTracks = tracks;
    m_nextFadeInFrames = static_cast<uint16_t>(fadeInFrames);
}

void Music::stop(float fadeTime)
{
    m_playing = false;
    m_musicQueued = false;
    m_nextSeq = -1;
    if (fadeTime <= 0) {
        playerStop();
    } else {
        fadeVolume(0, fadeTime, true);
    }
}

void Music::fadeVolume(float volume, float time, bool stopAfterFade)
{
    m_musicVolume = volume;
    {
        std::lock_guard lock(m_lock);
        m_volumeFadeStart = m_playerVolume;
    }
    m_volumeFadeTarget = volume;
    m_volumeFadeTime = std::max(time, 0.001f);
    m_volumeFadeElapsed = 0;
    m_volumeFading = true;
    m_stopAfterFade = stopAfterFade;
}

void Music::updateTempo(uint16_t tempo, float time)
{
    if (time <= 0) {
        {
            std::lock_guard lock(m_lock);
            m_sequencer.setTempoRatio(tempo);
        }
        m_baseTempo = tempo;
        m_tempoStart = m_tempoTarget = tempo;
        m_tempoRamping = false;
    } else if (m_tempoTarget != tempo) {
        std::lock_guard lock(m_lock);
        m_tempoStart = m_sequencer.tempoRatio();
        m_tempoTarget = tempo;
        m_tempoTime = time;
        m_tempoElapsed = 0;
        m_tempoRamping = true;
    }
}

void Music::updateTrackVolume(uint16_t tracks, uint8_t volume)
{
    std::lock_guard lock(m_lock);
    if (volume > 0) {
        for (int i = 0; i < 16; i++) {
            if (tracks & (1 << i)) {
                m_sequencer.setTrackVolume(i, volume);
            }
        }
        if (m_mutedTracks & tracks) {
            for (int i = 0; i < 16; i++) {
                if (tracks & (1 << i)) {
                    m_sequencer.setTrackMute(i, false);
                }
            }
            m_mutedTracks &= static_cast<uint16_t>(~(m_mutedTracks & tracks));
        }
    } else if ((m_mutedTracks & tracks) != tracks) {
        for (int i = 0; i < 16; i++) {
            if (tracks & (1 << i)) {
                m_sequencer.setTrackMute(i, true);
            }
        }
        m_mutedTracks |= tracks;
    }
}

void Music::setTrackFaders(uint16_t tracks, uint8_t target, float time)
{
    if (target == 0) {
        m_activeTracks &= static_cast<uint16_t>(~tracks);
    } else {
        m_activeTracks |= tracks;
    }
    std::lock_guard lock(m_lock);
    for (int i = 0; i < 16; i++) {
        if (tracks & (1 << i)) {
            TrackFader& fader = m_trackFaders[i];
            if (m_sequencer.hasTrack(i)) {
                fader.start = m_sequencer.trackVolume(i);
            }
            fader.target = target;
            fader.time = time;
            fader.elapsed = 0;
            fader.running = true;
        }
    }
    m_fadingTracks |= tracks;
}

void Music::update(float time)
{
    if (!loaded()) {
        return;
    }
    bool playerPlaying;
    {
        std::lock_guard lock(m_lock);
        playerPlaying = m_playerPlaying;
    }
    if (!playerPlaying) {
        if (m_musicQueued) {
            // Stopped (faded out): the queued music starts.
            playSeq(static_cast<SeqId>(m_nextSeq), m_nextTracks, false, 0, m_nextFadeInFrames);
        }
        return;
    }
    // ProcessVolume
    if (m_volumeFading) {
        m_volumeFadeElapsed += time;
        const float pct = m_volumeFadeElapsed / m_volumeFadeTime;
        if (pct >= 1) {
            m_musicVolume = m_volumeFadeTarget;
            m_volumeFading = false;
            if (m_stopAfterFade) {
                playerStop();
            }
            m_stopAfterFade = false;
        } else {
            m_musicVolume = m_volumeFadeStart + (m_volumeFadeTarget - m_volumeFadeStart) * pct;
        }
    }
    {
        std::lock_guard lock(m_lock);
        m_playerVolume = userVolume * m_musicVolume;
        // ProcessTempo
        if (m_tempoRamping && m_sequencer.tempoRatio() != m_tempoTarget) {
            m_tempoElapsed += time;
            const float pct = m_tempoElapsed / m_tempoTime;
            if (pct >= 1) {
                m_sequencer.setTempoRatio(m_tempoTarget);
                m_tempoRamping = false;
            } else {
                m_sequencer.setTempoRatio(static_cast<uint16_t>(m_tempoStart + (m_tempoTarget - m_tempoStart) * pct));
            }
        }
    }
    // ProcessTrackFaders
    if (m_fadingTracks == 0) {
        return;
    }
    for (int i = 0; i < 16; i++) {
        if (!(m_fadingTracks & (1 << i))) {
            continue;
        }
        TrackFader& fader = m_trackFaders[i];
        if (!fader.running) {
            continue;
        }
        uint8_t volume;
        bool hasTrack;
        {
            std::lock_guard lock(m_lock);
            hasTrack = m_sequencer.hasTrack(i);
            volume = m_sequencer.trackVolume(i);
        }
        if (!hasTrack || volume == fader.target) {
            continue;
        }
        fader.elapsed += time;
        const float pct = fader.time > 0 ? fader.elapsed / fader.time : 1;
        if (pct >= 1) {
            updateTrackVolume(static_cast<uint16_t>(1 << i), fader.target);
            fader.running = false;
            m_fadingTracks &= static_cast<uint16_t>(~(1 << i));
        } else {
            updateTrackVolume(static_cast<uint16_t>(1 << i), static_cast<uint8_t>(fader.start + (fader.target - fader.start) * pct));
        }
        std::lock_guard lock(m_lock);
        m_sequencer.setTrackMute(i, m_sequencer.trackVolume(i) == 0);
    }
}

} // namespace fp
