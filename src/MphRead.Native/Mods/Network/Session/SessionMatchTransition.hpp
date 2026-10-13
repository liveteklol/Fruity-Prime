#pragma once

#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    class ClientConnectionHealth;
    class ClientReplicaStream;
    class RemotePlayerChannel;
    class SessionAuthorityState;
    class SessionMatchIdentity;
    class SessionRoster;
    class SessionRuntimeState;

    // Applies accepted match identity changes to the session and connected game-state owners.
    class SessionMatchTransition final
    {
    public:
        using HelloSender = void (*)();

        SessionMatchTransition() = delete;

        static void Apply(const MatchStatePacket& state, bool rotated,
            SessionRuntimeState& runtime, SessionMatchIdentity& matchIdentity,
            ClientReplicaStream& replicaStream, RemotePlayerChannel& remotePlayers,
            SessionAuthorityState& authorityState, ClientConnectionHealth& connectionHealth,
            SessionRoster& roster, HelloSender sendHello);
        [[nodiscard]] static bool MatchesStream(std::uint16_t match, std::uint64_t epoch,
            bool host, const SessionMatchIdentity& matchIdentity);
    };
}
