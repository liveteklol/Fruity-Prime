#include "Entities.h"

#include "Enums.h"
#include "Model.h"

#include <cstring>
#include <stdexcept>

namespace fp {

namespace {

constexpr size_t kHeaderSize = 4 + 16 * 2; // version + 16 layer lengths
constexpr size_t kEntrySize = 24;          // node name[16], layer mask, length, data offset
constexpr size_t kDataHeaderSize = 40;     // type, id, position, up, facing

template <typename T>
T readAt(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset + sizeof(T) > bytes.size()) {
        throw std::runtime_error("entity file truncated at " + std::to_string(offset));
    }
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

std::array<float, 3> readVector(const std::vector<uint8_t>& bytes, size_t offset)
{
    return {fxToFloat(readAt<int32_t>(bytes, offset)), fxToFloat(readAt<int32_t>(bytes, offset + 4)),
        fxToFloat(readAt<int32_t>(bytes, offset + 8))};
}

} // namespace

const char* entityTypeName(EntityType type)
{
    static const char* names[] = {"Platform", "Object", "PlayerSpawn", "Door", "ItemSpawn", "ItemInstance", "EnemySpawn",
        "TriggerVolume", "AreaVolume", "JumpPad", "PointModule", "MorphCamera", "OctolithFlag", "FlagBase", "Teleporter",
        "NodeDefense", "LightSource", "Artifact", "CameraSequence", "ForceField"};
    const auto i = static_cast<size_t>(type);
    return i < std::size(names) ? names[i] : "Unknown";
}

uint8_t Entity::u8(size_t offset) const { return readAt<uint8_t>(data, offset); }
int32_t Entity::i32(size_t offset) const { return readAt<int32_t>(data, offset); }
uint32_t Entity::u32(size_t offset) const { return readAt<uint32_t>(data, offset); }

std::vector<Entity> loadEntities(const std::filesystem::path& file, int layerId)
{
    const std::vector<uint8_t> bytes = readFile(file);
    const auto version = readAt<uint32_t>(bytes, 0);
    if (version != 2) {
        throw std::runtime_error("unsupported entity file version " + std::to_string(version));
    }
    std::vector<Entity> entities;
    for (size_t i = 0;; i++) {
        const size_t entry = kHeaderSize + kEntrySize * i;
        const auto dataOffset = readAt<uint32_t>(bytes, entry + 20);
        if (dataOffset == 0) {
            break;
        }
        const auto layerMask = readAt<uint16_t>(bytes, entry + 16);
        const auto length = readAt<uint16_t>(bytes, entry + 18);
        if (layerId != -1 && (layerMask & (1u << layerId)) == 0) {
            continue;
        }
        if (length < kDataHeaderSize || dataOffset + length > bytes.size()) {
            throw std::runtime_error("entity record out of range");
        }
        Entity e;
        e.nodeName = std::string(reinterpret_cast<const char*>(bytes.data() + entry), strnlen(reinterpret_cast<const char*>(bytes.data() + entry), 16));
        e.layerMask = layerMask;
        e.type = static_cast<EntityType>(readAt<uint16_t>(bytes, dataOffset));
        e.id = readAt<int16_t>(bytes, dataOffset + 2);
        e.position = readVector(bytes, dataOffset + 4);
        e.up = readVector(bytes, dataOffset + 16);
        e.facing = readVector(bytes, dataOffset + 28);
        e.data.assign(bytes.begin() + dataOffset, bytes.begin() + dataOffset + length);
        entities.push_back(std::move(e));
    }
    return entities;
}

std::vector<PlayerSpawn> playerSpawns(const std::vector<Entity>& entities)
{
    std::vector<PlayerSpawn> out;
    for (const Entity& e : entities) {
        if (e.type != EntityType::PlayerSpawn) {
            continue;
        }
        out.push_back(PlayerSpawn{
            .position = e.position,
            .facing = e.facing,
            .teamIndex = static_cast<int8_t>(e.u8(42)),
            .active = e.u8(41) != 0,
            .availability = e.u8(40),
        });
    }
    return out;
}

} // namespace fp
