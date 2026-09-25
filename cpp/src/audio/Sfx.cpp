#include "Sfx.h"

#include "Mixer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace fp {

namespace {

// FP_DEBUG_SOUND: every sound started, on stderr.
void logSound(const char* kind, int id, const std::string& name, const SoundSource* source)
{
    static const bool enabled = std::getenv("FP_DEBUG_SOUND") != nullptr;
    if (enabled) {
        std::fprintf(stderr, "[sound] %s %d %s%s\n", kind, id & 0x3FFF, name.c_str(), source == nullptr ? " (free)" : "");
    }
}

float dgnValue(const std::vector<DgnData>& data, float amount)
{
    // GetDgnValue: the curve at `amount`, linear or along a quarter sine.
    if (data.empty()) {
        return 0;
    }
    if (amount <= data.front().amount) {
        return data.front().value & 0x3FFF;
    }
    if (amount >= data.back().amount) {
        return data.back().value & 0x3FFF;
    }
    if (data.size() == 1) {
        return 0;
    }
    size_t i = 0;
    while (amount >= data[i + 1].amount) {
        if (++i >= data.size() - 1) {
            return 0;
        }
    }
    const DgnData& a = data[i];
    const DgnData& b = data[i + 1];
    const float ratio = (amount - a.amount) / (b.amount - a.amount);
    const float value1 = a.value & 0x3FFF;
    const float value2 = b.value & 0x3FFF;
    if ((b.value & 0xC000) == 0x4000) {
        return value1 + (value2 - value1) * std::sin(3.14159265f / 2 * ratio);
    }
    if ((b.value & 0xC000) == 0x8000) {
        return 0;
    }
    return value1 + (value2 - value1) * ratio;
}

} // namespace

// ---- SoundSource ---------------------------------------------------------

SoundSource::~SoundSource()
{
    Sfx::instance().detach(this);
}

void SoundSource::update(const std::array<float, 3>& pos, int rangeIndex)
{
    Sfx& sfx = Sfx::instance();
    if (rangeIndex == -1) {
        position = sfx.listenerPosition();
        referenceDistance = FLT_MAX;
        maxDistance = FLT_MAX;
        self = true;
        return;
    }
    position = pos;
    if (rangeIndex >= 0 && rangeIndex < static_cast<int>(sfx.ranges().size())) {
        referenceDistance = sfx.ranges()[rangeIndex].falloffDistance;
        maxDistance = sfx.ranges()[rangeIndex].maxDistance;
    }
    self = false;
}

void SoundSource::playSfx(int id, bool loop, bool noUpdate, float recency, bool sourceOnly, bool cancellable, float amountA, float amountB)
{
    if (id < 0) {
        return;
    }
    Sfx& sfx = Sfx::instance();
    if (id & 0x8000) {
        sfx.playDgn(id, this, loop, noUpdate, recency, cancellable, amountA, amountB);
    } else if (id & 0x4000) {
        sfx.playScript(id, this, noUpdate, recency, sourceOnly, cancellable);
    } else {
        sfx.playSample(id, this, loop ? 1 : 0, noUpdate, recency, sourceOnly, cancellable);
    }
}

void SoundSource::stopSfx(int id)
{
    if (id >= 0) {
        Sfx::instance().stopSoundFromSource(this, id);
    }
}

void SoundSource::stopAllSfx(bool force) { Sfx::instance().stopSoundFromSource(this, force); }

int SoundSource::countSourcePlayingSfx(int id) const { return Sfx::instance().countPlayingSfx(id, this); }

void SoundSource::playEnvironmentSfx(int index) { Sfx::instance().playEnvironmentSfx(index, *this); }

// ---- Sfx -----------------------------------------------------------------

Sfx& Sfx::instance()
{
    static Sfx sfx;
    return sfx;
}

Sfx::Sfx()
    : m_environment{{{SfxId::ELECTRO_WAVE2}, {SfxId::ELECTRICITY}, {SfxId::ELECTRIC_BARRIER}, {SfxId::ENERGY_BALL},
          {SfxId::BLUE_FLAME}, {SfxId::CYLINDER_BOSS_ATTACK}, {SfxId::CYLINDER_BOSS_SPIN}, {SfxId::BUBBLES},
          {SfxId::ELEVATOR2_START}, {SfxId::GOREA_ATTACK3_LOOP}}}
{
}

