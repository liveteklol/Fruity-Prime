#pragma once

#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Builds the roster snapshot from the current peer, slot, and bot views.
    // The sequence number belongs here because it describes emitted rosters.
    class ServerRosterBuilder final
    {
    public:
        [[nodiscard]] RosterPacket Build(std::uint16_t matchId, std::uint64_t authorityEpoch,
            std::uint16_t sessionRevision, std::int32_t maxPlayers,
            const ServerPeerRegistry& peers, const ServerSlotIdentity& slots);

    private:
        std::uint32_t _revision = 0;
    };
}
