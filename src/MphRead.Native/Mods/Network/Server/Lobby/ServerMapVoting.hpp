#pragma once

#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"
#include "Mods/Network/Domain/MatchDefinition.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace MphRead
{
    enum class GameMode : std::uint8_t;
}

namespace MphRead::Mods::Network
{
    class DedicatedServer;

    // Narrow services the map-vote policy needs from the server loop.
    class ServerMapVotingHost
    {
    public:
        virtual ~ServerMapVotingHost() = default;

        [[nodiscard]] virtual ServerPeerRegistry& VotingPeers() noexcept = 0;
        [[nodiscard]] virtual bool MapVotesAllowed() const noexcept = 0;
        [[nodiscard]] virtual SessionPhase VotingPhase() const noexcept = 0;
        [[nodiscard]] virtual MatchDefinition VotingDefinition() const = 0;
        [[nodiscard]] virtual GameMode VotingModeForRoom(const std::string& roomKey) const = 0;
        virtual void SetPendingMapPick(const std::string& roomKey, GameMode mode) = 0;
        virtual void ClearPendingMapPick() = 0;
        virtual void MarkVotingPeerSeen(const ServerPeerRegistry::PeerHandle& peer, double now) noexcept = 0;
        virtual void TellVotingPeer(const ServerPeerRegistry::PeerHandle& peer, const std::string& text) = 0;
        virtual void AnnounceVote(const std::string& text) = 0;
        virtual void LogVote(const std::string& text) = 0;
        virtual void SendVotePacket(const ServerPeerRegistry::PeerHandle& peer, PacketType type,
            std::span<const std::uint8_t> payload) = 0;
        virtual void ApplyAcceptedMapVote(double now, const std::string& roomKey, GameMode mode) = 0;
    };

    // Owns map-pick ballots and map-change proposals for one dedicated server.
    class ServerMapVoting final
    {
    public:
        explicit ServerMapVoting(ServerMapVotingHost& host) noexcept;

        void HandleVote(const std::shared_ptr<System::Net::IPEndPoint>& sender,
            std::span<const std::uint8_t> payload, double now);
        void OpenBallot();
        void CloseBallot();
        void HandleMapPick(const std::shared_ptr<System::Net::IPEndPoint>& sender,
            std::span<const std::uint8_t> payload, double now);
        void ReviewPicks();
        void ReviewVote(double now);
        void Update(double now);
        void CancelForMapAdvance(double now);
        void ForgetPeer(std::uint32_t clientId) noexcept;
        void BroadcastMapChoices();

        [[nodiscard]] bool BallotOpen() const noexcept { return _ballotOpen; }
        [[nodiscard]] static std::optional<std::string> ResolveRoomKey(const std::string& roomKey);

    private:
        using Peer = ServerPeerRegistry::Peer;

        struct PeerVoteState final
        {
            std::uint8_t Ballot = 0;
            std::string Pick{};
            double LastProposal = -std::numeric_limits<double>::infinity();
        };

        static constexpr double VoteSeconds = 30.0;
        static constexpr double VoteCooldownSeconds = 90.0;
        static constexpr double ProposalCooldownSeconds = 180.0;
        static constexpr std::int32_t VoteMinimumPlayers = 1;

        [[nodiscard]] std::int32_t VotesFor(const std::string& roomKey) const;
        [[nodiscard]] PeerVoteState& StateFor(const Peer& peer);
        [[nodiscard]] const PeerVoteState* FindStateFor(const Peer& peer) const noexcept;
        void Recount();
        void ApplyLeader();
        void StartVote(const ServerPeerRegistry::PeerHandle& peer, const std::string& roomKey, double now);
        void Tally(double now);
        [[nodiscard]] std::tuple<std::int32_t, std::int32_t, std::int32_t, std::int32_t> CountVotes() const;
        void ResolveVote(double now, bool passed, const std::string& count);
        [[nodiscard]] GameMode ModeForRoom(const std::string& roomKey) const;
        void BroadcastVoteState(double now);

        ServerMapVotingHost& _host;
        bool _voteRunning = false;
        std::string _voteRoom{};
        GameMode _voteMode;
        std::string _voteProposer{};
        std::int32_t _voteProposerSlot = -1;
        double _voteStartedAt = 0.0;
        double _voteResolvedAt = -std::numeric_limits<double>::infinity();
        std::uint8_t _voteResult = VoteStatePacket::StateIdle;
        bool _ballotOpen = false;
        std::vector<std::string> _tallyRooms{};
        std::vector<std::int32_t> _tallyVotes{};
        std::unordered_map<std::uint32_t, PeerVoteState> _peerStates{};
        std::array<std::uint8_t, NetConfig::MaxPacketSize> _scratch{};
    };
}
