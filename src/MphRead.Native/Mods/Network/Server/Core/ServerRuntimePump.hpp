#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>

namespace MphRead::Mods::Network
{
    class DedicatedServerSessionPort;
    class HostPool;
    class MasterReporter;
    class NetTransport;
    class ServerAuthorityState;
    class ServerBotController;
    class ServerBotIntentRelay;
    class ServerLobbySession;
    class ServerLobbyState;
    class ServerMapVoting;
    class ServerMatchLifecycle;
    class ServerPeerConnectionChannel;
    class ServerPeerRegistry;
    class ServerSimulationController;
    class ServerStateBroadcastChannel;
    struct ReceivedPacket;

    // Owns the dedicated server's periodic work ordering and pacing.
    class ServerRuntimePump final
    {
    public:
        ServerRuntimePump() = delete;

        struct Ports final
        {
            std::atomic<bool>& running;
            NetTransport& transport;
            ServerBotController& bots;
            ServerLobbyState& lobby;
            ServerPeerConnectionChannel& peerConnection;
            ServerLobbySession& lobbySession;
            ServerSimulationController& simulation;
            ServerBotIntentRelay& botIntentRelay;
            DedicatedServerSessionPort& session;
            ServerPeerRegistry& peers;
            ServerMatchLifecycle& matchLifecycle;
            ServerStateBroadcastChannel& stateBroadcast;
            ServerMapVoting& mapVoting;
            ServerAuthorityState& authority;
            HostPool& hosts;
            std::shared_ptr<MasterReporter>& reporter;
            std::string& serverName;
            const bool& autoUpdate;
            const bool& runsTheMatch;
            std::int32_t maxPlayers;
            std::uint16_t listenPort;
            double& now;
            double& matchStarted;
            double& matchEndedAt;
            void* context;
            void (*handlePacket)(void*, const ReceivedPacket&, double);
            MatchDefinition (*currentDefinition)(void*);
            void (*writeLog)(void*, const std::string&);
        };

        static void Run(Ports& ports, std::stop_token cancel);
    };
}
