#include "Effects.h"

#include "formats/Metadata.h"
#include "formats/Model.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace fp {

namespace {

constexpr size_t kEntryMax = 64, kElementMax = 96, kParticleMax = 200; // in-game limits
constexpr float kStep = 1 / 60.0f;
constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

// FuncAction
namespace Action {
constexpr int SetParticleId = 9, IncreaseParticleAmount = 14, SetNewParticleSpeed = 15, SetNewParticlePosition = 16,
              SetNewParticleLifespan = 17, UpdateParticleSpeed = 18, SetParticleAlpha = 19, SetParticleRed = 20,
              SetParticleGreen = 21, SetParticleBlue = 22, SetParticleScale = 23, SetParticleRotation = 24,
              SetParticleRoField1 = 25, SetParticleRwField1 = 29;
} // namespace Action

float fx(int32_t raw) { return raw / 4096.0f; }

Vec3 normalized(Vec3 v)
{
    const float len = std::sqrt(dot(v, v));
    return len > 0 ? v * (1.0f / len) : v;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// Matrix.Vec3MultMtx4 / Vec3MultMtx3
Vec3 multMtx4(const Vec3& v, const Mat4& m)
{
    return {v[0] * m.m[0][0] + v[1] * m.m[1][0] + v[2] * m.m[2][0] + m.m[3][0],
        v[0] * m.m[0][1] + v[1] * m.m[1][1] + v[2] * m.m[2][1] + m.m[3][1],
        v[0] * m.m[0][2] + v[1] * m.m[1][2] + v[2] * m.m[2][2] + m.m[3][2]};
}

Vec3 multMtx3(const Vec3& v, const Mat4& m)
{
    return {v[0] * m.m[0][0] + v[1] * m.m[1][0] + v[2] * m.m[2][0], v[0] * m.m[0][1] + v[1] * m.m[1][1] + v[2] * m.m[2][1],
        v[0] * m.m[0][2] + v[1] * m.m[1][2] + v[2] * m.m[2][2]};
}

// Matrix.GetTransform4: rows up, direction, vector1, position.
Mat4 transform4(const Vec3& vector1, const Vec3& vector2, const Vec3& position)
{
    const Vec3 up = normalized(cross(vector2, vector1));
    const Vec3 direction = cross(vector1, up);
    Mat4 t;
    for (int i = 0; i < 3; i++) {
        t.m[0][i] = up[i];
        t.m[1][i] = direction[i];
        t.m[2][i] = vector1[i];
        t.m[3][i] = position[i];
    }
    t.m[3][3] = 1;
    return t;
}

uint32_t u32(const std::vector<uint8_t>& b, size_t at)
{
    uint32_t v = 0;
    if (at + 4 <= b.size()) {
        std::memcpy(&v, b.data() + at, 4);
    }
    return v;
}

std::string str(const std::vector<uint8_t>& b, size_t at, size_t max)
{
    std::string s;
    for (size_t i = 0; i < max && at + i < b.size() && b[at + i] != 0; i++) {
        s.push_back(static_cast<char>(b[at + i]));
    }
    return s;
}

struct Times {
    float global, elapsed, lifespan;
};

} // namespace

namespace {
uint32_t s_rng1 = 0x3DE9179B; // Rng.Rng1StartValue
uint32_t s_rng2 = 0;          // Rng.Rng2StartValue
} // namespace

uint32_t randomInt1(uint32_t max)
{
    s_rng1 = s_rng1 * 0x7FF8A3ED + 0x2AA01D31;
    return static_cast<uint32_t>((static_cast<uint64_t>(s_rng1 >> 16) * max) / 0x10000);
}

uint32_t randomInt2(uint32_t max)
{
    s_rng2 = s_rng2 * 0x7FF8A3ED + 0x2AA01D31;
    return static_cast<uint32_t>((static_cast<uint64_t>(s_rng2 >> 16) * max) / 0x10000);
}

uint32_t rngState1() { return s_rng1; }
uint32_t rngState2() { return s_rng2; }

struct Effects::Particle {
    float creationTime = 0, expirationTime = 0, lifespan = 0;
    Vec3 position{}, speed{};
    float scale = 0, rotation = 0, red = 1, green = 1, blue = 1, alpha = 1;
    int particleId = 0;
    float portionTotal = 0;
    std::array<float, 4> ro{}, rw{};
    int materialId = 0;
    int setVecsId = 0, drawId = 0;
};

struct Effects::Element {
    const EffectElementDef* def = nullptr;
    int effectId = 0;
    float creationTime = 0, expirationTime = 0;
    uint32_t flags = 0;
    Mat4 ownTransform = Mat4::identity(), transform = Mat4::identity();
    bool func39Called = false;
    float particleAmount = 0;
    bool expired = false;
    std::array<float, 4> ro{};
    int parity = 0;
    Entry* entry = nullptr;
    std::vector<Particle> particles;
};

namespace {

// EffectFuncBase: the functions, for an element (particle null) or one of its particles.
struct FxEval {
    const std::map<uint32_t, FxFunc>& funcs;
    float elementLifespan, elementCreation;
    const Mat4& elementTransform;
    bool& func39Called;
    Effects* unused = nullptr;
    struct ParticleView {
        Vec3 position, speed;
        float portionTotal, alpha, red, green, blue, scale, rotation;
        std::array<float, 4> ro, rw;
    };
    const ParticleView* particle = nullptr;

