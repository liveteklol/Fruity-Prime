#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Projects server-owned lobby state into the immutable session wire snapshot.
    class ServerSessionStateBuilder final
    {
    public:
        ServerSessionStateBuilder() = delete;

        [[nodiscard]] static SessionStatePacket Build(
            const ServerLobbyState& lobby,
            const ServerPeerRegistry& peers,
            const MatchDefinition& currentMatch,
            std::uint16_t matchId,
            std::uint64_t authorityEpoch,
            std::int32_t maxPlayers);
    };
}