bool Sfx::Instance::looping() const
{
    for (bool l : loop) {
        if (l) {
            return true;
        }
    }
    return false;
}

bool Sfx::load(const std::filesystem::path& root, Mixer* mixer)
{
    unload();
    if (mixer == nullptr) {
        return false;
    }
    try {
        m_data = SoundData::load(root);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[sound] cannot read the sound files (%s); continuing without sound\n", e.what());
        return false;
    }
    m_mixer = mixer;
    sfxMute = false;
    timedSfxMute = longSfxMute = 0;
    return true;
}

void Sfx::unload()
{
    if (m_mixer != nullptr) {
        for (Instance& inst : m_instances) {
            stop(inst);
        }
        m_mixer->stop(StreamVoice);
    }
    m_mixer = nullptr;
    m_queue.clear();
    for (EnvironmentItem& item : m_environment) {
        item.handle = -1;
        item.instances = 0;
        item.distanceSquared = FLT_MAX;
    }
}

void Sfx::stop(Instance& inst)
{
    for (int i = 0; i < MaxPerInstance; i++) {
        if (inst.channels[i] >= 0) {
            m_mixer->stop(inst.channels[i]);
            m_channelInUse[inst.channels[i]] = false;
            inst.channels[i] = -1;
        }
        inst.samples[i] = nullptr;
        inst.volume[i] = 1;
        inst.pitch[i] = 1;
        inst.loop[i] = false;
        inst.panned[i] = false;
    }
    inst.paused = false;
    inst.playTime = -1;
    inst.sfxId = -1;
    inst.dgn = nullptr;
    inst.script = nullptr;
    inst.scriptIndex = -1;
    inst.source = nullptr;
    inst.noUpdate = false;
    inst.cancellable = false;
    inst.handle = -1;
    inst.count = 0;
}

int Sfx::playFreeSfx(int id)
{
    if (id < 0 || !loaded()) {
        return -1;
    }
    if (id & 0x4000) {
        playScript(id, nullptr, false, -1, false, false);
        return -1;
    }
    return playSample(id, nullptr, -1, false, -1, false, false);
}

int Sfx::playSample(int id, SoundSource* source, int loop, bool noUpdate, float recency, bool sourceOnly, bool cancellable)
{
    Instance* inst = playSampleInstance(id, source, loop, noUpdate, recency, sourceOnly, cancellable);
    return inst != nullptr ? inst->handle : -1;
}

Sfx::Instance* Sfx::playSampleInstance(int id, SoundSource* source, int loop, bool noUpdate, float recency, bool sourceOnly,
    bool cancellable)
{
    if (!loaded() || id < 0 || id >= static_cast<int>(m_data.samples.size())) {
        return nullptr;
    }
    Instance* inst = nullptr;
    if (!setUpInstance(id, source, loop == 1, recency, sourceOnly, cancellable, inst)) {
        return nullptr;
    }
    if (!setUpSample(id, *inst, 0)) {
        stop(*inst);
        return nullptr;
    }
    inst->loop[0] = loop < 0 ? inst->samples[0]->loop : loop == 1;
    logSound("sample", id, inst->samples[0]->name, source);
    startInstance(*inst, noUpdate);
    return inst;
}

void Sfx::playDgn(int id, SoundSource* source, bool loop, bool noUpdate, float recency, bool cancellable, float amountA, float amountB)
{
    const int dgnId = id & 0x3FFF;
    if (!loaded() || dgnId >= static_cast<int>(m_data.dgns.size())) {
        return;
    }
    Instance* inst = nullptr;
    if (!setUpInstance(id, source, loop, recency, true, cancellable, inst)) {
        // Already playing: only its amounts change.
        if (inst != nullptr && inst->dgn != nullptr) {
            updateDgn(*inst, amountA, amountB);
        }
        return;
    }
    const DgnFile& dgn = m_data.dgns[dgnId];
    inst->dgn = &dgn;
    for (size_t i = 0; i < dgn.entries.size() && i < 3; i++) {
        if (!setUpSample(dgn.entries[i].sfxId, *inst, static_cast<int>(i))) {
            stop(*inst);
            return;
        }
        inst->loop[i] = loop;
    }
    updateDgn(*inst, amountA, amountB);
    for (int i = 0; i < inst->count; i++) {
        if (inst->volume[i] > 0) {
            logSound("dgn", id, dgn.name, source);
            startInstance(*inst, noUpdate);
            return;
        }
    }
    stop(*inst); // no volume above 0
}

