#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Room entity files (levels/entities/*_Ent.bin, version 2). Ported from
// Read.GetEntitiesFromPath and Formats/Entity.cs.
namespace fp {

enum class EntityType : uint16_t {
    Platform = 0,
    Object = 1,
    PlayerSpawn = 2,
    Door = 3,
    ItemSpawn = 4,
    ItemInstance = 5,
    EnemySpawn = 6,
    TriggerVolume = 7,
    AreaVolume = 8,
    JumpPad = 9,
    PointModule = 10,
    MorphCamera = 11,
    OctolithFlag = 12,
    FlagBase = 13,
    Teleporter = 14,
    NodeDefense = 15,
    LightSource = 16,
    Artifact = 17,
    CameraSequence = 18,
    ForceField = 19,
};

const char* entityTypeName(EntityType type);

struct Entity {
    EntityType type;
    int id;
    std::string nodeName;
    uint16_t layerMask;
    std::array<float, 3> position;
    std::array<float, 3> up;
    std::array<float, 3> facing;
    std::vector<uint8_t> data; // the whole record, header included

    // Typed reads of the record, at the offsets of the C# structs.
    uint8_t u8(size_t offset) const;
    int32_t i32(size_t offset) const;
    uint32_t u32(size_t offset) const;
};

struct PlayerSpawn {
    std::array<float, 3> position;
    std::array<float, 3> facing;
    int teamIndex;
    bool active;
    int availability;
};

// layerId -1 keeps every entity.
std::vector<Entity> loadEntities(const std::filesystem::path& file, int layerId);

std::vector<PlayerSpawn> playerSpawns(const std::vector<Entity>& entities);

} // namespace fp
