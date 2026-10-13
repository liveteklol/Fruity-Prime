#pragma once

#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

namespace MphRead::Mods::Network
{
    class ClientLobbyState;
    class ClientReplicaStream;
    class SessionMatchIdentity;

    // Projects an accepted roster revision into session slots and game-facing
    // identity without putting roster mutation in the NetSession facade.
    class SessionRosterSynchronizer final
    {
    public:
        SessionRosterSynchronizer() = delete;

        static void ApplyAccepted(const RosterPacket& roster, ClientReplicaStream& replicaStream,
            ClientLobbyState& clientLobby);
        static void ApplyReceived(const RosterPacket& roster, bool host,
            SessionMatchIdentity& matchIdentity, ClientReplicaStream& replicaStream,
            ClientLobbyState& clientLobby);
    };
}
