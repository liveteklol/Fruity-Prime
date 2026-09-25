#include "Sequencer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace fp {

namespace {

#include "SequencerTables.inc"

constexpr uint32_t Arm7Clock = 33514000;
constexpr double SecondsPerClockCycle = 64.0 * 2728.0 / Arm7Clock;
constexpr int TrackCount = 16, ChannelCount = 16, MaxCall = 3;
constexpr uint16_t TimerRate = 240;
constexpr uint8_t InvalidTrack = 0xFF;

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t readU32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

int16_t convertSustain(int sustain) { return kSustainTable[(sustain & 0x80) ? 0x7F : sustain]; }
int16_t convertScale(int scale) { return kScaleTable[(scale & 0x80) ? 0x7F : scale]; }

float mulDiv7(float value, uint8_t mul) { return mul == 127 ? value : value * mul * 0.0078125f; }

uint16_t channelVolume(int value)
{
    value = std::clamp(value, -723, 0);
    const int shift = value < -240 ? 3 : value < -120 ? 2 : value < -60 ? 1 : 0;
    return static_cast<uint16_t>(kVolumeTable[value + 723] | shift << 8);
}

uint16_t channelTimer(int timer, int pitch)
{
    int octave = 0;
    int normalized = -pitch;
    while (normalized < 0) {
        octave--;
        normalized += 768;
    }
    while (normalized >= 768) {
        octave++;
        normalized -= 768;
    }
    uint64_t result = kPitchTable[normalized];
    result += 0x10000;
    result *= static_cast<uint64_t>(timer);
    const int shift = octave - 16;
    if (shift <= 0) {
        result >>= -shift;
    } else if (shift < 32) {
        if (result & (~0ULL << (32 - shift))) {
            return 0xFFFF;
        }
        result <<= shift;
    } else {
        return 0xFFFF;
    }
    return static_cast<uint16_t>(std::clamp<uint64_t>(result, 0x10, 0xFFFF));
}

uint16_t decayCoefficient(int value)
{
    if (value & 0x80) {
        value = 0;
    }
    if (value == 127) {
        return 0xFFFF;
    }
    if (value == 126) {
        return 0x3C00;
    }
    if (value < 50) {
        return static_cast<uint16_t>(value * 2 + 1);
    }
    return static_cast<uint16_t>(0x1E00 / (126 - value));
}

constexpr int8_t kSinTable[33] = {0, 6, 12, 19, 25, 31, 37, 43, 49, 54, 60, 65, 71, 76, 81, 85, 90, 94, 98, 102, 106, 109, 112, 115,
    117, 120, 122, 123, 125, 126, 126, 127, 127};

int sinIndex(int x)
{
    if (x < 0x20) {
        return kSinTable[x];
    }
    if (x < 0x40) {
        return kSinTable[0x40 - x];
    }
    if (x < 0x60) {
        return -kSinTable[x - 0x40];
    }
    return -kSinTable[0x20 - (x - 0x60)];
}

constexpr float kWaveDuty[8][8] = {{-1, -1, -1, -1, -1, -1, -1, 1}, {-1, -1, -1, -1, -1, -1, 1, 1}, {-1, -1, -1, -1, -1, 1, 1, 1},
    {-1, -1, -1, -1, 1, 1, 1, 1}, {-1, -1, -1, 1, 1, 1, 1, 1}, {-1, -1, 1, 1, 1, 1, 1, 1}, {-1, 1, 1, 1, 1, 1, 1, 1},
    {-1, -1, -1, -1, -1, -1, -1, -1}};

constexpr int kAdpcmSteps[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
    88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr int kAdpcmIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

Swav readSwav(const uint8_t* p, size_t available)
{
    Swav swav;
    swav.waveType = p[0];
    swav.loop = p[1] != 0;
    swav.time = readU16(p + 4);
    uint32_t loopOffset = readU16(p + 6);
    uint32_t loopLength = readU32(p + 8);
    const uint32_t size = std::min<size_t>((loopOffset + loopLength) * 4, available >= 12 ? available - 12 : 0);
    const uint8_t* data = p + 12;
    switch (swav.waveType) {
    case 0:
        for (uint32_t i = 0; i < size; i++) {
            swav.data.push_back(static_cast<int8_t>(data[i]) / 127.0f);
        }
        loopOffset *= 4;
        loopLength *= 4;
        break;
    case 1:
        for (uint32_t i = 0; i + 1 < size; i += 2) {
            swav.data.push_back(static_cast<int16_t>(readU16(data + i)) / 32767.0f);
        }
        loopOffset *= 2;
        loopLength *= 2;
        break;
    case 2: {
        if (size < 4) {
            break;
        }
        int predicted = static_cast<int16_t>(readU16(data));
        int stepIndex = std::clamp<int>(readU16(data + 2), 0, 88);
        auto nibble = [&](int n) {
            const int step = kAdpcmSteps[stepIndex];
            stepIndex = std::clamp(stepIndex + kAdpcmIndex[n], 0, 88);
            int diff = step >> 3;
            if (n & 1) {
                diff += step >> 2;
            }
            if (n & 2) {
                diff += step >> 1;
            }
            if (n & 4) {
                diff += step;
            }
            // The hardware's clipping: -0x8000 stays when adding, becomes -0x7FFF when subtracting.
            predicted = (n & 8) == 0 ? std::min(predicted + diff, 0x7FFF) : std::max(predicted - diff, -0x7FFF);
            swav.data.push_back(predicted / 32767.0f);
        };
        for (uint32_t i = 4; i < size; i++) {
            nibble(data[i] & 0xF);
            nibble(data[i] >> 4);
        }
        if (loopOffset != 0) {
            loopOffset--;
        }
        loopOffset *= 8;
        loopLength *= 8;
        break;
    }
    default:
        break;
    }
    swav.loopOffset = loopOffset;
    swav.loopLength = loopLength;
    return swav;
}

} // namespace

// ---- SoundArchive ----------------------------------------------------------

SoundArchive::SoundArchive(std::vector<uint8_t> sdat)
    : m_sdat(std::move(sdat))
{
    if (m_sdat.size() < 64 || std::memcmp(m_sdat.data(), "SDAT", 4) != 0) {
        throw std::runtime_error("not an SDAT");
    }
    const uint32_t info = readU32(&m_sdat[24]);
    auto record = [&](uint32_t which, auto&& read) {
        const uint32_t at = info + readU32(&m_sdat[info + 8 + which * 4]);
        const uint32_t count = readU32(&m_sdat[at]);
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t offset = readU32(&m_sdat[at + 4 + i * 4]);
            read(offset != 0 ? &m_sdat[info + offset] : nullptr);
        }
    };
    record(0, [&](const uint8_t* p) {
        m_seqInfo.push_back(p ? SeqInfo{readU16(p), readU16(p + 4), p[6], p[9], true} : SeqInfo{0, 0, 0, 0, false});
    });
    record(2, [&](const uint8_t* p) {
        BankInfo bank{};
        if (p != nullptr) {
            bank = {readU16(p), {readU16(p + 4), readU16(p + 6), readU16(p + 8), readU16(p + 10)}, true};
        }
        m_bankInfo.push_back(bank);
    });
    record(3, [&](const uint8_t* p) { m_waveArcFiles.push_back(p ? readU16(p) : 0xFFFFFFFF); });
    record(4, [&](const uint8_t* p) {
        const uint16_t mask = p ? readU16(p + 2) : 0;
        m_playerMasks.push_back(mask == 0 ? 0xFFFF : mask);
    });
}

