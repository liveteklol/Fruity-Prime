#pragma once

#include "Mods/Network/Protocol/Packets/RealtimePackets.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Session/SessionTrafficMetrics.hpp"

namespace MphRead::Mods::Network
{
    // Owns client intent metadata, wire encoding, and ordered claim transmission.
    class SessionIntentPublisher final
    {
    public:
        SessionIntentPublisher() = delete;

        static void Send(IntentPacket intent, bool gameplayFrozen,
            SessionConnection& connection, const SessionRuntimeState& runtime,
            const SessionMatchIdentity& matchIdentity, SessionTrafficMetrics& traffic);
    };
}
