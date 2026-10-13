#pragma once

#include "Mods/Network/Session/ClientConnectionHealth.hpp"
#include "Mods/Network/Session/SessionAuthorityState.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Advances network frames, dispatches received packets and maintains liveness.
    class SessionRuntimePump final
    {
    public:
        SessionRuntimePump() = delete;

        struct Ports final
        {
            SessionRuntimeState& runtime;
            SessionConnection& connection;
            SessionAuthorityState& authority;
            ClientConnectionHealth& connectionHealth;
            bool playbackActive;
            double silenceBeforeRejoin;
            void (*recordPacket)(ReceivedPacket packet);
            void (*handlePacket)(ReceivedPacket packet, double time);
            void (*pumpLobby)(double time);
            void (*dropTimedOutPeers)(double time);
            void (*broadcastHostControl)();
            void (*sendHello)();
            void (*sendIdentify)();
            bool (*identityNeedsRefresh)();
            double (*clock)();
        };

        struct MapTransferPorts final
        {
            SessionConnection& connection;
            void (*handlePacket)(ReceivedPacket packet, double time);
            double (*clock)();
        };

        static void Update(Ports& ports, double time);
        static void PumpMapTransfer(MapTransferPorts& ports);
    };
}