const uint8_t* SoundArchive::file(uint32_t fileId, uint32_t& size) const
{
    const uint32_t fat = readU32(&m_sdat[32]);
    const uint32_t count = readU32(&m_sdat[fat + 8]);
    if (fileId >= count) {
        return nullptr;
    }
    const uint32_t offset = readU32(&m_sdat[fat + 12 + fileId * 16]);
    size = readU32(&m_sdat[fat + 12 + fileId * 16 + 4]);
    return offset + size <= m_sdat.size() ? &m_sdat[offset] : nullptr;
}

const Swar* SoundArchive::swar(uint16_t index)
{
    if (auto it = m_swars.find(index); it != m_swars.end()) {
        return it->second.get();
    }
    if (index >= m_waveArcFiles.size()) {
        return nullptr;
    }
    uint32_t size = 0;
    const uint8_t* p = file(m_waveArcFiles[index], size);
    if (p == nullptr || size < 0x3C || std::memcmp(p, "SWAR", 4) != 0) {
        return nullptr;
    }
    auto swar = std::make_unique<Swar>();
    const uint32_t count = readU32(p + 0x38);
    for (uint32_t i = 0; i < count && 0x3C + i * 4 + 4 <= size; i++) {
        const uint32_t offset = readU32(p + 0x3C + i * 4);
        if (offset != 0 && offset + 12 <= size) {
            swar->swavs[i] = readSwav(p + offset, size - offset);
        }
    }
    return (m_swars[index] = std::move(swar)).get();
}

bool SoundArchive::sequence(int index, Sequence& out)
{
    if (index < 0 || index >= static_cast<int>(m_seqInfo.size()) || !m_seqInfo[index].valid) {
        return false;
    }
    const SeqInfo& info = m_seqInfo[index];
    uint32_t size = 0;
    const uint8_t* p = file(info.fileId, size);
    if (p == nullptr || size < 0x1C || std::memcmp(p, "SSEQ", 4) != 0) {
        return false;
    }
    const uint32_t dataSize = readU32(p + 0x14) - 0x0C;
    const uint32_t dataOffset = readU32(p + 0x18);
    if (dataOffset + dataSize > size) {
        return false;
    }
    out.data.assign(p + dataOffset, p + dataOffset + dataSize);
    out.volume = info.volume == 0 ? 0x7F : info.volume;
    out.channelMask = info.playerNo < m_playerMasks.size() ? m_playerMasks[info.playerNo] : 0xFFFF;
    if (info.bank >= m_bankInfo.size() || !m_bankInfo[info.bank].valid) {
        return false;
    }
    const BankInfo& bank = m_bankInfo[info.bank];
    auto& sbnk = m_banks[info.bank];
    if (!sbnk) {
        uint32_t bankSize = 0;
        const uint8_t* b = file(bank.fileId, bankSize);
        if (b == nullptr || bankSize < 0x3C || std::memcmp(b, "SBNK", 4) != 0) {
            return false;
        }
        sbnk = std::make_unique<Sbnk>();
        const uint32_t count = readU32(b + 0x38);
        for (uint32_t i = 0; i < count && 0x3C + i * 4 + 4 <= bankSize; i++) {
            Sbnk::Entry entry;
            entry.record = b[0x3C + i * 4];
            const uint16_t offset = readU16(b + 0x3C + i * 4 + 1);
            auto instrument = [&](const uint8_t* q, uint8_t record, uint8_t low, uint8_t high) {
                Instrument inst;
                inst.record = record;
                inst.lowNote = low;
                inst.highNote = high;
                inst.swav = readU16(q);
                inst.swar = readU16(q + 2);
                inst.noteNumber = q[4];
                inst.attack = q[5];
                inst.decay = q[6];
                inst.sustain = q[7];
                inst.release = q[8];
                inst.pan = q[9];
                entry.instruments.push_back(inst);
            };
            if (offset + 10u <= bankSize) {
                const uint8_t* q = b + offset;
                if (entry.record == 16) {
                    // A drum table: one instrument per key.
                    const uint8_t low = q[0], high = q[1];
                    for (int n = 0; n <= high - low && offset + 2u + (n + 1) * 12u <= bankSize; n++) {
                        const uint8_t* e = q + 2 + n * 12;
                        instrument(e + 2, e[0], static_cast<uint8_t>(low + n), static_cast<uint8_t>(low + n));
                    }
                } else if (entry.record == 17) {
                    // A key split: up to eight ranges.
                    for (int n = 0; n < 8 && q[n] != 0 && offset + 8u + (n + 1) * 12u <= bankSize; n++) {
                        const uint8_t* e = q + 8 + n * 12;
                        instrument(e + 2, e[0], static_cast<uint8_t>(n != 0 ? q[n - 1] + 1 : 0), q[n]);
                    }
                } else if (entry.record != 0) {
                    instrument(q, entry.record, 0, 127);
                }
            }
            sbnk->entries.push_back(std::move(entry));
        }
    }
    out.bank = sbnk.get();
    for (int i = 0; i < 4; i++) {
        out.swars[i] = bank.waveArcs[i] != 0xFFFF ? swar(bank.waveArcs[i]) : nullptr;
    }
    return true;
}

// ---- the driver ------------------------------------------------------------