    const FxFunc* at(int32_t offset) const
    {
        auto it = funcs.find(static_cast<uint32_t>(offset));
        return it != funcs.end() ? &it->second : nullptr;
    }

    float value(int32_t offset, const Times& t) const
    {
        const FxFunc* f = at(offset);
        return f ? floatFunc(f->id, f->params, t) : 0;
    }

    Vec3 vector(int32_t offset, const Times& t) const
    {
        Vec3 v{};
        if (const FxFunc* f = at(offset)) {
            vecFunc(*f, t, v);
        }
        return v;
    }

    float floatFunc(uint32_t id, const std::vector<int32_t>& p, const Times& t) const
    {
        auto param = [&](size_t i) { return i < p.size() ? p[i] : 0; };
        switch (id) {
        case 21:
        case 28:
            return 0;
        case 22:
            return elementLifespan;
        case 23:
            return t.global - elementCreation;
        case 24:
            return particle ? particle->alpha : 0;
        case 25:
            return particle ? particle->red : 0;
        case 26:
            return particle ? particle->green : 0;
        case 27:
            return particle ? particle->blue : 0;
        case 29:
            return particle ? particle->scale : 0;
        case 30:
            return particle ? particle->rotation : 0;
        case 31:
        case 32:
        case 33:
        case 34:
            return particle ? particle->ro[id - 31] : 0;
        case 35:
        case 36:
        case 37:
        case 38:
            return particle ? particle->rw[id - 35] : 0;
        case 39:
            if (func39Called) {
                return 0;
            }
            func39Called = true;
            return fx(param(0));
        case 40:
            return t.elapsed / t.lifespan <= fx(param(0)) ? fx(param(1)) : fx(param(2));
        case 41: {
            const float percent = t.elapsed / t.lifespan;
            if (percent < fx(param(0))) {
                return fx(param(1));
            }
            bool none = true;
            size_t i = 0;
            do {
                if (fx(param(i)) > percent) {
                    break;
                }
                none = false;
                i += 2;
            } while (i < p.size() && p[i] != INT32_MIN);
            if (none) {
                return 0;
            }
            i -= 2;
            if (i + 2 >= p.size() || p[i + 2] == INT32_MIN) {
                return fx(param(i + 1));
            }
            return fx(param(i + 1))
                + (fx(param(i + 3)) - fx(param(i + 1))) * ((percent - fx(param(i))) / (fx(param(i + 2)) - fx(param(i))));
        }
        case 42:
            return fx(param(0));
        case 43:
            return fx(static_cast<int32_t>(randomInt1(4096)));
        case 44:
            return fx(static_cast<int32_t>(randomInt1(4096))) - 0.5f;
        case 45:
            return fx(static_cast<int32_t>(randomInt1(0x168000)));
        case 46:
            return value(param(0), t) + value(param(1), t);
        case 47:
            return value(param(0), t) - value(param(1), t);
        case 48:
            return value(param(0), t) * value(param(1), t);
        case 49:
            return value(param(0), t) >= value(param(1), t) ? value(param(2), t) : value(param(3), t);
        default:
            return 0;
        }
    }

