#pragma once

#include <cstdint>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    struct PlayerState;

    // Coordinates wire timer capture and lifecycle reconciliation. Each
    // status policy retains the behavior that is unique to that effect.
    class StatusEffectSynchronization final
    {
    public:
        StatusEffectSynchronization() = delete;

        static void Capture(const Entities::PlayerEntity& player, PlayerState& state);
        static void Apply(Entities::PlayerEntity& player, const PlayerState& state,
            std::uint32_t snapshotAgeFrames);
    };
}
