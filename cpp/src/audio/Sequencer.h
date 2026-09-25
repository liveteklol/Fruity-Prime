#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The DS sound driver playing an SSEQ, ported from NcsfPlay (itself after
// the Pokémon Diamond decompilation): sixteen tracks reading the sequence,
// sixteen channels with their envelopes, LFO and sweep, PCM samples from the
// bank's wave archives, PSG and noise. The music of the game, read straight
// from sound_data.sdat (the _seq/*.minincsf stubs only name the sequence).
namespace fp {

// SWAV: one decoded sample of a wave archive.
struct Swav {
    std::vector<float> data;
    int waveType = 0; // 0 PCM8, 1 PCM16, 2 IMA-ADPCM
    bool loop = false;
    uint16_t time = 0; // the channel timer for its own rate
    uint32_t loopOffset = 0, loopLength = 0; // in samples
};

struct Swar {
    std::map<uint32_t, Swav> swavs;
};

struct Instrument {
    uint8_t record = 0, lowNote = 0, highNote = 127;
    uint16_t swav = 0, swar = 0;
    uint8_t noteNumber = 60, attack = 127, decay = 127, sustain = 127, release = 127, pan = 64;
};

struct Sbnk {
    struct Entry {
        uint8_t record = 0;
        std::vector<Instrument> instruments;
    };
    std::vector<Entry> entries;
};

// The parts of an SDAT the music needs, loaded once; banks and wave archives decoded on first use.
class SoundArchive {
public:
    // Throws when the file is not an SDAT.
    explicit SoundArchive(std::vector<uint8_t> sdat);

    struct Sequence {
        std::vector<uint8_t> data;
        const Sbnk* bank = nullptr;
        std::array<const Swar*, 4> swars{};
        int volume = 127;
        uint16_t channelMask = 0xFFFF;
    };
    // SSEQ `index` with what it plays with, or false.
    bool sequence(int index, Sequence& out);
    size_t sequenceCount() const { return m_seqInfo.size(); }

private:
    struct SeqInfo {
        uint32_t fileId;
        uint16_t bank;
        uint8_t volume, playerNo;
        bool valid;
    };
    struct BankInfo {
        uint32_t fileId;
        std::array<uint16_t, 4> waveArcs;
        bool valid;
    };
    const uint8_t* file(uint32_t fileId, uint32_t& size) const;
    const Swar* swar(uint16_t index);

    std::vector<uint8_t> m_sdat;
    std::vector<SeqInfo> m_seqInfo;
    std::vector<BankInfo> m_bankInfo;
    std::vector<uint32_t> m_waveArcFiles;
    std::vector<uint16_t> m_playerMasks;
    std::map<uint16_t, std::unique_ptr<Sbnk>> m_banks;
    std::map<uint16_t, std::unique_ptr<Swar>> m_swars;
};

class Sequencer {
public:
    Sequencer();
    ~Sequencer();
    Sequencer(Sequencer&&) noexcept;
    Sequencer& operator=(Sequencer&&) noexcept;

    // Starts `sequence` for output at `sampleRate`; skips up to five seconds of silence at the start.
    void start(const SoundArchive::Sequence& sequence, uint32_t sampleRate);
    // Adds the next `frames` stereo frames, times `gain`.
    void render(float* out, int frames, float gain);
    bool playing() const;
    void stop();

    // Player.TempoRatio: 256 is normal speed.
    uint16_t tempoRatio() const;
    void setTempoRatio(uint16_t ratio);
    // The sequence's track `index` (its own numbering), if it has one: volume 0-127 and mute.
    bool hasTrack(int index) const;
    uint8_t trackVolume(int index) const;
    void setTrackVolume(int index, uint8_t volume);
    void setTrackMute(int index, bool mute);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace fp
