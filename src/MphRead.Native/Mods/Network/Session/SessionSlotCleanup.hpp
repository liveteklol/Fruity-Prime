#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    class ClientReplicaStream;
    class PeerRegistry;
    class RemotePlayerChannel;

    // Clears per-slot remote session state when a player leaves or changes life.
    class SessionSlotCleanup final
    {
    public:
        SessionSlotCleanup() = delete;

        static void Forget(std::int32_t slot, PeerRegistry& peers,
            ClientReplicaStream& replicaStream, RemotePlayerChannel& remotePlayers);
    };
}