    void vecFunc(const FxFunc& f, const Times& t, Vec3& vec) const
    {
        const auto& p = f.params;
        auto param = [&](size_t i) { return i < p.size() ? p[i] : 0; };
        auto rnd = [] { return fx(static_cast<int32_t>(randomInt1(4096))); };
        switch (f.id) {
        case 1:
        case 2:
            vec = particle ? particle->position : Vec3{elementTransform.m[3][0], elementTransform.m[3][1], elementTransform.m[3][2]};
            break;
        case 3:
            vec = particle ? particle->speed : Vec3{};
            break;
        case 4:
            vec = {fx(param(0)), fx(param(1)), fx(param(2))};
            break;
        case 5:
            vec[0] = rnd();
            vec[1] = rnd();
            vec[2] = rnd();
            break;
        case 6:
            vec[0] = rnd();
            vec[1] = 0;
            vec[2] = rnd();
            break;
        case 7:
            vec[0] = rnd();
            vec[1] = 1;
            vec[2] = rnd();
            break;
        case 8:
            vec[0] = rnd() - 0.5f;
            vec[1] = rnd() - 0.5f;
            vec[2] = rnd() - 0.5f;
            break;
        case 9:
            vec[0] = rnd() - 0.5f;
            vec[1] = 0;
            vec[2] = rnd() - 0.5f;
            break;
        case 10:
            vec[0] = rnd() - 0.5f;
            vec[1] = 1;
            vec[2] = rnd() - 0.5f;
            break;
        case 11: {
            const float angle = 360 * (particle ? particle->portionTotal : 0) * kDegToRad;
            vec = {std::sin(angle), 0, std::cos(angle)};
            break;
        }
        case 13: {
            // The second parameter is not quite a function's offset: the next one after it is.
            uint32_t offset = static_cast<uint32_t>(param(1));
            auto it = funcs.lower_bound(offset);
            while (it != funcs.end() && (it->first - offset) % 4 != 0) {
                ++it;
            }
            const float v = it != funcs.end() ? floatFunc(static_cast<uint32_t>(param(0)), it->second.params, t) : 1;
            float percent = t.elapsed / v;
            if (v < 0) {
                percent *= -1;
            }
            const float angle = 360 * percent * kDegToRad;
            vec = {std::sin(angle), 0, std::cos(angle)};
            break;
        }
        case 14: {
            const Vec3 temp = vector(param(0), t);
            const float v = value(param(1), t);
            float div = t.elapsed / v;
            if (v < 0) {
                div *= -1;
            }
            vec = temp * div;
            break;
        }
        case 15: {
            const float v1 = value(param(0), t);
            const float v2 = value(param(1), t);
            const float angle = (randomInt1(0xFFFF) >> 4) * (360 / 4096.0f) * kDegToRad;
            vec = {std::sin(angle) * v1, v2, std::cos(angle) * v1};
            break;
        }
        case 16: {
            const float v1 = value(param(0), t);
            const float v2 = value(param(1), t);
            vec[0] = (rnd() - 0.5f) * v1;
            vec[1] = 0;
            vec[2] = (rnd() - 0.5f) * v2;
            break;
        }
        case 17:
        case 18:
        case 19: {
            const Vec3 a = vector(param(0), t);
            const Vec3 b = vector(param(1), t);
            for (int i = 0; i < 3; i++) {
                vec[i] = f.id == 17 ? a[i] + b[i] : f.id == 18 ? a[i] - b[i] : a[i] * b[i];
            }
            break;
        }
        case 20: {
            const float v = value(param(0), t);
            vec = vector(param(1), t) * v;
            break;
        }
        default:
            break;
        }
    }
};

FxEval::ParticleView view(const Effects* /*unused*/, const auto& p)
{
    return {p.position, p.speed, p.portionTotal, p.alpha, p.red, p.green, p.blue, p.scale, p.rotation, p.ro, p.rw};
}

// EffectFuncBase.GetFuncIds: (setVecsId, drawId) by the element's flags and draw type.
std::pair<int, int> funcIds(uint32_t flags, int drawType)
{
    if (flags & EffElemFlags::UseMesh) {
        return {drawType == 3 ? 4 : 5, 7};
    }
    const bool alternate = flags & EffElemFlags::UseTransform;
    switch (drawType) {
    case 1:
        return {1, alternate ? 1 : 2};
    case 2:
        return {2, alternate ? 1 : 2};
    case 3:
        return {3, 3};
    case 4:
        return {1, alternate ? 4 : 5};
    case 5:
        return {2, alternate ? 4 : 5};
    case 6:
        return {3, 6};
    default:
        return {1, 2};
    }
}

} // namespace

Effects::Effects(Scene& scene, const std::filesystem::path& root)
    : m_scene(scene)
    , m_root(root)
{
    m_entries.resize(kEntryMax);
}

Effects::~Effects() = default;

const EffectDef* Effects::load(int effectId)
{
    // Read.LoadEffect
    if (auto it = m_effects.find(effectId); it != m_effects.end()) {
        return it->second.get();
    }
    const auto& table = effectTable();
    if (effectId < 1 || effectId >= static_cast<int>(table.size())) {
        return nullptr;
    }
    const EffectMetadata& meta = table[effectId];
    const std::string path = meta.archive ? std::string("_archives/") + meta.archive + "/" + meta.name + "_PS.bin"
                                          : std::string("effects/") + meta.name + "_PS.bin";
    std::vector<uint8_t> bytes;
    try {
        bytes = readFile(resolveCaseInsensitive(m_root, path));
    } catch (const std::exception& e) {
        if (qEnvironmentVariableIsSet("FP_DEBUG_EFFECTS")) {
            qWarning("effect %d (%s): %s", effectId, meta.name, e.what()); // a few are in no version's files
        }
        m_effects[effectId] = nullptr;
        return nullptr;
    }
    auto effect = std::make_unique<EffectDef>();
    effect->id = effectId;
    effect->name = meta.name;
    // RawEffect: Field0, FuncCount, FuncOffset, Count2, Offset2, ElementCount, ElementOffset
    const uint32_t funcCount = u32(bytes, 4), funcOffset = u32(bytes, 8);
    const uint32_t elementCount = u32(bytes, 20), elementOffset = u32(bytes, 24);
    for (uint32_t i = 0; i < funcCount; i++) {
        const uint32_t offset = u32(bytes, funcOffset + i * 4);
        FxFunc func;
        func.id = u32(bytes, offset);
        const uint32_t paramOffset = u32(bytes, offset + 4);
        // (offset - paramOffset) / 4 parameters; with none, the C# reads from the file's start, which nothing uses.
        if (paramOffset != 0 && paramOffset < offset) {
            for (uint32_t at = paramOffset; at < offset; at += 4) {
                func.params.push_back(static_cast<int32_t>(u32(bytes, at)));
            }
        }
        effect->funcs.emplace(offset, std::move(func));
    }
    for (uint32_t e = 0; e < elementCount; e++) {
        const uint32_t at = u32(bytes, elementOffset + e * 4);
        EffectElementDef element;
        element.name = str(bytes, at, 32);
        const std::string modelName = str(bytes, at + 32, 32);
        const uint32_t particleCount = u32(bytes, at + 64), particleOffset = u32(bytes, at + 68);
        element.flags = u32(bytes, at + 72);
        element.acceleration = {fx(static_cast<int32_t>(u32(bytes, at + 76))), fx(static_cast<int32_t>(u32(bytes, at + 80))),
            fx(static_cast<int32_t>(u32(bytes, at + 84)))};
        element.childEffectId = static_cast<int>(u32(bytes, at + 88));
        element.lifespan = fx(static_cast<int32_t>(u32(bytes, at + 92)));
        element.drainTime = fx(static_cast<int32_t>(u32(bytes, at + 96)));
        element.bufferTime = fx(static_cast<int32_t>(u32(bytes, at + 100)));
        element.drawType = static_cast<int>(u32(bytes, at + 104));
        const uint32_t elemFuncCount = u32(bytes, at + 108), elemFuncOffset = u32(bytes, at + 112);
        const Model* model = m_scene.model(m_root, modelName);
        for (uint32_t i = 0; i < particleCount; i++) {
            std::string particleName = str(bytes, u32(bytes, particleOffset + i * 4), 16);
            // Read.GetParticle
            if (modelName == "geo1" && particleName == "gib") {
                particleName = "gib3";
            }
            EffectParticleDef particle;
            particle.model = model;
            if (model != nullptr) {
                const auto& nodes = model->nodes();
                for (size_t n = 0; n < nodes.size(); n++) {
                    if (nodes[n].name == particleName) {
                        particle.node = static_cast<int>(n);
                        break;
                    }
                }
                if (particle.node >= 0 && nodes[particle.node].meshCount > 0) {
                    const int mesh = nodes[particle.node].meshId / 2;
                    particle.materialId = mesh < static_cast<int>(model->meshes().size()) ? model->meshes()[mesh].materialId : 0;
                } else {
                    particle.model = nullptr;
                }
            }
            if (particle.model == nullptr) {
                qWarning("effect %s: no particle %s in %s", meta.name, particleName.c_str(), modelName.c_str());
            }
            element.particles.push_back(particle);
        }
        for (uint32_t i = 0; i < elemFuncCount; i++) {
            const uint32_t index = u32(bytes, elemFuncOffset + i * 8);
            const uint32_t offset = u32(bytes, elemFuncOffset + i * 8 + 4);
            if (offset != 0 && index < element.actions.size()) {
                if (auto it = effect->funcs.find(offset); it != effect->funcs.end()) {
                    element.actions[index] = &it->second;
                }
            }
        }
        effect->elements.push_back(std::move(element));
    }
    for (EffectElementDef& element : effect->elements) {
        element.funcs = &effect->funcs;
    }
    const EffectDef* result = effect.get();
    m_effects[effectId] = std::move(effect);
    for (const EffectElementDef& element : result->elements) {
        if (element.childEffectId != 0) {
            load(element.childEffectId);
        }
    }
    return result;
}

void Effects::loadAll()
{
    for (int id = 1; id < static_cast<int>(effectTable().size()); id++) {
        load(id);
    }
}

Effects::Element* Effects::initElement(const EffectDef& effect, const EffectElementDef& def, bool child)
{
    // Scene.InitEffectElement
    if (m_active.size() >= kElementMax) {
        return nullptr;
    }
    auto element = std::make_unique<Element>();
    element->def = &def;
    element->effectId = effect.id;
    element->creationTime = m_elapsedTime + (child ? kStep : 0);
    element->expirationTime = element->creationTime + def.lifespan;
    element->flags = def.flags | EffElemFlags::DrawEnabled;
    element->parity = static_cast<int>(m_effectFrame % 2);
    m_active.push_back(std::move(element));
    return m_active.back().get();
}

void Effects::unlinkElement(Element* element)
{
    m_particleCount -= element->particles.size();
    auto it = std::find_if(m_active.begin(), m_active.end(), [&](const auto& e) { return e.get() == element; });
    if (it != m_active.end()) {
        m_active.erase(it);
    }
}

void Effects::spawnInternal(int effectId, const Mat4& transform, bool child, Entry* entry)
{
    // Scene.SpawnEffect
    const EffectDef* effect = load(effectId);
    if (effect == nullptr) {
        return;
    }
    if (qEnvironmentVariableIsSet("FP_DEBUG_EFFECTS")) {
        qInfo("spawn effect %d %s at (%.2f %.2f %.2f): %zu elements, %zu active", effectId, effect->name.c_str(), transform.m[3][0],
            transform.m[3][1], transform.m[3][2], effect->elements.size(), m_active.size());
        for (const EffectElementDef& def : effect->elements) {
            const FxFunc* f = def.actions[Action::IncreaseParticleAmount];
            qInfo("  element %s flags %x draw %d life %.2f particles %zu amount func %d (%zu params)", def.name.c_str(), def.flags,
                def.drawType, def.lifespan, def.particles.size(), f ? static_cast<int>(f->id) : -1, f ? f->params.size() : 0);
        }
    }
    Mat4 t = transform;
    for (const EffectElementDef& def : effect->elements) {
        Element* element = initElement(*effect, def, child);
        if (element == nullptr) {
            return;
        }
        if (entry != nullptr) {
            element->entry = entry;
            entry->elements.push_back(element);
        }
        if (element->flags & EffElemFlags::SpawnUnitVecs) {
            t = transform4({0, 1, 0}, {1, 0, 0}, {t.m[3][0], t.m[3][1], t.m[3][2]});
        }
        element->transform = element->ownTransform = t;
    }
}

void Effects::spawn(int effectId, const Mat4& transform, bool child) { spawnInternal(effectId, transform, child, nullptr); }

void Effects::spawn(int effectId, const Vec3& facing, const Vec3& up, const Vec3& position)
{
    spawnInternal(effectId, Mat4::fromVectors(facing, up, position), false, nullptr);
}

Effects::Handle Effects::spawnEntry(int effectId, const Mat4& transform)
{
    for (size_t i = 0; i < m_entries.size(); i++) {
        Entry& entry = m_entries[i];
        if (!entry.active) {
            entry.active = true;
            entry.effectId = effectId;
            entry.elements.clear();
            spawnInternal(effectId, transform, false, &entry);
            return static_cast<Handle>(i);
        }
    }
    return -1;
}

void Effects::setTransform(Handle handle, const Vec3& position, const Mat4& transform)
{
    // EffectEntry.Transform
    if (handle < 0 || !m_entries[handle].active) {
        return;
    }
    Mat4 t = transform;
    t.m[3][0] = position[0];
    t.m[3][1] = position[1];
    t.m[3][2] = position[2];
    for (Element* element : m_entries[handle].elements) {
        element->ownTransform = t;
    }
}

void Effects::setElementExtension(Handle handle, bool set)
{
    if (handle < 0 || !m_entries[handle].active) {
        return;
    }
    for (Element* element : m_entries[handle].elements) {
        element->flags = set ? element->flags | EffElemFlags::ElementExtension : element->flags & ~EffElemFlags::ElementExtension;
    }
}

void Effects::setDrawEnabled(Handle handle, bool set)
{
    if (handle < 0 || !m_entries[handle].active) {
        return;
    }
    for (Element* element : m_entries[handle].elements) {
        element->flags = set ? element->flags | EffElemFlags::DrawEnabled : element->flags & ~EffElemFlags::DrawEnabled;
    }
}

void Effects::setReadOnlyField(Handle handle, int index, float value)
{
    if (handle < 0 || !m_entries[handle].active || index < 0 || index > 3) {
        return;
    }
    for (Element* element : m_entries[handle].elements) {
        element->ro[index] = value;
    }
}

void Effects::unlink(Handle handle)
{
    if (handle < 0 || !m_entries[handle].active) {
        return;
    }
    Entry& entry = m_entries[handle];
    for (Element* element : entry.elements) {
        unlinkElement(element);
    }
    entry.elements.clear();
    entry.active = false;
}

void Effects::detach(Handle handle, bool setExpired)
{
    // DetachEffectEntry
    if (handle < 0 || !m_entries[handle].active) {
        return;
    }
    Entry& entry = m_entries[handle];
    for (Element* element : entry.elements) {
        if (element->flags & EffElemFlags::DestroyOnDetach) {
            unlinkElement(element);
        } else {
            element->flags &= ~EffElemFlags::ElementExtension;
            element->flags |= EffElemFlags::KeepAlive; // until its particles expire
            element->entry = nullptr;
            if (setExpired) {
                element->expired = true;
            }
        }
    }
    entry.elements.clear();
    entry.active = false;
}

bool Effects::isFinished(Handle handle) const
{
    if (handle < 0 || !m_entries[handle].active) {
        return true;
    }
    for (const Element* element : m_entries[handle].elements) {
        if (!element->expired || !element->particles.empty()) {
            return false;
        }
    }
    return true;
}

void Effects::clear()
{
    m_active.clear();
    for (Entry& entry : m_entries) {
        entry.active = false;
        entry.elements.clear();
    }
    m_particleCount = 0;
}

size_t Effects::particleCount() const { return m_particleCount; }

void Effects::process(const RoomCollision* collision)
{
    // Scene.ProcessEffects, one 60 Hz step. The clock moves on after it: what
    // was spawned during the step sees an elapsed time of zero, which is when
    // the one-shot bursts (func 40) put their particles out.
    struct Advance {
        float& time;
        ~Advance() { time += kStep; }
    } advance{m_elapsedTime};
    const unsigned long long effectFrame = m_effectFrame++;
    for (size_t i = 0; i < m_active.size(); i++) {
        Element& element = *m_active[i];
        const EffectElementDef& def = *element.def;
        const auto& actions = def.actions;
        if (!element.expired && m_elapsedTime > element.expirationTime) {
            if (element.entry == nullptr && !(element.flags & EffElemFlags::KeepAlive)) {
                unlinkElement(&element);
                i--;
                continue;
            }
            element.expired = true;
        }
        if (element.expired) {
            // With an entry, kept until the owner lets go; else until its particles are gone.
            if (element.entry == nullptr && element.particles.empty()) {
                unlinkElement(&element);
                i--;
                continue;
            }
            element.transform = element.ownTransform;
        } else {
            if (element.flags & EffElemFlags::ElementExtension) {
                if (m_elapsedTime - element.creationTime > def.bufferTime) {
                    element.creationTime += def.bufferTime - def.drainTime;
                    element.expirationTime += def.bufferTime - def.drainTime;
                }
            }
            element.transform = element.ownTransform;
            const Times times{m_elapsedTime, m_elapsedTime - element.creationTime, def.lifespan};
            FxEval elementEval{*def.funcs, def.lifespan, element.creationTime, element.transform, element.func39Called};
            if (effectFrame % 2 == static_cast<unsigned long long>(element.parity) && actions[Action::IncreaseParticleAmount]) {
                const FxFunc& f = *actions[Action::IncreaseParticleAmount];
                element.particleAmount += elementEval.floatFunc(f.id, f.params, times);
            }
            const int spawnCount = static_cast<int>(std::floor(element.particleAmount));
            element.particleAmount -= spawnCount;
            float portionTotal = 0;
            for (int j = 0; j < spawnCount; j++) {
                if (m_particleCount >= kParticleMax || def.particles.empty()) {
                    break;
                }
                Particle particle;
                particle.creationTime = m_elapsedTime;
                std::tie(particle.setVecsId, particle.drawId) = funcIds(element.flags, def.drawType);
                particle.portionTotal = portionTotal;
                particle.materialId = def.particles[0].materialId;
                FxEval::ParticleView pv = view(this, particle);
                FxEval eval = elementEval;
                eval.particle = &pv;
                auto call = [&](int action) -> float {
                    pv = view(this, particle);
                    return eval.floatFunc(actions[action]->id, actions[action]->params, times);
                };
                if (const FxFunc* f = actions[Action::SetNewParticlePosition]) {
                    Vec3 temp{};
                    pv = view(this, particle);
                    eval.vecFunc(*f, times, temp);
                    particle.position = temp;
                }
                if (const FxFunc* f = actions[Action::SetNewParticleSpeed]) {
                    Vec3 temp{};
                    pv = view(this, particle);
                    eval.vecFunc(*f, times, temp);
                    particle.speed = temp;
                }
                if (!(element.flags & EffElemFlags::UseTransform)) {
                    particle.position = multMtx4(particle.position, element.transform);
                    particle.speed = multMtx3(particle.speed, element.transform);
                }
                for (int k = 0; k < 4; k++) {
                    particle.ro[k] = actions[Action::SetParticleRoField1 + k] ? call(Action::SetParticleRoField1 + k) : element.ro[k];
                }
                if (const FxFunc* f = actions[Action::SetNewParticleLifespan]) {
                    const Times temp{m_elapsedTime, 1.0f, def.lifespan};
                    pv = view(this, particle);
                    particle.lifespan = eval.floatFunc(f->id, f->params, temp);
                    particle.expirationTime = particle.creationTime + particle.lifespan;
                } else {
                    particle.lifespan = def.lifespan;
                    particle.expirationTime = element.expirationTime;
                }
                // One-time functions are applied now (and the game unsets them).
                if (const FxFunc* f = actions[Action::UpdateParticleSpeed]; f && f->id == 4) {
                    Vec3 temp = particle.speed;
                    pv = view(this, particle);
                    eval.vecFunc(*f, times, temp);
                    particle.speed = temp;
                }
                auto once = [&](int action, float fallback) {
                    const FxFunc* f = actions[action];
                    return f && f->id == 42 ? call(action) : fallback;
                };
                particle.red = once(Action::SetParticleRed, 1);
                particle.green = once(Action::SetParticleGreen, 1);
                particle.blue = once(Action::SetParticleBlue, 1);
                particle.alpha = std::max(once(Action::SetParticleAlpha, 1), 0.0f);
                particle.scale = once(Action::SetParticleScale, 0);
                if (const FxFunc* f = actions[Action::SetParticleRotation]; f && f->id == 42) {
                    particle.rotation = call(Action::SetParticleRotation);
                }
                for (int k = 0; k < 4; k++) {
                    if (actions[Action::SetParticleRwField1 + k]) {
                        particle.rw[k] = call(Action::SetParticleRwField1 + k);
                    }
                }
                portionTotal += 1.0f / spawnCount;
                element.particles.push_back(particle);
                m_particleCount++;
                m_particlesSpawned++;
            }
        }
        for (size_t j = 0; j < element.particles.size(); j++) {
            Particle& particle = element.particles[j];
            if ((element.flags & EffElemFlags::ElementExtension) && (element.flags & EffElemFlags::ParticleExtension)) {
                if (m_elapsedTime - particle.creationTime > def.bufferTime) {
                    particle.creationTime += def.bufferTime - def.drainTime;
                    particle.expirationTime += def.bufferTime - def.drainTime;
                }
            }
            if (m_elapsedTime < particle.expirationTime) {
                const Times times{m_elapsedTime, m_elapsedTime - particle.creationTime, particle.lifespan};
                FxEval::ParticleView pv = view(this, particle);
                FxEval eval{*def.funcs, def.lifespan, element.creationTime, element.transform, element.func39Called};
                eval.particle = &pv;
                auto call = [&](int action) -> float {
                    pv = view(this, particle);
                    return eval.floatFunc(actions[action]->id, actions[action]->params, times);
                };
                for (int k = 0; k < 4; k++) {
                    if (actions[Action::SetParticleRwField1 + k]) {
                        particle.rw[k] = call(Action::SetParticleRwField1 + k);
                    }
                }
                if (actions[Action::SetParticleId]) {
                    particle.particleId = static_cast<int>(call(Action::SetParticleId));
                    particle.particleId = std::clamp(particle.particleId, 0, static_cast<int>(def.particles.size()) - 1);
                    particle.materialId = def.particles[particle.particleId].materialId;
                }
                if (const FxFunc* f = actions[Action::UpdateParticleSpeed]) {
                    Vec3 temp = particle.speed;
                    pv = view(this, particle);
                    eval.vecFunc(*f, times, temp);
                    particle.speed = temp;
                }
                if (actions[Action::SetParticleRed]) {
                    particle.red = call(Action::SetParticleRed);
                }
                if (actions[Action::SetParticleGreen]) {
                    particle.green = call(Action::SetParticleGreen);
                }
                if (actions[Action::SetParticleBlue]) {
                    particle.blue = call(Action::SetParticleBlue);
                }
                if (actions[Action::SetParticleAlpha]) {
                    particle.alpha = std::max(call(Action::SetParticleAlpha), 0.0f);
                }
                if (actions[Action::SetParticleScale]) {
                    particle.scale = call(Action::SetParticleScale);
                }
                if (actions[Action::SetParticleRotation]) {
                    particle.rotation = call(Action::SetParticleRotation);
                }
                if (element.flags & EffElemFlags::UseAcceleration) {
                    particle.speed = particle.speed + def.acceleration * kStep;
                }
                const Vec3 prevPos = particle.position;
                particle.position = particle.position + particle.speed * kStep;
                if ((element.flags & EffElemFlags::CheckCollision) && collision != nullptr) {
                    CollisionResult res;
                    if (collision->checkBetweenPoints(prevPos, particle.position, 0, res)) {
                        particle.position = res.position;
                        particle.expirationTime = m_elapsedTime;
                    }
                }
            } else {
                if ((element.flags & EffElemFlags::SpawnChildEffect) && def.childEffectId != 0) {
                    const Vec3 vec1 = normalized(particle.speed * -1.0f);
                    Vec3 vec2 = vec1[2] <= fx(-3686) || vec1[2] >= fx(3686) ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
                    vec2 = normalized(cross(vec1, vec2));
                    const Vec3 position = particle.position;
                    const int child = def.childEffectId;
                    element.particles.erase(element.particles.begin() + static_cast<long>(j));
                    m_particleCount--;
                    j--;
                    spawn(child, transform4(vec2, vec1, position)); // may grow m_active; `element` stays valid
                    continue;
                }
                element.particles.erase(element.particles.begin() + static_cast<long>(j));
                m_particleCount--;
                j--;
            }
        }
    }
}

void Effects::draw(std::vector<Vertex>& vertices, std::vector<DynamicDraw>& draws, const Mat4& view) const
{
    // GetDrawItems' particle pass: InvokeSetVecsFunc, InvokeDrawFunc, AddRenderItem.
    auto vertex = [](const Vec3& pos, float u, float v, const Vec3& color) {
        Vertex out{};
        out.pos[0] = pos[0];
        out.pos[1] = pos[1];
        out.pos[2] = pos[2];
        out.normal[2] = 1;
        out.color[0] = color[0];
        out.color[1] = color[1];
        out.color[2] = color[2];
        out.color[3] = 1;
        out.uv[0] = u;
        out.uv[1] = v;
        return out;
    };
    for (const auto& ptr : m_active) {
        const Element& element = *ptr;
        if (!(element.flags & EffElemFlags::DrawEnabled)) {
            continue;
        }
        const EffectElementDef& def = *element.def;
        const bool useTransform = element.flags & EffElemFlags::UseTransform;
        for (const Particle& particle : element.particles) {
            if (particle.alpha <= 0 || particle.particleId < 0 || particle.particleId >= static_cast<int>(def.particles.size())) {
                continue;
            }
            const EffectParticleDef& pdef = def.particles[particle.particleId];
            const Model* model = pdef.model;
            if (model == nullptr || particle.materialId >= static_cast<int>(model->materials().size())) {
                continue;
            }
            const Material& material = model->materials()[particle.materialId];
            const Vec3 color{particle.red, particle.green, particle.blue};
            // SetVecs
            Vec3 ev1{1, 0, 0}, ev2{0, -1, 0};
            bool billboard = false;
            switch (particle.setVecsId) {
            case 1: // B0: facing the camera
                billboard = true;
                break;
            case 2: // BC: flat on the element's XZ plane
                ev2 = {0, 0, 1};
                break;
            case 3: { // C0: along -Y, turned to the camera
                Mat4 m = view;
                if (useTransform && !(element.flags & EffElemFlags::UseMesh)) {
                    m = element.transform * view;
                }
                ev1 = {0, -1, 0};
                ev2 = {m.m[0][2], m.m[1][2], m.m[2][2]};
                break;
            }
            default: // D4 is never used; D8: mesh billboards
                billboard = true;
                break;
            }
            DynamicDraw draw;
            draw.model = model;
            draw.textureId = material.textureId;
            draw.paletteId = material.paletteId;
            draw.xRepeat = material.xRepeat;
            draw.yRepeat = material.yRepeat;
            draw.alpha = particle.alpha;
            draw.billboard = billboard;
            draw.firstVertex = static_cast<uint32_t>(vertices.size());
            if (particle.drawId == 7) {
                // DrawDC: the particle node's mesh, scaled, at the particle.
                if (pdef.node < 0) {
                    continue;
                }
                const Node& node = model->nodes()[pdef.node];
                const int meshIndex = node.meshId / 2;
                if (meshIndex >= static_cast<int>(model->meshes().size())) {
                    continue;
                }
                const Mesh& mesh = model->meshes()[meshIndex];
                const Vec3 pos = useTransform ? particle.position + Vec3{element.transform.m[3][0], element.transform.m[3][1], element.transform.m[3][2]}
                                              : particle.position;
                draw.transform = Mat4::scale(particle.scale) * Mat4::translation(pos[0], pos[1], pos[2]);
                const Material& meshMaterial = model->materials()[mesh.materialId];
                draw.textureId = meshMaterial.textureId;
                draw.paletteId = meshMaterial.paletteId;
                draw.xRepeat = meshMaterial.xRepeat;
                draw.yRepeat = meshMaterial.yRepeat;
                Mat4 texMtx = Mat4::identity();
                if (meshMaterial.texgenMode == TexgenMode::Texcoord) {
                    texMtx = Mat4::translation(meshMaterial.scaleS * meshMaterial.translateS, meshMaterial.scaleT * meshMaterial.translateT, 0);
                    texMtx = Mat4::scale(meshMaterial.scaleS, meshMaterial.scaleT, 1) * texMtx;
                    texMtx = Mat4::rotationZ(meshMaterial.rotateZ) * texMtx;
                }
                const DrawRange& range = model->dlistRange(mesh.dlistId);
                for (uint32_t k = 0; k < range.vertexCount; k++) {
                    Vertex v = model->vertices()[range.firstVertex + k];
                    v.matrixId = 0; // the draw's own matrix, not the model's node stack
                    if (v.color[3] < 0) {
                        v.color[0] = color[0];
                        v.color[1] = color[1];
                        v.color[2] = color[2];
                    }
                    v.color[3] = 1;
                    const float u = v.uv[0], w = v.uv[1];
                    v.uv[0] = u * texMtx.m[0][0] + w * texMtx.m[1][0] + texMtx.m[3][0];
                    v.uv[1] = u * texMtx.m[0][1] + w * texMtx.m[1][1] + texMtx.m[3][1];
                    vertices.push_back(v);
                }
            } else {
                // The quad's corners around the particle (DrawB8, DrawCC, DrawShared).
                Vec3 corner[4];
                const float s = particle.scale;
                if (particle.drawId == 1 || particle.drawId == 2) {
                    const Vec3 e1 = ev1 * s, e2 = ev2 * s;
                    const Vec3 v0 = e1 * -0.5f + e2 * 0.5f;
                    corner[0] = v0;
                    corner[1] = v0 + e1;
                    corner[2] = v0 + e1 - e2;
                    corner[3] = v0 - e2;
                } else if (particle.drawId == 4 || particle.drawId == 5) {
                    const float a1 = particle.rotation * kDegToRad, a2 = (particle.rotation + 90) * kDegToRad;
                    const Vec3 e20 = (ev1 * std::sin(a2) + ev2 * std::cos(a2)) * s;
                    const Vec3 e26 = (ev1 * std::sin(a1) + ev2 * std::cos(a1)) * s;
                    const Vec3 v0 = e20 * -0.5f + e26 * 0.5f;
                    corner[0] = v0;
                    corner[1] = v0 + e20;
                    corner[2] = v0 + e20 - e26;
                    corner[3] = v0 - e26;
                } else {
                    // DrawC4 (along the speed, when moving) and DrawD0.
                    if (particle.drawId == 3) {
                        if (dot(particle.speed, particle.speed) <= fx(128)) {
                            continue;
                        }
                        ev1 = normalized(particle.speed);
                    }
                    const Vec3 c = normalized(cross(ev1, ev2)) * particle.rotation;
                    const Vec3 e1 = ev1 * s;
                    const Vec3 v0 = e1 * -0.5f + c * 0.5f;
                    corner[0] = v0;
                    corner[1] = v0 + e1;
                    corner[2] = v0 + e1 - c;
                    corner[3] = v0 - c;
                }
                // SingleParticle/EffectParticle.AddRenderItem: where the quad is drawn.
                if (useTransform) {
                    if (billboard) {
                        const Vec3 p = multMtx4(particle.position, element.transform);
                        draw.transform = Mat4::translation(p[0], p[1], p[2]);
                    } else {
                        draw.transform = Mat4::translation(particle.position[0], particle.position[1], particle.position[2]) * element.transform;
                    }
                } else {
                    draw.transform = Mat4::translation(particle.position[0], particle.position[1], particle.position[2]);
                }
                const float scaleS = material.xRepeat == RepeatMode::Mirror ? material.scaleS : 1;
                const float scaleT = material.yRepeat == RepeatMode::Mirror ? material.scaleT : 1;
                const float uvs[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
                const Vertex q[4] = {vertex(corner[0], uvs[0][0] * scaleS, uvs[0][1] * scaleT, color),
                    vertex(corner[1], uvs[1][0] * scaleS, uvs[1][1] * scaleT, color),
                    vertex(corner[2], uvs[2][0] * scaleS, uvs[2][1] * scaleT, color),
                    vertex(corner[3], uvs[3][0] * scaleS, uvs[3][1] * scaleT, color)};
                for (int k : {0, 1, 2, 0, 2, 3}) {
                    vertices.push_back(q[k]);
                }
            }
            draw.vertexCount = static_cast<uint32_t>(vertices.size()) - draw.firstVertex;
            if (draw.vertexCount > 0) {
                draws.push_back(draw);
            }
        }
    }
}

} // namespace fp