namespace {

enum class LfoTarget : uint8_t { Pitch, Volume, Pan };

struct LfoParam {
    LfoTarget target = LfoTarget::Pitch;
    uint8_t speed = 16, depth = 0, range = 1;
    uint16_t delay = 0;
};

struct Lfo {
    LfoParam param;
    uint16_t delayCounter = 0, counter = 0;
    void start() { counter = delayCounter = 0; }
    void update()
    {
        if (delayCounter < param.delay) {
            delayCounter++;
        } else {
            uint32_t tmp = counter;
            tmp += static_cast<uint32_t>(param.speed << 6);
            tmp >>= 8;
            while (tmp >= 0x80) {
                tmp -= 0x80;
            }
            counter = static_cast<uint16_t>(counter + (param.speed << 6));
            counter &= 0xFF;
            counter |= static_cast<uint16_t>(tmp << 8);
        }
    }
    int value() const
    {
        return param.depth == 0 || delayCounter < param.delay ? 0 : sinIndex(counter >> 8) * param.depth * param.range;
    }
};

enum class ChannelType : uint8_t { Pcm, Psg, Noise };
enum class EnvelopeState : uint8_t { Attack, Decay, Sustain, Release };
namespace ChFlag {
constexpr uint8_t Active = 1, Start = 2, AutoSweep = 4;
}
namespace Sync {
constexpr uint8_t Stop = 1, Start = 2, Timer = 4, Volume = 8, Pan = 16;
}

struct Register {
    uint8_t volumeMultiplier = 0, volumeDivisor = 0, panning = 0, waveDuty = 0, repeatMode = 0, format = 0;
    bool enable = false;
    const Swav* source = nullptr;
    uint16_t psgX = 0;
    float psgLast = 0;
    uint32_t psgLastCount = 0;
    double samplePosition = 0, sampleIncrease = 0;
    uint32_t loopStart = 0, length = 0, totalLength = 0;
    void clearControl()
    {
        volumeMultiplier = volumeDivisor = panning = waveDuty = repeatMode = format = 0;
        enable = false;
    }
};

struct Track;
struct Player;

struct Channel {
    uint8_t id = 0;
    ChannelType type = ChannelType::Pcm;
    EnvelopeState envelopeStatus = EnvelopeState::Attack;
    uint8_t flags = 0, syncFlags = 0;
    uint8_t panRange = 127, rootMidiKey = 60, midiKey = 60, velocity = 127;
    int8_t initialPan = 0, userPan = 0;
    int16_t userDecay = 0, userPitch = 0;
    int envelopeAttenuation = 0;
    int sweepCounter = 0, sweepLength = 0;
    uint8_t envelopeAttack = 0, envelopeSustain = 0;
    uint16_t envelopeDecay = 0, envelopeRelease = 0;
    uint8_t priority = 0, pan = 0;
    uint16_t volume = 0, timer = 0;
    Lfo lfo;
    int16_t sweepPitch = 0;
    int length = 0;
    const Swav* wave = nullptr;
    int dutyCycle = 0;
    uint16_t waveTimer = 0;
    Track* owner = nullptr; // the Callback
    Player* player = nullptr;
    Register reg;

    bool active() const { return flags & ChFlag::Active; }
    void init(int channelId)
    {
        id = static_cast<uint8_t>(channelId);
        syncFlags = 0;
        reg.clearControl();
        flags |= ChFlag::Active;
    }
    void setAttack(int attack) { envelopeAttack = attack < 109 ? static_cast<uint8_t>(255 - attack) : kAttackTable[127 - attack]; }
    void setDecay(int decay) { envelopeDecay = decayCoefficient(decay); }
    void setSustain(int sustain) { envelopeSustain = static_cast<uint8_t>(sustain); }
    void setRelease(int release) { envelopeRelease = decayCoefficient(release); }
    void release() { envelopeStatus = EnvelopeState::Release; }
    void free() { owner = nullptr; }
    void setup(Track* track, int newPriority)
    {
        owner = track;
        length = sweepLength = sweepCounter = 0;
        priority = static_cast<uint8_t>(newPriority);
        volume = 127;
        flags &= ~ChFlag::Start;
        flags |= ChFlag::AutoSweep;
        midiKey = rootMidiKey = 60;
        velocity = panRange = 127;
        initialPan = userPan = 0;
        userDecay = userPitch = sweepPitch = 0;
        setAttack(127);
        setSustain(127);
        setDecay(127);
        setRelease(127);
        lfo.param = {};
    }
    void start(int newLength)
    {
        envelopeAttenuation = -92544;
        envelopeStatus = EnvelopeState::Attack;
        length = newLength;
        lfo.start();
        flags |= ChFlag::Start | ChFlag::Active;
    }
    int volumeCompare(const Channel& other) const
    {
        static constexpr uint8_t shifts[4] = {0, 1, 2, 4};
        int a = (volume & 0xFF) << 4;
        int b = (other.volume & 0xFF) << 4;
        a >>= shifts[(volume >> 8) & 3];
        b >>= shifts[(other.volume >> 8) & 3];
        return a != b ? (a < b ? 1 : -1) : 0;
    }
    int updateEnvelope()
    {
        switch (envelopeStatus) {
        case EnvelopeState::Attack:
            envelopeAttenuation = -((-envelopeAttenuation * envelopeAttack) >> 8);
            if (envelopeAttenuation == 0) {
                envelopeStatus = EnvelopeState::Decay;
            }
            break;
        case EnvelopeState::Decay: {
            const int sustain = convertSustain(envelopeSustain) << 7;
            envelopeAttenuation -= envelopeDecay;
            if (envelopeAttenuation <= sustain) {
                envelopeAttenuation = sustain;
                envelopeStatus = EnvelopeState::Sustain;
            }
            break;
        }
        case EnvelopeState::Release:
            envelopeAttenuation -= envelopeRelease;
            break;
        default:
            break;
        }
        return envelopeAttenuation >> 7;
    }
    int updateSweep()
    {
        int64_t result = 0;
        if (sweepPitch != 0 && sweepCounter < sweepLength) {
            result = static_cast<int64_t>(sweepPitch) * (sweepLength - sweepCounter) / sweepLength;
            if (flags & ChFlag::AutoSweep) {
                sweepCounter++;
            }
        }
        return static_cast<int>(result);
    }
    int updateLfo()
    {
        int64_t result = lfo.value();
        if (result != 0) {
            result = lfo.param.target == LfoTarget::Volume ? result * 60 : result << 6;
            result >>= 14;
        }
        lfo.update();
        return static_cast<int>(result);
    }
    void kill();
    void update();
    void main();
    bool noteOn(int key, int noteVelocity, int noteLength, const Instrument& inst);
    float generateSample();
    void incrementSample();
};

struct Track {
    static constexpr uint8_t Active = 1, NoteWait = 2, Tie = 4, NoteFinishWait = 8, Portamento = 16, Compare = 32;
    uint8_t flags = 0;
    uint8_t panRange = 127;
    uint16_t program = 0;
    uint8_t volume = 127, expression = 127;
    int8_t pitchBend = 0;
    uint8_t bendRange = 2;
    int8_t pan = 0;
    uint8_t envelopeAttack = 255, envelopeDecay = 255, envelopeSustain = 255, envelopeRelease = 255;
    uint8_t priority = 64;
    int8_t transpose = 0;
    uint8_t portamentoKey = 60, portamentoTime = 0;
    int16_t sweepPitch = 0;
    LfoParam modulation;
    int wait = 0;
    const std::vector<uint8_t>* base = nullptr;
    int currentPos = -1;
    std::array<int, MaxCall> positionCallStack{};
    std::array<uint8_t, MaxCall> loopCount{};
    uint8_t callStackDepth = 0;
    Player* player = nullptr;
    uint8_t id = 0;
    bool mute = false;
    std::vector<int> channels;

