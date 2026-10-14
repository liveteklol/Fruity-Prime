#pragma once

#include "Formats/Enums.hpp"

#include <cstdint>

namespace MphRead::Entities
{
    class EntityBase;
}

namespace MphRead::Mods::Network
{
    // A name for an enemy that every machine gives it alike, so a homing
    // shot's target can travel on the wire: derived from what spawned it (a
    // room entity's id, or the parent enemy's own name), how many it had
    // spawned before, and its type. 0 for an enemy with no such origin.
    class EnemyTargetIdentity final
    {
    public:
        EnemyTargetIdentity() = delete;

        [[nodiscard]] static std::uint64_t ForSpawn(Entities::EntityBase* spawner, EnemyType type) noexcept;
    };
}
