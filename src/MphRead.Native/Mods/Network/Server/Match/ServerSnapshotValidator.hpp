#pragma once

#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace MphRead::Mods::Network
{
    struct ServerSnapshotSlotLife final
    {
        std::uint8_t SlotIndex = 0;
        std::uint16_t LifeId = 0;
    };

    struct ValidatedServerSnapshot final
    {
        SnapshotHeader Header{};
        std::array<ServerSnapshotSlotLife, ServerSlotIdentity::SlotCapacity> SlotLives{};
        std::uint8_t SlotCount = 0;
    };

    // Validates an authority snapshot against the current match and slot lifetimes.
    // The returned slot/life pairs can be applied only after the stream accepts its frame.
    class ServerSnapshotValidator final
    {
    public:
        ServerSnapshotValidator() = delete;

        [[nodiscard]] static std::optional<ValidatedServerSnapshot> Validate(
            std::span<const std::uint8_t> payload,
            std::uint16_t matchId,
            std::uint64_t authorityEpoch,
            const ServerSlotIdentity& slotIdentity);
    };
}