    uint8_t readU8() { return currentPos < static_cast<int>(base->size()) ? (*base)[currentPos++] : (currentPos++, 0xFF); }
    uint16_t readU16Seq()
    {
        const uint16_t lo = readU8();
        return static_cast<uint16_t>(lo | readU8() << 8);
    }
    uint32_t readU24()
    {
        const uint32_t a = readU8(), b = readU8(), c = readU8();
        return a | b << 8 | c << 16;
    }
    int readVlv()
    {
        int result = 0;
        int b;
        do {
            b = readU8();
            result = (result << 7) | (b & 0x7F);
        } while (b & 0x80);
        return result;
    }
    void init()
    {
        base = nullptr;
        currentPos = -1;
        flags |= NoteWait | Compare;
        flags &= ~(Tie | NoteFinishWait | Portamento);
        callStackDepth = portamentoTime = 0;
        program = 0;
        priority = 64;
        volume = expression = panRange = 127;
        pan = pitchBend = transpose = 0;
        envelopeAttack = envelopeDecay = envelopeSustain = envelopeRelease = 255;
        bendRange = 2;
        portamentoKey = 60;
        sweepPitch = 0;
        modulation = {};
        wait = 0;
        channels.clear();
    }
    void start(const std::vector<uint8_t>* data, int offset)
    {
        base = data;
        currentPos = offset;
    }
    int parseValue(int valueType);
    void releaseChannels(int releaseRate);
    void freeChannels();
    void stop();
    void channelCallback(Channel& channel, bool free);
    void updateChannel(bool releaseIfDone);
    const Instrument* instrument(int prog, int key) const;
    void playNote(int key, int noteVelocity, int noteLength);
    bool stepTicks();
};

struct Player {
    uint8_t priority = 64, volume = 0x7F;
    std::array<uint8_t, TrackCount> trackIds{};
    uint16_t tempo = 120, tempoRatio = 256, tempoCounter = TimerRate;
    std::array<int16_t, 32> variables{};
    std::array<Track, TrackCount> tracks;
    std::array<Channel, ChannelCount> channels;
    const Sbnk* bank = nullptr;
    std::array<const Swar*, 4> swars{};
    int16_t sseqVolume = 0;
    uint16_t channelMask = 0xFFFF;
    uint32_t sampleRate = 32728;

