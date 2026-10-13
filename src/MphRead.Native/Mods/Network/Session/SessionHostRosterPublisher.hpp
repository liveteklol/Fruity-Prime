#pragma once

#include "Mods/Network/Session/ClientReplicaStream.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Mods/Network/Session/SessionPlayerIdentity.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"

namespace MphRead::Mods::Network
{
    class PeerRegistry;

    // Publishes the host's match identity and authoritative peer roster together.
    class SessionHostRosterPublisher final
    {
    public:
        SessionHostRosterPublisher() = delete;

        static void Publish(SessionConnection& connection, PeerRegistry& peers,
            const SessionRuntimeState& runtime,
            const SessionMatchIdentity& matchIdentity, ClientReplicaStream& replicaStream,
            const SessionPlayerIdentity& playerIdentity);
    };
}