void Sfx::playScript(int id, SoundSource* source, bool noUpdate, float recency, bool sourceOnly, bool cancellable)
{
    const int scriptId = id & 0x3FFF;
    if (!loaded() || scriptId >= static_cast<int>(m_data.scripts.size())) {
        return;
    }
    const SfxScriptFile& script = m_data.scripts[scriptId];
    if (script.entries.empty()) {
        return; // TELEPORT_ACTIVATE_SCR
    }
    Instance* inst = nullptr;
    if (!setUpInstance(id, source, false, recency, sourceOnly, cancellable, inst)) {
        return;
    }
    inst->noUpdate = noUpdate;
    inst->script = &script;
    logSound("script", id, script.name, source);
}

bool Sfx::setUpInstance(int id, SoundSource* source, bool loop, float recency, bool sourceOnly, bool cancellable, Instance*& inst)
{
    if (loop) {
        recency = FLT_MAX;
        sourceOnly = true;
    }
    if (recency >= 0) {
        if (Instance* recent = findRecent(id, recency, sourceOnly ? source : nullptr)) {
            inst = recent;
            return false;
        }
    }
    inst = &findInstance(source);
    if (inst->playTime >= 0) {
        stop(*inst); // the longest playing gives way
    }
    inst->source = source;
    inst->paused = false;
    inst->playTime = 0;
    inst->cancellable = cancellable;
    inst->sfxId = id;
    inst->handle = m_nextHandle++;
    return true;
}

bool Sfx::setUpSample(int id, Instance& inst, int index)
{
    if (id < 0 || id >= static_cast<int>(m_data.samples.size()) || m_data.samples[id].pcm == nullptr) {
        return false;
    }
    inst.samples[index] = &m_data.samples[id];
    if (inst.channels[index] < 0) {
        const int channel = freeChannel();
        if (channel < 0) {
            return false;
        }
        inst.channels[index] = channel;
        m_channelInUse[channel] = true;
        inst.count++;
    }
    return true;
}

int Sfx::freeChannel()
{
    for (int i = 0; i < ChannelCount; i++) {
        if (!m_channelInUse[i]) {
            return i;
        }
    }
    return -1;
}

Sfx::Instance* Sfx::findRecent(int id, float recency, const SoundSource* source)
{
    // recency 0: started this frame; 1: within the last second; FLT_MAX: playing at all.
    for (Instance& inst : m_instances) {
        if ((source == nullptr || inst.source == source) && inst.sfxId == id && inst.playTime <= recency) {
            return &inst;
        }
    }
    return nullptr;
}

Sfx::Instance& Sfx::findInstance(const SoundSource* source)
{
    // A free one, else the one playing longest (from the same source first).
    float maxTime = 0;
    Instance* bySource = nullptr;
    Instance* any = nullptr;
    for (Instance& inst : m_instances) {
        if (inst.playTime < 0) {
            return inst;
        }
        if (inst.playTime >= maxTime) {
            if (source != nullptr && inst.source == source) {
                bySource = &inst;
            }
            any = &inst;
            maxTime = inst.playTime;
        }
    }
    return bySource != nullptr ? *bySource : *any;
}

void Sfx::updatePosition(Instance& inst)
{
    if (inst.source == nullptr || inst.source->self) {
        inst.position = m_listenerPosition;
        inst.referenceDistance = inst.maxDistance = FLT_MAX;
        inst.rolloff = 1;
    } else if (!inst.noUpdate) {
        inst.position = inst.source->position;
        inst.referenceDistance = inst.source->referenceDistance;
        inst.maxDistance = inst.source->maxDistance;
        inst.rolloff = inst.source->rolloff;
    }
}

