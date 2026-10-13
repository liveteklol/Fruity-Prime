#pragma once

#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"

#include <optional>

namespace MphRead::Mods::Network
{
    class ServerLobbyState;
    struct ReceivedPacket;

    // Applies a connected peer's validated identity update to its server-owned
    // profile and lobby readiness. Logging and announcements remain with the
    // server lifecycle coordinator.
    class ServerPeerIdentityChannel final
    {
    public:
        ServerPeerIdentityChannel() = delete;

        struct Change final
        {
            ServerPeerRegistry::PeerHandle Peer{};
            bool FirstName = false;
        };

        [[nodiscard]] static std::optional<Change> Receive(const ReceivedPacket& packet,
            double now, ServerPeerRegistry& peers, const ServerLobbyState& lobby);
    };
}
