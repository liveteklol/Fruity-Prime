#pragma once

#include "Mods/Network/Session/ClientConnectionHealth.hpp"
#include "Mods/Network/Session/ClientLobbyState.hpp"
#include "Mods/Network/Session/ClientReplicaStream.hpp"
#include "Mods/Network/Session/SessionAuthorityState.hpp"
#include "Mods/Network/Session/SessionConnection.hpp"
#include "Mods/Network/Session/SessionMatchIdentity.hpp"
#include "Mods/Network/Session/SessionPlayerIdentity.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"
#include "Mods/Network/Session/SessionRuntimeState.hpp"
#include "Mods/Network/Session/SessionSimulationCallbacks.hpp"
#include "Mods/Network/Session/SessionTrafficMetrics.hpp"
#include "NativeRuntime/System/Guid.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace MphRead::Mods::Network
{
    // Owns transport and role lifecycle transitions behind the NetSession facade.
    class SessionLifecycle final
    {
    public:
        struct Ports final
        {
            SessionRuntimeState& runtime;
            SessionConnection& connection;
            SessionSimulationCallbacks& simulationCallbacks;
            SessionAuthorityState& authorityState;
            ClientLobbyState& clientLobby;
            ClientConnectionHealth& connectionHealth;
            SessionMatchIdentity& matchIdentity;
            SessionPlayerIdentity& playerIdentity;
            ClientReplicaStream& replicaStream;
            SessionTrafficMetrics& traffic;
            std::array<std::uint8_t, 4> (*resolveIPv4)(const std::string& address);
            double (*clock)();
            void (*sendHello)();
            void (*sendIdentify)();
            void (*resetLobbySession)();
        };

        static void StartServerAuthority(Ports& ports, SnapshotSink sink,
            std::function<void()> matchEnded);
        static void StartHost(Ports& ports, std::int32_t port);
        static void StartClient(Ports& ports, const std::string& address, std::int32_t port,
            NativeRuntime::Guid ownerToken);
        static void StartPlayback(Ports& ports);
        static void RebindSocket(Ports& ports);
        static void Stop(Ports& ports);
    };
}
