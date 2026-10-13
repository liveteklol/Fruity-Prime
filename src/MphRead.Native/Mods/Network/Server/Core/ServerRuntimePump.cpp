#include "Mods/Network/Server/Core/ServerRuntimePump.hpp"

#include "Mods/Network/Discovery/NetMaster.hpp"
#include "Mods/Network/Server/Core/HostPool.hpp"
#include "Mods/Network/Server/Core/ServerAuthorityState.hpp"
#include "Mods/Network/Server/Core/ServerPeerConnectionChannel.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Core/ServerStateBroadcastChannel.hpp"
#include "Mods/Network/Server/Diagnostics/ServerSimulationDiagnostics.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbySession.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Lobby/ServerMapVoting.hpp"
#include "Mods/Network/Server/Match/ServerBotIntentRelay.hpp"
#include "Mods/Network/Server/Match/ServerBotController.hpp"
#include "Mods/Network/Server/Match/ServerMatchLifecycle.hpp"
#include "Mods/Network/Server/Match/ServerSimulationController.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"
#include "Mods/Update/ServerUpdate.hpp"

#include "NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    void ServerRuntimePump::Run(Ports& ports, std::stop_token cancel)
    {
        const auto clock = std::chrono::steady_clock::now();
        double lastReport = 0;
        double lastStateBroadcast = 0;
        ports.matchStarted = 0;

        while (ports.running.load() && !cancel.stop_requested())
        {
            const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - clock).count();
            ports.now = now;
            ports.bots.ApplyPendingConfiguration();
            for (ReceivedPacket packet : ports.transport.Drain())
            {
                ports.handlePacket(ports.context, packet, now);
            }
            ports.peerConnection.DropTimedOut(now);
            ports.lobbySession.CheckLoadBarrier(now);
            if ((ports.lobby.Phase() == SessionPhase::InMatch
                || ports.lobby.Phase() == SessionPhase::PostMatch)
                && ports.matchLifecycle.Simulating())
            {
                ports.simulation.Advance(now);
                ports.botIntentRelay.RelayFrame(ports.session.Clock.ReadFrame(),
                    ports.session.Identity.ReadMatchId(), ports.session.Identity.ReadAuthorityEpoch(),
                    ports.maxPlayers, &ports.transport, ports.peers);
            }

            const float limit = ports.currentDefinition(ports.context).TimeLimitSeconds;
            if (ports.lobby.Phase() == SessionPhase::InMatch && ports.matchEndedAt < 0 && limit > 0
                && !ports.peers.All().empty() && now - ports.matchStarted >= limit)
            {
                ports.matchLifecycle.EndMatch(now, "time limit");
            }
            else if (ports.matchEndedAt >= 0
                && now - ports.matchEndedAt >= ports.matchLifecycle.EndSequenceFor())
            {
                if (ports.lobby.Policy() == ServerSessionPolicy::Lobby)
                {
                    ports.lobbySession.ReturnToLobby();
                }
                else
                {
                    ports.matchLifecycle.AdvanceMap(now);
                }
            }

            if (now - lastStateBroadcast >= 1.0)
            {
                lastStateBroadcast = now;
                ports.peerConnection.PingPeers(now);
                ports.stateBroadcast.BroadcastSessionState();
                if (ports.lobby.Phase() == SessionPhase::InMatch
                    || ports.lobby.Phase() == SessionPhase::PostMatch)
                {
                    ports.stateBroadcast.BroadcastMatchState(now);
                }
                ports.stateBroadcast.BroadcastRoster();
                ports.mapVoting.Update(now);
                if (ports.mapVoting.BallotOpen())
                {
                    ports.mapVoting.BroadcastMapChoices();
                }
                if (ports.authority.HasAuthority() && !ports.runsTheMatch)
                {
                    ports.stateBroadcast.NotifyAuthority(ports.authority.Current());
                }
                if (ports.reporter != nullptr)
                {
                    const MatchDefinition definition = ports.currentDefinition(ports.context);
                    ports.reporter->Beat(now, ports.serverName, ports.listenPort,
                        static_cast<std::uint8_t>(ports.peers.All().size()),
                        static_cast<std::uint8_t>(ports.maxPlayers),
                        static_cast<std::uint8_t>(definition.Mode), definition.RoomKey.value_or(""));
                }
            }

            ports.hosts.Reap(now);
            if (ports.autoUpdate && Update::ServerUpdate::ShouldRestart(
                static_cast<std::int32_t>(ports.peers.All().size()) + ports.hosts.Count()))
            {
                ports.writeLog(ports.context, "shutting down to come back on the new build");
                ports.running.store(false);
                break;
            }
            if (now - lastReport >= 30)
            {
                lastReport = now;
                std::string line = std::to_string(ports.peers.All().size()) + " peer(s) connected"
                    + (ports.matchLifecycle.Simulating() ? std::string(", authority = this server")
                        : ports.authority.HasAuthority() ? ", authority = slot "
                            + std::to_string(ports.authority.Current()->SlotIndex)
                        : std::string(", no authority"))
                    + ", map " + ports.currentDefinition(ports.context).RoomKey.value_or("");
                const float timeLimit = ports.currentDefinition(ports.context).TimeLimitSeconds;
                if (timeLimit > 0)
                {
                    line += ", " + Runtime::ToString(std::max(0.0,
                        static_cast<double>(timeLimit) - (now - ports.matchStarted)), "0") + " s left";
                }
                const NetTransportQueueStats queueStats = ports.transport.QueueStats();
                const bool hasQueueDrops = queueStats.Inbox.ExistingSupersededDrops > 0
                    || queueStats.Inbox.IncomingSupersededDrops > 0
                    || queueStats.Inbox.CriticalOverflowDrops > 0
                    || queueStats.HeldInput.LossDrops > 0
                    || queueStats.HeldInput.CapacityDrops > 0
                    || queueStats.HeldOutput.LossDrops > 0
                    || queueStats.HeldOutput.CapacityDrops > 0;
                if (hasQueueDrops)
                {
                    line += ", inbox=" + std::to_string(queueStats.Inbox.Depth)
                        + "/" + std::to_string(queueStats.Inbox.Capacity)
                        + " dropped(existing-superseded="
                        + std::to_string(queueStats.Inbox.ExistingSupersededDrops)
                        + ", incoming-superseded="
                        + std::to_string(queueStats.Inbox.IncomingSupersededDrops)
                        + ", critical-overflow="
                        + std::to_string(queueStats.Inbox.CriticalOverflowDrops) + ")"
                        + ", held-rx=" + std::to_string(queueStats.HeldInput.Depth)
                        + "/" + std::to_string(queueStats.HeldInput.Capacity)
                        + " dropped(loss=" + std::to_string(queueStats.HeldInput.LossDrops)
                        + ", capacity=" + std::to_string(queueStats.HeldInput.CapacityDrops) + ")"
                        + ", held-tx=" + std::to_string(queueStats.HeldOutput.Depth)
                        + "/" + std::to_string(queueStats.HeldOutput.Capacity)
                        + " dropped(loss=" + std::to_string(queueStats.HeldOutput.LossDrops)
                        + ", capacity=" + std::to_string(queueStats.HeldOutput.CapacityDrops) + ")";
                }
                ports.writeLog(ports.context, line);
                if (ports.matchLifecycle.Simulating())
                {
                    for (const std::string& detail : ServerSimulationDiagnostics::Describe(ports.simulation))
                    {
                        ports.writeLog(ports.context, "sim: " + detail);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(
                ports.peers.All().empty() && !ports.matchLifecycle.Simulating() ? 20 : 1));
        }
    }
}
