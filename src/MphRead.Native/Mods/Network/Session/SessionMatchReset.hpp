#pragma once

namespace MphRead::Mods::Network
{
    class ClientReplicaStream;
    class RemotePlayerChannel;

    // Coordinates match teardown across session and gameplay state owners.
    class SessionMatchReset final
    {
    public:
        static void Apply(RemotePlayerChannel& remotePlayers,
            ClientReplicaStream& replicaStream);
    };
}