    Player()
    {
        trackIds.fill(InvalidTrack);
        variables.fill(-1);
    }
    Channel* allocateChannel(uint32_t mask, int prio, Track* track)
    {
        static constexpr uint8_t order[16] = {4, 5, 6, 7, 2, 0, 3, 1, 8, 9, 10, 11, 14, 12, 15, 13};
        Channel* prev = nullptr;
        for (uint8_t candidate : order) {
            if (mask & (1u << candidate)) {
                Channel& ch = channels[candidate];
                if (prev == nullptr || (ch.priority <= prev->priority && (ch.priority != prev->priority || prev->volumeCompare(ch) < 0))) {
                    prev = &ch;
                }
            }
        }
        if (prev == nullptr || prio < prev->priority) {
            return nullptr;
        }
        if (prev->owner != nullptr) {
            prev->owner->channelCallback(*prev, false);
        }
        prev->syncFlags = Sync::Stop;
        prev->flags &= ~ChFlag::Active;
        prev->setup(track, prio);
        return prev;
    }
    int allocateTrack()
    {
        for (int i = 0; i < TrackCount; i++) {
            if (!(tracks[i].flags & Track::Active)) {
                tracks[i].flags |= Track::Active;
                return i;
            }
        }
        return -1;
    }
    Track* track(int index)
    {
        return index < 0 || index >= TrackCount || trackIds[index] == InvalidTrack ? nullptr : &tracks[trackIds[index]];
    }
    void stopTrack(int index)
    {
        if (Track* t = track(index)) {
            t->stop();
            t->flags &= ~Track::Active;
            trackIds[index] = InvalidTrack;
        }
    }
    void stop()
    {
        for (int i = 0; i < TrackCount; i++) {
            stopTrack(i);
        }
    }
    void init(int16_t seqVolume)
    {
        tempo = 120;
        tempoRatio = 256;
        tempoCounter = TimerRate;
        volume = 0x7F;
        priority = 64;
        trackIds.fill(InvalidTrack);
        variables.fill(-1);
        for (int i = 0; i < TrackCount; i++) {
            tracks[i].id = static_cast<uint8_t>(i);
            tracks[i].player = this;
            tracks[i].flags &= ~Track::Active;
        }
        sseqVolume = seqVolume;
        for (int i = 0; i < ChannelCount; i++) {
            channels[i].player = this;
            channels[i].init(i);
        }
    }
    void prepareSequence(const std::vector<uint8_t>* data, int16_t seqVolume)
    {
        stop();
        init(seqVolume);
        int index = allocateTrack();
        if (index < 0) {
            return;
        }
        Track& first = tracks[index];
        first.init();
        first.start(data, 0);
        trackIds[0] = static_cast<uint8_t>(index);
        if (first.readU8() == 0xFE) { // AllocateTrack
            uint16_t mask = static_cast<uint16_t>(first.readU16Seq() >> 1);
            for (int t = 1; mask != 0; t++, mask >>= 1) {
                if (mask & 1) {
                    index = allocateTrack();
                    if (index < 0) {
                        break;
                    }
                    tracks[index].init();
                    trackIds[t] = static_cast<uint8_t>(index);
                }
            }
        } else {
            first.currentPos--;
        }
    }
    void stepTicks()
    {
        for (int i = 0; i < TrackCount; i++) {
            Track* t = track(i);
            if (t != nullptr && t->currentPos != -1 && !t->stepTicks()) {
                stopTrack(i);
            }
        }
    }
    void mainTick()
    {
        int ticks = 0;
        while (tempoCounter >= TimerRate) {
            tempoCounter -= TimerRate;
            ticks++;
        }
        for (int i = 0; i < ticks; i++) {
            stepTicks();
        }
        tempoCounter = static_cast<uint16_t>(tempoCounter + ((tempo * tempoRatio) >> 8));
    }
    void sequenceMain()
    {
        for (Channel& ch : channels) {
            ch.update();
        }
        mainTick();
        for (int i = 0; i < TrackCount; i++) {
            if (Track* t = track(i)) {
                t->updateChannel(true);
            }
        }
        for (Channel& ch : channels) {
            ch.main();
        }
    }
};

uint32_t g_random = 0x12345678;

uint16_t calculateRandom()
{
    g_random = g_random * 1664525 + 1013904223;
    return static_cast<uint16_t>(g_random >> 16);
}

void Channel::kill()
{
    if (owner == nullptr) {
        priority = 0;
    } else {
        owner->channelCallback(*this, true);
    }
    volume = 0;
    flags &= ~ChFlag::Active;
}

void Channel::update()
{
    if (syncFlags == 0) {
        return;
    }
    if (syncFlags & Sync::Stop) {
        reg.enable = false;
    }
    if (syncFlags & Sync::Start) {
        reg.clearControl();
        reg.panning = pan;
        reg.volumeMultiplier = static_cast<uint8_t>(volume & 0xFF);
        reg.volumeDivisor = static_cast<uint8_t>(volume >> 8);
        switch (type) {
        case ChannelType::Pcm:
            reg.format = static_cast<uint8_t>(wave->waveType & 3);
            reg.repeatMode = wave->loop ? 1 : 2;
            reg.loopStart = wave->loopOffset;
            reg.length = wave->loopLength;
            reg.totalLength = reg.loopStart + reg.length;
            reg.source = wave;
            break;
        case ChannelType::Psg:
            reg.format = 3;
            reg.waveDuty = static_cast<uint8_t>(dutyCycle);
            break;
        case ChannelType::Noise:
            reg.format = 3;
            break;
        }
        reg.enable = true;
        syncFlags = 0;
    } else {
        if (syncFlags & Sync::Volume) {
            reg.volumeMultiplier = static_cast<uint8_t>(volume & 0xFF);
            reg.volumeDivisor = static_cast<uint8_t>(volume >> 8);
        }
        if (syncFlags & Sync::Pan) {
            reg.panning = pan;
        }
    }
}

void Channel::main()
{
    if (!active()) {
        return;
    }
    if (flags & ChFlag::Start) {
        syncFlags |= Sync::Start;
        flags &= ~ChFlag::Start;
        reg.enable = false;
    } else if (!reg.enable) {
        kill();
        return;
    }
    int vol = convertSustain(velocity);
    int pitch = (midiKey - rootMidiKey) * 0x40;
    vol += updateEnvelope();
    pitch += updateSweep();
    vol += userDecay;
    pitch += userPitch;
    const int lfoValue = updateLfo();
    int newPan = 0;
    switch (lfo.param.target) {
    case LfoTarget::Volume:
        if (vol > -0x8000) {
            vol += lfoValue;
        }
        break;
    case LfoTarget::Pan:
        newPan += lfoValue;
        break;
    case LfoTarget::Pitch:
        pitch += lfoValue;
        break;
    }
    newPan += initialPan;
    if (panRange != 127) {
        newPan = (newPan * panRange + 0x40) >> 7;
    }
    newPan += userPan;
    if (envelopeStatus == EnvelopeState::Release && vol <= -723) {
        syncFlags = Sync::Stop;
        kill();
        return;
    }
    vol = channelVolume(vol);
    uint16_t newTimer = channelTimer(waveTimer, pitch);
    if (type == ChannelType::Psg) {
        newTimer &= 0xFFFC;
    }
    newPan = std::clamp(newPan + 0x40, 0, 127);
    if (vol != volume) {
        volume = static_cast<uint16_t>(vol);
        syncFlags |= Sync::Volume;
    }
    if (newTimer != timer) {
        timer = newTimer;
        reg.sampleIncrease = Arm7Clock / (player->sampleRate * 2.0) / newTimer;
        syncFlags |= Sync::Timer;
    }
    if (newPan != pan) {
        pan = static_cast<uint8_t>(newPan);
        syncFlags |= Sync::Pan;
    }
}

bool Channel::noteOn(int key, int noteVelocity, int noteLength, const Instrument& inst)
{
    uint8_t releaseRate = inst.release;
    if (releaseRate == 0xFF) {
        noteLength = -1;
        releaseRate = 0;
    }
    bool success = false;
    switch (inst.record) {
    case 1: { // PCM
        const Swar* swar = inst.swar < 4 ? player->swars[inst.swar] : nullptr;
        if (swar != nullptr) {
            if (auto it = swar->swavs.find(inst.swav); it != swar->swavs.end() && !it->second.data.empty()) {
                type = ChannelType::Pcm;
                reg.samplePosition = it->second.waveType == 2 ? -11 : -3;
                wave = &it->second;
                waveTimer = wave->time;
                start(noteLength);
                success = true;
            }
        }
        break;
    }
    case 2: // PSG
        if (id >= 8 && id <= 13) {
            type = ChannelType::Psg;
            reg.samplePosition = -1;
            dutyCycle = inst.swav;
            waveTimer = 8006;
            start(noteLength);
            success = true;
        }
        break;
    case 3: // noise
        if (id >= 14) {
            type = ChannelType::Noise;
            reg.samplePosition = -1;
            reg.psgX = 0x7FFF;
            waveTimer = 8006;
            start(noteLength);
            success = true;
        }
        break;
    default:
        break;
    }
    if (!success) {
        return false;
    }
    midiKey = static_cast<uint8_t>(key);
    rootMidiKey = inst.noteNumber;
    velocity = static_cast<uint8_t>(noteVelocity);
    setAttack(inst.attack);
    setSustain(inst.sustain);
    setDecay(inst.decay);
    setRelease(releaseRate);
    initialPan = static_cast<int8_t>(inst.pan - 0x40);
    return true;
}

float Channel::generateSample()
{
    if (reg.samplePosition < 0) {
        return 0;
    }
    if (reg.format != 3) {
        const size_t index = static_cast<size_t>(reg.samplePosition);
        return reg.source != nullptr && index < reg.source->data.size() ? reg.source->data[index] : 0.0f;
    }
    if (id < 8) {
        return 0;
    }
    if (id < 14) {
        return kWaveDuty[reg.waveDuty & 7][static_cast<int>(reg.samplePosition) & 7];
    }
    const uint32_t position = static_cast<uint32_t>(reg.samplePosition);
    if (reg.psgLastCount != position) {
        for (uint32_t i = reg.psgLastCount; i < position; i++) {
            if (reg.psgX & 1) {
                reg.psgX = static_cast<uint16_t>((reg.psgX >> 1) ^ 0x6000);
                reg.psgLast = -1;
            } else {
                reg.psgX >>= 1;
                reg.psgLast = 1;
            }
        }
        reg.psgLastCount = position;
    }
    return reg.psgLast;
}

void Channel::incrementSample()
{
    reg.samplePosition += reg.sampleIncrease;
    if (reg.format != 3 && reg.samplePosition >= reg.totalLength) {
        if (reg.repeatMode == 1 && reg.length > 0) {
            while (reg.samplePosition >= reg.totalLength) {
                reg.samplePosition -= reg.length;
            }
        } else {
            kill();
        }
    }
}

int Track::parseValue(int valueType)
{
    switch (valueType) {
    case 0:
        return readU8();
    case 1:
        return readU16Seq();
    case 2:
        return readVlv();
    case 3:
        return player->variables[readU8() & 31];
    case 4: {
        const int lo = readU16Seq() << 16;
        const int hi = static_cast<int16_t>(readU16Seq());
        const int ran = calculateRandom();
        int result = hi - (lo >> 16) + 1;
        result = (ran * result) >> 16;
        return result + (lo >> 16);
    }
    default:
        return 0;
    }
}

void Track::releaseChannels(int releaseRate)
{
    updateChannel(false);
    for (int channelId : channels) {
        Channel& ch = player->channels[channelId];
        if (ch.active()) {
            if (releaseRate >= 0) {
                ch.setRelease(releaseRate & 0xFF);
            }
            ch.priority = 1;
            ch.release();
        }
    }
}

void Track::freeChannels()
{
    for (int channelId : channels) {
        player->channels[channelId].free();
    }
    channels.clear();
}

void Track::stop()
{
    currentPos = -1;
    releaseChannels(-1);
    freeChannels();
}

void Track::channelCallback(Channel& channel, bool free)
{
    if (free) {
        channel.priority = 0;
        channel.free();
    }
    channels.erase(std::remove(channels.begin(), channels.end(), channel.id), channels.end());
}

void Track::updateChannel(bool releaseIfDone)
{
    int vol = mute ? -0x8000 : convertSustain(volume) + convertSustain(expression) + convertSustain(player->volume) + player->sseqVolume;
    int pitch = pitchBend;
    pitch *= bendRange << 6;
    pitch >>= 7;
    int newPan = pan;
    if (panRange != 127) {
        newPan = (newPan * panRange + 0x40) >> 7;
    }
    vol = std::max(vol, -0x8000);
    newPan = std::clamp(newPan, -128, 127);
    for (int channelId : channels) {
        Channel& ch = player->channels[channelId];
        if (ch.envelopeStatus != EnvelopeState::Release) {
            ch.userDecay = static_cast<int16_t>(vol);
            ch.userPitch = static_cast<int16_t>(pitch);
            ch.userPan = static_cast<int8_t>(newPan);
            ch.panRange = panRange;
            ch.lfo.param = modulation;
            if (ch.length == 0 && releaseIfDone) {
                ch.priority = 1;
                ch.release();
            }
        }
    }
}

const Instrument* Track::instrument(int prog, int key) const
{
    const Sbnk* sbnk = player->bank;
    if (sbnk == nullptr || prog < 0 || prog >= static_cast<int>(sbnk->entries.size())) {
        return nullptr;
    }
    const Sbnk::Entry& entry = sbnk->entries[prog];
    const auto& insts = entry.instruments;
    if (insts.empty()) {
        return nullptr;
    }
    switch (entry.record) {
    case 1:
    case 2:
    case 3:
    case 5:
        return &insts[0];
    case 16:
        if (key < insts.front().lowNote || key > insts.back().highNote) {
            return nullptr;
        }
        return static_cast<size_t>(key - insts.front().lowNote) < insts.size() ? &insts[key - insts.front().lowNote] : nullptr;
    case 17:
        for (const Instrument& inst : insts) {
            if (key <= inst.highNote) {
                return &inst;
            }
        }
        return nullptr;
    default:
        return nullptr;
    }
}

void Track::playNote(int key, int noteVelocity, int noteLength)
{
    Channel* ch = nullptr;
    if ((flags & Tie) && !channels.empty()) {
        ch = &player->channels[channels[0]];
        ch->midiKey = static_cast<uint8_t>(key);
        ch->velocity = static_cast<uint8_t>(noteVelocity);
    }
    if (ch == nullptr) {
        const Instrument* note = instrument(program, key);
        if (note == nullptr) {
            return;
        }
        uint32_t allowed;
        switch (note->record) {
        case 1:
            allowed = 0xFFFF;
            break;
        case 2:
            allowed = 0x3F00;
            break;
        case 3:
            allowed = 0xC000;
            break;
        default:
            return;
        }
        allowed &= player->channelMask;
        ch = player->allocateChannel(allowed, player->priority + priority, this);
        if (ch == nullptr) {
            return;
        }
        if (!ch->noteOn(key, noteVelocity, (flags & Tie) ? -1 : noteLength, *note)) {
            ch->priority = 0;
            ch->free();
            return;
        }
        channels.insert(channels.begin(), ch->id);
    }
    if (envelopeAttack != 0xFF) {
        ch->setAttack(envelopeAttack);
    }
    if (envelopeDecay != 0xFF) {
        ch->setDecay(envelopeDecay);
    }
    if (envelopeSustain != 0xFF) {
        ch->setSustain(envelopeSustain);
    }
    if (envelopeRelease != 0xFF) {
        ch->setRelease(envelopeRelease);
    }
    ch->sweepPitch = sweepPitch;
    if (flags & Portamento) {
        ch->sweepPitch = static_cast<int16_t>(ch->sweepPitch + ((portamentoKey - key) << 6));
    }
    if (portamentoTime != 0) {
        int sweep = portamentoTime * portamentoTime;
        sweep *= ch->sweepPitch < 0 ? -ch->sweepPitch : ch->sweepPitch;
        sweep >>= 11;
        ch->sweepLength = sweep;
    } else {
        ch->sweepLength = noteLength;
        ch->flags &= ~ChFlag::AutoSweep;
    }
    ch->sweepCounter = 0;
}

bool Track::stepTicks()
{
    for (int channelId : channels) {
        Channel& ch = player->channels[channelId];
        if (ch.length > 0) {
            ch.length--;
        }
        if (!(ch.flags & ChFlag::AutoSweep) && ch.sweepCounter < ch.sweepLength) {
            ch.sweepCounter++;
        }
    }
    if (flags & NoteFinishWait) {
        if (!channels.empty()) {
            return true;
        }
        flags &= ~NoteFinishWait;
    }
    if (wait > 0 && --wait > 0) {
        return true;
    }
    int guard = 0;
    while (wait == 0 && !(flags & NoteFinishWait)) {
        if (++guard > 100000 || base == nullptr || currentPos < 0 || currentPos >= static_cast<int>(base->size())) {
            return false; // a sequence that never waits, or runs off its data
        }
        bool runCmd = true;
        int valueType = -1;
        uint8_t cmd = readU8();
        if (cmd == 0xA2) { // If
            cmd = readU8();
            runCmd = (flags & Compare) != 0;
        }
        if (cmd == 0xA0) { // Random
            cmd = readU8();
            valueType = 4;
        }
        if (cmd == 0xA1) { // FromVariable
            cmd = readU8();
            valueType = 3;
        }
        if (!(cmd & 0x80)) {
            const int noteVelocity = readU8();
            const int noteLength = parseValue(valueType >= 0 ? valueType : 2);
            if (runCmd) {
                const int key = std::clamp(cmd + transpose, 0, 127);
                playNote(key, noteVelocity, noteLength > 0 ? noteLength : -1);
                portamentoKey = static_cast<uint8_t>(key);
                if (flags & NoteWait) {
                    wait = noteLength;
                    if (noteLength == 0) {
                        flags |= NoteFinishWait;
                    }
                }
            }
            continue;
        }
        switch (cmd & 0xF0) {
        case 0x80: {
            const int par = parseValue(valueType >= 0 ? valueType : 2);
            if (runCmd) {
                if (cmd == 0x80) {
                    wait = par;
                } else if (cmd == 0x81 && par < 0x10000) {
                    program = static_cast<uint16_t>(par);
                }
            }
            break;
        }
        case 0x90:
            if (cmd == 0x93) { // OpenTrack
                const int par = readU8();
                const uint32_t off = readU24();
                if (runCmd) {
                    Track* other = player->track(par);
                    if (other != nullptr && other != this) {
                        other->stop();
                        other->start(base, static_cast<int>(off));
                    }
                }
            } else if (cmd == 0x94) { // Goto
                const uint32_t off = readU24();
                if (runCmd) {
                    currentPos = static_cast<int>(off);
                }
            } else if (cmd == 0x95) { // Call
                const uint32_t off = readU24();
                if (runCmd && callStackDepth < MaxCall) {
                    positionCallStack[callStackDepth++] = currentPos;
                    currentPos = static_cast<int>(off);
                }
            }
            break;
        case 0xC0:
        case 0xD0: {
            const uint8_t par = static_cast<uint8_t>(parseValue(valueType >= 0 ? valueType : 0));
            if (!runCmd) {
                break;
            }
            switch (cmd) {
            case 0xC1: volume = par; break;
            case 0xD5: expression = par; break;
            case 0xC2: player->volume = par; break;
            case 0xC5: bendRange = par; break;
            case 0xC6: priority = par; break;
            case 0xC7:
                if (par != 0) {
                    flags |= NoteWait;
                } else {
                    flags &= ~NoteWait;
                }
                break;
            case 0xCF: portamentoTime = par; break;
            case 0xCA: modulation.depth = par; break;
            case 0xCB: modulation.speed = par; break;
            case 0xCC: modulation.target = static_cast<LfoTarget>(par); break;
            case 0xCD: modulation.range = par; break;
            case 0xD0: envelopeAttack = par; break;
            case 0xD1: envelopeDecay = par; break;
            case 0xD2: envelopeSustain = par; break;
            case 0xD3: envelopeRelease = par; break;
            case 0xD4: // LoopStart
                if (callStackDepth < MaxCall) {
                    positionCallStack[callStackDepth] = currentPos;
                    loopCount[callStackDepth++] = par;
                }
                break;
            case 0xC8: // Tie
                if (par != 0) {
                    flags |= Tie;
                } else {
                    flags &= ~Tie;
                }
                releaseChannels(-1);
                freeChannels();
                break;
            case 0xC9:
                portamentoKey = static_cast<uint8_t>(par + transpose);
                flags |= Portamento;
                break;
            case 0xCE:
                if (par != 0) {
                    flags |= Portamento;
                } else {
                    flags &= ~Portamento;
                }
                break;
            case 0xC3: transpose = static_cast<int8_t>(par); break;
            case 0xC4: pitchBend = static_cast<int8_t>(par); break;
            case 0xC0: pan = static_cast<int8_t>(par - 0x40); break;
            default: break;
            }
            break;
        }
        case 0xE0: {
            const int16_t par = static_cast<int16_t>(parseValue(valueType >= 0 ? valueType : 1));
            if (runCmd) {
                if (cmd == 0xE3) {
                    sweepPitch = par;
                } else if (cmd == 0xE1) {
                    player->tempo = static_cast<uint16_t>(par);
                } else if (cmd == 0xE0) {
                    modulation.delay = static_cast<uint16_t>(par);
                }
            }
            break;
        }
        case 0xB0: {
            const int varNum = readU8() & 31;
            int16_t par = static_cast<int16_t>(parseValue(valueType >= 0 ? valueType : 1));
            if (!runCmd) {
                break;
            }
            int16_t var = player->variables[varNum];
            auto compare = [&](bool result) {
                if (result) {
                    flags |= Compare;
                } else {
                    flags &= ~Compare;
                }
            };
            switch (cmd) {
            case 0xB0: var = par; break;
            case 0xB1: var = static_cast<int16_t>(var + par); break;
            case 0xB2: var = static_cast<int16_t>(var - par); break;
            case 0xB3: var = static_cast<int16_t>(var * par); break;
            case 0xB4:
                if (par != 0) {
                    var = static_cast<int16_t>(var / par);
                }
                break;
            case 0xB5: var = static_cast<int16_t>(par >= 0 ? var << par : var >> -par); break;
            case 0xB6: {
                bool negative = false;
                if (par < 0) {
                    negative = true;
                    par = static_cast<int16_t>(-par);
                }
                int random = (calculateRandom() * (par + 1)) >> 16;
                var = static_cast<int16_t>(negative ? -random : random);
                break;
            }
            case 0xB8: compare(var == par); break;
            case 0xB9: compare(var >= par); break;
            case 0xBA: compare(var > par); break;
            case 0xBB: compare(var <= par); break;
            case 0xBC: compare(var < par); break;
            case 0xBD: compare(var != par); break;
            default: break;
            }
            player->variables[varNum] = var;
            break;
        }
        case 0xF0:
            if (runCmd) {
                if (cmd == 0xFD) { // Return
                    if (callStackDepth != 0) {
                        currentPos = positionCallStack[--callStackDepth];
                    }
                } else if (cmd == 0xFC) { // LoopEnd
                    if (callStackDepth != 0) {
                        uint8_t count = loopCount[callStackDepth - 1];
                        if (count != 0 && --count == 0) {
                            callStackDepth--;
                            break;
                        }
                        loopCount[callStackDepth - 1] = count;
                        currentPos = positionCallStack[callStackDepth - 1];
                    }
                } else if (cmd == 0xFF) {
                    return false;
                }
            }
            break;
        default:
            break;
        }
    }
    return true;
}

} // namespace

