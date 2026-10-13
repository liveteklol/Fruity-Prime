#pragma once

#include "Mods/Network/Session/SessionAuthorityState.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Session/SessionSimulationCallbacks.hpp"
#include "Mods/Network/Session/SessionTrafficMetrics.hpp"

#include <cstdint>
#include <span>

namespace MphRead::Mods::Network
{
    // Captures authoritative game state, writes the snapshot datagram, and
    // publishes it to the host, server sink, or connected peers.
    class SessionSnapshotPublisher final
    {
    public:
        SessionSnapshotPublisher() = delete;

        static void Publish(SessionConnection& connection, const SessionRuntimeState& runtime,
            const SessionAuthorityState& authority, const SessionMatchIdentity& matchIdentity,
            const SessionSimulationCallbacks& simulationCallbacks,
            SessionTrafficMetrics& traffic);
    };
}