void Sfx::updateParameters(Instance& inst)
{
    const float mute = sfxMute && inst.source != nullptr ? 0.0f : 1.0f;
    const float sourceVolume = inst.source != nullptr ? inst.source->volume : 1.0f;
    for (int i = 0; i < MaxPerInstance; i++) {
        if (inst.channels[i] < 0 || inst.samples[i] == nullptr) {
            continue;
        }
        VoiceParams p;
        p.gain = volume * inst.volume[i] * inst.samples[i]->volume * mute * sourceVolume;
        p.pitch = inst.pitch[i];
        p.loop = inst.loop[i];
        if (inst.panned[i]) {
            // A script entry's pan: relative to the listener, no rolloff.
            p.relative = true;
            p.rolloff = 0;
            p.position = {inst.pan[i], 0, inst.pan[i] == 0 ? 0 : -std::sqrt(1 - inst.pan[i] * inst.pan[i])};
        } else {
            p.position = inst.position;
            p.referenceDistance = inst.referenceDistance;
            p.maxDistance = inst.maxDistance;
            p.rolloff = inst.rolloff;
        }
        m_mixer->setParams(inst.channels[i], p);
    }
}

void Sfx::updateInstance(Instance& inst, bool noUpdate)
{
    inst.noUpdate = false;
    updatePosition(inst);
    inst.noUpdate = noUpdate;
    inst.panned.fill(false); // UpdateParameters sets every channel back to world-relative
    updateParameters(inst);
}

void Sfx::playChannel(Instance& inst, int index)
{
    VoiceParams p; // set right after by updateParameters
    p.gain = 0;
    m_mixer->play(inst.channels[index], inst.samples[index]->pcm, p);
}

void Sfx::startInstance(Instance& inst, bool noUpdate)
{
    for (int i = 0; i < inst.count; i++) {
        playChannel(inst, i);
    }
    updateInstance(inst, noUpdate);
}

void Sfx::updateDgn(Instance& inst, float amountA, float amountB)
{
    for (int i = 0; i < inst.count && i < static_cast<int>(inst.dgn->entries.size()); i++) {
        const DgnEntry& entry = inst.dgn->entries[i];
        const float volumeA = dgnValue(entry.data[0], amountA);
        const float volumeB = dgnValue(entry.data[1], amountB);
        const float pitchA = dgnValue(entry.data[2], amountA);
        const float pitchB = dgnValue(entry.data[3], amountB);
        float volumeFactor = volumeA / 127 * volumeB;
        volumeFactor = volumeFactor / 127 * inst.dgn->initialVolume / 127;
        if (volumeFactor < 1 / 130.0f) {
            volumeFactor = 0;
        }
        inst.volume[i] = volumeFactor;
        float pitchFactor = pitchA / 0x2000 * pitchB;
        if (pitchFactor >= 0x4000) {
            pitchFactor = 0x3FFF;
        }
        inst.pitch[i] = pitchDiv(pitchFactor);
    }
}

void Sfx::updateScript(Instance& inst, float time)
{
    if (inst.paused) {
        return;
    }
    inst.playTime = inst.playTime >= 0 ? inst.playTime + time : 0;
    int playing = 0;
    for (int i = 0; i < MaxPerInstance; i++) {
        if (inst.channels[i] < 0) {
            continue;
        }
        if (m_mixer->playing(inst.channels[i])) {
            playing++;
        } else {
            m_mixer->stop(inst.channels[i]);
            m_channelInUse[inst.channels[i]] = false;
            inst.channels[i] = -1;
            inst.samples[i] = nullptr;
            inst.loop[i] = false;
            inst.count--;
        }
    }
    const auto& entries = inst.script->entries;
    if (inst.scriptIndex >= static_cast<int>(entries.size()) - 1 && playing == 0) {
        stop(inst);
        return;
    }
    for (size_t i = static_cast<size_t>(inst.scriptIndex + 1); i < entries.size(); i++) {
        const SfxScriptEntry& entry = entries[i];
        if (entry.delay > inst.playTime) {
            break;
        }
        inst.scriptIndex = static_cast<int>(i);
        const int sfxId = entry.sfxData & 0x3FFF;
        if (entry.sfxData & 0x8000) {
            if (inst.source == nullptr) {
                stopSoundById(sfxId);
            } else {
                stopSoundFromSource(inst.source, sfxId);
            }
            continue;
        }
        int index = -1;
        for (int j = 0; j < MaxPerInstance; j++) {
            if (inst.channels[j] < 0) {
                index = j;
                break;
            }
        }
        if (index == -1) {
            stop(inst);
            return;
        }
        inst.volume[index] = entry.volume;
        inst.pitch[index] = entry.pitch;
        if (!setUpSample(sfxId, inst, index)) {
            stop(inst);
            return;
        }
        inst.loop[index] = (entry.sfxData & 0x4000) != 0;
        playChannel(inst, index);
        updateInstance(inst, inst.noUpdate);
        // -1: no panning; 0 is centred, which overrides the position.
        if (entry.pan > -1) {
            inst.panned[index] = true;
            inst.pan[index] = std::abs(entry.pan) > 1 / 128.0f ? entry.pan : 0;
            updateParameters(inst);
        }
    }
}

