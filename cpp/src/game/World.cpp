#include "World.h"
#include "audio/Music.h"

#include "formats/Enums.h"
#include "formats/Metadata.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace fp {

namespace {

constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;

Vec3 fxVec(const Entity& e, size_t offset)
{
    return {fxToFloat(e.i32(offset)), fxToFloat(e.i32(offset + 4)), fxToFloat(e.i32(offset + 8))};
}

Vec3 normalized(Vec3 v)
{
    const float len = std::sqrt(dot(v, v));
    return len > 0 ? v * (1.0f / len) : v;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// Matrix.Vec3MultMtx3 with an entity transform: the rotation part only.
Vec3 rotate(const Vec3& v, const Mat4& m)
{
    return {v[0] * m.m[0][0] + v[1] * m.m[1][0] + v[2] * m.m[2][0], v[0] * m.m[0][1] + v[1] * m.m[1][1] + v[2] * m.m[2][1],
        v[0] * m.m[0][2] + v[1] * m.m[1][2] + v[2] * m.m[2][2]};
}

// SpinningEntityBase.GetItemRotation
float nextItemRotation()
{
    static uint16_t next = 0;
    const float rotation = next / static_cast<float>(0x10000) * 360.0f;
    next = static_cast<uint16_t>(next + 0x2000);
    return rotation;
}

namespace Offsets {
[[maybe_unused]] constexpr size_t DoorPaletteId = 56, DoorType = 60, DoorLocked = 69;
[[maybe_unused]] constexpr size_t JumpPadVolume = 48, JumpPadBeam = 112, JumpPadSpeed = 124, JumpPadLock = 128, JumpPadCooldown = 130,
                 JumpPadActive = 132, JumpPadModel = 136, JumpPadTrigger = 144;
[[maybe_unused]] constexpr size_t ItemType = 44, ItemEnabled = 48, ItemHasBase = 49, ItemMaxSpawn = 52, ItemInterval = 54, ItemDelay = 56;
constexpr size_t TeleporterArtifactId = 42, TeleporterActive = 43, TeleporterInvisible = 44, TeleporterTarget = 64;
constexpr size_t ForceFieldType = 40, ForceFieldWidth = 44, ForceFieldHeight = 48, ForceFieldActive = 52;
} // namespace Offsets

constexpr uint32_t TriggerPlayerBiped = 0x200;

} // namespace

TriggerVolume TriggerVolume::fromRaw(const Entity& e, size_t o, const Vec3& moveBy)
{
    TriggerVolume t;
    t.type = static_cast<Type>(e.u32(o));
    if (t.type == Box) {
        t.v1 = fxVec(e, o + 4);
        t.v2 = fxVec(e, o + 16);
        t.v3 = fxVec(e, o + 28);
        t.position = fxVec(e, o + 40) + moveBy;
        t.d1 = fxToFloat(e.i32(o + 52));
        t.d2 = fxToFloat(e.i32(o + 56));
        t.d3 = fxToFloat(e.i32(o + 60));
    } else if (t.type == Cylinder) {
        t.v1 = fxVec(e, o + 4);
        t.position = fxVec(e, o + 16) + moveBy;
        t.radius = fxToFloat(e.i32(o + 28));
        t.d1 = fxToFloat(e.i32(o + 32));
    } else {
        t.position = fxVec(e, o + 4) + moveBy;
        t.radius = fxToFloat(e.i32(o + 16));
    }
    return t;
}

bool TriggerVolume::testPoint(const Vec3& point) const
{
    // CollisionVolume.TestPoint
    if (type == Box) {
        const Vec3 diff = point - position;
        const float a = dot(v1, diff), b = dot(v2, diff), c = dot(v3, diff);
        return a >= 0 && a <= d1 && b >= 0 && b <= d2 && c >= 0 && c <= d3;
    }
    if (type == Cylinder) {
        const Vec3 bottom = position;
        const Vec3 top = bottom + v1 * d1;
        const Vec3 axis = top - bottom;
        if (dot(point - bottom, axis) >= 0 && dot(point - top, axis) <= 0) {
            const Vec3 c = cross(point - bottom, axis);
            return std::sqrt(dot(c, c)) / std::sqrt(dot(axis, axis)) <= radius;
        }
        return false;
    }
    const Vec3 d = point - position;
    return std::sqrt(dot(d, d)) <= radius;
}

bool TriggerVolume::overlapsSphere(const Vec3& center, float r) const
{
    if (type == Cylinder) {
        Vec3 between = center - position;
        const float d = dot(v1, between);
        if (d >= -r && d <= d1 + r) {
            between = between - v1 * d;
            const float radii = r + radius;
            return dot(between, between) <= radii * radii;
        }
        return false;
    }
    if (type == Sphere) {
        const Vec3 between = position - center;
        const float radii = radius + r;
        return dot(between, between) <= radii * radii;
    }
    const Vec3 between = center - position;
    const float a = dot(v1, between), b = dot(v2, between), c = dot(v3, between);
    return a >= -r && a <= d1 + r && b >= -r && b <= d2 + r && c >= -r && c <= d3 + r;
}

World::World(Scene& scene, const std::filesystem::path& root, std::unique_ptr<RoomCollision> collision,
    const std::vector<Entity>& entities, int hunter)
    : m_scene(scene)
    , m_root(root)
    , m_collision(std::move(collision))
{
    scene.externalAnimation = true;
    // What beams draw with, loaded before the renderer builds its vertex buffer.
    m_trail = beamTexture(scene.model(root, "trail"), 0);
    m_electroTrail = beamTexture(scene.model(root, "electroTrail"), 0);
    m_arcWelder = beamTexture(scene.model(root, "arcWelder"), 0);
    m_arcWelder1 = beamTexture(scene.model(root, "arcWelder1"), 0);
    // Every effect and its particle models, before the renderer builds its vertex buffer.
    m_effects = std::make_unique<Effects>(scene, root);
    m_effects->loadAll();
    scene.model(root, "KandenAlt_TailBomb");
    for (const char* name : {"iceShard", "iceWave", "sniperBeam"}) {
        scene.model(root, name);
    }
    // Metadata.SingleParticles: Fuzzball is the "fuzzBall" node of "particles", Death the "death" node of "deathParticle".
    auto singleParticle = [&](const char* modelName, const char* nodeName) {
        const Model* model = scene.model(root, modelName);
        if (model != nullptr) {
            for (const Node& node : model->nodes()) {
                const int mesh = model->firstMesh(node);
                if (node.name == nodeName && node.meshCount > 0 && mesh >= 0 && mesh < static_cast<int>(model->meshes().size())) {
                    return beamTexture(model, model->meshes()[mesh].materialId);
                }
            }
        }
        return BeamTexture{};
    };
    m_fuzzball = singleParticle("particles", "fuzzBall");
    m_deathParticle = singleParticle("deathParticle", "death");
    m_spawns = playerSpawns(entities);
    m_spawnCooldowns.assign(m_spawns.size(), 0);
    createSlot(hunter);
    // The ammo killed players drop: a few of each, hidden until dropped.
    for (ItemType type : {ItemType::UASmall, ItemType::MissileSmall}) {
        const Model* model = scene.model(root, itemTable()[static_cast<int>(type)]);
        for (int i = 0; model != nullptr && i < 8; i++) {
            ModelInstance inst;
            inst.model = model;
            inst.visible = false;
            inst.spinDegreesPerSecond = 0.35f * 360.0f;
            inst.floats = true;
            scene.instances.push_back(inst);
            ItemInstance item;
            item.type = type;
            item.modelInstance = scene.instances.size() - 1;
            m_droppedItems.push_back(item);
        }
    }

    auto add = [&](const std::string& name, const Mat4& transform, int recolor = 0) -> std::optional<size_t> {
        const Model* model = scene.model(root, name);
        if (model == nullptr) {
            return std::nullopt;
        }
        ModelInstance inst;
        inst.model = model;
        inst.transform = transform;
        inst.recolor = recolor >= 0 && recolor < static_cast<int>(model->recolorCount()) ? recolor : 0;
        inst.animation.set(model->animations(), 0);
        scene.instances.push_back(inst);
        return scene.instances.size() - 1;
    };

    for (const Entity& e : entities) {
        const Mat4 transform = Mat4::fromVectors(e.facing, e.up, e.position);
        switch (e.type) {
        case EntityType::Door: {
            const uint32_t type = e.u32(Offsets::DoorType);
            if (type >= doorTable().size()) {
                break;
            }
            const DoorMetadata& meta = doorTable()[type];
            auto inst = add(meta.name, transform);
            if (!inst) {
                break;
            }
            // Closed: animation 0 ended at its first frame, reversed (DoorEntity constructor).
            ModelInstance& door = scene.instances[*inst];
            door.animation.set(door.model->animations(), 0, AnimFlags::Ended | AnimFlags::NoLoop);
            door.animation.flags |= AnimFlags::Reverse;
            Door d;
            d.instance = *inst;
            d.position = e.position;
            d.facing = e.facing;
            d.lockPosition = e.position + Vec3{0, meta.lockOffset, 0};
            d.radiusSquared = meta.radius * meta.radius;
            d.type = type;
            m_doors.push_back(d);
            break;
        }
        case EntityType::MorphCamera:
            m_morphCameras.push_back({e.id, e.position, TriggerVolume::fromRaw(e, 40, e.position)});
            break;
        case EntityType::LightSource: {
            // LightSourceEntityData: the volume, then two lights of an enable byte, an 8-bit color and a vector.
            if (e.data.size() < 136) {
                break;
            }
            LightSource light;
            light.volume = TriggerVolume::fromRaw(e, 40, e.position);
            light.light1Enabled = e.u8(104) != 0;
            light.light1Color = {e.u8(105) / 255.0f, e.u8(106) / 255.0f, e.u8(107) / 255.0f};
            light.light1Vector = fxVec(e, 108);
            light.light2Enabled = e.u8(120) != 0;
            light.light2Color = {e.u8(121) / 255.0f, e.u8(122) / 255.0f, e.u8(123) / 255.0f};
            light.light2Vector = fxVec(e, 124);
            m_lightSources.push_back(light);
            break;
        }
        case EntityType::Teleporter: {
            // TeleporterEntity, multiplayer: TeleporterMP (small ones) closed,
            // its animation 2 ended at the first frame; the invisible ones have no model.
            Teleporter t;
            t.id = e.id;
            t.position = e.position;
            t.facing = e.facing;
            t.targetPosition = fxVec(e, Offsets::TeleporterTarget);
            t.active = e.u8(Offsets::TeleporterActive) != 0;
            t.invisible = e.u8(Offsets::TeleporterInvisible) != 0;
            t.big = e.u8(Offsets::TeleporterArtifactId) < 8;
            t.triggered.fill(true);
            if (!t.invisible) {
                t.model = add(t.big ? "Teleporter" : "TeleporterMP", transform);
                if (t.model) {
                    AnimationState& anim = scene.instances[*t.model].animation;
                    anim.set(scene.instances[*t.model].model->animations(), 2, AnimFlags::NoLoop | AnimFlags::Reverse);
                    anim.frame = 0;
                    anim.flags |= AnimFlags::Ended;
                }
            }
            m_teleporters.push_back(t);
            break;
        }
        case EntityType::JumpPad: {
            const uint32_t modelId = e.u32(Offsets::JumpPadModel);
            if (modelId < jumpPadTable().size()) {
                add(jumpPadTable()[modelId], transform);
            }
            const Vec3 beam = normalized(fxVec(e, Offsets::JumpPadBeam));
            JumpPad pad;
            pad.active = e.u8(Offsets::JumpPadActive) != 0;
            pad.volume = TriggerVolume::fromRaw(e, Offsets::JumpPadVolume, e.position);
            pad.beamVector = rotate(beam, transform) * fxToFloat(e.i32(Offsets::JumpPadSpeed));
            pad.controlLockTime = static_cast<uint16_t>(e.u32(Offsets::JumpPadLock) & 0xFFFF);
            pad.cooldownTime = static_cast<uint16_t>(e.u32(Offsets::JumpPadLock) >> 16);
            pad.triggerFlags = e.u32(Offsets::JumpPadTrigger);
            pad.position = e.position;
            m_jumpPads.push_back(pad);
            if (pad.active) {
                const Vec3 up = beam[0] != 0 || beam[2] != 0 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                Mat4 beamTransform = Mat4::fromVectors(beam, up, {0, 0, 0});
                beamTransform.m[3][1] = 0.25f;
                add("JumpPad_Beam", beamTransform * transform);
            }
            break;
        }
        case EntityType::ItemSpawn: {
            if (e.u8(Offsets::ItemHasBase) != 0) {
                add("items_base", transform);
            }
            const int32_t itemType = e.i32(Offsets::ItemType);
            if (itemType < 0 || itemType >= static_cast<int>(itemTable().size())) {
                break;
            }
            ItemSpawn spawn;
            spawn.id = e.id;
            spawn.type = static_cast<ItemType>(itemType);
            spawn.position = e.position;
            spawn.instance.type = spawn.type;
            spawn.instance.position = e.position + Vec3{0, 0.65f, 0};
            spawn.active = e.u8(Offsets::ItemEnabled) != 0;
            const uint32_t counts = e.u32(Offsets::ItemMaxSpawn);
            spawn.maxSpawnCount = static_cast<uint16_t>(counts & 0xFFFF);
            spawn.spawnInterval = static_cast<uint16_t>(counts >> 16);
            spawn.cooldown = static_cast<uint16_t>((e.u32(Offsets::ItemDelay) & 0xFFFF) * 2);
            spawn.initialCooldown = spawn.cooldown;
            // The item model is loaded now and shown while the item is there.
            const Vec3& at = spawn.instance.position;
            auto inst = add(itemTable()[itemType], Mat4::translation(at[0], at[1], at[2]));
            if (!inst) {
                break;
            }
            ModelInstance& item = scene.instances[*inst];
            item.visible = false;
            item.spinDegreesPerSecond = 0.35f * 360.0f;
            item.spinStartDegrees = nextItemRotation();
            item.floats = true;
            spawn.instance.modelInstance = *inst;
            m_items.push_back(spawn);
            break;
        }
        case EntityType::ForceField: {
            const float width = fxToFloat(e.i32(Offsets::ForceFieldWidth));
            const float height = fxToFloat(e.i32(Offsets::ForceFieldHeight));
            const Vec3 facing = normalized(e.facing);
            ForceField ff;
            ff.active = e.u8(Offsets::ForceFieldActive) != 0;
            ff.obstacle.position = e.position;
            ff.obstacle.up = normalized(e.up);
            ff.obstacle.right = normalized(cross(e.up, e.facing));
            ff.obstacle.width = width;
            ff.obstacle.height = height;
            ff.obstacle.plane = {facing[0], facing[1], facing[2], dot(facing, e.position)};
            m_forceFields.push_back(ff);
            if (ff.active) {
                const uint32_t type = e.u32(Offsets::ForceFieldType);
                const int recolor = type < doorPaletteTable().size() ? doorPaletteTable()[type] : 0;
                add("ForceField", Mat4::scale(width, height, 1.0f) * transform, recolor);
            }
            break;
        }
        case EntityType::AreaVolume: {
            // AreaVolumeEntityData, after the header and the volume.
            if (e.data.size() < 152) {
                break;
            }
            AreaVolume area;
            area.id = e.id;
            area.volume = TriggerVolume::fromRaw(e, 40, e.position);
            area.active = e.u8(106) != 0;
            area.allowMultiple = e.u8(108) != 0;
            area.insideMessage = e.u32(112);
            area.insideParam1 = e.i32(116);
            area.insideParam2 = e.i32(120);
            area.parentId = static_cast<int16_t>(e.u32(124) & 0xFFFF);
            area.exitMessage = e.u32(128);
            area.exitParam1 = e.i32(132);
            area.exitParam2 = e.i32(136);
            area.childId = static_cast<int16_t>(e.u32(140) & 0xFFFF);
            area.cooldownTime = static_cast<int>(e.u32(140) >> 16);
            if (area.cooldownTime > 0) {
                area.cooldownTime--;
            }
            area.cooldownTime *= 2;
            area.priority = e.u32(144);
            area.triggerFlags = e.u32(148);
            area.prioritySlots.fill(area.priority);
            m_areaVolumes.push_back(area);
            break;
        }
        case EntityType::OctolithFlag:
        case EntityType::FlagBase:
        case EntityType::NodeDefense:
            m_modeEntities.push_back(e); // set up with the mode
            break;
        default:
            break;
        }
    }
}

void World::setSuit(size_t slot, int suit)
{
    if (slot < m_slots.size()) {
        m_slots[slot].recolor = suit;
        applyTeamVisuals(m_slots[slot]);
    }
}

std::string World::slotName(size_t slot) const
{
    if (slot >= m_slots.size()) {
        return {};
    }
    return m_slots[slot].name.empty() ? "Player" + std::to_string(slot + 1) : m_slots[slot].name;
}

void World::setNetDamageHooks(NetDamageHooks* hooks)
{
    m_netHooks = hooks;
    for (PlayerSlot& slot : m_slots) {
        slot.player->setNetDamageHooks(hooks);
    }
}

World::PlayerSlot& World::createSlot(int hunter)
{
    // PlayerEntity: the hunter's models (Metadata.HunterModels), in the slot's recolor.
    static constexpr const char* hunterModels[8][3] = {
        {"Samus_lod0", "SamusAlt_lod0", "SamusGun"},
        {"Kanden_lod0", "KandenAlt_lod0", "KandenGun"},
        {"Trace_lod0", "TraceAlt_lod0", "TraceGun"},
        {"Sylux_lod0", "SyluxAlt_lod0", "SyluxGun"},
        {"Nox_lod0", "NoxAlt_lod0", "NoxGun"},
        {"Spire_lod0", "SpireAlt_lod0", "SpireGun"},
        {"Weavel_lod0", "WeavelAlt_lod0", "WeavelGun"},
        {"Guardian_lod0", "SamusAlt_lod0", "SamusGun"},
    };
    hunter = std::clamp(hunter, 0, 7);
    const int slotIndex = static_cast<int>(m_slots.size());
    const auto& names = hunterModels[hunter];
    auto playerInstance = [&](const char* name, int animation) -> std::optional<size_t> {
        const Model* model = m_scene.model(m_root, name);
        if (model == nullptr) {
            return std::nullopt;
        }
        ModelInstance inst;
        inst.model = model;
        inst.recolor = slotIndex < static_cast<int>(model->recolorCount()) ? slotIndex : 0;
        inst.visible = false;
        inst.includeModelScale = false;
        inst.animation.set(model->animations(), animation);
        m_scene.instances.push_back(inst);
        m_ownedInstances.push_back(m_scene.instances.size() - 1);
        return m_scene.instances.size() - 1;
    };
    PlayerSlot slot;
    slot.recolor = slotIndex;
    slot.lights = roomLights();
    slot.player = std::make_unique<Player>(hunter, m_collision.get());
    slot.player->setSlot(slotIndex);
    slot.player->setMainPlayer(slotIndex == 0);
    slot.player->setNetDamageHooks(m_netHooks);
    for (auto& sound : slot.beamSounds) {
        sound = std::make_unique<SoundSource>();
    }
    slot.player->setTeamIndex(slotIndex); // everybody for themselves, until assignTeams
    slot.biped = playerInstance(names[0], 8); // PlayerAnimation.Idle
    slot.alt = playerInstance(names[1], 0);
    slot.gun = playerInstance(names[2], gunAnimationId(hunter, GunAnim::Idle, 0));
    if (hunter == static_cast<int>(Hunter::Weavel)) {
        slot.turret = playerInstance("WeavelAlt_Turret_lod0", 1);
    }
    PlayerModels playerModels;
    if (slot.turret) {
        updateTurretModel(slot);
    }
    if (slot.gun) {
        ModelInstance& gun = m_scene.instances[*slot.gun];
        // Sylux's gun keeps its texcoord animation through a weapon switch.
        gun.secondaryTexcoord = hunter != static_cast<int>(Hunter::Sylux);
        playerModels.gun = &gun.model->animations();
    }
    if (slot.biped) {
        ModelInstance& biped = m_scene.instances[*slot.biped];
        biped.useNodeTransform = false;
        biped.animationScale = 1.0f;
        playerModels.biped = &biped.model->animations();
        const auto& nodes = biped.model->nodes();
        for (size_t i = 0; i < nodes.size(); i++) {
            if (nodes[i].name == "Spine_1") {
                slot.spineNode = static_cast<int>(i);
            }
        }
    }
    if (slot.alt) {
        const Model& alt = *m_scene.instances[*slot.alt].model;
        playerModels.alt = &alt.animations();
        if (hunter == 1 && alt.nodes().size() >= 5) {
            // PlayerEntity.GenerateKandenAltNodeDistances
            for (size_t i = 0; i < 4; i++) {
                const Vec3 a = alt.nodes()[i].transform.translationPart();
                const Vec3 b = alt.nodes()[i + 1].transform.translationPart();
                playerModels.kandenNodeDistances[i] = std::sqrt(dot(a - b, a - b));
            }
        }
    }
    if (slot.alt) {
        playerModels.altModel = m_scene.instances[*slot.alt].model;
    }
    slot.player->setModels(playerModels);
    slot.player->setDamageListener([this](Player& victim, const DamageSource& source, bool died, uint32_t flags) {
        onDamage(victim, source, died, flags);
        if (m_authority.damage) {
            m_authority.damage(victim, source, died, flags);
        }
    });
    slot.player->setPlayers(&m_players);
    slot.player->setEffects(m_effects.get());
    slot.player->setFireCallback([this](Player& owner, EquipInfo& equip, const Vec3& position, const Vec3& direction, int flags) {
        if (!m_authority.beginShot) {
            return spawnBeam(owner.slot(), equip, *equip.weapon, position, direction, flags, nullptr);
        }
        m_authority.beginShot(owner);
        const int beam = spawnBeam(owner.slot(), equip, *equip.weapon, position, direction, flags, nullptr);
        m_authority.endShot(owner);
        return beam;
    });
    slot.player->setBombCallback([this](Player& owner, const Mat4& transform) { return spawnBomb(owner, transform); });
    m_slots.push_back(std::move(slot));
    m_players.push_back(m_slots.back().player.get());
    if (teams() && !m_networked) {
        assignTeams();
    }
    updateStandings();
    return m_slots.back();
}

int World::addBot(int hunter, int level)
{
    PlayerSlot& slot = createSlot(hunter);
    slot.player->setBot(true, level);
    slot.ai = std::make_unique<PlayerAi>(*slot.player, *this);
    respawn(slot);
    return slot.player->slot();
}

void World::loadNodeData(const std::filesystem::path& file)
{
    // SceneSetup.LoadNodeData: and in a simple one, the jump pads' nodes.
    try {
        m_nodeData = NodeData::load(file);
    } catch (const std::exception& e) {
        qWarning("[nodes] %s: %s; bots in this room will not navigate", file.string().c_str(), e.what());
        m_nodeData.reset();
        return;
    }
    if (m_nodeData->Simple()) {
        for (JumpPad& pad : m_jumpPads) {
            pad.closestNode = m_nodeData->findClosestNode(pad.position, true);
        }
        for (OctolithFlag& flag : m_octolithFlags) {
            flag.closestNode = m_nodeData->findClosestNode(flag.position);
            flag.baseClosestNode = m_nodeData->findClosestNode(flag.basePosition);
        }
        for (FlagBase& base : m_flagBases) {
            base.closestNode = m_nodeData->findClosestNode(base.position);
        }
        for (NodeDefense& defense : m_nodeDefenses) {
            defense.closestNode = m_nodeData->findClosestNode(defense.position);
        }
    }
    if (qEnvironmentVariableIsSet("FP_DEBUG_NODES")) {
        for (size_t set = 0; set < m_nodeData->Data.size(); set++) {
            for (size_t list = 0; list < m_nodeData->Data[set].size(); list++) {
                for (const NodeData3& node : m_nodeData->Data[set][list]) {
                    qInfo("node set %zu list %zu id %u type %d field4 %u at (%.2f %.2f %.2f)", set, list, node.Id,
                        static_cast<int>(node.NodeType), node.Field4, node.Position[0], node.Position[1], node.Position[2]);
                }
            }
        }
    }
}

int World::roomId() const { return m_scene.room != nullptr ? m_scene.room->id : -1; }

void World::processBots()
{
    // Renderer: the bots see who they can see, then decide what to press next tick.
    bool any = false;
    for (PlayerSlot& slot : m_slots) {
        any = any || slot.ai != nullptr;
    }
    if (!any) {
        return;
    }
    if (m_aiPersonality == nullptr) {
        // AiPersonality.LoadAll (Battle: the default tree) and the first InitializeAtLoad.
        try {
            m_aiPersonality = std::make_unique<AiPersonality>(m_root);
        } catch (const std::exception& e) {
            qWarning("AI personality data: %s", e.what());
            for (PlayerSlot& slot : m_slots) {
                slot.ai.reset();
            }
            return;
        }
        PlayerAi::InitializeGlobals(m_aiGlobals);
        // AiPersonality.LoadAll: each mode has its tree.
        int offset = 32896; // Battle
        if (m_mode == GameMode::Survival || m_mode == GameMode::SurvivalTeams) {
            offset = 45696;
        } else if (octolithMode()) {
            offset = 32968;
        } else if (nodesMode() || defenderMode()) {
            offset = 33012;
        } else if (m_mode == GameMode::PrimeHunter) {
            offset = 45220;
        }
        const AiPersonalityData1* battle = m_aiPersonality->load(offset);
        for (PlayerSlot& slot : m_slots) {
            if (slot.ai != nullptr) {
                slot.ai->Reset();
                slot.ai->setPersonality(battle);
                slot.ai->InitializeAtLoad();
            }
        }
    }
    PlayerAi::UpdateVisibilityAndGlobals(*this);
    for (PlayerSlot& slot : m_slots) {
        slot.player->closestNode = nullptr;
    }
    for (PlayerSlot& slot : m_slots) {
        if (slot.ai != nullptr && slot.player->health() != 0) {
            slot.ai->Process();
        }
    }
}

int World::addPlayer(int hunter)
{
    PlayerSlot& slot = createSlot(hunter);
    respawn(slot);
    return slot.player->slot();
}

void World::spawnPlayer(const Vec3& position, const Vec3& facing)
{
    m_slots[0].player->spawn(position, facing);
    m_slots[0].lights = roomLights();
    m_introActive = false;
}

void World::setIntro(std::optional<CameraSequence> intro)
{
    m_intro = std::move(intro);
    m_introActive = false;
    if (m_intro) {
        m_slots[0].player->despawn();
        ensureIntro();
    }
}

void World::ensureIntro()
{
    if (m_intro && !m_introActive) {
        m_intro->loop = true;
        m_intro->start();
        m_introActive = true;
        m_introStartTick = m_globalTicks;
    }
}

std::optional<CameraView> World::introCamera() const
{
    if (!m_introActive || !m_intro) {
        return std::nullopt;
    }
    return m_intro->view();
}

void World::respawn(PlayerSlot& slot)
{
    // PlayerProcess.GetRespawnPoint: a point with nobody alive within 10 units,
    // else the one whose nearest player is furthest.
    int chosen = -1;
    std::vector<int> valid;
    float bestDistance = 0;
    int best = -1;
    for (size_t i = 0; i < m_spawns.size() && i < 25; i++) {
        if (!m_spawns[i].active || m_spawnCooldowns[i] != 0) {
            continue;
        }
        if (m_mode == GameMode::Capture && m_spawns[i].teamIndex != -1 && m_spawns[i].teamIndex != slot.player->teamIndex()) {
            continue; // Capture: each team's own spawn points
        }
        float minDistSqr = 100;
        for (const PlayerSlot& other : m_slots) {
            if (other.player->health() > 0 && &other != &slot) {
                const Vec3 between = m_spawns[i].position - other.player->position();
                minDistSqr = std::min(minDistSqr, dot(between, between));
            }
        }
        if (minDistSqr >= 100) {
            valid.push_back(static_cast<int>(i));
        } else if (minDistSqr > bestDistance) {
            bestDistance = minDistSqr;
            best = static_cast<int>(i);
        }
    }
    if (!valid.empty()) {
        chosen = valid[static_cast<size_t>(m_ticks) % valid.size()];
    } else {
        chosen = best;
    }
    if (chosen < 0) {
        for (size_t i = 0; i < m_spawns.size(); i++) {
            if (m_spawns[i].active) {
                chosen = static_cast<int>(i);
                break;
            }
        }
    }
    if (chosen < 0) {
        return;
    }
    m_spawnCooldowns[chosen] = 2 * 2;
    // PlayerEntity.Spawn: the Lockjaw bombs of a past life are no longer this one's.
    slot.syluxBombs = {-1, -1, -1};
    slot.syluxBombCount = 0;
    slot.player->spawn(m_spawns[chosen].position, m_spawns[chosen].facing, true);
    slot.lights = roomLights(); // PlayerEntity.Spawn
    if (&slot == &m_slots[0]) {
        m_introActive = false; // PlayerEntity.Spawn ends the intro
    }
    if (slot.ai != nullptr && m_aiPersonality != nullptr) {
        slot.ai->InitializeAtSpawn();
    }
}

void World::onDamage(Player& victim, const DamageSource& source, bool died, uint32_t flags)
{
    // TakeDamage's multiplayer bookkeeping for a battle: a kill scores, a
    // suicide costs, and the main player hears about what it did or suffered.
    Player* main = m_slots[0].player.get();
    const bool headshot = flags & DamageFlags::Headshot;
    PlayerSlot& hitSlot = m_slots[victim.slot()];
    if (source.attacker != nullptr && source.attacker != &victim) {
        // Fighting is not hiding.
        source.attacker->hidingTimer = 0;
        victim.hidingTimer = 0;
    }
    if (hitSlot.ai != nullptr) {
        if (source.turretDamage >= 0) {
            hitSlot.ai->DamageFromHalfturret = static_cast<uint32_t>(source.turretDamage);
        }
        const bool weavelAlt = source.fromHalfturret && source.attacker != nullptr && source.attacker->hunter() == Hunter::Weavel
            && source.attacker->isAltForm();
        hitSlot.ai->OnTakeDamage(source.damage, source.beam >= 0, source.beam, weavelAlt, source.attacker);
    }
    const bool allies = source.attacker != nullptr && source.attacker != &victim
        && areAllies(source.attacker->teamIndex(), victim.teamIndex());
    if (headshot && source.attacker == main && &victim != main && !(allies && !teamRules().friendlyFire)) {
        m_hudEvents.push_back({HudEvent::Headshot});
    }
    if (source.attacker == main && &victim != main && (source.damage > 0 || died)) {
        m_hitMarkerTimer = 12; // "did that land": the X over the crosshair
    }
    if (!died) {
        return;
    }
    if (&victim == main) {
        HudEvent event;
        if (source.attacker == nullptr || source.attacker == main) {
            event = {HudEvent::SelfDestructed, -1, source.beam};
        } else {
            event = {headshot ? HudEvent::HeadshotKilledBy : HudEvent::KilledBy, static_cast<int>(source.attacker->hunter()), source.beam};
        }
        // TakeDamage's killedBy.
        if (flags & DamageFlags::Deathalt) {
            event.killedByTable = 'M';
            event.killedById = 250; // DEATHALT
        } else if (flags & DamageFlags::Burn) {
            event.killedByTable = 'M';
            event.killedById = 251; // MAGMAUL BURN
        } else if (source.fromHalfturret && source.attacker != nullptr) {
            event.killedByTable = 'A';
            event.killedById = static_cast<int>(source.attacker->hunter()) + 1;
        } else if (source.beam >= 0 && source.beam <= 8) {
            event.killedByTable = 'W';
            event.killedById = source.beam + 1;
        } else if (source.bomb == BombType::MorphBall) {
            event.killedByTable = 'M';
            event.killedById = 252; // MORPH BALL BOMB
        } else if (source.bomb == BombType::Stinglarva || source.bomb == BombType::Lockjaw) {
            event.killedByTable = 'A';
            event.killedById = (source.bomb == BombType::Stinglarva ? static_cast<int>(Hunter::Kanden) : static_cast<int>(Hunter::Sylux)) + 1;
        } else if (source.attacker != nullptr && source.beam < 0) {
            if (source.attacker->hunter() == Hunter::Weavel) {
                event.killedByTable = 'M';
                event.killedById = 253; // HALFTURRET SLICE
            } else {
                event.killedByTable = 'A';
                event.killedById = static_cast<int>(source.attacker->hunter()) + 1;
            }
        }
        m_hudEvents.push_back(event);
    } else if (source.attacker == main) {
        m_hudEvents.push_back({allies ? HudEvent::KilledTeammate
                : headshot            ? HudEvent::YourHeadshotKilled
                                      : HudEvent::YouKilled,
            static_cast<int>(victim.hunter())});
    }
    if (m_networked) {
        // The score is the server's, in every snapshot; only what a death shows is played here.
        if (source.attacker != nullptr && source.attacker != &victim) {
            const WeaponInfo* weapon = source.attacker->equip().weapon;
            const ItemType type = weapon != nullptr && weapon->ammoType == 1 ? ItemType::MissileSmall : ItemType::UASmall;
            dropItem(type, victim.volumeCenter() + Vec3{0, 0.35f, 0}, 300 * 2);
        }
        return;
    }
    PlayerSlot& victimSlot = m_slots[victim.slot()];
    victimSlot.deaths++;
    victimSlot.killStreak = 0;
    Player* attacker = source.attacker;
    const bool battle = m_mode == GameMode::Battle || m_mode == GameMode::BattleTeams;
    if (attacker == nullptr || attacker == &victim) {
        if (battle) {
            victimSlot.points--;
        }
    } else if (allies) {
        // A teammate: no kill, and the streak is over.
        m_slots[attacker->slot()].killStreak = 0;
    } else {
        PlayerSlot& attackerSlot = m_slots[attacker->slot()];
        attackerSlot.kills++;
        attackerSlot.killStreak = std::min(attackerSlot.killStreak + 1, 255);
        if (attackerSlot.killStreak == 5) {
            Sfx::instance().queueStream(VoiceId::VOICE_CONSECUTIVE_KILLS, 1);
            m_hudEvents.push_back({HudEvent::KillStreak, attacker == main ? -1 : static_cast<int>(attacker->hunter())});
        }
        if (m_mode == GameMode::PrimeHunter) {
            // The Prime Hunter heals by killing; anybody else becomes it by
            // killing it, or with the first kill of the match.
            if (attacker->primeHunter) {
                attacker->gainHealth(70);
            } else if (attacker->health() > 0 && (m_primeHunter < 0 || victim.primeHunter)) {
                setPrimeHunter(attacker->slot());
                if (attacker == main) {
                    Sfx::instance().queueStream(VoiceId::VOICE_PRIME, 1);
                }
                m_hudEvents.push_back({HudEvent::NewPrimeHunter, static_cast<int>(attacker->hunter())});
            }
        } else if (battle) {
            attackerSlot.points = std::min(attackerSlot.points + 1, 99999);
        }
    }
    if (attacker != nullptr && attacker != &victim) {
        // The victim drops ammo of the kind the killer's weapon uses, for ten seconds.
        const WeaponInfo* weapon = attacker->equip().weapon;
        const ItemType type = weapon != nullptr && weapon->ammoType == 1 ? ItemType::MissileSmall : ItemType::UASmall;
        dropItem(type, victim.volumeCenter() + Vec3{0, 0.35f, 0}, 300 * 2);
    }
    if (victim.primeHunter) {
        // Killed by nobody (the drain, a fall) or by somebody already dead.
        setPrimeHunter(-1);
        m_hudEvents.push_back({HudEvent::PrimeHunterDead});
    }
    updateStandings();
    if (qEnvironmentVariableIsSet("FP_DEBUG_WORLD")) {
        qInfo("tick %lld: slot %d killed by %d", m_ticks, victim.slot(), attacker ? attacker->slot() : -1);
    }
}

void World::processPlayers(const PlayerInput& input, bool movePlayer)
{
    for (size_t i = 0; i < m_slots.size(); i++) {
        PlayerSlot& slot = m_slots[i];
        Player& p = *slot.player;
        const bool main = i == 0;
        if ((main && !movePlayer) || (m_server && !slot.active)) {
            continue;
        }
        PlayerInput other = !main && m_otherInput ? m_otherInput(i, m_ticks) : PlayerInput{};
        if (slot.ai != nullptr && m_aiPersonality != nullptr) {
            other = slot.ai->ProcessInput();
        }
        const PlayerInput& own = main ? input : other;
        if (p.dead() && !m_networked) {
            // PlayerProcess: the main player respawns on FIRE or when the wait runs out; the others at once.
            // Survival: out of lives, out of the match (the respawn timer never runs out).
            if (p.respawnTimer() == 0 && !eliminated(i) && (!main || m_server || own.shootHeld || timeUntilRespawn(p) <= 0)) {
                respawn(slot);
                continue;
            }
        }
        if (m_authority.beforePlayer) {
            m_authority.beforePlayer(i);
        }
        p.tick(own);
        if (m_authority.afterPlayer) {
            m_authority.afterPlayer(i);
        }
        if (p.health() > 0) {
            updateLights(slot.lights, p.volumeCenter());
        }
        // Falling out of the room.
        const RoomMetadata* room = m_scene.room;
        if (room != nullptr && p.health() > 0) {
            bool fell = p.position()[1] < room->killHeight;
            if (room->hasLimits && p.position()[1] < room->playerMin[1]) {
                fell = true;
            }
            if (fell) {
                p.takeDamage(0, DamageFlags::Death, nullptr, {});
            }
        }
    }
    if (survivalMode()) {
        for (PlayerSlot& slot : m_slots) {
            processHiding(*slot.player);
        }
        for (size_t i = 1; i < m_slots.size(); i++) {
            const Player& other = *m_slots[i].player;
            if (other.health() > 0 && other.radarReveal && !other.radarRevealPrevious) {
                Sfx::instance().queueStream(VoiceId::VOICE_CAMPING, 1); // COWARD DETECTED!
            }
        }
    }
    // HalfturretEntity.Process, after the players.
    const float killHeight = m_scene.room != nullptr ? m_scene.room->killHeight : -1e9f;
    for (size_t i = 0; i < m_slots.size(); i++) {
        if (!(i == 0 && !movePlayer)) {
            PlayerSlot& slot = m_slots[i];
            slot.player->processHalfturret(killHeight);
            // HalfturretEntity: the owner's lights when it comes out, then its own.
            const Halfturret& turret = slot.player->halfturret();
            if (turret.active && !slot.turretLit) {
                slot.turretLights = slot.lights;
            }
            slot.turretLit = turret.active;
            if (turret.active) {
                updateLights(slot.turretLights, turret.position);
            }
        }
    }
    for (int& cooldown : m_spawnCooldowns) {
        if (cooldown > 0) {
            cooldown--;
        }
    }
}

void World::processDoors()
{
    // DoorEntity.Process, the parts a multiplayer-style door uses (no locks, no connectors).
    static const bool debug = qEnvironmentVariableIsSet("FP_DEBUG_WORLD");
    for (Door& door : m_doors) {
        AnimationState& anim = m_scene.instances[door.instance].animation;
        if (debug && m_ticks % 30 == 0) {
            qInfo("door at (%.1f %.1f %.1f): shot %d should %d open %d closed %d anim %d frame %d/%d flags %x", door.position[0],
                door.position[1], door.position[2], door.shotOpen, door.shouldOpen, door.open, door.closed, anim.index, anim.frame,
                anim.frameCount, anim.flags);
        }
        bool near = false;
        for (const PlayerSlot& slot : m_slots) {
            const Vec3 between = door.position - slot.player->position();
            near = near || (slot.player->health() > 0 && dot(between, between) < 16);
        }
        door.shouldOpen = !door.locked && door.shotOpen && near;
        if (!door.shouldOpen && door.opening) {
            door.shotOpen = false;
        }
        door.sound->update(door.position, 6);
        if (door.shouldOpen) {
            door.closed = false;
            if ((anim.flags & AnimFlags::Ended) && (anim.flags & AnimFlags::Reverse)) {
                anim.flags &= static_cast<uint16_t>(~(AnimFlags::Ended | AnimFlags::Paused | AnimFlags::Reverse));
                door.sound->playSfx(door.type == 2 ? SfxId::DOOR2_OPEN : SfxId::DOOR_OPEN);
            }
        } else if (anim.flags & AnimFlags::Ended) {
            if (!(anim.flags & AnimFlags::Reverse)) {
                door.closed = true;
                anim.flags &= static_cast<uint16_t>(~(AnimFlags::Ended | AnimFlags::Paused));
                anim.flags |= AnimFlags::Reverse;
                door.sound->playSfx(door.type == 2 ? SfxId::DOOR2_CLOSE_SCR : door.type == 3 ? SfxId::DOOR3_CLOSE_SCR : SfxId::DOOR_CLOSE);
            }
        }
        if (door.shouldOpen) {
            if (anim.frame > anim.frameCount / 2) {
                door.open = true;
            }
        } else {
            door.open = false;
        }
        door.opening = door.shouldOpen;
    }
}

void World::processJumpPads()
{
    // JumpPadEntity.Process
    for (JumpPad& pad : m_jumpPads) {
        if (pad.cooldown > 0) {
            pad.cooldown--;
        }
        if (!pad.active || pad.cooldown != 0 || !(pad.triggerFlags & TriggerPlayerBiped)) {
            continue;
        }
        for (PlayerSlot& slot : m_slots) {
            Player& p = *slot.player;
            if (p.health() > 0 && pad.cooldown == 0 && pad.volume.testPoint(p.position())) {
                p.activateJumpPad(pad.beamVector, pad.controlLockTime);
                pad.cooldown = static_cast<uint16_t>(pad.cooldownTime * 2);
            }
        }
    }
}

ModelInstance::Lights World::roomLights() const
{
    ModelInstance::Lights lights{};
    if (const RoomMetadata* room = m_scene.room) {
        lights = {room->light1Vector, room->light1Color.toFloat(), room->light2Vector, room->light2Color.toFloat()};
    }
    return lights;
}

void World::updateLights(ModelInstance::Lights& lights, const Vec3& position) const
{
    constexpr float colorStep = 8 / 255.0f;
    constexpr float frames = 0.5f; // FrameTime * 30
    auto channel = [](float current, float source) {
        const float diff = source - current;
        if (std::abs(diff) < colorStep) {
            return source;
        }
        if (current > source) {
            const int factor = static_cast<int>(std::trunc((diff + colorStep) / (8 * colorStep)));
            return factor <= -1 ? current + (factor - 1) * colorStep * frames : current - colorStep * frames;
        }
        const int factor = static_cast<int>(std::trunc(diff / (8 * colorStep)));
        return factor >= 1 ? current + factor * colorStep * frames : current + colorStep * frames;
    };
    auto toward = [&](Vec3& vector, Vec3& color, const Vec3& targetVector, const Vec3& targetColor) {
        for (int i = 0; i < 3; i++) {
            vector[i] += (targetVector[i] - vector[i]) / 8 * frames;
            color[i] = channel(color[i], targetColor[i]);
        }
    };
    bool has1 = false, has2 = false;
    for (const LightSource& source : m_lightSources) {
        if (!source.volume.testPoint(position)) {
            continue;
        }
        if (source.light1Enabled) {
            has1 = true;
            toward(lights.light1Vector, lights.light1Color, source.light1Vector, source.light1Color);
        }
        if (source.light2Enabled) {
            has2 = true;
            toward(lights.light2Vector, lights.light2Color, source.light2Vector, source.light2Color);
        }
    }
    const ModelInstance::Lights room = roomLights();
    if (!has1) {
        toward(lights.light1Vector, lights.light1Color, room.light1Vector, room.light1Color);
    }
    if (!has2) {
        toward(lights.light2Vector, lights.light2Color, room.light2Vector, room.light2Color);
    }
    lights.light1Vector = normalized(lights.light1Vector);
    lights.light2Vector = normalized(lights.light2Vector);
}

void World::processMorphCameras()
{
    // MorphCameraEntity.Process
    for (const MorphCamera& camera : m_morphCameras) {
        for (PlayerSlot& slot : m_slots) {
            Player& p = *slot.player;
            if (p.isAltForm()) {
                const bool inside = camera.volume.overlapsSphere(p.volumeCenter(), p.volumeRadius());
                if (p.morphCamera() < 0 && inside) {
                    p.setMorphCamera(camera.id, camera.position);
                } else if (p.morphCamera() == camera.id && !inside) {
                    p.clearMorphCamera(true);
                }
            } else if (p.morphCamera() >= 0) {
                p.clearMorphCamera(false);
            }
        }
    }
}

void World::processTeleporters()
{
    // TeleporterEntity.Process
    for (Teleporter& t : m_teleporters) {
        ModelInstance* inst = t.model ? &m_scene.instances[*t.model] : nullptr;
        // ActivateAnimaton: open (animation 2 forwards) when somebody comes near.
        auto open = [&] {
            if (!t.opening && inst != nullptr) {
                t.opening = true;
                t.closing = false;
                AnimationState& anim = inst->animation;
                if (anim.index == 2) {
                    if (m_ticks > 1 && (anim.flags & AnimFlags::Reverse) && anim.frame < anim.frameCount / 2 && m_ticks % 2 == 0) {
                        t.sound->update(t.position, 23);
                        t.sound->playSfx(SfxId::TELEPORT_ACTIVATE);
                    }
                    anim.flags |= AnimFlags::NoLoop;
                    anim.flags &= ~(AnimFlags::Ended | AnimFlags::Reverse);
                }
            }
        };
        if (inst != nullptr) {
            AnimationState& anim = inst->animation;
            if (anim.index == 2 && !(anim.flags & AnimFlags::Reverse) && (anim.flags & AnimFlags::Ended)) {
                anim.set(inst->model->animations(), 0); // open: the idle loop
            }
            if (t.closing && (anim.index != 0 || anim.frame == anim.frameCount - 1)) {
                // InitiateAnimaton: close at the end of the idle loop (animation 2 backwards).
                t.opening = t.closing = false;
                if (anim.index == 2) {
                    anim.flags |= AnimFlags::NoLoop | AnimFlags::Reverse;
                    anim.flags &= ~AnimFlags::Ended;
                } else if (anim.index == 0) {
                    anim.set(inst->model->animations(), 2, AnimFlags::NoLoop | AnimFlags::Reverse);
                }
            }
        }
        if (!t.active) {
            continue;
        }
        t.sound->update(t.position, 23);
        t.sound->playSfx(SfxId::TELEPORTER_LOOP, true);
        bool activated = false;
        const Vec3 testPos = t.position + Vec3{0, 1, 0};
        for (PlayerSlot& slot : m_slots) {
            Player& p = *slot.player;
            const size_t index = static_cast<size_t>(p.slot()) & 15;
            if (p.health() == 0) {
                continue;
            }
            const Vec3 between = p.volumeCenter() - t.position;
            if (!(between[1] < 1.5f && between[1] > -1.5f && between[0] * between[0] + between[2] * between[2] < 49)) {
                t.triggered[index] = false;
                continue;
            }
            activated = true;
            open();
            CollisionResult discard;
            if (!checkCylinderOverlapSphere(p.prevPosition(), p.volumeCenter(), testPos, 1.75f, discard)) {
                t.triggered[index] = false;
                continue;
            }
            if (t.triggered[index] || !checkCylinderOverlapSphere(p.prevPosition(), p.volumeCenter(), testPos, t.big ? 1.5f : 1, discard)) {
                continue;
            }
            p.teleport(t.targetPosition + Vec3{0, 0.5f, 0}, t.facing);
            if (slot.ai != nullptr) {
                slot.ai->Field118 = 148 * 2;
            }
            t.triggered[index] = true;
            if (qEnvironmentVariableIsSet("FP_DEBUG_WORLD")) {
                qInfo("tick %lld: slot %d teleported by %d to (%.2f %.2f %.2f)", m_ticks, p.slot(), t.id, t.targetPosition[0],
                    t.targetPosition[1], t.targetPosition[2]);
            }
        }
        if (!activated && t.opening) {
            t.closing = true;
        }
    }
}

void World::dropItem(ItemType type, const Vec3& position, int despawnTime)
{
    // A free one of the type, else the one closest to despawning.
    ItemInstance* chosen = nullptr;
    for (ItemInstance& item : m_droppedItems) {
        if (item.type == type && (chosen == nullptr || item.despawnTimer < chosen->despawnTimer)) {
            chosen = &item;
        }
    }
    if (chosen == nullptr) {
        return;
    }
    chosen->position = position;
    chosen->despawnTimer = despawnTime;
    if (qEnvironmentVariableIsSet("FP_DEBUG_WORLD")) {
        qInfo("tick %lld: item %d dropped at (%.2f %.2f %.2f)", m_ticks, static_cast<int>(type), position[0], position[1], position[2]);
    }
    ModelInstance& inst = m_scene.instances[chosen->modelInstance];
    inst.transform = Mat4::translation(position[0], position[1], position[2]);
    inst.spinStartDegrees = nextItemRotation() - static_cast<float>(m_scene.seconds) * inst.spinDegreesPerSecond;
    inst.visible = true;
}

void World::processItems()
{
    // ItemSpawnEntity.Process, ItemInstanceEntity.Process and PlayerEntity.PickUpItems.
    // Online the health pickups are the server's (NetHealthSync): NetGame shows them.
    auto serverOwned = [&](const ItemSpawn& spawn) { return m_networked && static_cast<int>(spawn.type) <= 2; };
    auto pickUp = [&](ItemInstance& item) {
        if (item.owner != nullptr && serverOwned(*item.owner)) {
            return;
        }
        for (PlayerSlot& slot : m_slots) {
            if (slot.player->health() > 0 && slot.player->inPickupRange(item.position) && slot.player->pickUp(item.type)) {
                if (qEnvironmentVariableIsSet("FP_DEBUG_WORLD")) {
                    qInfo("tick %lld: slot %d picks up item %d", m_ticks, slot.player->slot(), static_cast<int>(item.type));
                }
                item.despawnTimer = 0; // OnPickedUp
                if (item.owner != nullptr && static_cast<int>(item.owner->type) <= 2) {
                    item.owner->lastPicker = slot.player->slot();
                }
                break;
            }
        }
    };
    // ItemInstanceEntity._sfxIds: the hum of the health and ammo pickups.
    static constexpr int itemSfx[22] = {33, 33, 33, -1, 34, -1, 34, -1, -1, -1, -1, -1, -1, 33, 33, 33, 33, -1, -1, -1, -1, -1};
    auto hum = [&](ItemInstance& item) {
        item.sound->update(item.position, 7);
        const int type = static_cast<int>(item.type);
        if (item.active() && type >= 0 && type < 22 && itemSfx[type] != -1) {
            item.sound->playSfx(itemSfx[type], true);
        }
    };
    for (ItemInstance& item : m_droppedItems) {
        if (item.active()) {
            pickUp(item);
            if (item.despawnTimer > 0) {
                item.despawnTimer--;
            }
            if (!item.active()) {
                m_scene.instances[item.modelInstance].visible = false;
                item.sound->stopAllSfx(true);
            } else {
                hum(item);
            }
        }
    }
    for (ItemSpawn& spawn : m_items) {
        if (spawn.item != nullptr) {
            pickUp(*spawn.item);
            if (!spawn.item->active()) {
                m_scene.instances[spawn.item->modelInstance].visible = false;
                spawn.item->sound->stopAllSfx(true);
                spawn.item = nullptr;
            } else {
                hum(*spawn.item);
            }
        }
        if (!spawn.active || serverOwned(spawn)) {
            continue;
        }
        if (spawn.item == nullptr && spawn.cooldown > 0) {
            spawn.cooldown--;
        }
        if (spawn.item == nullptr && spawn.cooldown == 0 && (spawn.maxSpawnCount == 0 || spawn.spawnCount < spawn.maxSpawnCount)) {
            spawn.instance.despawnTimer = -1;
            spawn.instance.owner = &spawn;
            spawn.item = &spawn.instance;
            spawn.lastPicker = -1;
            m_scene.instances[spawn.instance.modelInstance].visible = true;
            spawn.instance.sound->update(spawn.instance.position, 7);
            spawn.instance.sound->playSfx(SfxId::ITEM_SPAWN1);
            spawn.cooldown = static_cast<uint16_t>(spawn.spawnInterval * 2);
            spawn.spawnCount++;
        }
    }
}

void World::updateTurretModel(PlayerSlot& slot)
{
    // HalfturretEntity.GetDrawInfo: the legs face where they were left, the
    // gun on top (TurretBase) turns to its aim.
    Player& p = *slot.player;
    ModelInstance& inst = m_scene.instances[*slot.turret];
    const Halfturret& t = p.halfturret();
    inst.visible = t.active && p.health() > 0;
    if (t.animation >= 0) {
        inst.animation.set(inst.model->animations(), t.animation, AnimFlags::NoLoop);
        p.takeHalfturretAnimation();
    } else if (m_ticks % 2 == 0) {
        inst.animation.advance(); // owned: the World steps it here
    }
    if (!inst.visible) {
        return;
    }
    const Model& model = *inst.model;
    const auto& nodes = model.nodes();
    int baseNode = -1;
    for (size_t i = 0; i < nodes.size(); i++) {
        if (nodes[i].name == "TurretBase") {
            baseNode = static_cast<int>(i);
        }
    }
    if (baseNode < 0 || nodes[baseNode].parentIndex < 0) {
        return;
    }
    const int baseParent = nodes[baseNode].parentIndex;
    const AnimationSet& anims = model.animations();
    const int index = inst.animation.index;
    const NodeAnimationGroup* group
        = index >= 0 && index < static_cast<int>(anims.node.size()) && !anims.node[index].empty() ? &anims.node[index] : nullptr;
    std::vector<Mat4> animation(nodes.size(), Mat4::identity());
    std::vector<std::optional<Mat4>> before(nodes.size());
    // Model.AnimateNodes2, without node transforms, with the TurretBase switches.
    auto animate = [&](auto&& self, int start, const Mat4& parentTransform) -> void {
        for (int i = start; i >= 0 && i < static_cast<int>(nodes.size());) {
            const Node& node = nodes[i];
            Mat4 transform = Mat4::identity();
            if (group != nullptr) {
                if (auto it = group->animations.find(node.name); it != group->animations.end()) {
                    transform = animateNode(*group, it->second, 1.0f, inst.animation.frame);
                    if (node.parentIndex >= 0 && i != baseNode) {
                        transform *= animation[node.parentIndex];
                    }
                }
            }
            animation[i] = transform;
            if (before[i]) {
                animation[i] = animation[i] * parentTransform * *before[i];
            }
            if (node.childIndex >= 0 && i != baseParent) {
                self(self, node.childIndex, parentTransform);
            }
            animation[i] *= parentTransform;
            i = node.nextIndex;
        }
    };
    animate(animate, 0, Mat4::fromVectors(t.facing, {0, 1, 0}, {0, 0, 0}));
    before[baseNode] = Mat4::fromVectors(t.aimVector, {0, 1, 0}, animation[baseParent].translationPart());
    const auto skipParent = [&](auto&& self, int start) -> void {
        // AnimIgnoreChild on the parent is off again: its children now, TurretBase ignoring it.
        animate(self, start, Mat4::identity());
    };
    skipParent(animate, nodes[baseParent].childIndex);
    const Mat4 root = Mat4::translation(t.position[0], t.position[1] - 0.45f, t.position[2]);
    for (Mat4& m : animation) {
        m *= root;
    }
    inst.nodeOverrides = std::move(animation);
}

void World::updatePlayerModels(PlayerSlot& slot, bool main)
{
    const Player& p = *slot.player;
    // The main player's own models only while walking; everybody else's while alive.
    const bool show = (main ? m_showPlayer : true) && p.health() > 0 && !p.spectating();
    for (const std::optional<size_t>& model : {slot.alt, slot.biped, slot.gun}) {
        if (model) {
            m_scene.instances[*model].lights = slot.lights;
        }
    }
    if (slot.turret) {
        m_scene.instances[*slot.turret].lights = slot.turretLights;
    }
    if (slot.alt) {
        ModelInstance& alt = m_scene.instances[*slot.alt];
        alt.visible = show && p.isAltForm();
        alt.animation = p.altAnimation();
        if (p.hunter() == Hunter::Kanden) {
            alt.nodeOverrides.assign(p.kandenSegments().begin(), p.kandenSegments().end());
        } else {
            alt.transform = p.altModelTransform();
        }
    }
    if (slot.biped) {
        ModelInstance& biped = m_scene.instances[*slot.biped];
        biped.visible = show && (!main ? !p.isAltForm() : p.drawBiped());
        biped.transform = p.bipedTransform();
        if (p.bipedAnimation().index >= 0) {
            biped.animation = p.bipedAnimation();
        }
        animateBiped(slot);
        biped.alpha = 1.0f;
        if (main && p.isUnmorphing() && biped.animation.frameCount > 0) {
            // PlayerDraw fades the biped out as it unmorphs back into first person.
            biped.alpha = std::clamp(1.0f - static_cast<float>(biped.animation.frame) / biped.animation.frameCount, 0.0f, 1.0f);
        }
    }
    if (slot.turret) {
        updateTurretModel(slot);
    }
    if (slot.gun) {
        ModelInstance& gun = m_scene.instances[*slot.gun];
        // PlayerDraw: the first-person gun is the main player's; Guardians have none.
        gun.visible = main && show && !p.isAltForm() && !p.drawBiped() && p.hunter() != Hunter::Guardian;
        gun.transform = p.gunTransform();
        gun.animation = p.gunAnimation();
        gun.secondary = p.gunMaterialAnimation();
    }
}

void World::animateBiped(PlayerSlot& slot)
{
    // PlayerDraw: the legs up to the spine play the first animation slot, the
    // spine leans to the aim, and everything above it plays the second slot.
    const Player& p = *slot.player;
    ModelInstance& biped = m_scene.instances[*slot.biped];
    const Model& model = *biped.model;
    const auto& nodes = model.nodes();
    const AnimationSet& anims = model.animations();
    auto group = [&](const AnimationState& state) -> const NodeAnimationGroup* {
        return state.index >= 0 && state.index < static_cast<int>(anims.node.size()) && !anims.node[state.index].empty()
            ? &anims.node[state.index]
            : nullptr;
    };
    const AnimationState& anim1 = p.bipedAnimation();
    const AnimationState& anim2 = p.bipedAnimation2();
    const NodeAnimationGroup* group1 = group(anim1);
    const NodeAnimationGroup* group2 = slot.spineNode >= 0 ? group(anim2) : group1;
    std::vector<Mat4>& out = slot.bipedNodes;
    out.assign(nodes.size(), Mat4::identity());
    // Model.AnimateNodes without node transforms, at scale 1.
    auto animate = [&](auto&& self, int start, const NodeAnimationGroup* g, int frame) -> void {
        for (int i = start; i >= 0 && i < static_cast<int>(nodes.size());) {
            const Node& node = nodes[i];
            Mat4 transform = Mat4::identity();
            if (g != nullptr) {
                if (auto it = g->animations.find(node.name); it != g->animations.end()) {
                    transform = animateNode(*g, it->second, 1.0f, frame);
                    if (node.parentIndex >= 0) {
                        transform *= out[node.parentIndex];
                    }
                }
            }
            out[i] = transform;
            if (i == slot.spineNode) {
                out[i] = Mat4::rotationZ(p.spineAngle()) * out[i];
            } else if (node.childIndex >= 0) {
                self(self, node.childIndex, g, frame);
            }
            i = node.nextIndex;
        }
    };
    animate(animate, 0, group1, anim1.frame);
    if (slot.spineNode >= 0 && nodes[slot.spineNode].childIndex >= 0) {
        animate(animate, nodes[slot.spineNode].childIndex, group2, anim2.frame);
    }
    const Mat4 transform = p.bipedTransform();
    for (Mat4& m : out) {
        m *= transform;
    }
    biped.nodeOverrides = out;
}

void World::tick(const PlayerInput& input, bool movePlayer)
{
    // Renderer.OnUpdateFrame: GameState.ProcessFrame, the scene only while the
    // match is on, then GameState.UpdateTime. Once it is over everything stays
    // where it was, beams and particles included.
    m_globalTicks++;
    if (m_hitMarkerTimer > 0) {
        m_hitMarkerTimer--;
    }
    if (m_introActive) {
        m_intro->process(1 / 60.0f);
    }
    processMatchState();
    m_slots[0].player->updateTimedSounds();
    if (m_matchState == MatchState::InProgress) {
        simulate(input, movePlayer);
    } else if (m_showPlayer && matchEndCamera()) {
        // The main player's camera leaves its eyes to watch the winner
        // (Third2): its biped shows and its gun goes.
        PlayerSlot& main = m_slots[0];
        const Player& p = *main.player;
        if (main.biped) {
            m_scene.instances[*main.biped].visible = p.health() > 0 && !p.isAltForm();
            m_scene.instances[*main.biped].alpha = 1.0f;
        }
        if (main.gun) {
            m_scene.instances[*main.gun].visible = false;
        }
    }
    if (m_matchTime > 0) {
        m_matchTime = std::max(m_matchTime - 1 / 60.0f, 0.0f);
    }
}

void World::setMode(GameMode mode)
{
    // GameState.Setup
    m_mode = mode;
    m_timeGoal = 0;
    switch (mode) {
    case GameMode::Survival:
        setRules(2, 15 * 60); // spare lives
        break;
    case GameMode::PrimeHunter:
        m_timeGoal = 1.5f * 60;
        setRules(0, 15 * 60);
        break;
    case GameMode::Bounty:
        setRules(3, 15 * 60);
        break;
    case GameMode::Capture:
        setRules(5, 15 * 60);
        break;
    case GameMode::Defender:
        m_timeGoal = 1.5f * 60;
        setRules(0, 15 * 60);
        break;
    case GameMode::Nodes:
        setRules(70, 15 * 60);
        break;
    case GameMode::BattleTeams:
        setRules(7, 7 * 60);
        break;
    case GameMode::SurvivalTeams:
        setRules(2, 15 * 60);
        break;
    case GameMode::BountyTeams:
        setRules(3, 15 * 60);
        break;
    case GameMode::NodesTeams:
        setRules(70, 15 * 60);
        break;
    case GameMode::DefenderTeams:
        m_timeGoal = 1.5f * 60;
        setRules(0, 15 * 60);
        break;
    default:
        m_mode = GameMode::Battle;
        setRules(7, 7 * 60);
        break;
    }
    // GameState.IsTeamMode: Capture is a team mode too.
    TeamRules& rules = teamRules();
    rules.teams = m_mode == GameMode::BattleTeams || m_mode == GameMode::SurvivalTeams || m_mode == GameMode::Capture
        || m_mode == GameMode::BountyTeams || m_mode == GameMode::NodesTeams || m_mode == GameMode::DefenderTeams;
    if (m_mode == GameMode::Capture) {
        rules.teamCount = 2; // one Octolith a team
    }
    if (!m_networked) {
        assignTeams();
    }
    setUpModeEntities();
}

void World::setTeamCount(int count)
{
    teamRules().teamCount = m_mode == GameMode::Capture ? 2 : std::clamp(count, 2, 4);
    assignTeams();
}

void World::assignTeams()
{
    // TeamRules.ChooseTeam with even teams, one player after another: the
    // team with the fewest players, the first on a tie. Without teams every
    // player is its own team.
    std::array<int, 4> counts{};
    for (size_t i = 0; i < m_slots.size(); i++) {
        int team = static_cast<int>(i);
        if (teams()) {
            team = 0;
            for (int t = 1; t < teamCount(); t++) {
                if (counts[t] < counts[team]) {
                    team = t;
                }
            }
            counts[team]++;
        }
        m_slots[i].player->setTeamIndex(team);
        applyTeamVisuals(m_slots[i]);
    }
}

void World::applyTeamVisuals(PlayerSlot& slot)
{
    // TeamVisuals.Apply: teams A and B wear the orange and green suits
    // (recolors 4 and 5) and glow in their color; C and D keep a hunter's
    // own suit. PlayerDraw.GetEmission for the glow.
    int recolor = slot.recolor;
    uint16_t emission = 0;
    if (teams()) {
        const int team = slot.player->teamIndex();
        if (team == 0) {
            recolor = 4;
            emission = 0x14F0; // Metadata.EmissionOrange
        } else if (team == 1) {
            recolor = 5;
            emission = 0x1565; // Metadata.EmissionGreen
        } else if (recolor >= 4) {
            recolor = 0;
        }
    }
    for (const std::optional<size_t>& index : {slot.biped, slot.alt, slot.gun, slot.turret}) {
        if (index) {
            ModelInstance& inst = m_scene.instances[*index];
            inst.recolor = recolor < static_cast<int>(inst.model->recolorCount()) ? recolor : 0;
            inst.emission = emission;
        }
    }
}

bool World::eliminated(size_t slot) const
{
    // Out of the team's lives. (The C# holds the respawn by TeamDeaths[SlotIndex],
    // which with teams is some other team's count; the port uses the player's team.)
    const PlayerSlot& s = m_slots.at(slot);
    return survivalMode() && s.player->health() == 0 && teamDeaths(s.player->teamIndex()) > m_pointGoal;
}

int World::timeUntilRespawn(const Player& player) const
{
    // Longer with more players in a battle; none in Survival once the
    // respawn timer has run out.
    if (survivalMode()) {
        return 0;
    }
    const int count = static_cast<int>(m_slots.size());
    const int wait = count > 3 ? 900 * 2 : count > 2 ? 600 * 2 : 300 * 2;
    return wait - player.timeSinceDead();
}

void World::processHiding(Player& p)
{
    if (p.radarReveal) {
        p.radarRevealPrevious = true;
    }
    p.radarReveal = false;
    if (p.health() == 0 || m_radarPlayers) {
        p.hidingTimer = 0;
        return;
    }
    const int revealTime = (m_slots.size() > 2 ? 600 : 300) * 2;
    const Vec3 moved = p.position() - p.idlePosition();
    if (dot(moved, moved) >= 25) {
        p.hidingTimer = p.hidingTimer > 35 * 2 ? p.hidingTimer - 35 * 2 : 0;
        if (p.hidingTimer < revealTime && p.hidingTimer > revealTime - 35 * 2) {
            // at least a second before being revealed again
            p.hidingTimer = revealTime - 35 * 2;
        }
    }
    if (p.hidingTimer < revealTime + 150 * 2) {
        p.hidingTimer++;
    }
    if (p.hidingTimer >= revealTime) {
        p.radarReveal = true;
    }
}

void World::updateSurvival()
{
    // Every player still in counts time; with fewer than two left the match
    // ends and the survivors' time is "MAX".
    m_radarPlayers = false;
    int alive = 0;
    std::array<bool, 16> teamsAlive{};
    int aliveTeams = 0;
    for (size_t i = 0; i < m_slots.size(); i++) {
        if (!eliminated(i)) {
            m_slots[i].time += 1 / 60.0f;
            alive++;
            const int team = m_slots[i].player->teamIndex();
            if (teams() && team >= 0 && team < teamCount() && !teamsAlive[team]) {
                teamsAlive[team] = true;
                aliveTeams++;
            }
        }
    }
    if (alive < 2 || (teams() && aliveTeams < 2)) {
        m_matchTime = 0;
        for (size_t i = 0; i < m_slots.size(); i++) {
            if (!eliminated(i)) {
                m_slots[i].time = -1;
            }
        }
    } else if (alive == 2 && m_slots.size() > 2) {
        m_radarPlayers = true;
    }
}

void World::setPrimeHunter(int slot)
{
    if (slot != m_primeHunter && qEnvironmentVariableIsSet("FP_DEBUG_WORLD")) {
        qInfo("tick %lld: prime hunter %d -> %d", m_ticks, m_primeHunter, slot);
    }
    m_primeHunter = slot;
    for (size_t i = 0; i < m_slots.size(); i++) {
        m_slots[i].player->primeHunter = static_cast<int>(i) == slot;
    }
}

void World::updatePrimeHunter()
{
    if (m_primeHunter < 0) {
        return;
    }
    PlayerSlot& slot = m_slots[m_primeHunter];
    if (m_ticks % (10 * 2) == 0) {
        slot.player->takeDamage(1, DamageFlags::NoDmgInvuln, nullptr, {});
    }
    // The drain may have killed it.
    if (m_primeHunter >= 0) {
        slot.time += 1 / 60.0f;
        if (m_timeGoal > 0 && slot.time >= m_timeGoal) {
            m_matchTime = 0;
        }
    }
}

void World::setRules(int pointGoal, float seconds)
{
    m_pointGoal = std::max(pointGoal, 0);
    m_matchTime = m_matchLength = seconds > 0 ? seconds : -1;
}

void World::processMatchState()
{
    if (m_matchState == MatchState::InProgress) {
        if (m_networked) {
            // The server decides when the match ends (NetGame).
        } else if (survivalMode()) {
            updateSurvival();
        } else if (m_mode == GameMode::PrimeHunter) {
            updatePrimeHunter();
        } else if (defenderMode()) {
            // ModeStateDefender: a team's ring time reaches the goal.
            for (PlayerSlot& slot : m_slots) {
                const int team = slot.player->teamIndex();
                if (m_timeGoal > 0 && team >= 0 && team < 16 && m_teamTime[team] >= m_timeGoal) {
                    m_matchTime = 0;
                    break;
                }
            }
        } else if (m_pointGoal > 0) {
            // ModeStateBattle, Capture, Bounty, Nodes: EndIfPointGoalReached,
            // by team (each player its own team without teams).
            updateStandings(); // the team totals
            for (PlayerSlot& slot : m_slots) {
                const int team = slot.player->teamIndex();
                if (team >= 0 && team < 16 && m_teamPoints[team] >= m_pointGoal) {
                    m_teamPoints[team] = m_pointGoal; // several node points in the same tick
                    if (!teams()) {
                        slot.points = m_pointGoal;
                    }
                    m_matchTime = 0;
                    break;
                }
            }
        }
        if (m_matchTime > 0 && m_matchTime < 60 && !m_tempoChanged) {
            Music::instance().updateTempo(307, 900 / 30.0f); // the last minute, faster
            m_tempoChanged = true;
        }
        if (m_matchTime > 0 && m_matchTime < 10) {
            // The last ten seconds' alarm, quickening over the last five.
            static constexpr float intervals[4] = {1 / 30.0f, 8 / 30.0f, 15 / 30.0f, 6 / 30.0f};
            const float now = m_globalTicks / 60.0f;
            const float comparison = m_matchTime < 6 ? intervals[m_nextAlarmIndex] : 1;
            if (m_lastAlarmTime == 0 || now - m_lastAlarmTime >= comparison) {
                Sfx::instance().playFreeSfx(SfxId::ALARM);
                m_lastAlarmTime = now;
                m_nextAlarmIndex = (m_nextAlarmIndex + 1) % 4;
            }
        }
        if (m_matchTime == 0) {
            // Time's up or the goal is reached: "GAME OVER" for three seconds.
            if (survivalMode()) {
                // Out of time, everybody still in has survived the whole match.
                for (size_t i = 0; i < m_slots.size(); i++) {
                    if (!eliminated(i)) {
                        m_slots[i].time = -1;
                        const int team = m_slots[i].player->teamIndex();
                        if (team >= 0 && team < 16) {
                            m_teamTime[team] = -1;
                        }
                    }
                }
            }
            m_matchState = MatchState::GameOver;
            m_matchTime = 90 / 30.0f;
            m_matchEndTick = m_globalTicks;
            // Everything stops; the sounds of the world stay silent until the next match (StopLongSfx).
            Sfx& sfx = Sfx::instance();
            sfx.stopFreeSfxScripts();
            sfx.stopAllSound(true);
            m_slots[0].player->stopAllSfx();
            sfx.sfxMute = true;
            sfx.timedSfxMute++;
            sfx.longSfxMute++;
            sfx.stopEnvironmentSfx();
            Music::instance().playSeq(SeqId::TIMEOUT);
            updateStandings();
            if (qEnvironmentVariableIsSet("FP_DEBUG_WORLD")) {
                qInfo("tick %lld: match over, winner slot %d%s", m_ticks, m_resultSlots.empty() ? -1 : m_resultSlots[0],
                    resultTie() ? " (tie)" : "");
            }
        }
    } else if (m_matchState == MatchState::GameOver) {
        if (!matchEndCamera()) {
            ensureIntro(); // a tie or a dead winner: the room's intro instead
        }
        if (m_matchTime == 0) {
            // Ten seconds of results, as the C# gives them (the DS gave five).
            m_matchState = MatchState::Ending;
            m_matchTime = 10;
        }
    } else if (m_matchState == MatchState::Ending) {
        ensureIntro();
    }
}

int World::comparePlayers(size_t slot1, size_t slot2) const
{
    // GameState.ComparePlayers: a battle by points then fewer deaths,
    // Survival by time survived ("MAX" beats any) then fewer deaths, Prime
    // Hunter by prime time then more kills.
    const PlayerSlot& a = m_slots[slot1];
    const PlayerSlot& b = m_slots[slot2];
    if (survivalMode()) {
        const float time1 = a.time == -1 ? std::numeric_limits<float>::max() : a.time;
        const float time2 = b.time == -1 ? std::numeric_limits<float>::max() : b.time;
        if (time1 == time2 && a.deaths == b.deaths) {
            return 0;
        }
        if (time1 < time2 || (time1 == time2 && a.deaths > b.deaths)) {
            return -1;
        }
        return 1;
    }
    if (m_mode == GameMode::PrimeHunter || defenderMode()) {
        // By prime time or ring time, then more kills.
        if (a.time == b.time && a.kills == b.kills) {
            return 0;
        }
        if (a.time < b.time || (a.time == b.time && a.kills < b.kills)) {
            return -1;
        }
        return 1;
    }
    if (octolithMode() || nodesMode()) {
        // By points, then more kills.
        if (a.points == b.points && a.kills == b.kills) {
            return 0;
        }
        if (a.points < b.points || (a.points == b.points && a.kills < b.kills)) {
            return -1;
        }
        return 1;
    }
    if (a.points == b.points && a.deaths == b.deaths) {
        return 0;
    }
    if (a.points < b.points || (a.points == b.points && a.deaths > b.deaths)) {
        return -1;
    }
    return 1;
}

int World::compareTeams(int team1, int team2) const
{
    // GameState.CompareTeams: as ComparePlayers, with the teams' totals.
    const int points1 = teamPoints(team1), points2 = teamPoints(team2);
    float time1 = teamTime(team1), time2 = teamTime(team2);
    if (survivalMode()) {
        time1 = time1 == -1 ? std::numeric_limits<float>::max() : time1;
        time2 = time2 == -1 ? std::numeric_limits<float>::max() : time2;
    }
    const int deaths1 = teamDeaths(team1), deaths2 = teamDeaths(team2);
    const int kills1 = teamKills(team1), kills2 = teamKills(team2);
    auto order = [](bool equal, bool less) { return equal ? 0 : less ? -1 : 1; };
    switch (m_mode) {
    case GameMode::BattleTeams:
        return order(points1 == points2 && deaths1 == deaths2, points1 < points2 || (points1 == points2 && deaths1 > deaths2));
    case GameMode::SurvivalTeams:
        return order(time1 == time2 && deaths1 == deaths2, time1 < time2 || (time1 == time2 && deaths1 > deaths2));
    case GameMode::DefenderTeams:
        return order(time1 == time2 && kills1 == kills2, time1 < time2 || (time1 == time2 && kills1 < kills2));
    case GameMode::Capture:
    case GameMode::NodesTeams:
    case GameMode::BountyTeams:
        return order(points1 == points2 && kills1 == kills2, points1 < points2 || (points1 == points2 && kills1 < kills2));
    default:
        return 0;
    }
}

void World::updateStandings()
{
    // GameState.UpdateState's totals by team, then GameStateTeams.UpdateStandings:
    // ResultSlots best first (by team, then by player; the lower slot or team
    // first on a tie), Standings counting who is strictly ahead -- the teams
    // ahead, with teams -- and TeamStandings the teammates ahead.
    m_teamPoints.fill(0);
    m_teamDeaths.fill(0);
    m_teamKills.fill(0);
    if (survivalMode()) {
        m_teamTime.fill(0);
    }
    const int teamLimit = teams() ? teamCount() : 16;
    for (size_t i = 0; i < m_slots.size(); i++) {
        const PlayerSlot& slot = m_slots[i];
        const int team = slot.player->teamIndex();
        if (team < 0 || team >= teamLimit || !slot.active) {
            continue;
        }
        m_teamPoints[team] += slot.points;
        m_teamDeaths[team] += slot.deaths;
        m_teamKills[team] += slot.kills;
        if (survivalMode() && (slot.time == -1 || (m_teamTime[team] != -1 && m_teamTime[team] < slot.time))) {
            m_teamTime[team] = slot.time; // the team's best survivor
        }
    }
    m_resultSlots.clear();
    std::array<bool, 16> represented{};
    for (size_t i = 0; i < m_slots.size(); i++) {
        m_slots[i].standing = m_slots[i].teamStanding = static_cast<int>(m_slots.size()) - 1;
        const int team = m_slots[i].player->teamIndex();
        if (team < 0 || team >= teamLimit || !m_slots[i].active) {
            continue;
        }
        m_resultSlots.push_back(static_cast<int>(i));
        represented[team] = true;
    }
    auto teamOf = [&](int slot) { return m_slots[slot].player->teamIndex(); };
    for (size_t i = 0; i < m_resultSlots.size(); i++) {
        for (size_t j = i + 1; j < m_resultSlots.size(); j++) {
            const int first = m_resultSlots[i];
            const int second = m_resultSlots[j];
            const int firstTeam = teamOf(first), secondTeam = teamOf(second);
            int compare;
            if (teams() && firstTeam != secondTeam) {
                compare = compareTeams(firstTeam, secondTeam);
                if (compare == 0) {
                    compare = secondTeam < firstTeam ? -1 : (secondTeam > firstTeam ? 1 : 0);
                }
            } else {
                compare = comparePlayers(first, second);
                if (compare == 0) {
                    compare = second < first ? -1 : (second > first ? 1 : 0); // second.CompareTo(first)
                }
            }
            if (compare < 0) {
                std::swap(m_resultSlots[i], m_resultSlots[j]);
            }
        }
    }
    for (const int slot : m_resultSlots) {
        const int team = teamOf(slot);
        int rank = 0, memberRank = 0;
        if (teams()) {
            for (int other = 0; other < teamCount(); other++) {
                if (represented[other] && compareTeams(other, team) > 0) {
                    rank++;
                }
            }
        }
        for (const int other : m_resultSlots) {
            if (comparePlayers(other, slot) <= 0) {
                continue;
            }
            if (!teams()) {
                rank++;
            } else if (teamOf(other) == team) {
                memberRank++;
            }
        }
        m_slots[slot].standing = rank;
        m_slots[slot].teamStanding = memberRank;
    }
}

void World::updateState()
{
    // GameState.UpdateState: Defender counts each player the team's ring time.
    if (defenderMode()) {
        for (PlayerSlot& slot : m_slots) {
            const int team = slot.player->teamIndex();
            if (team >= 0 && team < 16) {
                slot.time = m_teamTime[team];
            }
        }
    }
    updateStandings();
    // The announcer: one kill (or point) to win, a team out of lives.
    const int mainTeam = m_slots[0].player->teamIndex() & 15;
    if (m_mode == GameMode::Battle || m_mode == GameMode::BattleTeams || m_mode == GameMode::Capture || octolithMode() || nodesMode()) {
        if (m_teamPoints[mainTeam] != m_voicePoints[mainTeam] && m_teamPoints[mainTeam] == m_pointGoal - 1) {
            Sfx::instance().queueStream(VoiceId::VOICE_ONE_KILL_TO_WIN, 1);
        }
    } else if (survivalMode()) {
        int opponents = 0, lastTeam = -1, mask = 0;
        for (const PlayerSlot& slot : m_slots) {
            const Player& player = *slot.player;
            const int team = player.teamIndex() & 15;
            if ((player.health() > 0 || m_teamDeaths[team] <= m_pointGoal) && team != mainTeam && !(mask & (1 << team))) {
                mask |= 1 << team;
                opponents++;
                lastTeam = team;
            }
            if (m_teamDeaths[team] > m_pointGoal && player.respawnTimer() == 90 * 2) {
                Sfx::instance().queueStream(VoiceId::VOICE_ELIMINATED);
            }
        }
        if (opponents == 1 && lastTeam != -1 && m_teamDeaths[lastTeam] != m_voiceDeaths[lastTeam] && m_teamDeaths[lastTeam] == m_pointGoal) {
            Sfx::instance().queueStream(VoiceId::VOICE_ONE_KILL_TO_WIN, 1);
        }
    }
    m_voicePoints = m_teamPoints;
    m_voiceDeaths = m_teamDeaths;
}

bool World::resultTie() const
{
    // GameState.IsResultTie: somebody after the leader shares first place --
    // on another team, with teams.
    if (m_resultSlots.empty()) {
        return false;
    }
    const int leaderTeam = m_slots[m_resultSlots[0]].player->teamIndex();
    for (size_t i = 1; i < m_resultSlots.size(); i++) {
        const PlayerSlot& slot = m_slots[m_resultSlots[i]];
        if (slot.standing == 0 && (!teams() || slot.player->teamIndex() != leaderTeam)) {
            return true;
        }
    }
    return false;
}

std::optional<CameraPose> World::matchEndCamera() const
{
    if (m_matchState != MatchState::GameOver || m_resultSlots.empty() || resultTie()) {
        return std::nullopt;
    }
    const Player& winner = *m_slots[m_resultSlots[0]].player;
    if (winner.health() <= 0) {
        return std::nullopt; // the C# flies the room's intro camera instead
    }
    return winner.matchEndCamera((m_globalTicks - m_matchEndTick) / 60.0f);
}

void World::restartMatch()
{
    // Not the C#'s: it goes back to the launcher after the results. The port
    // has no launcher yet, so the same room starts over.
    for (PlayerSlot& slot : m_slots) {
        slot.points = slot.kills = slot.deaths = 0;
        slot.time = 0;
        for (BeamProjectile& beam : slot.beams) {
            if (beam.lifespan > 0) {
                endBeamEffect(beam);
                beam.lifespan = 0;
            }
        }
    }
    for (size_t i = 0; i < m_bombs.size(); i++) {
        if (m_bombs[i].active) {
            destroyBomb(i);
        }
    }
    for (BeamEffect& effect : m_beamEffects) {
        effect.lifespan = 0;
        m_scene.instances[effect.instance].visible = false;
    }
    for (ItemInstance& item : m_droppedItems) {
        item.despawnTimer = 0;
        m_scene.instances[item.modelInstance].visible = false;
    }
    for (ItemSpawn& spawn : m_items) {
        if (spawn.item != nullptr) {
            m_scene.instances[spawn.item->modelInstance].visible = false;
            spawn.item = nullptr;
        }
        spawn.spawnCount = 0;
        spawn.cooldown = spawn.initialCooldown;
    }
    std::fill(m_spawnCooldowns.begin(), m_spawnCooldowns.end(), 0);
    setPrimeHunter(-1);
    resetModeEntities();
    m_matchState = MatchState::InProgress;
    m_introActive = false;
    Sfx& sfx = Sfx::instance();
    sfx.sfxMute = false; // RestartLongSfx(force)
    sfx.timedSfxMute = sfx.longSfxMute = 0;
    m_lastAlarmTime = 0;
    m_nextAlarmIndex = 0;
    m_tempoChanged = false;
    Music::instance().playRoomMusic(roomId(), 0);
    for (PlayerSlot& slot : m_slots) {
        if (m_networked || (m_server && !slot.active)) {
            slot.player->despawn(); // the server spawns everybody again; nobody holds this one
        } else if (&slot == &m_slots[0] && m_intro) {
            slot.player->despawn();
            ensureIntro();
        } else {
            respawn(slot);
        }
    }
    m_matchTime = m_matchLength;
    m_radarPlayers = false;
    updateStandings();
}

void World::simulate(const PlayerInput& input, bool movePlayer)
{
    m_ticks++;
    processDoors();
    processJumpPads();
    processTeleporters();
    processMorphCameras();
    std::vector<DoorObstacle> doors;
    for (const Door& door : m_doors) {
        if (!door.open) {
            doors.push_back({door.lockPosition, door.facing, door.radiusSquared});
        }
    }
    std::vector<ForceFieldObstacle> forceFields;
    for (const ForceField& ff : m_forceFields) {
        if (ff.active) {
            forceFields.push_back(ff.obstacle);
        }
    }
    for (PlayerSlot& slot : m_slots) {
        slot.player->setObstacles(doors, forceFields);
    }
    processPlayers(input, movePlayer);
    processBombs();
    processBeams();
    processBeamEffects();
    processItems();
    processAreaVolumes();
    processModeEntities();
    processBots();
    updateState();
    for (size_t i = 0; i < m_slots.size(); i++) {
        updatePlayerModels(m_slots[i], i == 0);
    }
    // EntityBase.UpdateAnimFrames: animations step every other tick; players step their own.
    if (m_ticks % 2 == 0) {
        for (size_t i = 0; i < m_scene.instances.size(); i++) {
            if (std::find(m_ownedInstances.begin(), m_ownedInstances.end(), i) == m_ownedInstances.end()) {
                m_scene.instances[i].animation.advance();
                m_scene.instances[i].secondary.advance();
            }
        }
    }
    drawBeams();
    drawBombs();
    drawDeathParticles();
    m_effects->process(m_collision.get());
    drawEffects();
    m_scene.seconds = m_ticks / 60.0;
}

void World::addSingleParticle(const BeamTexture& tex, const Vec3& position, const Vec3& color, float alpha, float scale)
{
    // SingleParticle: a quad `scale` out each way, facing the camera.
    if (tex.model == nullptr || alpha <= 0) {
        return;
    }
    std::vector<Vertex>& vertices = m_scene.dynamicVertices;
    DynamicDraw draw;
    draw.model = tex.model;
    draw.textureId = tex.textureId;
    draw.paletteId = tex.paletteId;
    draw.xRepeat = tex.xRepeat;
    draw.yRepeat = tex.yRepeat;
    draw.transform = Mat4::translation(position[0], position[1], position[2]);
    draw.billboard = true;
    draw.alpha = alpha;
    draw.firstVertex = static_cast<uint32_t>(vertices.size());
    auto vertex = [&](float x, float y, float u, float v) {
        Vertex out{};
        out.pos[0] = x;
        out.pos[1] = y;
        out.normal[2] = 1;
        out.color[0] = color[0];
        out.color[1] = color[1];
        out.color[2] = color[2];
        out.color[3] = 1;
        out.uv[0] = u;
        out.uv[1] = v;
        return out;
    };
    const Vertex v0 = vertex(-scale, scale, 0, 0);
    const Vertex v1 = vertex(scale, scale, 1, 0);
    const Vertex v2 = vertex(scale, -scale, 1, 1);
    const Vertex v3 = vertex(-scale, -scale, 0, 1);
    for (const Vertex* v : {&v0, &v1, &v2, &v0, &v2, &v3}) {
        vertices.push_back(*v);
    }
    draw.vertexCount = 6;
    m_scene.dynamicDraws.push_back(draw);
}

void World::drawDeathParticles()
{
    // PlayerDraw.DrawDeathParticles: in the first third of the respawn wait,
    // sparks along the biped's bones rise and spread out as they fade.
    constexpr float respawnTime = 90 * 2;
    int budget = 200; // Renderer._singleParticleMax
    for (PlayerSlot& slot : m_slots) {
        const Player& p = *slot.player;
        if (p.health() > 0 || slot.bipedNodes.empty() || !slot.biped) {
            continue;
        }
        const float timePct = 1 - ((p.respawnTimer() - 2 / 3.0f * respawnTime) / (1 / 3.0f * respawnTime));
        if (timePct < 0 || timePct > 1) {
            continue;
        }
        const float scale = timePct / 2 + 0.1f;
        const float angle = std::sin((270 - 90 * timePct) * kDegToRad);
        const float offset = (angle - -1.0f) / (0.0f - -1.0f);
        const Vec3 position = p.position();
        const auto& nodes = m_scene.instances[*slot.biped].model->nodes();
        auto nodePos = [&](int index) {
            Vec3 pos = slot.bipedNodes[index].translationPart();
            pos[1] += offset;
            return pos;
        };
        auto emit = [&](Vec3 pos) {
            if (budget-- > 0) {
                const Vec3 out = pos - position;
                const float len = std::sqrt(dot(out, out));
                if (len > 0) {
                    pos = pos + out * (offset / len);
                }
                addSingleParticle(m_deathParticle, pos, {1, 1, 1}, 1 - timePct, scale);
            }
        };
        for (size_t i = 1; i < nodes.size(); i++) {
            const Vec3 pos = nodePos(static_cast<int>(i));
            for (int link : {nodes[i].childIndex, nodes[i].nextIndex}) {
                if (link > 0) {
                    const Vec3 other = nodePos(link);
                    for (int j = 1; j < 5; j++) {
                        emit(pos + (other - pos) * (j / 5.0f));
                    }
                }
            }
            emit(pos);
        }
    }
}

void World::drawEffects()
{
    // The particles face the main player's camera (the view the game draws them for).
    const CameraPose cam = m_slots[0].player->camera();
    Vec3 z = cam.position - cam.target;
    const float len = std::sqrt(dot(z, z));
    z = len > 0 ? z * (1 / len) : Vec3{0, 0, 1};
    Vec3 x = cross(cam.up, z);
    const float xl = std::sqrt(dot(x, x));
    x = xl > 0 ? x * (1 / xl) : Vec3{1, 0, 0};
    const Vec3 y = cross(z, x);
    Mat4 view = Mat4::identity();
    for (int i = 0; i < 3; i++) {
        view.m[i][0] = x[i];
        view.m[i][1] = y[i];
        view.m[i][2] = z[i];
    }
    const size_t before = m_scene.dynamicDraws.size();
    m_effects->draw(m_scene.dynamicVertices, m_scene.dynamicDraws, view);
    if (qEnvironmentVariableIsSet("FP_DEBUG_PARTICLES") && m_ticks % 10 == 0) {
        qInfo("tick %lld: %zu particle draws", m_ticks, m_scene.dynamicDraws.size() - before);
        for (size_t i = before; i < m_scene.dynamicDraws.size() && i < before + 3; i++) {
            const DynamicDraw& d = m_scene.dynamicDraws[i];
            const Vertex& v = m_scene.dynamicVertices[d.firstVertex];
            qInfo("  tex %d pal %d alpha %.2f bb %d at (%.2f %.2f %.2f) v0 (%.2f %.2f %.2f) col %.2f %.2f %.2f %.2f uv %.2f %.2f n %u",
                d.textureId, d.paletteId, d.alpha, d.billboard, d.transform.m[3][0], d.transform.m[3][1], d.transform.m[3][2], v.pos[0],
                v.pos[1], v.pos[2], v.color[0], v.color[1], v.color[2], v.color[3], v.uv[0], v.uv[1], d.vertexCount);
        }
    }
}

HudContext World::hudContext() const
{
    HudContext context;
    // NetHitPrediction.MarkerAlpha: twelve frames, fading over the last six.
    context.hitMarker = m_hitMarkerTimer >= 6 ? 1.0f : m_hitMarkerTimer / 6.0f;
    const int mainTeam = m_slots[0].player->teamIndex();
    context.teams = teams();
    context.teamCount = teamCount();
    // PlayerHud.FormatModeScore: the team's points with teams.
    context.points = teams() ? teamPoints(mainTeam) : m_slots[0].points;
    context.deaths = teamDeaths(mainTeam);
    for (int team = 0; team < 4; team++) {
        context.teamScores[team] = {teamPoints(team), teamKills(team), teamDeaths(team), teamTime(team)};
    }
    for (const OctolithFlag& flag : m_octolithFlags) {
        context.teamCarryingOctolith |= flag.carrier != nullptr && flag.carrier->teamIndex() == mainTeam;
    }
    context.pointGoal = m_pointGoal;
    context.time = m_slots[0].time;
    context.timeGoal = m_timeGoal;
    context.primeHunter = m_primeHunter;
    context.mode = m_mode;
    context.respawnWait = timeUntilRespawn(*m_slots[0].player);
    context.intro = introPlaying();
    context.introTime = (m_globalTicks - m_introStartTick) / 60.0f;
    context.eliminated = eliminated(0);
    context.faceOff = m_radarPlayers;
    context.revealed = m_slots[0].player->radarReveal;
    context.teamIndex = m_slots[0].player->teamIndex();
    context.carryingOctolith = m_slots[0].player->octolithFlag != nullptr;
    if (nodesMode()) {
        for (const NodeDefense& defense : m_nodeDefenses) {
            context.nodes.push_back({defense.currentTeam, defense.occupyingTeam, defense.blinking(),
                defense.occupiedBy[m_slots[0].player->slot() & 15], defense.progress});
        }
    }
    for (size_t i = 1; i < m_slots.size(); i++) {
        const Player& other = *m_slots[i].player;
        context.cowardDetected |= other.health() > 0 && other.radarReveal && !other.radarRevealPrevious;
    }
    context.matchState = m_matchState;
    context.matchTime = m_matchTime;
    context.elapsed = m_ticks / 60.0f;
    for (const int slot : m_resultSlots) {
        const PlayerSlot& s = m_slots[slot];
        context.scores.push_back(
            {slot, static_cast<int>(s.player->hunter()), s.points, s.kills, s.deaths, s.time, s.player->teamIndex(), s.standing, s.name});
    }
    for (const ItemSpawn& spawn : m_items) {
        if (spawn.item == nullptr) {
            continue;
        }
        // Radar.IsWeaponItem
        bool weapon = false;
        switch (spawn.type) {
        case ItemType::VoltDriver:
        case ItemType::Battlehammer:
        case ItemType::Imperialist:
        case ItemType::Judicator:
        case ItemType::Magmaul:
        case ItemType::ShockCoil:
        case ItemType::OmegaCannon:
        case ItemType::AffinityWeapon:
        case ItemType::PickWpnMissile:
        case ItemType::MissileExpansion:
        case ItemType::UASmall:
        case ItemType::UABig:
        case ItemType::MissileSmall:
        case ItemType::MissileBig:
        case ItemType::UAExpansion:
            weapon = true;
            break;
        default:
            break;
        }
        context.items.push_back({spawn.item->position, weapon});
    }
    for (const ItemInstance& item : m_droppedItems) {
        if (item.active()) {
            context.items.push_back({item.position, true});
        }
    }
    addLocators(context);
    return context;
}

void World::addLocators(HudContext& context) const
{
    const Player& main = *m_slots[0].player;
    // Over the biped's head, or at the alt form.
    auto above = [](const Player& p) { return p.isAltForm() ? p.position() : p.position() + Vec3{0, 0.75f, 0}; };
    if (survivalMode()) {
        // ProcessHudSurvival: players hiding too long, or everybody in a face-off (pulsing).
        for (size_t i = 1; i < m_slots.size(); i++) {
            const Player& other = *m_slots[i].player;
            if (other.health() == 0 || other.teamIndex() == main.teamIndex()) {
                continue;
            }
            float alpha = 1;
            if (m_radarPlayers) {
                const float past = std::fmod(context.elapsed, 120 / 30.0f);
                if (past > 32 / 30.0f) {
                    alpha = 0;
                } else {
                    const float pct = std::fmod(past / (32 / 30.0f), 1.0f);
                    alpha = pct <= 0.5f ? pct * 2 : 1 - (pct - 0.5f) * 2;
                }
            } else if (!other.radarReveal) {
                continue;
            }
            context.locators.push_back({above(other), teams() ? teamColor(other.teamIndex()) : std::array<float, 3>{1, 1, 1}, alpha});
        }
    } else if (m_mode == GameMode::PrimeHunter && m_primeHunter >= 0 && m_primeHunter != 0) {
        // ProcessHudPrimeHunter: the Prime Hunter, in red, for everybody else.
        context.locators.push_back({above(*m_slots[m_primeHunter].player), {1, 0, 0}, 1});
    } else {
        addModeLocators(context);
    }
}

} // namespace fp
