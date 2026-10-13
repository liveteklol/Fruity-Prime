#pragma once

#include "Mods/Network/Server/Core/HostPool.hpp"
#include "Mods/Network/Server/Core/ServerAuthorityState.hpp"
#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Protocol/NetProtocol.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"
#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Server/Lobby/ServerMapVoting.hpp"
#include "Mods/Network/Server/Match/ServerBotIntentRelay.hpp"
#include "Mods/Network/Server/Match/ServerBotController.hpp"
#include "Mods/Network/Server/Match/ServerMatchLifecycle.hpp"
#include "Mods/Network/Server/Match/ServerIntentChannel.hpp"
#include "Mods/Network/Server/Core/ServerStateBroadcastChannel.hpp"
#include "Mods/Network/Server/Core/ServerPeerConnectionChannel.hpp"
#include "Mods/Network/Server/Core/ServerDirectoryRequestChannel.hpp"
#include "Mods/Network/Server/Match/ServerSnapshotChannel.hpp"
#include "Mods/Network/Server/Match/ServerSnapshotStream.hpp"
#include "Mods/Network/Server/Match/ServerSimulationController.hpp"
#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"
#include "Mods/Network/Server/Core/ServerClaimChannel.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"
#include "Mods/Network/Server/Lobby/ServerMapVotingHostAdapter.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbySession.hpp"
#include "Mods/Network/Session/SessionPorts.hpp"

#include "Mods/Multiplayer/MatchWorldProfile.hpp"
#include "NativeRuntime/System/Guid.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>

namespace MphRead
{
    enum class GameMode : std::uint8_t;
}

namespace MphRead::Mods::Network
{
    class MapRotation;
    class MasterReporter;
    class NetTransport;
    class RotationEntry;
    class ServerPacketDispatcher;
    struct ReceivedPacket;

    // DedicatedServer.cs and LobbyCommands.cs: one partial class, one header.
    class DedicatedServer final
    {
    private:
        friend class ServerPacketDispatcher;

        using Peer = ServerPeerRegistry::Peer;

    public:
        explicit DedicatedServer(std::int32_t port = NetConfig::DefaultPort,
            std::int32_t maxPlayers = 4,
            std::shared_ptr<MapRotation> rotation = nullptr);

        DedicatedServer(const DedicatedServer&) = delete;
        DedicatedServer(DedicatedServer&&) = delete;
        DedicatedServer& operator=(const DedicatedServer&) = delete;
        DedicatedServer& operator=(DedicatedServer&&) = delete;
        ~DedicatedServer();

        [[nodiscard]] bool AllowMapVotes() const noexcept { return _allowMapVotes; }
        void AllowMapVotes(bool value) noexcept { _allowMapVotes = value; }
        [[nodiscard]] const std::string& ServerName() const noexcept { return _serverName; }
        void ServerName(std::string value) { _serverName = std::move(value); }
        [[nodiscard]] std::int32_t PeerCount() const noexcept { return _peerRegistry.Count(); }
        [[nodiscard]] bool EverOccupied() const noexcept { return _everOccupied.load(); }
        [[nodiscard]] bool Listening() const noexcept { return _listening.load(); }
        [[nodiscard]] std::int32_t BoundPort() const noexcept { return _boundPort.load(); }
        [[nodiscard]] std::shared_ptr<MasterReporter> Reporter() const noexcept { return _reporter; }
        void Reporter(std::shared_ptr<MasterReporter> value) noexcept { _reporter = std::move(value); }
        [[nodiscard]] bool FriendlyFire() const noexcept { return _friendlyFire; }
        void FriendlyFire(bool value) noexcept { _friendlyFire = value; }
        [[nodiscard]] static std::int32_t DamageLevel() noexcept { return 1; }
        [[nodiscard]] bool AffinityWeapons() const noexcept { return _affinityWeapons; }
        void AffinityWeapons(bool value) noexcept { _affinityWeapons = value; }
        // AI players run by this server in the top `count` slots (ServerBots).
        void Bots(std::int32_t count, std::int32_t level);
        [[nodiscard]] bool ShadowFreeze() const noexcept { return _shadowFreeze; }
        void ShadowFreeze(bool value) noexcept { _shadowFreeze = value; }
        [[nodiscard]] bool AutoUpdate() const noexcept { return _autoUpdate; }
        void AutoUpdate(bool value) noexcept { _autoUpdate = value; }
        [[nodiscard]] bool RunsTheMatch() const noexcept { return _runsTheMatch; }
        void RunsTheMatch(bool value) noexcept { _runsTheMatch = value; }
        [[nodiscard]] HostPool& Hosts() noexcept { return _hosts; }
        [[nodiscard]] bool Simulating() const;

