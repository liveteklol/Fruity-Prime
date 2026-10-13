#pragma once

#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Protocol/Packets/PacketTypes.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"

namespace MphRead::Mods::Network
{
    // Handles peer departure and timeout cleanup for a session's host role.
    class SessionPeerMaintenance final
    {
    public:
        SessionPeerMaintenance() = delete;

        struct Ports final
        {
            NetRole role;
            void (*broadcastHostControl)();
            void (*stopSession)();
        };

        static void HandleBye(ReceivedPacket packet, const Ports& ports);
        static void DropTimedOut(double time, const Ports& ports);
    };
}
