#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    class ClientReplicaStream;
    class SessionTrafficMetrics;
    struct IntentPacket;
    struct ReceivedPacket;

    // Receives and validates intent streams, then forwards accepted state to
    // the session replica and the gameplay event owners.
    class RemoteIntentChannel final
    {
    public:
        RemoteIntentChannel() = delete;

        static void ReceiveHostIntent(const ReceivedPacket& packet, double time, std::uint32_t frame);
        static void ReceiveAuthorityIntent(const ReceivedPacket& packet, std::int32_t localSlot,
            std::uint32_t frame, ClientReplicaStream& replicaStream, SessionTrafficMetrics& traffic);
        static void AcceptAuthorityIntent(std::int32_t slot, IntentPacket intent, std::int32_t localSlot,
            std::uint32_t frame, ClientReplicaStream& replicaStream, SessionTrafficMetrics& traffic);
    };
}
