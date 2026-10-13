#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    // Builds the wire snapshot of one dedicated server's authoritative match state.
    class ServerMatchStateBuilder final
    {
    public:
        ServerMatchStateBuilder() = delete;

        [[nodiscard]] static MatchStatePacket Build(const MatchDefinition& definition,
            SessionPhase phase, double now, double matchStarted, bool ending,
            std::size_t playerCount, std::uint16_t matchId, std::uint64_t authorityEpoch,
            std::int32_t damageLevel, const std::string& nextRoomKey);
    };
}
