#pragma once

#include "Mods/Network/Session/SessionAuthorityState.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Session/SessionTrafficMetrics.hpp"
#include "Mods/Network/Session/SessionSimulationCallbacks.hpp"
#include "Mods/Network/Session/SessionPlayerIdentity.hpp"
#include "Mods/Network/Session/ClientConnectionHealth.hpp"
#include "Mods/Network/Session/ClientReplicaStream.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Mods/Network/Session/ClientLobbyState.hpp"

namespace MphRead::Mods::Network
{
    class NetSession;
    class NetSessionFixture;

    // The mutable state that defines one client/host/server session lifetime.
    // Domain services keep their own invariants; this aggregate only gives the
    // compatibility facade one owner for their lifetime and reset boundary.
    class SessionContext final
    {
        friend class NetSession;
        friend class NetSessionFixture;

        SessionConnection _connection{};
        SessionRuntimeState _runtime{};
        SessionTrafficMetrics _traffic{};
        SessionSimulationCallbacks _simulationCallbacks{};
        SessionPlayerIdentity _playerIdentity{};
        ClientConnectionHealth _connectionHealth{};
        ClientReplicaStream _replicaStream{};
        SessionMatchIdentity _matchIdentity{};
        SessionAuthorityState _authorityState{};
        ClientLobbyState _clientLobby{};
    };
}