void Sfx::stopSoundFromSource(const SoundSource* source, bool force)
{
    if (!loaded()) {
        return;
    }
    for (Instance& inst : m_instances) {
        if (inst.source == source && inst.playTime >= 0) {
            // A forced stop spares the sounds left where they were (noUpdate).
            if ((force || inst.looping() || inst.cancellable) && (!force || !inst.noUpdate)) {
                stop(inst);
            }
        }
    }
}

void Sfx::detach(const SoundSource* source)
{
    if (!loaded()) {
        return;
    }
    for (Instance& inst : m_instances) {
        if (inst.source == source) {
            if (inst.looping() || inst.script != nullptr) {
                stop(inst);
            } else {
                inst.source = nullptr; // a one-shot finishes where it was
                inst.noUpdate = true;
            }
        }
    }
}

void Sfx::stopSoundFromSource(const SoundSource* source, int id)
{
    if (!loaded()) {
        return;
    }
    for (Instance& inst : m_instances) {
        if (inst.source == source && inst.sfxId == id) {
            stop(inst);
        }
    }
}

void Sfx::stopSoundById(int id)
{
    if (!loaded()) {
        return;
    }
    for (Instance& inst : m_instances) {
        if (inst.sfxId == id) {
            stop(inst);
        }
    }
}

void Sfx::stopSoundByHandle(int handle)
{
    if (!loaded() || handle < 0) {
        return;
    }
    for (Instance& inst : m_instances) {
        if (inst.handle == handle) {
            stop(inst);
        }
    }
}

void Sfx::stopFreeSfxScripts()
{
    if (!loaded()) {
        return;
    }
    for (Instance& inst : m_instances) {
        if (inst.script != nullptr) {
            stop(inst);
        }
    }
}

void Sfx::setPausedFreeSfxScripts(bool paused)
{
    for (Instance& inst : m_instances) {
        if (inst.script != nullptr) {
            inst.paused = paused;
        }
    }
}

void Sfx::stopAllSound(bool force)
{
    if (!loaded()) {
        return;
    }
    for (Instance& inst : m_instances) {
        if (inst.sfxId != -1 && (force || inst.source != nullptr || inst.looping())) {
            stop(inst);
        }
    }
    m_mixer->stop(StreamVoice);
}

bool Sfx::isHandlePlaying(int handle) const
{
    if (!loaded() || handle < 0) {
        return false;
    }
    for (const Instance& inst : m_instances) {
        if (inst.handle == handle) {
            return true;
        }
    }
    return false;
}

int Sfx::countPlayingSfx(int id, const SoundSource* source) const
{
    if (!loaded()) {
        return 0;
    }
    int count = 0;
    for (const Instance& inst : m_instances) {
        if (inst.handle != -1 && inst.sfxId == id && (source == nullptr || inst.source == source)) {
            count++;
        }
    }
    return count;
}

void Sfx::playEnvironmentSfx(int index, const SoundSource& source)
{
    // The closest source's range, straight ahead of the listener at its distance (no panning).
    if (!loaded() || index < 0 || index >= static_cast<int>(m_environment.size())) {
        return;
    }
    EnvironmentItem& item = m_environment[index];
    const float dx = source.position[0] - m_listenerPosition[0], dy = source.position[1] - m_listenerPosition[1],
                dz = source.position[2] - m_listenerPosition[2];
    const float distSqr = dx * dx + dy * dy + dz * dz;
    if (distSqr < item.distanceSquared) {
        item.distanceSquared = distSqr;
        const float dist = std::sqrt(distSqr);
        for (int i = 0; i < 3; i++) {
            item.source.position[i] = m_listenerPosition[i] + m_listenerFacing[i] * dist;
        }
        item.source.referenceDistance = source.referenceDistance;
        item.source.maxDistance = source.maxDistance;
        item.source.rolloff = source.rolloff;
    }
    item.instances++;
}

