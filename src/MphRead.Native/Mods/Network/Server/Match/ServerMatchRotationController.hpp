#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    class ServerPeerRegistry;
    class ServerSlotIdentity;
    class ServerLobbyState;
    class ServerSnapshotStream;

    // Applies the in-memory state transition at a map boundary. Packet sends,
    // revision broadcasts, and ballot effects stay with DedicatedServer.
    class ServerMatchRotationController final
    {
    public:
        ServerMatchRotationController() = delete;

        static void Advance(const MatchDefinition& nextDefinition, double now,
            ServerLobbyState& lobby, ServerPeerRegistry& peers,
            ServerSlotIdentity& slotIdentity, ServerSnapshotStream& snapshots,
            std::uint16_t& matchId, double& matchStarted, double& matchEndedAt);
    };
}
