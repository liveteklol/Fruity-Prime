#include "Mods/Network/Server/Core/DedicatedServer.hpp"
#include "Mods/Network/Server/Core/ServerChatChannel.hpp"
#include "Mods/Network/Server/Core/ServerPacketDispatcher.hpp"
#include "Mods/Network/Server/Core/ServerRuntimePump.hpp"

#include "GameState.hpp"
#include "NativeRuntime/System/Console.hpp"
#include "NativeRuntime/System/DateTime.hpp"
#include "NativeRuntime/System/Runtime.hpp"
#include "Mods/Network/Server/Match/MapRotation.hpp"
#include "Mods/Network/Discovery/NetMaster.hpp"
#include "Mods/Network/Transport/NetTransport.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    DedicatedServer::DedicatedServer(std::int32_t port, std::int32_t maxPlayers,
        std::shared_ptr<MapRotation> rotation)
        : _sessionPort(MakeNetSessionDedicatedServerPort()),
          _claimChannel(_peerRegistry, _scratch, _sessionPort.Identity),
          _port(port),
          _maxPlayers(std::clamp(maxPlayers, 2, static_cast<std::int32_t>(ServerSlotIdentity::SlotCapacity))),
          _rotation(rotation != nullptr ? std::move(rotation) : std::make_shared<MapRotation>()),
          _boundPort(port),
          _authorityState(static_cast<std::uint64_t>(Runtime::DateTimeUtcNowTicks())),
          _serverName(Runtime::EnvironmentMachineName()),
          _mapVotingHost(_peerRegistry, _allowMapVotes, _lobbyState, _rotation,
              _transport, _scratch,
              ServerMapVotingHostAdapter::Callbacks{
                  this,
                  +[](void* context) -> MatchDefinition
                  {
                      return static_cast<DedicatedServer*>(context)->CurrentDefinition();
                  },
                  +[](void* context, double now, const std::string&, GameMode)
                  {
                      auto& server = *static_cast<DedicatedServer*>(context);
                      if (server._lobbyState.Policy() == ServerSessionPolicy::Lobby)
                      {
                          server._matchLifecycle.EndMatch(now, "map vote passed");
                      }
                      else
                      {
                          server._matchLifecycle.AdvanceMap(now);
                      }
                  },
                  &DedicatedServer::Log
              }),
          _mapVoting(_mapVotingHost),
          _snapshotChannel(_peerRegistry, _authorityState, _slotIdentity, _snapshotStream,
              _simulation, _lobbyState, _matchId),
          _stateBroadcast(StateBroadcastPorts()),
          _directoryRequests(DirectoryRequestPorts()),
          _intentChannel(_peerRegistry, _authorityState, _slotIdentity, _simulation,
              _sessionPort, _lobbyState, _matchId, _scratch),
          _lobbySession(LobbySessionPorts()),
          _botController(BotControllerPorts()),
          _peerConnection(PeerConnectionPorts()),
          _matchLifecycle(MatchLifecyclePorts())
    {
        _hosts.Log = [](const std::string& message) { Log(message); };
        _hosts.ReporterFactory = [this]() -> std::shared_ptr<MasterReporter>
        {
            return _reporter == nullptr
                ? nullptr
                : std::make_shared<MasterReporter>(_reporter->Host(), _reporter->Port());
        };
    }

    DedicatedServer::~DedicatedServer() = default;

    ServerLobbySession::Ports DedicatedServer::LobbySessionPorts() noexcept
    {
        ServerLobbySession::Ports ports{};
        ports.Context = this;
        ports.Lobby = &_lobbyState;
        ports.Peers = &_peerRegistry;
        ports.Slots = &_slotIdentity;
        ports.Snapshots = &_snapshotStream;
        ports.Simulation = &_simulation;
        ports.State = &_stateBroadcast;
        ports.Voting = &_mapVoting;
        ports.Session = &_sessionPort;
        ports.Rotation = &_rotation;
        ports.Transport = &_transport;
        ports.Scratch = &_scratch;
        ports.MaxPlayers = _maxPlayers;
        ports.MatchId = &_matchId;
        ports.Now = &_now;
        ports.MatchStarted = &_matchStarted;
        ports.MatchEndedAt = &_matchEndedAt;
        ports.CurrentDefinition = +[](void* context) -> MatchDefinition
        {
            return static_cast<DedicatedServer*>(context)->CurrentDefinition();
        };
        ports.DefinitionFor = +[](void* context, const RotationEntry& entry) -> MatchDefinition
        {
            return static_cast<DedicatedServer*>(context)->DefinitionFor(entry);
        };
        ports.StartSimulation = +[](void* context)
        {
            static_cast<DedicatedServer*>(context)->_matchLifecycle.StartSimulation();
        };
        ports.SyncSimulationState = +[](void* context, double now)
        {
            static_cast<DedicatedServer*>(context)->_matchLifecycle.SynchronizeSimulationState(now);
        };
        ports.RemovePeer = +[](void* context, const ServerLobbySession::PeerHandle& peer,
            const std::string& reason)
        {
            static_cast<DedicatedServer*>(context)->_peerConnection.Remove(peer, reason);
        };
        ports.SendRefusal = +[](void* context,
            const std::shared_ptr<System::Net::IPEndPoint>& endpoint, std::uint8_t reason)
        {
            static_cast<DedicatedServer*>(context)->_peerConnection.SendRefusal(endpoint, reason);
        };
        ports.WriteLog = +[](void*, const std::string& message)
        {
            DedicatedServer::Log(message);
        };
        return ports;
    }

    ServerStateBroadcastChannel::Ports DedicatedServer::StateBroadcastPorts() noexcept
    {
        ServerStateBroadcastChannel::Ports ports{};
        ports.Lobby = &_lobbyState;
        ports.Peers = &_peerRegistry;
        ports.Authority = &_authorityState;
        ports.Slots = &_slotIdentity;
        ports.Snapshots = &_snapshotChannel;
        ports.Simulation = &_simulation;
        ports.Session = &_sessionPort;
        ports.Rotation = &_rotation;
        ports.Transport = &_transport;
        ports.Scratch = &_scratch;
        ports.MaxPlayers = _maxPlayers;
        ports.DamageLevel = DamageLevel();
        ports.MatchId = &_matchId;
        ports.Now = &_now;
        ports.MatchStarted = &_matchStarted;
        ports.MatchEndedAt = &_matchEndedAt;
        ports.DefinitionContext = this;
        ports.ReadDefinition = +[](void* context) -> MatchDefinition
        {
            return static_cast<DedicatedServer*>(context)->CurrentDefinition();
        };
        return ports;
    }

    ServerBotController::Ports DedicatedServer::BotControllerPorts() noexcept
    {
        ServerBotController::Ports ports{};
        ports.Peers = &_peerRegistry;
        ports.Slots = &_slotIdentity;
        ports.Lobby = &_lobbyState;
        ports.LobbySession = &_lobbySession;
        ports.Simulation = &_simulation;
        ports.Running = &_running;
        ports.MaxPlayers = _maxPlayers;
        ports.Context = this;
        ports.WriteLog = +[](void*, const std::string& message)
        {
            DedicatedServer::Log(message);
        };
        return ports;
    }

    ServerDirectoryRequestChannel::Ports DedicatedServer::DirectoryRequestPorts() noexcept
    {
        ServerDirectoryRequestChannel::Ports ports{};
        ports.Lobby = &_lobbyState;
        ports.State = &_stateBroadcast;
        ports.Hosts = &_hosts;
        ports.Transport = &_transport;
        ports.Scratch = &_scratch;
        ports.MaxPlayers = _maxPlayers;
        ports.ServerName = &_serverName;
        ports.Context = this;
        ports.CurrentDefinition = +[](void* context) -> MatchDefinition
        {
            return static_cast<DedicatedServer*>(context)->CurrentDefinition();
        };
        ports.WriteLog = +[](void*, const std::string& message)
        {
            DedicatedServer::Log(message);
        };
        return ports;
    }

    ServerPeerConnectionChannel::Ports DedicatedServer::PeerConnectionPorts() noexcept
    {
        ServerPeerConnectionChannel::Ports ports{};
        ports.Peers = &_peerRegistry;
        ports.Slots = &_slotIdentity;
        ports.Authority = &_authorityState;
        ports.Snapshots = &_snapshotStream;
        ports.SnapshotChannel = &_snapshotChannel;
        ports.Lobby = &_lobbyState;
        ports.Voting = &_mapVoting;
        ports.LobbySession = &_lobbySession;
        ports.State = &_stateBroadcast;
        ports.Simulation = &_simulation;
        ports.Transport = &_transport;
        ports.Scratch = &_scratch;
        ports.EverOccupied = &_everOccupied;
        ports.MaxPlayers = _maxPlayers;
        ports.RunsTheMatch = &_runsTheMatch;
        ports.MatchId = &_matchId;
        ports.Now = &_now;
        ports.MatchStarted = &_matchStarted;
        ports.MatchEndedAt = &_matchEndedAt;
        ports.Context = this;
        ports.CurrentDefinition = +[](void* context) -> MatchDefinition
        {
            return static_cast<DedicatedServer*>(context)->CurrentDefinition();
        };
        ports.WriteLog = +[](void*, const std::string& message)
        {
            DedicatedServer::Log(message);
        };
        return ports;
    }

    ServerMatchLifecycle::Ports DedicatedServer::MatchLifecyclePorts() noexcept
    {
        ServerMatchLifecycle::Ports ports{};
        ports.Lobby = &_lobbyState;
        ports.Authority = &_authorityState;
        ports.Peers = &_peerRegistry;
        ports.Slots = &_slotIdentity;
        ports.SnapshotStream = &_snapshotStream;
        ports.SnapshotChannel = &_snapshotChannel;
        ports.Voting = &_mapVoting;
        ports.LobbySession = &_lobbySession;
        ports.State = &_stateBroadcast;
        ports.Simulation = &_simulation;
        ports.Claims = &_claimChannel;
        ports.Session = &_sessionPort;
        ports.Rotation = &_rotation;
        ports.Transport = &_transport;
        ports.Scratch = &_scratch;
        ports.MaxPlayers = _maxPlayers;
        ports.RunsTheMatch = &_runsTheMatch;
        ports.MatchId = &_matchId;
        ports.Now = &_now;
        ports.MatchStarted = &_matchStarted;
        ports.MatchEndedAt = &_matchEndedAt;
        ports.EndSequenceSeconds = ServerMatchLifecycle::EndSequenceSeconds();
        ports.Context = this;
        ports.CurrentDefinition = +[](void* context) -> MatchDefinition
        {
            return static_cast<DedicatedServer*>(context)->CurrentDefinition();
        };
        ports.WriteLog = +[](void*, const std::string& message)
        {
            DedicatedServer::Log(message);
        };
        return ports;
    }

    void DedicatedServer::Bots(std::int32_t count, std::int32_t level)
    {
        _botController.RequestConfiguration(count, level);
    }

    bool DedicatedServer::Simulating() const
    {
        return _matchLifecycle.Simulating();
    }

    void DedicatedServer::Run(std::stop_token cancel)
    {
        _transport = std::make_unique<NetTransport>(_port);
        _boundPort.store(_transport->LocalPort());
        _running.store(true);
        _listening.store(true);
        Log("listening on UDP " + std::to_string(_transport->LocalPort()) + ", up to "
            + std::to_string(_maxPlayers) + " players");
        _lobbyState.SetLobbyMatch(DefinitionFor(*_rotation->Current()));
        _lobbyState.SetPhase(_lobbyState.Policy() == ServerSessionPolicy::Lobby
            ? SessionPhase::Lobby : SessionPhase::InMatch);
        const std::uint16_t listenPort = static_cast<std::uint16_t>(_transport->LocalPort());
        try
        {
            if (_lobbyState.Phase() == SessionPhase::InMatch)
            {
                _matchLifecycle.StartSimulation();
            }
            Log(Simulating()
                ? "this server runs the match itself"
                : "hosted game: the first client to connect runs the match");
            Log("rotation: " + std::to_string(_rotation->Entries().size()) + " map(s), starting on "
                + _rotation->Current()->ToString());
            Log(_hosts.Describe());
            ServerRuntimePump::Ports ports{
                _running,
                *_transport,
                _botController,
                _lobbyState,
                _peerConnection,
                _lobbySession,
                _simulation,
                _botIntentRelay,
                _sessionPort,
                _peerRegistry,
                _matchLifecycle,
                _stateBroadcast,
                _mapVoting,
                _authorityState,
                _hosts,
                _reporter,
                _serverName,
                _autoUpdate,
                _runsTheMatch,
                _maxPlayers,
                listenPort,
                _now,
                _matchStarted,
                _matchEndedAt,
                this,
                +[](void* context, const ReceivedPacket& packet, double now)
                {
                    static_cast<DedicatedServer*>(context)->Handle(packet, now);
                },
                +[](void* context) -> MatchDefinition
                {
                    return static_cast<DedicatedServer*>(context)->CurrentDefinition();
                },
                +[](void*, const std::string& message)
                {
                    DedicatedServer::Log(message);
                }
            };
            ServerRuntimePump::Run(ports, cancel);
        }
        catch (...)
        {
            Shutdown(listenPort);
            throw;
        }
        Shutdown(listenPort);
    }

    void DedicatedServer::Shutdown(std::uint16_t listenPort)
    {
        Log("shutting down");
        _claimChannel.Stop();
        _hosts.StopAll("the server is shutting down");
        _running.store(false);
        try
        {
            if (_reporter != nullptr)
            {
                _reporter->Farewell(listenPort);
            }
        }
        catch (...)
        {
        }
        if (_reporter != nullptr)
        {
            _reporter->Dispose();
        }
        _reporter.reset();
        if (_transport != nullptr)
        {
            _transport->Dispose();
        }
        _transport.reset();
        _boundPort.store(_port);
        _listening.store(false);
        _simulation.Stop();
    }

    void DedicatedServer::Stop() noexcept
    {
        _running.store(false);
    }

    void DedicatedServer::Handle(const ReceivedPacket& packet, double now)
    {
        ServerPacketDispatcher::Dispatch(*this, packet, now);
    }

    void DedicatedServer::HandleLobbyCommand(const ReceivedPacket& packet, double now)
    {
        _lobbySession.HandleCommand(packet, now);
    }

    void DedicatedServer::HandleMatchLoaded(const ReceivedPacket& packet, double now)
    {
        _lobbySession.HandleMatchLoaded(packet, now);
    }

    void DedicatedServer::HandleMatchLoadFailed(const ReceivedPacket& packet)
    {
        _lobbySession.HandleMatchLoadFailed(packet);
    }

    void DedicatedServer::HandleHello(const ReceivedPacket& packet, double now)
    {
        _peerConnection.HandleHello(packet, now);
    }

    void DedicatedServer::HandleIntent(const ReceivedPacket& packet, double now)
    {
        _intentChannel.Receive(packet, now, _transport.get());
    }

    void DedicatedServer::HandleSnapshot(const ReceivedPacket& packet, double now)
    {
        _snapshotChannel.Receive(packet, now, _transport.get());
    }

    void DedicatedServer::HandleBye(const ReceivedPacket& packet)
    {
        _peerConnection.HandleBye(packet);
    }

    void DedicatedServer::HandleIdentify(const ReceivedPacket& packet, double now)
    {
        _peerConnection.HandleIdentify(packet, now);
    }

    void DedicatedServer::HandlePing(const ReceivedPacket& packet)
    {
        _peerConnection.HandlePing(packet);
    }

    void DedicatedServer::HandlePong(const ReceivedPacket& packet, double now)
    {
        _peerConnection.HandlePong(packet, now);
    }

    void DedicatedServer::HandleHostRequest(const ReceivedPacket& packet, double now)
    {
        _directoryRequests.HandleHostRequest(packet, now);
    }

    void DedicatedServer::HandleStatusQuery(const ReceivedPacket& packet, double now)
    {
        _directoryRequests.SendStatus(packet.Sender, now);
    }

    void DedicatedServer::HandleMatchEnd(const ReceivedPacket& packet, double now)
    {
        _matchLifecycle.HandleMatchEnd(packet, now);
    }

    void DedicatedServer::HandleVote(const ReceivedPacket& packet, double now)
    {
        _mapVoting.HandleVote(packet.Sender, packet.Payload(), now);
    }

    void DedicatedServer::HandleMapPick(const ReceivedPacket& packet, double now)
    {
        _mapVoting.HandleMapPick(packet.Sender, packet.Payload(), now);
    }

    void DedicatedServer::HandleHitClaim(const ReceivedPacket& packet, double now)
    {
        _claimChannel.ReceiveHitClaim(packet, now,
            _lobbyState.Phase() == SessionPhase::InMatch, Simulating());
    }

    void DedicatedServer::HandlePickupClaim(const ReceivedPacket& packet, double now)
    {
        _claimChannel.ReceivePickupClaim(packet, now,
            _lobbyState.Phase() == SessionPhase::InMatch, Simulating());
    }

    void DedicatedServer::HandleChat(const ReceivedPacket& packet, double now)
    {
        const bool teamMode = GameState::IsTeamMode(CurrentDefinition().Mode);
        ServerChatChannel::Receive(packet, now, _peerRegistry, _transport.get(), _scratch,
            teamMode, &DedicatedServer::Log);
    }

    void DedicatedServer::Log(const std::string& message)
    {
        Runtime::ConsoleWriteLine("[" + Runtime::DateTimeToString(Runtime::DateTimeNow(), "HH:mm:ss")
            + "] [server] " + message);
    }
}