bool Sfx::checkEnvironmentSfx(int index) const
{
    return loaded() && index >= 0 && index < static_cast<int>(m_environment.size()) && m_environment[index].instances > 0;
}

void Sfx::stopEnvironmentSfx()
{
    for (EnvironmentItem& item : m_environment) {
        if (item.instances > 0) {
            stopSoundByHandle(item.handle);
            item.handle = -1;
            item.instances = 0;
            item.distanceSquared = FLT_MAX;
        }
    }
}

void Sfx::updateEnvironmentSfx()
{
    for (EnvironmentItem& item : m_environment) {
        if (item.instances > 0) {
            if (Instance* inst = playSampleInstance(static_cast<int>(item.sfxId), &item.source, 1, false, -1, false, false)) {
                item.handle = inst->handle;
            }
        } else if (item.handle != -1) {
            stopSoundByHandle(item.handle);
            item.handle = -1;
        }
        item.instances = 0;
        item.distanceSquared = FLT_MAX;
    }
}

void Sfx::queueStream(VoiceId id, float delay, float expiration)
{
    const int index = static_cast<int>(id);
    if (!loaded() || m_queue.size() >= 16 || index < 0 || index >= static_cast<int>(m_data.streams.size())) {
        return;
    }
    m_queue.push_back({&m_data.streams[index], delay, expiration, false});
}

void Sfx::playFreeStream(VoiceId id)
{
    m_queue.clear();
    queueStream(id);
    updateStreams(0);
}

void Sfx::updateStreams(float time)
{
    // UpdateStreams: the first item plays when its delay is over and leaves
    // the queue when done; the ones behind it expire.
    for (size_t index = 0; index < m_queue.size();) {
        QueueItem& item = m_queue[index];
        if (index == 0 && item.playing) {
            if (!m_mixer->playing(StreamVoice)) {
                m_queue.pop_front();
                continue;
            }
        } else if (item.delay > 0) {
            item.delay = std::max(item.delay - time, 0.0f);
        }
        if (item.delay == 0 && !item.playing && index == 0) {
            VoiceParams p;
            p.gain = volume * item.stream->volume;
            p.loop = item.stream->loop;
            p.relative = true;
            p.rolloff = 0;
            m_mixer->play(StreamVoice, item.stream->pcm, p);
            item.playing = true;
            index++;
            continue;
        }
        if (index > 0 && item.expiration > 0) {
            item.expiration -= time;
            if (item.expiration <= 0) {
                m_queue.erase(m_queue.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
        }
        index++;
    }
}

void Sfx::update(float time, const std::array<float, 3>& position, const std::array<float, 3>& facing,
    const std::array<float, 3>& up)
{
    m_listenerPosition = position;
    m_listenerFacing = facing;
    m_listenerUp = up;
    if (!loaded()) {
        return;
    }
    m_mixer->setListener(position, facing, up);
    for (Instance& inst : m_instances) {
        if (inst.playTime < 0) {
            continue;
        }
        if (inst.script != nullptr) {
            if (inst.source == nullptr || !sfxMute) {
                updateScript(inst, time);
            }
            continue;
        }
        bool playing = false;
        for (int j = 0; j < MaxPerInstance; j++) {
            const int channel = inst.channels[j];
            if (channel >= 0 && m_mixer->playing(channel)) {
                if (inst.volume[j] == 0) {
                    m_mixer->stop(channel); // a DGN turned all the way down
                    continue;
                }
                playing = true;
                break;
            }
        }
        if (playing) {
            inst.playTime += time;
            updatePosition(inst);
            updateParameters(inst);
        } else {
            stop(inst);
        }
    }
    if (longSfxMute == 0) {
        updateEnvironmentSfx();
    }
    updateStreams(time);
}

} // namespace fp
