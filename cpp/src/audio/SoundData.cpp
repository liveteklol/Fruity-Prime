#include "SoundData.h"

#include "formats/Model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace fp {

namespace {

constexpr int adpcmTable[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
    88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr int imaIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

class Reader {
public:
    explicit Reader(std::vector<uint8_t> bytes) : m_bytes(std::move(bytes)) {}
    const std::vector<uint8_t>& bytes() const { return m_bytes; }
    template <typename T>
    T get(size_t offset) const
    {
        if (offset + sizeof(T) > m_bytes.size()) {
            throw std::runtime_error("sound file truncated");
        }
        T value;
        std::memcpy(&value, m_bytes.data() + offset, sizeof(T));
        return value;
    }
    uint32_t u32(size_t o) const { return get<uint32_t>(o); }
    uint16_t u16(size_t o) const { return get<uint16_t>(o); }
    uint8_t u8(size_t o) const { return get<uint8_t>(o); }
    std::string string(size_t offset) const
    {
        std::string s;
        while (offset < m_bytes.size() && m_bytes[offset] != 0) {
            s.push_back(static_cast<char>(m_bytes[offset++]));
        }
        return s;
    }
    // Read.ReadStrings: `count` NUL-terminated strings one after the other.
    std::vector<std::string> strings(size_t offset, size_t count) const
    {
        std::vector<std::string> result;
        for (size_t i = 0; i < count; i++) {
            result.push_back(string(offset));
            offset += result.back().size() + 1;
        }
        return result;
    }

private:
    std::vector<uint8_t> m_bytes;
};

Reader open(const std::filesystem::path& root, const char* file)
{
    return Reader(readFile(resolveCaseInsensitive(root, std::string("data/sound/") + file)));
}

// One PCM8 or IMA-ADPCM block, appended to `out`.
void decode(const uint8_t* data, size_t size, int format, uint32_t count, std::vector<float>& out)
{
    if (format == 2) {
        const std::vector<float> samples = decodeAdpcm(data, size, count);
        out.insert(out.end(), samples.begin(), samples.end());
    } else {
        for (uint32_t i = 0; i < count && i < size; i++) {
            out.push_back(static_cast<int8_t>(data[i]) / 128.0f);
        }
    }
}

} // namespace

float pitchDiv(float pitchFactor)
{
    if (pitchFactor == 0) {
        pitchFactor = 1;
    }
    int pitch = static_cast<int>(pitchFactor);
    if (pitchFactor <= 0xFFF) {
        pitch = -((0x600000 / pitch) >> 1);
    } else if (pitchFactor <= 0x1FFF) {
        pitch = (768 * (pitch - 0x2000)) >> 12;
    } else {
        pitch = (768 * (pitch - 0x2000)) >> 13;
    }
    const float semitones = pitch / 64.0f;
    const float octaves = std::abs(semitones / 12);
    return semitones >= 0 ? std::pow(2.0f, octaves) : std::pow(0.5f, octaves);
}

std::vector<float> decodeAdpcm(const uint8_t* data, size_t size, uint32_t count)
{
    std::vector<float> out;
    if (size < 4) {
        return out;
    }
    out.reserve(count);
    int16_t first, index;
    std::memcpy(&first, data, 2);
    std::memcpy(&index, data + 2, 2);
    int sample = first;
    int stepIndex = std::clamp<int>(index, 0, 88);
    size_t at = 4;
    bool low = true;
    for (uint32_t i = 0; i < count && at < size; i++) {
        uint8_t value = data[at];
        if (!low) {
            value >>= 4;
            at++;
        }
        value &= 0xF;
        const int step = adpcmTable[stepIndex];
        int diff = step >> 3;
        if (value & 1) {
            diff += step >> 2;
        }
        if (value & 2) {
            diff += step >> 1;
        }
        if (value & 4) {
            diff += step;
        }
        sample = std::clamp((value & 8) ? sample - diff : sample + diff, -32768, 32767);
        stepIndex = std::clamp(stepIndex + imaIndexTable[value], 0, 88);
        out.push_back(sample / 32768.0f);
        low = !low;
    }
    return out;
}

SoundData SoundData::load(const std::filesystem::path& root)
{
    SoundData sound;
    {
        // SNDSAMPLES.DAT: offsets to a SoundSampleHeader and its data.
        const Reader r = open(root, "SNDSAMPLES.DAT");
        const uint32_t count = r.u32(0);
        for (uint32_t i = 0; i < count; i++) {
            SoundSample sample;
            const uint32_t offset = r.u32(4 + i * 4);
            if (offset != 0) {
                const int format = r.u8(offset);
                sample.loop = r.u8(offset + 1) != 0;
                const uint16_t rate = r.u16(offset + 2);
                const uint32_t loopStartWords = r.u16(offset + 6);
                const uint32_t loopLengthWords = r.u32(offset + 8);
                const size_t start = offset + 12;
                const size_t size = (loopStartWords + loopLengthWords) * 4;
                if (start + size > r.bytes().size()) {
                    throw std::runtime_error("SNDSAMPLES.DAT: sample out of the file");
                }
                auto pcm = std::make_shared<PcmBuffer>();
                pcm->sampleRate = rate;
                uint32_t loopStart = loopStartWords, loopLength = loopLengthWords;
                if (format == 2) {
                    loopStart = (loopStartWords * 4 - 4) * 2;
                    loopLength = loopLengthWords * 8;
                }
                decode(r.bytes().data() + start, size, format, loopStart + loopLength, pcm->data);
                pcm->frames = static_cast<int>(pcm->data.size());
                // Loop points only matter when it loops (AL LoopPointsSOFT).
                pcm->loopStart = static_cast<int>(loopStart);
                pcm->loopEnd = static_cast<int>(loopStart + loopLength);
                sample.pcm = std::move(pcm);
            }
            sound.samples.push_back(std::move(sample));
        }
    }
    {
        // SNDTBLS.DAT: 12-byte RawSoundTableEntry each, then the names.
        const Reader r = open(root, "SNDTBLS.DAT");
        const uint32_t count = r.u32(0);
        const std::vector<std::string> names = r.strings(4 + count * 12, count);
        for (uint32_t i = 0; i < count && i < sound.samples.size(); i++) {
            sound.samples[i].volume = r.u8(4 + i * 12 + 4) / 127.0f;
            sound.samples[i].name = names[i];
        }
    }
    {
        const Reader r = open(root, "SND3DLIST.DAT");
        const uint32_t count = r.u32(0);
        for (uint32_t i = 0; i < count; i++) {
            sound.ranges.push_back({r.u32(4 + i * 8) / 4096.0f, r.u32(8 + i * 8) / 4096.0f});
        }
    }
    {
        // SFXSCRIPTFILES.DAT: SfxScriptHeader (offset, size, volume, slots) each, the names after the last file.
        const Reader r = open(root, "SFXSCRIPTFILES.DAT");
        const uint32_t count = r.u32(0);
        const size_t namesAt = r.u32(4 + (count - 1) * 8) + r.u16(8 + (count - 1) * 8);
        const std::vector<std::string> names = r.strings(namesAt, count);
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t offset = r.u32(4 + i * 8);
            const int initialVolume = r.u8(10 + i * 8);
            SfxScriptFile file;
            file.name = names[i];
            const uint32_t entries = r.u32(offset);
            for (uint32_t j = 0; j < entries; j++) {
                const size_t o = offset + 4 + j * 12;
                SfxScriptEntry entry;
                entry.sfxData = r.u16(o);
                entry.delay = r.u16(o + 2) / 30.0f;
                entry.volume = r.u8(o + 4) / 127.0f * initialVolume / 127.0f;
                if (int pan = r.u8(o + 5); pan != 255) {
                    if (pan == 127) {
                        pan = 128;
                    }
                    entry.pan = (pan - 64) / 64.0f / 2.0f;
                }
                entry.pitch = pitchDiv(r.u16(o + 6));
                file.entries.push_back(entry);
            }
            sound.scripts.push_back(std::move(file));
        }
    }
    {
        // DGNFILES.DAT: the same headers; each entry four curves of (amount, value).
        const Reader r = open(root, "DGNFILES.DAT");
        const uint32_t count = r.u32(0);
        const size_t namesAt = r.u32(4 + (count - 1) * 8) + r.u16(8 + (count - 1) * 8);
        const std::vector<std::string> names = r.strings(namesAt, count);
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t offset = r.u32(4 + i * 8);
            DgnFile file;
            file.name = names[i];
            file.initialVolume = r.u8(10 + i * 8);
            const uint32_t entries = r.u32(offset);
            for (uint32_t j = 0; j < entries; j++) {
                const size_t o = offset + 4 + j * 36;
                DgnEntry entry;
                entry.sfxId = r.u16(o + 2);
                for (int k = 0; k < 4; k++) {
                    const uint32_t n = r.u32(o + 4 + k * 8);
                    const uint32_t at = offset + r.u32(o + 8 + k * 8);
                    for (uint32_t m = 0; m < n; m++) {
                        entry.data[k].push_back({r.u16(at + m * 4), r.u16(at + m * 4 + 2)});
                    }
                }
                file.entries.push_back(std::move(entry));
            }
            sound.dgns.push_back(std::move(file));
        }
    }
    {
        // sound_data.sdat: the STRM files, found through the INFO and FAT blocks.
        const Reader r = open(root, "sound_data.sdat");
        if (std::memcmp(r.bytes().data(), "SDAT", 4) != 0) {
            throw std::runtime_error("sound_data.sdat: not an SDAT");
        }
        const uint32_t symb = r.u32(16), info = r.u32(24), fat = r.u32(32);
        auto list = [&](uint32_t block, uint32_t table) {
            std::vector<uint32_t> offsets;
            const uint32_t count = r.u32(block + table);
            for (uint32_t i = 0; i < count; i++) {
                if (const uint32_t o = r.u32(block + table + 4 + i * 4); o != 0) {
                    offsets.push_back(block + o);
                }
            }
            return offsets;
        };
        const std::vector<uint32_t> names = list(symb, r.u32(symb + 36));
        const std::vector<uint32_t> infos = list(info, r.u32(info + 36));
        for (size_t i = 0; i < infos.size(); i++) {
            SoundStream stream;
            stream.name = i < names.size() ? r.string(names[i]) : std::string();
            stream.volume = r.u8(infos[i] + 4) / 127.0f;
            const uint32_t fileId = r.u32(infos[i]);
            const uint32_t at = r.u32(fat + 12 + fileId * 16);
            const int format = r.u8(at + 24);
            stream.loop = r.u8(at + 25) != 0;
            const int channels = r.u8(at + 26);
            const uint32_t dataOffset = r.u32(at + 40), blockCount = r.u32(at + 44), blockSize = r.u32(at + 48);
            const uint32_t blockSamples = r.u32(at + 52), lastBlockSize = r.u32(at + 56), lastBlockSamples = r.u32(at + 60);
            auto pcm = std::make_shared<PcmBuffer>();
            pcm->sampleRate = r.u16(at + 28);
            pcm->channels = channels;
            std::vector<std::vector<float>> decoded(static_cast<size_t>(channels));
            for (int j = 0; j < channels; j++) {
                // Blocks interleave by channel; the last ones are shorter.
                uint32_t start = at + dataOffset + blockSize * j;
                for (uint32_t k = 0; k < blockCount; k++) {
                    const bool last = k == blockCount - 1;
                    const uint32_t size = last ? lastBlockSize : blockSize;
                    const uint32_t samples = last ? lastBlockSamples : blockSamples;
                    uint32_t increment = 0;
                    if (!last) {
                        increment = j > 0 && k == blockCount - 2 ? size + lastBlockSize : size * channels;
                    }
                    if (start + size > r.bytes().size()) {
                        throw std::runtime_error("sound_data.sdat: stream out of the file");
                    }
                    decode(r.bytes().data() + start, size, format, samples, decoded[j]);
                    start += increment;
                }
            }
            pcm->frames = channels > 0 ? static_cast<int>(decoded[0].size()) : 0;
            pcm->data.resize(static_cast<size_t>(pcm->frames) * channels);
            for (int j = 0; j < channels; j++) {
                for (int f = 0; f < pcm->frames && f < static_cast<int>(decoded[j].size()); f++) {
                    pcm->data[static_cast<size_t>(f) * channels + j] = decoded[j][f];
                }
            }
            stream.pcm = std::move(pcm);
            sound.streams.push_back(std::move(stream));
        }
    }
    {
        // Extract._romData: TerrianSfx, BeamSfx, HunterSfx in overlay9_2 for each release.
        struct Offsets {
            const char* version;
            uint32_t terrain, beam, hunter;
        };
        static constexpr Offsets table[] = {
            {"AMHE0", 0x1DA08, 0x1DA98, 0x1DB4C}, {"AMHP0", 0x1DA08, 0x1DA98, 0x1DB4C}, {"AMHE1", 0x1DA68, 0x1DAF8, 0x1DBAC},
            {"AMHP1", 0x1DA68, 0x1DAF8, 0x1DBAC}, {"AMHJ0", 0x1DA68, 0x1DAF8, 0x1DBAC}, {"AMHJ1", 0x1DA68, 0x1DAF8, 0x1DBAC},
            {"AMHK0", 0x1BDBA, 0x1BE4A, 0x1BEFE}, {"A76E0", 0x1D828, 0x1D8B8, 0x1D96C},
        };
        const Reader r(readFile(resolveCaseInsensitive(root, "_bin/overlay9_2")));
        auto read = [&](const Offsets& o) {
            auto value = [&](uint32_t at) {
                const uint16_t v = r.u16(at);
                return v == 0xFFFF ? -1 : static_cast<int>(v);
            };
            for (int h = 0; h < 8; h++) {
                for (int i = 0; i < 17; i++) {
                    sound.hunterSfx[h][i] = value(o.hunter + (h * 17 + i) * 2);
                }
            }
            for (int b = 0; b < 9; b++) {
                for (int i = 0; i < 10; i++) {
                    sound.beamSfx[b][i] = value(o.beam + (b * 10 + i) * 2);
                }
            }
            for (int t = 0; t < 12; t++) {
                for (int i = 0; i < 6; i++) {
                    sound.terrainSfx[t][i] = value(o.terrain + (t * 6 + i) * 2);
                }
            }
        };
        // By the folder's name, else the first set that reads as sound ids.
        const std::string folder = root.filename().string();
        const Offsets* chosen = nullptr;
        for (const Offsets& o : table) {
            if (folder.find(o.version) != std::string::npos) {
                chosen = &o;
            }
        }
        auto plausible = [&] {
            for (const auto& row : sound.hunterSfx) {
                for (int id : row) {
                    if (id >= 0 && (id & 0x3FFF) >= 600) {
                        return false;
                    }
                }
            }
            return true;
        };
        if (chosen != nullptr) {
            read(*chosen);
        } else {
            for (const Offsets& o : table) {
                if (o.hunter + 272 <= r.bytes().size()) {
                    read(o);
                    if (plausible()) {
                        break;
                    }
                }
            }
        }
    }
    return sound;
}

} // namespace fp
