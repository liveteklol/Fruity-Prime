#pragma once

#include "Sequencer.h"
#include "SoundIds.h"

#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

// The music, ported from Sound/Music.cs: the rooms' music (ASSIGNMUSIC.DAT)
// as entries of INTERMUSICINFO.DAT -- a sequence, the tracks of it that
// play, and fade times -- with changes queued behind a fade out, tracks
// faded in and out, the volume faded, and the tempo ramped. Silent until
// load() succeeds.
namespace fp {

class Mixer;

class Music {
public:
    static Music& instance();

    bool load(const std::filesystem::path& root, Mixer* mixer);
    void unload();
    bool loaded() const { return m_mixer != nullptr; }

    float userVolume = 1;

    void playMusic(MusicId id, int tracks = -1, bool toggleOnTracks = false, bool toggleOffTracks = false);
    // PlayRoomMusic: the room's track 0 (normal), 1 or 2 (the modes' tension).
    void playRoomMusic(int roomId, int track);
    void playSeq(SeqId seq, uint16_t tracks = 0xFFFF, bool queue = false, int fadeOutFrames = 0, int fadeInFrames = 0);
    void stop(float fadeTime = 0);
    void fadeVolume(float volume, float time, bool stopAfterFade = false);
    // UpdateTempo: 256 is normal; over `time` seconds, or at once.
    void updateTempo(uint16_t tempo, float time);
    // UpdateMusic, once a tick: the fades, the tempo, the queued music.
    void update(float time);

private:
    Music() = default;
    struct TrackFader {
        uint8_t start = 127, target = 127;
        float time = 0, elapsed = 0;
        bool running = false;
    };
    void playerStop(); // MusicPlayer.Stop
    void updateTrackVolume(uint16_t tracks, uint8_t volume);
    void setTrackFaders(uint16_t tracks, uint8_t target, float time);

    Mixer* m_mixer = nullptr;
    std::unique_ptr<SoundArchive> m_archive;
    struct MusicTrack {
        int seqId;
        uint16_t fadeOutFrames, tracks, fadeInFrames;
    };
    std::vector<MusicTrack> m_musicInfo;
    struct RoomMusic {
        uint16_t roomId;
        std::array<uint16_t, 3> trackIds;
    };
    std::vector<RoomMusic> m_roomMusic;

    std::mutex m_lock; // the sequencer, shared with the audio thread
    Sequencer m_sequencer;
    bool m_playerPlaying = false; // MusicPlayer.State != Stopped
    float m_playerVolume = 1;

    MusicId m_currentMusicId = MusicId::None;
    int m_currentSeq = -1, m_nextSeq = -1;
    bool m_playing = false, m_musicQueued = false;
    uint16_t m_nextTracks = 0, m_pendingTracks = 0, m_activeTracks = 0, m_mutedTracks = 0, m_fadingTracks = 0;
    uint16_t m_nextFadeInFrames = 0;
    float m_musicVolume = 1;
    float m_volumeFadeStart = 1, m_volumeFadeTarget = 1, m_volumeFadeTime = 0, m_volumeFadeElapsed = 0;
    bool m_volumeFading = false, m_stopAfterFade = false;
    uint16_t m_baseTempo = 256, m_tempoStart = 256, m_tempoTarget = 256;
    float m_tempoTime = 0, m_tempoElapsed = 0;
    bool m_tempoRamping = false;
    std::array<TrackFader, 16> m_trackFaders{};
};

} // namespace fp