        // Server match-definition projection.
        [[nodiscard]] ServerSessionPolicy SessionPolicy() const noexcept { return _lobbyState.Policy(); }
        void SessionPolicy(ServerSessionPolicy value) noexcept { _lobbyState.SetPolicy(value); }
        [[nodiscard]] MatchFormat Format() const noexcept { return _lobbyState.Format(); }
        void Format(MatchFormat value) noexcept { _lobbyState.SetFormat(value); }
        [[nodiscard]] bool RequireReady() const noexcept { return _lobbyState.RequireReady(); }
        [[nodiscard]] bool AllowJoinInProgress() const noexcept { return _lobbyState.AllowJoinInProgress(); }
        [[nodiscard]] const NativeRuntime::Guid& OwnerToken() const noexcept { return _lobbyState.OwnerToken(); }
        void OwnerToken(const NativeRuntime::Guid& value) noexcept { _lobbyState.SetOwnerToken(value); }
        void SetSessionOptions(bool requireReady, bool allowJoinInProgress) noexcept
        {
            _lobbyState.SetSessionOptions(requireReady, allowJoinInProgress);
        }
        [[nodiscard]] bool LockTeams() const noexcept { return _lobbyState.LockTeams(); }

        void Run(std::stop_token cancel = {});
        void Stop() noexcept;

    private:
        double _now = 0.0;
        bool _allowMapVotes = true;
        ServerPeerRegistry _peerRegistry{};
        std::array<std::uint8_t, NetConfig::MaxPacketSize> _scratch{};
        DedicatedServerSessionPort _sessionPort{};
        ServerClaimChannel _claimChannel;
        ServerBotIntentRelay _botIntentRelay{};
        ServerSlotIdentity _slotIdentity{};
        const std::int32_t _port;
        const std::int32_t _maxPlayers;
        const std::shared_ptr<MapRotation> _rotation;
        std::unique_ptr<NetTransport> _transport{};
        std::atomic<bool> _listening{false};
        std::atomic<std::int32_t> _boundPort;
        ServerAuthorityState _authorityState{};
        ServerSimulationController _simulation{};
        ServerSnapshotStream _snapshotStream{};
        std::atomic<bool> _running{false};
        double _matchStarted = 0.0;
        double _matchEndedAt = -1.0;
        std::uint16_t _matchId = 1;

        std::string _serverName;
        std::atomic<bool> _everOccupied{false};
        std::shared_ptr<MasterReporter> _reporter{};
        bool _friendlyFire = false;
        bool _affinityWeapons = false;
        bool _shadowFreeze = true;
        bool _autoUpdate = false;
        bool _runsTheMatch = true;
        HostPool _hosts{};

        ServerLobbyState _lobbyState{};
        ServerMapVotingHostAdapter _mapVotingHost;
        ServerMapVoting _mapVoting;

        void Shutdown(std::uint16_t listenPort);
        void Handle(const ReceivedPacket& packet, double now);
        void HandleLobbyCommand(const ReceivedPacket& packet, double now);
        void HandleMatchLoaded(const ReceivedPacket& packet, double now);
        void HandleMatchLoadFailed(const ReceivedPacket& packet);
        void HandleHello(const ReceivedPacket& packet, double now);
        void HandleIntent(const ReceivedPacket& packet, double now);
        void HandleSnapshot(const ReceivedPacket& packet, double now);
        void HandleBye(const ReceivedPacket& packet);
        void HandleIdentify(const ReceivedPacket& packet, double now);
        void HandlePing(const ReceivedPacket& packet);
        void HandlePong(const ReceivedPacket& packet, double now);
        void HandleHostRequest(const ReceivedPacket& packet, double now);
        void HandleStatusQuery(const ReceivedPacket& packet, double now);
        void HandleMatchEnd(const ReceivedPacket& packet, double now);
        void HandleVote(const ReceivedPacket& packet, double now);
        void HandleMapPick(const ReceivedPacket& packet, double now);
        void HandleHitClaim(const ReceivedPacket& packet, double now);
        void HandlePickupClaim(const ReceivedPacket& packet, double now);
        void HandleChat(const ReceivedPacket& packet, double now);
        static void Log(const std::string& message);

        // LobbyCommands.cs
        [[nodiscard]] MatchDefinition CurrentDefinition() const;
        [[nodiscard]] MatchDefinition DefinitionFor(const RotationEntry& entry) const;

        ServerSnapshotChannel _snapshotChannel;
        ServerStateBroadcastChannel _stateBroadcast;
        ServerDirectoryRequestChannel _directoryRequests;
        ServerIntentChannel _intentChannel;
        ServerLobbySession _lobbySession;
        ServerBotController _botController;
        ServerPeerConnectionChannel _peerConnection;
        ServerMatchLifecycle _matchLifecycle;
        [[nodiscard]] ServerLobbySession::Ports LobbySessionPorts() noexcept;
        [[nodiscard]] ServerStateBroadcastChannel::Ports StateBroadcastPorts() noexcept;
        [[nodiscard]] ServerDirectoryRequestChannel::Ports DirectoryRequestPorts() noexcept;
        [[nodiscard]] ServerBotController::Ports BotControllerPorts() noexcept;
        [[nodiscard]] ServerPeerConnectionChannel::Ports PeerConnectionPorts() noexcept;
        [[nodiscard]] ServerMatchLifecycle::Ports MatchLifecyclePorts() noexcept;
    };
}
