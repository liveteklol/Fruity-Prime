#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <cstdint>

namespace MphRead::Entities
{
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    // Captures the authoritative game-owned fields into their wire snapshot model.
    class PlayerStateSnapshotCapture final
    {
    public:
        PlayerStateSnapshotCapture() = delete;

        [[nodiscard]] static PlayerState Capture(Entities::PlayerEntity& player, std::int32_t slot,
            std::uint16_t slotGeneration, std::uint16_t lifeId);
    };
}
