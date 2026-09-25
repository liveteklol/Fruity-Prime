#pragma once

#include "Mixer.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// The game's sound files under data/sound, ported from Formats/Sound.cs:
// the samples (SNDSAMPLES.DAT, IMA-ADPCM), their table (SNDTBLS.DAT), the 3D
// ranges (SND3DLIST.DAT), the sound scripts (SFXSCRIPTFILES.DAT), the
// dynamic sounds (DGNFILES.DAT) and the voice streams of sound_data.sdat.
namespace fp {

struct SoundSample {
    std::string name;
    bool loop = false;
    float volume = 1; // SNDTBLS InitialVolume / 127
    std::shared_ptr<PcmBuffer> pcm; // null for an empty slot
};

// Sound3dEntry: where a sound starts to fade and where it is gone.
struct SoundRange {
    float falloffDistance = 1, maxDistance = FLT_MAX;
};

struct SfxScriptEntry {
    int sfxData = 0; // id, 0x4000 loop, 0x8000 stop
    float delay = 0; // seconds
    float volume = 1;
    float pan = -1; // -1: none
    float pitch = 1;
};

struct SfxScriptFile {
    std::string name;
    std::vector<SfxScriptEntry> entries;
};

struct DgnData {
    uint16_t amount, value;
};

// DGN: up to three samples whose volume and pitch follow two amounts.
struct DgnEntry {
    int sfxId = 0;
    std::vector<DgnData> data[4]; // volume percentage, max volume, pitch percentage, max pitch
};

struct DgnFile {
    std::string name;
    int initialVolume = 127;
    std::vector<DgnEntry> entries;
};

struct SoundStream {
    std::string name;
    bool loop = false;
    float volume = 1;
    std::shared_ptr<PcmBuffer> pcm;
};

struct SoundData {
    std::vector<SoundSample> samples;
    std::vector<SoundRange> ranges;
    std::vector<SfxScriptFile> scripts;
    std::vector<DgnFile> dgns;
    std::vector<SoundStream> streams;
    // Metadata.HunterSfx, BeamSfx, TerrainSfx out of overlay9_2: sound ids by
    // hunter and HunterSfx, beam and BeamSfx, terrain and TerrainSfx; -1 for none.
    std::array<std::array<int, 17>, 8> hunterSfx{};
    std::array<std::array<int, 10>, 9> beamSfx{};
    std::array<std::array<int, 6>, 12> terrainSfx{};

    // Throws when a file is missing or malformed.
    static SoundData load(const std::filesystem::path& root);
};

// Sfx.CalculatePitchDiv: a DS pitch factor (0x2000 = unchanged) as a playback rate.
float pitchDiv(float pitchFactor);

// SoundRead.GetWaveData for IMA-ADPCM: `count` samples after the 4-byte header.
std::vector<float> decodeAdpcm(const uint8_t* data, size_t size, uint32_t count);

} // namespace fp
