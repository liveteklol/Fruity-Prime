#pragma once

#include "Collision.h"
#include "Player.h"
#include "formats/Entities.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

// The room's entities the World drives and the bots look at.
namespace fp {

struct NodeData3;

// CollisionVolume: the box, cylinder or sphere trigger volumes entities carry.
struct TriggerVolume {
    enum Type { Box = 0, Cylinder = 1, Sphere = 2 } type = Sphere;
    Vec3 v1{}, v2{}, v3{}, position{};
    float d1 = 0, d2 = 0, d3 = 0;
    float radius = 0;

    static TriggerVolume fromRaw(const Entity& e, size_t offset, const Vec3& moveBy);
    bool testPoint(const Vec3& point) const;
    // CollisionDetection.CheckSphereOverlapVolume, the answer only.
    bool overlapsSphere(const Vec3& center, float radius) const;
};

// MorphCameraEntity: a fixed camera for alt forms inside its volume.
struct MorphCamera {
    int id = -1;
    Vec3 position{};
    TriggerVolume volume;
};

struct Door {
    size_t instance;
    Vec3 position, facing, lockPosition;
    float radiusSquared;
    bool locked = false, shotOpen = false, shouldOpen = false, closed = true, open = false, opening = false;
    uint32_t type = 0; // DoorType: Standard, MorphBall, Boss, Thin
    std::shared_ptr<SoundSource> sound = std::make_shared<SoundSource>();
};

struct JumpPad {
    TriggerVolume volume;
    Vec3 beamVector;
    uint16_t controlLockTime, cooldownTime, cooldown = 0;
    uint32_t triggerFlags;
    bool active;
    Vec3 position{};
    const NodeData3* closestNode = nullptr; // for the bots, when the room's node data is simple
};

// LightSourceEntity: inside its volume, players and Halfturrets take its
// lights instead of the room's.
struct LightSource {
    TriggerVolume volume;
    bool light1Enabled = false, light2Enabled = false;
    Vec3 light1Color{}, light1Vector{}, light2Color{}, light2Vector{};
};

// TeleporterEntity, as the arenas use it: a pad that sends whoever steps
// into its middle to TargetPosition, animating open while someone is near.
struct Teleporter {
    int id = -1;
    Vec3 position{}, facing{};
    Vec3 targetPosition{};
    bool active = false;
    bool invisible = false;
    bool big = false;          // the adventure's artifact teleporters
    bool opening = false;      // _bool3: somebody is near, the pad opens
    bool closing = false;      // _bool4: nobody is, it closes when it can
    std::optional<size_t> model;
    // One per slot, all set: a slot only becomes eligible once it has left
    // the pad, so nobody is teleported the moment it arrives or spawns on one.
    std::array<bool, 16> triggered{};
    std::shared_ptr<SoundSource> sound = std::make_shared<SoundSource>();
};

struct ItemSpawn;
// ItemInstanceEntity: an item lying there to be picked up.
struct ItemInstance {
    ItemType type = ItemType::HealthMedium;
    Vec3 position{};
    int despawnTimer = 0;       // -1: stays until picked up; 0: gone
    ItemSpawn* owner = nullptr; // the spawner, none for dropped ammo
    size_t modelInstance = 0;
    const NodeData3* closestNode = nullptr; // for the bots, found once
    std::shared_ptr<SoundSource> sound = std::make_shared<SoundSource>();
    bool active() const { return despawnTimer != 0; }
};
// ItemSpawnEntity
struct ItemSpawn {
    int id = -1; // the entity's
    ItemType type;
    Vec3 position;                // the entity's; its item floats 0.65 above
    ItemInstance* item = nullptr; // while it is there
    ItemInstance instance;        // the one it spawns
    bool active;
    uint16_t maxSpawnCount, spawnInterval, spawnCount = 0, cooldown;
    uint16_t initialCooldown = 0; // ItemDelay, for a new match
    int lastPicker = -1;          // the world slot that last took its health pickup (NetHealthSync.PickerSlot)
};

// AreaVolumeEntity: a volume that sends a message to the players in it. In
// the arenas only three kinds are used, all sent to the player: Damage,
// Death (pits) and Gravity (Fuel Stack's fans, Head Shot's low gravity).
struct AreaVolume {
    int id = -1;
    TriggerVolume volume;
    bool active = true, allowMultiple = false;
    uint32_t insideMessage = 0, exitMessage = 0;
    int insideParam1 = 0, insideParam2 = 0, exitParam1 = 0, exitParam2 = 0;
    int parentId = -1, childId = -1;
    int cooldownTime = 0;
    uint32_t priority = 0, triggerFlags = 0;
    std::array<int, 16> cooldownSlots{};
    std::array<bool, 16> triggeredSlots{};
    std::array<uint32_t, 16> prioritySlots{};
};

// OctolithFlagEntity: one Octolith a team in Capture, the one Octolith of
// Bounty. It sits 1.25 above its base, rides on its carrier's back, falls
// when dropped and goes home after 20 seconds on the ground.
struct OctolithFlag {
    int teamId = 0;
    Vec3 position{}, basePosition{};
    Player* carrier = nullptr;
    Player* lastCarrier = nullptr;
    bool atBase = true, grounded = true;
    float resetTimer = 0, gravity = 0;
    size_t instance = 0;                   // octolith_ctf
    std::optional<size_t> baseInstance;    // flagbase_ctf / flagbase_bounty, at the base
    const NodeData3* closestNode = nullptr; // for the bots
    const NodeData3* baseClosestNode = nullptr;
};

// FlagBaseEntity: where an Octolith is brought to score -- any carrier's in
// Bounty, a team's own in Capture.
struct FlagBase {
    int teamId = 0;
    Vec3 position{};
    TriggerVolume volume;
    const NodeData3* closestNode = nullptr;
};

// NodeDefenseEntity: a ring to stand in -- Nodes' capture points, Defender's one ring.
struct NodeDefense {
    static constexpr int NoTeam = -1;
    Vec3 position{};
    TriggerVolume volume;
    float circleScale = 1; // the ring model's scale: the volume's cylinder radius
    int currentTeam = NoTeam, occupyingTeam = NoTeam;
    std::array<bool, 16> occupiedBy{}; // by slot
    float blinkTimer = 0, progress = 0, scoreTimer = 0, curRotation = 0, spinSpeed = 0;
    bool contested = false, inProgress = false;
    Player* capturedPlayer = nullptr;
    size_t terminalInstance = 0, ringInstance = 0; // koth_data_flow, koth_terminal
    const NodeData3* closestNode = nullptr;
    bool blinking() const { return blinkTimer > 0; }
    bool isOccupied() const
    {
        for (bool occupied : occupiedBy) {
            if (occupied) {
                return true;
            }
        }
        return false;
    }
};

} // namespace fp