// ---- Sequencer ---------------------------------------------------------------

struct Sequencer::Impl {
    Player player;
    std::vector<uint8_t> data;
    bool started = false;
    double secondsPerSample = 1 / 32728.0;
    int samplesIntoPlayback = 0;

    void frame(float& left, float& right)
    {
        samplesIntoPlayback++;
        left = right = 0;
        for (Channel& ch : player.channels) {
            if (ch.active() && ch.reg.enable) {
                float sample = ch.generateSample();
                ch.incrementSample();
                static constexpr float divisors[4] = {1, 0.5f, 0.25f, 0.0625f};
                sample = mulDiv7(sample, ch.reg.volumeMultiplier) * divisors[ch.reg.volumeDivisor & 3];
                left += mulDiv7(sample, static_cast<uint8_t>(127 - ch.reg.panning));
                right += mulDiv7(sample, ch.reg.panning);
            }
        }
        if (samplesIntoPlayback * secondsPerSample >= SecondsPerClockCycle) {
            player.sequenceMain();
            samplesIntoPlayback = 0;
        }
    }
};

Sequencer::Sequencer()
    : m(std::make_unique<Impl>())
{
}

Sequencer::~Sequencer() = default;
Sequencer::Sequencer(Sequencer&&) noexcept = default;
Sequencer& Sequencer::operator=(Sequencer&&) noexcept = default;

