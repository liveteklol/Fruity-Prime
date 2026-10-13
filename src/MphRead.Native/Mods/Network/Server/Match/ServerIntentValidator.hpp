#pragma once

#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace MphRead::Mods::Network
{
    // Validates the authenticated peer's intent payload against the current
    // match, authority epoch, and slot lifecycle before server-side use.
    class ServerIntentValidator final
    {
    public:
        ServerIntentValidator() = delete;

        [[nodiscard]] static std::optional<IntentPacket> Validate(
            std::span<const std::uint8_t> payload, std::uint16_t matchId,
            std::uint64_t authorityEpoch, std::uint16_t slotGeneration,
            std::uint16_t lifeId);
    };
}
