#pragma once

#include "HostPool.hpp"
#include "MatchDefinition.hpp"
#include "NetProtocol.hpp"
#include "SessionProtocol.hpp"

#include "../Multiplayer/MatchWorldProfile.hpp"
#include "../../Entities/Players/PlayerEntity.hpp"
#include "../../NativeRuntime/System/Guid.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <tuple>
#include <vector>

namespace System::Net
{
    class IPEndPoint;
}

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
    class ServerSim;
    struct ReceivedPacket;

    // DedicatedServer.cs and LobbyCommands.cs: one partial class, one header.
    class DedicatedServer final
    {
    private:
        static constexpr double ChatRatePerSecond = 0.5;
        static constexpr double ChatBurst = 3.0;

        class Peer final
        {
        public:
            std::shared_ptr<System::Net::IPEndPoint> EndPoint{};
            std::int32_t SlotIndex = -1;
            double LastSeen = 0.0;
            std::uint32_t LastIntentFrame = 0;
            std::string Name{};
            std::uint8_t Hunter = 0;
            std::uint8_t Color = 0;
            std::int32_t Ping = 0;
            double PingSentAt = 0.0;
            std::uint8_t PingId = 0;
            bool PingPending = false;
            double ChatCredit = ChatBurst;
            double ChatCreditAt = 0.0;
            std::int32_t ChatDropped = 0;
            bool PostMatchReady = false;
            bool LobbyReady = false;
            std::int8_t TeamIndex = -1;
            std::map<std::uint32_t, LobbyCommandResultPacket> Commands{};
            std::deque<std::uint32_t> CommandOrder{};
            std::uint8_t Ballot = 0;
            std::string Pick{};
            std::uint32_t ClientId = 0;
            double LastProposal = -std::numeric_limits<double>::infinity();
        };

        static constexpr double VoteThreshold = 0.70;
        static constexpr double VoteSeconds = 30.0;
        static constexpr double VoteCooldownSeconds = 90.0;
        static constexpr double ProposalCooldownSeconds = 180.0;
        // One: a player alone on a server can vote the map they want, and
        // the vote passes on their own ballot.
        static constexpr std::int32_t VoteMinimumPlayers = 1;
        static constexpr double ReadyWaitSeconds = 30.0;
        static constexpr double AllReadySeconds = 5.0;

    public:
        explicit DedicatedServer(std::int32_t port = NetConfig::DefaultPort,
            std::int32_t maxPlayers = 4,
            std::shared_ptr<MapRotation> rotation = nullptr);

        DedicatedServer(const DedicatedServer&) = delete;
        DedicatedServer(DedicatedServer&&) = delete;
        DedicatedServer& operator=(const DedicatedServer&) = delete;
        DedicatedServer& operator=(DedicatedServer&&) = delete;
        ~DedicatedServer();

        [[nodiscard]] static double EndSequenceSeconds();

        [[nodiscard]] bool AllowMapVotes() const noexcept { return _allowMapVotes; }
        void AllowMapVotes(bool value) noexcept { _allowMapVotes = value; }
        [[nodiscard]] const std::string& ServerName() const noexcept { return _serverName; }
        void ServerName(std::string value) { _serverName = std::move(value); }
        [[nodiscard]] std::int32_t PeerCount() const noexcept { return _peerCount.load(); }
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
        [[nodiscard]] bool ShadowFreeze() const noexcept { return _shadowFreeze; }
        void ShadowFreeze(bool value) noexcept { _shadowFreeze = value; }
        [[nodiscard]] bool AutoUpdate() const noexcept { return _autoUpdate; }
        void AutoUpdate(bool value) noexcept { _autoUpdate = value; }
        [[nodiscard]] bool RunsTheMatch() const noexcept { return _runsTheMatch; }
        void RunsTheMatch(bool value) noexcept { _runsTheMatch = value; }
        [[nodiscard]] HostPool& Hosts() noexcept { return _hosts; }
        [[nodiscard]] bool Simulating() const;

        // LobbyCommands.cs
        [[nodiscard]] ServerSessionPolicy SessionPolicy() const noexcept { return _sessionPolicy; }
        void SessionPolicy(ServerSessionPolicy value) noexcept { _sessionPolicy = value; }
        [[nodiscard]] MatchFormat Format() const noexcept { return _format; }
        void Format(MatchFormat value) noexcept { _format = value; }
        [[nodiscard]] bool RequireReady() const noexcept { return _requireReady; }
        [[nodiscard]] bool AllowJoinInProgress() const noexcept { return _allowJoinInProgress; }
        [[nodiscard]] const NativeRuntime::Guid& OwnerToken() const noexcept { return _ownerToken; }
        void OwnerToken(const NativeRuntime::Guid& value) noexcept { _ownerToken = value; }
        void SetSessionOptions(bool requireReady, bool allowJoinInProgress) noexcept
        {
            _requireReady = requireReady;
            _allowJoinInProgress = allowJoinInProgress;
        }
        [[nodiscard]] bool LockTeams() const noexcept { return _lockTeams; }

        void Run(std::stop_token cancel = {});
        void Stop() noexcept;

    private:
        double _now = 0.0;
        bool _voteRunning = false;
        std::string _voteRoom{};
        GameMode _voteMode;
        std::string _voteProposer{};
        std::int32_t _voteProposerSlot = -1;
        double _voteStartedAt = 0.0;
        double _voteResolvedAt = -std::numeric_limits<double>::infinity();
        std::uint8_t _voteResult = VoteStatePacket::StateIdle;

        bool _allowMapVotes = true;
        std::vector<std::shared_ptr<Peer>> _peers{};
        std::atomic<std::int32_t> _peerCount{0};
        std::array<std::uint8_t, NetConfig::MaxPacketSize> _scratch{};
        const std::int32_t _port;
        const std::int32_t _maxPlayers;
        const std::shared_ptr<MapRotation> _rotation;
        std::unique_ptr<NetTransport> _transport{};
        std::atomic<bool> _listening{false};
        std::atomic<std::int32_t> _boundPort;
        std::shared_ptr<Peer> _authority{};
        std::shared_ptr<ServerSim> _sim{};
        std::shared_ptr<std::vector<std::uint8_t>> _lastSnapshot{};
        std::atomic<bool> _running{false};
        double _matchStarted = 0.0;
        double _matchEndedAt = -1.0;
        std::uint16_t _matchId = 1;
        std::uint64_t _authorityEpoch;
        std::uint32_t _rosterRevision = 0;
        std::array<std::uint16_t, Entities::PlayerEntity::SlotCapacity> _slotGenerations{};
        std::array<std::uint16_t, Entities::PlayerEntity::SlotCapacity> _slotLives{};
        std::uint32_t _snapshotFrame = 0;
        bool _snapshotSeen = false;

        std::string _serverName;
        std::atomic<bool> _everOccupied{false};
        std::shared_ptr<MasterReporter> _reporter{};
        bool _friendlyFire = false;
        bool _affinityWeapons = false;
        bool _shadowFreeze = true;
        bool _autoUpdate = false;
        bool _runsTheMatch = true;
        HostPool _hosts{};

        bool _ballotOpen = false;
        std::vector<std::string> _tallyRooms{};
        std::vector<std::int32_t> _tallyVotes{};

        // LobbyCommands.cs
        ServerSessionPolicy _sessionPolicy = ServerSessionPolicy::Continuous;
        MatchFormat _format = MatchFormat::Auto;
        bool _requireReady = true;
        bool _allowJoinInProgress = true;
        NativeRuntime::Guid _ownerToken{};
        SessionPhase _phase = SessionPhase::InMatch;
        MatchDefinition _lobbyMatch{};
        MatchDefinition _frozenMatch{};
        Multiplayer::MatchWorldProfile _frozenWorldProfile{};
        // Continuous rotation: the world a map was loaded with, from the
        // people connected when it started, kept until the next map. Which
        // pickups exist must not change under a running map.
        Multiplayer::MatchWorldProfile _mapWorldProfile{};
        bool _lockTeams = false;
        std::uint16_t _sessionRevision = 1;
        std::uint32_t _lobbyOwnerClientId = 0;
        std::uint8_t _expectedLoadedSlots = 0;
        std::uint8_t _loadedSlots = 0;
        double _startDeadline = 0;

        [[nodiscard]] double EndSequenceFor() const;
        void Shutdown(std::uint16_t listenPort);
        void EndMatch(double now, const std::string& reason);
        void AdvanceMap(double now);
        void FreezeMapWorldProfile();
        [[nodiscard]] MatchStatePacket BuildState(double now) const;
        void BroadcastMatchState(double now);
        void StartSimulation();
        void SendSnapshot(std::span<const std::uint8_t> payload);
        void SyncSimulationState(double now);
        void Handle(const ReceivedPacket& packet, double now);
        void HandleHitClaim(const ReceivedPacket& packet, double now);
        void SendVerdicts(std::int32_t slot, std::span<const std::pair<std::uint16_t, std::uint8_t>> verdicts);
        void HandleVote(const ReceivedPacket& packet, double now);
        void OpenBallot();
        void CloseBallot();
        void HandleMapPick(const ReceivedPacket& packet, double now);
        [[nodiscard]] std::int32_t VotesFor(const std::string& roomKey) const;
        void Recount();
        void ApplyLeader();
        void BroadcastMapChoices();
        void ReviewPicks();
        void StartVote(const std::shared_ptr<Peer>& peer, const std::string& roomKey, double now);
        void Tally(double now);
        [[nodiscard]] std::tuple<std::int32_t, std::int32_t, std::int32_t, std::int32_t> CountVotes() const;
        void ResolveVote(double now, bool passed, const std::string& count);
        void ReviewVote(double now);
        [[nodiscard]] static std::optional<std::string> ResolveRoomKey(const std::string& roomKey);
        [[nodiscard]] GameMode ModeForRoom(const std::string& roomKey) const;
        void BroadcastVoteState(double now);
        void Tell(const std::shared_ptr<Peer>& peer, const std::string& text);
        void HandleChat(const ReceivedPacket& packet, double now);
        void Announce(const std::string& text);
        void SendStatus(const std::shared_ptr<System::Net::IPEndPoint>& sender, double now);
        void HandleHostRequest(const ReceivedPacket& packet, double now);
        void HandleHello(const ReceivedPacket& packet, double now);
        void HandleMatchEnd(const ReceivedPacket& packet, double now);
        void SendRefusal(const std::shared_ptr<System::Net::IPEndPoint>& to, std::uint8_t reason);
        void HandleIdentify(const ReceivedPacket& packet, double now);
        void NotifyAuthority(const std::shared_ptr<Peer>& peer);
        void PingPeers(double now);
        void HandlePong(const ReceivedPacket& packet, double now);
        [[nodiscard]] RosterPacket BuildRoster();
        void BroadcastRoster();
        void HandleIntent(const ReceivedPacket& packet, double now);
        void HandleSnapshot(const ReceivedPacket& packet, double now);
        void HandleBye(const ReceivedPacket& packet);
        void DropTimedOut(double now);
        void Remove(const std::shared_ptr<Peer>& peer, const std::string& reason);
        [[nodiscard]] std::shared_ptr<Peer> Find(const std::shared_ptr<System::Net::IPEndPoint>& endPoint) const;
        [[nodiscard]] bool SlotFree(std::int32_t slot) const;
        [[nodiscard]] std::int32_t NextFreeSlot() const;
        static void Log(const std::string& message);

        // LobbyCommands.cs
        [[nodiscard]] MatchDefinition CurrentDefinition() const;
        [[nodiscard]] MatchDefinition DefinitionFor(const RotationEntry& entry) const;
        void InvalidateLobbyReady();
        void TouchLobbyRevision(const std::string& reason);
        void SetPhase(SessionPhase phase);
        [[nodiscard]] SessionStatePacket BuildSessionState() const;
        void BroadcastSessionState();
        [[nodiscard]] std::int8_t ChooseTeam(const MatchDefinition& match, const Peer* exclude = nullptr) const;
        void NormalizeTeams();
        void ClaimOwner(const std::shared_ptr<Peer>& peer, std::span<const std::uint8_t> hello);
        void HandleLobbyCommand(const ReceivedPacket& packet, double now);
        [[nodiscard]] LobbyResultCode ExecuteLobbyCommand(const std::shared_ptr<Peer>& peer,
            const LobbyCommandPacket& command, std::string& reason);
        void HandleMatchLoaded(const ReceivedPacket& packet, double now);
        void CheckLoadBarrier(double now);
        void HandleMatchLoadFailed(const ReceivedPacket& packet);
        void ReturnToLobby();
        void LobbyPeerRemoved(const std::shared_ptr<Peer>& peer);
    };
}