void Sequencer::start(const SoundArchive::Sequence& sequence, uint32_t sampleRate)
{
    m = std::make_unique<Impl>();
    m->data = sequence.data;
    Player& p = m->player;
    p.channelMask = sequence.channelMask;
    p.sampleRate = sampleRate;
    p.prepareSequence(&m->data, convertScale(sequence.volume));
    p.bank = sequence.bank;
    p.swars = sequence.swars;
    p.sequenceMain(); // one tick at the start, to skip silent samples before the first
    m->secondsPerSample = 1.0 / sampleRate;
    m->started = true;
    // NCSFPlayerStream's skipSilenceOnStartSec: the silence before the music, up to five seconds, goes.
    float prevLeft = 0, prevRight = 0;
    constexpr float level = 0.000213623046875f;
    for (uint32_t i = 0; i < sampleRate * 5; i++) {
        float left, right;
        m->frame(left, right);
        if (left - prevLeft > level || right - prevRight > level) {
            break;
        }
        prevLeft = left;
        prevRight = right;
    }
}

void Sequencer::render(float* out, int frames, float gain)
{
    if (!m->started) {
        return;
    }
    for (int i = 0; i < frames; i++) {
        float left, right;
        m->frame(left, right);
        out[i * 2] += std::clamp(left * gain, -1.0f, 1.0f);
        out[i * 2 + 1] += std::clamp(right * gain, -1.0f, 1.0f);
    }
}

bool Sequencer::playing() const
{
    if (!m->started) {
        return false;
    }
    for (int i = 0; i < TrackCount; i++) {
        if (m->player.trackIds[i] != InvalidTrack) {
            return true;
        }
    }
    for (const Channel& ch : m->player.channels) {
        if (ch.active() && ch.reg.enable) {
            return true;
        }
    }
    return false;
}

void Sequencer::stop() { m = std::make_unique<Impl>(); }

uint16_t Sequencer::tempoRatio() const { return m->player.tempoRatio; }

void Sequencer::setTempoRatio(uint16_t ratio) { m->player.tempoRatio = ratio; }

bool Sequencer::hasTrack(int index) const { return m->player.track(index) != nullptr; }

uint8_t Sequencer::trackVolume(int index) const
{
    const Track* t = m->player.track(index);
    return t != nullptr ? t->volume : 0;
}

void Sequencer::setTrackVolume(int index, uint8_t volume)
{
    if (Track* t = m->player.track(index)) {
        t->volume = volume;
    }
}

void Sequencer::setTrackMute(int index, bool mute)
{
    if (Track* t = m->player.track(index)) {
        t->mute = mute;
    }
}

} // namespace fp
