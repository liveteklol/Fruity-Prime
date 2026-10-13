#pragma once

#include "Mods/Network/Session/ClientLobbyState.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"

#include <optional>

namespace MphRead::Mods::Network
{
    struct SessionLobbyTransition final
    {
        bool Accepted = false;
        bool ResetMatch = false;
        std::optional<MatchStatePacket> MatchState{};
    };

    // Owns the ordering and projection policy between authoritative lobby
    // state and the match identity consumed by the gameplay session.
    class SessionLobbySynchronizer final
    {
    public:
        [[nodiscard]] static SessionLobbyTransition Apply(ClientLobbyState& lobby,
            const SessionMatchIdentity& matchIdentity, const SessionStatePacket& state);
    };
}
