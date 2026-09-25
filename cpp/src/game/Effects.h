#pragma once

#include "Collision.h"
#include "Rng.h"
#include "formats/Mat4.h"
#include "render/Scene.h"

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fp {

// EffElemFlags
namespace EffElemFlags {
constexpr uint32_t UseTransform = 0x1;
constexpr uint32_t UseAcceleration = 0x2;
constexpr uint32_t UseMesh = 0x4;
constexpr uint32_t SpawnUnitVecs = 0x8;
constexpr uint32_t KeepAlive = 0x10;
constexpr uint32_t ParticleExtension = 0x20;
constexpr uint32_t CheckCollision = 0x40;
constexpr uint32_t SpawnChildEffect = 0x80;
constexpr uint32_t DestroyOnDetach = 0x100;
constexpr uint32_t ElementExtension = 0x80000;
constexpr uint32_t DrawEnabled = 0x100000;
} // namespace EffElemFlags

// FxFuncInfo: one of the effect file's little functions and its parameters.
struct FxFunc {
    uint32_t id = 0;
    std::vector<int32_t> params;
};

// Particle: a model node whose mesh (or material's texture) a particle draws.
struct EffectParticleDef {
    const Model* model = nullptr;
    int node = -1;
    int materialId = 0;
};

// EffectElement, as read from the _PS.bin file.
struct EffectElementDef {
    std::string name;
    std::vector<EffectParticleDef> particles;
    uint32_t flags = 0;
    Vec3 acceleration{};
    int childEffectId = 0;
    float lifespan = 0, drainTime = 0, bufferTime = 0;
    int drawType = 0;
    std::array<const FxFunc*, 33> actions{}; // by FuncAction
    const std::map<uint32_t, FxFunc>* funcs = nullptr;
};

// Effect: the file's functions (keyed by file offset: some parameters are
// offsets of other functions) and its elements.
struct EffectDef {
    int id = 0;
    std::string name;
    std::map<uint32_t, FxFunc> funcs;
    std::vector<EffectElementDef> elements;
};

// The Renderer's effect system (EffectEntry, EffectElementEntry,
// EffectParticle and ProcessEffects): particles spawned and moved by the
// effect files' functions, one 60 Hz step at a time, drawn as the
// renderer's translucent Particle items.
class Effects {
public:
    using Handle = int; // an EffectEntry, -1 for none

    Effects(Scene& scene, const std::filesystem::path& root);
    ~Effects();

    // Read.LoadEffect: the file, its child effects and the particle models.
    // Loaded models join the renderer's vertex buffer only if loaded before it
    // is built, so every effect a match can spawn is loaded up front.
    const EffectDef* load(int effectId);
    void loadAll();
    // Scene.SpawnEffect: fire and forget.
    void spawn(int effectId, const Mat4& transform, bool child = false);
    void spawn(int effectId, const Vec3& facing, const Vec3& up, const Vec3& position);
    // Scene.SpawnEffectGetEntry: an effect its owner moves (Transform) and ends (Unlink/Detach).
    Handle spawnEntry(int effectId, const Mat4& transform);
    void setTransform(Handle entry, const Vec3& position, const Mat4& transform);
    void setElementExtension(Handle entry, bool set);
    void setDrawEnabled(Handle entry, bool set);
    void setReadOnlyField(Handle entry, int index, float value);
    // UnlinkEffectEntry: it and its particles go at once.
    void unlink(Handle entry);
    // DetachEffectEntry: the particles out live on until they expire.
    void detach(Handle entry, bool setExpired);
    bool isFinished(Handle entry) const;
    int effectId(Handle entry) const { return entry >= 0 && m_entries[entry].active ? m_entries[entry].effectId : 0; }
    void clear();

    // One simulation step (ProcessEffects), then the particles as dynamic draws.
    // `view` is the camera's world-to-view rotation (for the particles that
    // stretch along their speed).
    void process(const RoomCollision* collision);
    void draw(std::vector<Vertex>& vertices, std::vector<DynamicDraw>& draws, const Mat4& view) const;
    size_t particleCount() const;
    long long particlesSpawned() const { return m_particlesSpawned; }

private:
    struct Element;
    struct Particle;
    struct Entry {
        bool active = false;
        int effectId = 0;
        std::vector<Element*> elements;
    };

    Element* initElement(const EffectDef& effect, const EffectElementDef& def, bool child);
    void unlinkElement(Element* element);
    void spawnInternal(int effectId, const Mat4& transform, bool child, Entry* entry);

    Scene& m_scene;
    std::filesystem::path m_root;
    std::map<int, std::unique_ptr<EffectDef>> m_effects;
    std::vector<std::unique_ptr<Element>> m_active; // _activeElements, in creation order
    std::vector<Entry> m_entries;                   // _effectEntryMax of them
    size_t m_particleCount = 0;
    float m_elapsedTime = 0;
    unsigned long long m_effectFrame = 0;
    long long m_particlesSpawned = 0;
};


} // namespace fp
