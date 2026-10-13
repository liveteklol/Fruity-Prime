#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"
#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"
#include "Mods/Network/Session/ClientReplicaStream.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Session/SessionTrafficMetrics.hpp"

#include <array>

namespace MphRead::Mods::Network
{
    struct ReceivedPacket;

    // Owns inbound snapshot validation, ordering, replica updates, and apply routing.
    class SessionSnapshotReceiver final
    {
    public:
        SessionSnapshotReceiver() = delete;

        static void Receive(ReceivedPacket packet, bool gameplayFrozen,
            const SessionRuntimeState& runtime, const SessionMatchIdentity& matchIdentity,
            ClientReplicaStream& replicaStream, SessionTrafficMetrics& traffic);

    private:
        static std::array<PlayerState, NetworkSlotCapacity> _snapshotScratch;
    };
}
