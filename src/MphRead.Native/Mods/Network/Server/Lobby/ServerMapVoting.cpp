#include "Mods/Network/Server/Lobby/ServerMapVoting.hpp"
#include "Mods/Network/Domain/MapVoteRules.hpp"

#include "GameState.hpp"
#include "Metadata/Metadata.hpp"
#include "NativeRuntime/System/Globalization.hpp"
#include "NativeRuntime/System/Number.hpp"
#include "NativeRuntime/System/Runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    namespace
    {
        [[nodiscard]] std::span<const std::uint8_t> First(
            const std::array<std::uint8_t, NetConfig::MaxPacketSize>& scratch, std::size_t count)
        {
            return std::span<const std::uint8_t>(scratch.data(), count);
        }
    }

    ServerMapVoting::ServerMapVoting(ServerMapVotingHost& host) noexcept
        : _host(host), _voteMode(GameMode::Battle)
    {
    }

    ServerMapVoting::PeerVoteState& ServerMapVoting::StateFor(const Peer& peer)
    {
        return _peerStates[peer.ClientId];
    }

    const ServerMapVoting::PeerVoteState* ServerMapVoting::FindStateFor(const Peer& peer) const noexcept
    {
        const auto found = _peerStates.find(peer.ClientId);
        return found == _peerStates.end() ? nullptr : &found->second;
    }

    void ServerMapVoting::ForgetPeer(std::uint32_t clientId) noexcept
    {
        _peerStates.erase(clientId);
    }

    void ServerMapVoting::Update(double now)
    {
        Tally(now);
        BroadcastVoteState(now);
    }

    void ServerMapVoting::CancelForMapAdvance(double now)
    {
        if (!_voteRunning)
        {
            return;
        }
        _voteRunning = false;
        _voteResolvedAt = now;
        _voteResult = VoteStatePacket::StateFailed;
        for (const std::shared_ptr<Peer>& peer : _host.VotingPeers().All())
        {
            StateFor(*peer).Ballot = 0;
        }
    }
    void ServerMapVoting::HandleVote(const std::shared_ptr<System::Net::IPEndPoint>& sender,
        std::span<const std::uint8_t> payload, double now)
    {
        const std::shared_ptr<Peer> peer = _host.VotingPeers().Find(sender);
        if (peer == nullptr || peer->SlotIndex < 0 || payload.size() < static_cast<std::size_t>(VotePacket::Size))
        {
            return;
        }
        _host.MarkVotingPeerSeen(peer, now);
        if (!_host.MapVotesAllowed())
        {
            return;
        }
        const VotePacket vote = VotePacket::Read(payload);
        if (vote.Kind == VotePacket::KindPropose)
        {
            StartVote(peer, vote.RoomKey.value_or(""), now);
            return;
        }
        if (!_voteRunning || (vote.Kind != VotePacket::KindYes && vote.Kind != VotePacket::KindNo))
        {
            return;
        }
        if (StateFor(*peer).Ballot != 0)
        {
            return;
        }
        StateFor(*peer).Ballot = vote.Kind;
        BroadcastVoteState(now);
        Tally(now);
    }

    void ServerMapVoting::OpenBallot()
    {
        _ballotOpen = _host.MapVotesAllowed();
        _tallyRooms.clear();
        _tallyVotes.clear();
        for (const std::shared_ptr<Peer>& peer : _host.VotingPeers().All())
        {
            StateFor(*peer).Pick.clear();
        }
    }

    void ServerMapVoting::CloseBallot()
    {
        _ballotOpen = false;
        _tallyRooms.clear();
        _tallyVotes.clear();
        for (const std::shared_ptr<Peer>& peer : _host.VotingPeers().All())
        {
            StateFor(*peer).Pick.clear();
        }
    }

    void ServerMapVoting::HandleMapPick(const std::shared_ptr<System::Net::IPEndPoint>& sender,
        std::span<const std::uint8_t> payload, double now)
    {
        const std::shared_ptr<Peer> peer = _host.VotingPeers().Find(sender);
        if (peer == nullptr || peer->SlotIndex < 0 || payload.size() < static_cast<std::size_t>(MapPickPacket::Size))
        {
            return;
        }
        _host.MarkVotingPeerSeen(peer, now);
        if (!_ballotOpen)
        {
            return;
        }
        std::string key = MapPickPacket::Read(payload).RoomKey.value_or("");
        if (!key.empty())
        {
            const std::optional<std::string> resolved = ResolveRoomKey(key);
            if (!resolved.has_value()
                || Runtime::StringEqualsOrdinalIgnoreCase(*resolved, _host.VotingDefinition().RoomKey.value_or("")))
            {
                _host.TellVotingPeer(peer, !resolved.has_value() ? "no map called \"" + key + "\"" : std::string("that is the map you are on"));
                return;
            }
            key = *resolved;
        }
        if (Runtime::StringEqualsOrdinalIgnoreCase(StateFor(*peer).Pick, key))
        {
            return;
        }
        const std::string was = StateFor(*peer).Pick;
        StateFor(*peer).Pick = key;
        Recount();
        if (!key.empty() && VotesFor(key) == 1)
        {
            const std::string who = !peer->Profile.Name().empty() ? peer->Profile.Name() : "Player" + std::to_string(peer->SlotIndex + 1);
            _host.AnnounceVote(who + " wants " + key + " next -- pick it to agree; "
                "the map with the most votes is the one loaded");
        }
        else if (key.empty() && !was.empty())
        {
            _host.LogVote("slot " + std::to_string(peer->SlotIndex) + " took back its pick of " + was);
        }
        ApplyLeader();
        BroadcastMapChoices();
    }

    std::int32_t ServerMapVoting::VotesFor(const std::string& roomKey) const
    {
        for (std::size_t i = 0; i < _tallyRooms.size(); i++)
        {
            if (Runtime::StringEqualsOrdinalIgnoreCase(_tallyRooms[i], roomKey))
            {
                return _tallyVotes[i];
            }
        }
        return 0;
    }

    void ServerMapVoting::Recount()
    {
        _tallyRooms.clear();
        _tallyVotes.clear();
        for (const std::shared_ptr<Peer>& peer : _host.VotingPeers().All())
        {
            const std::string& key = StateFor(*peer).Pick;
            if (key.empty())
            {
                continue;
            }
            std::int32_t at = -1;
            for (std::size_t j = 0; j < _tallyRooms.size(); j++)
            {
                if (Runtime::StringEqualsOrdinalIgnoreCase(_tallyRooms[j], key))
                {
                    at = static_cast<std::int32_t>(j);
                    break;
                }
            }
            if (at < 0)
            {
                _tallyRooms.push_back(key);
                _tallyVotes.push_back(1);
            }
            else
            {
                _tallyVotes[static_cast<std::size_t>(at)]++;
            }
        }
        for (std::size_t i = 1; i < _tallyRooms.size(); i++)
        {
            for (std::size_t j = i; j > 0 && _tallyVotes[j] > _tallyVotes[j - 1]; j--)
            {
                std::swap(_tallyVotes[j], _tallyVotes[j - 1]);
                std::swap(_tallyRooms[j], _tallyRooms[j - 1]);
            }
        }
    }

    void ServerMapVoting::ApplyLeader()
    {
        if (_tallyRooms.empty() || _tallyVotes[0] <= 0)
        {
            _host.ClearPendingMapPick();
            return;
        }
        _host.SetPendingMapPick(_tallyRooms[0], ModeForRoom(_tallyRooms[0]));
    }

    void ServerMapVoting::BroadcastMapChoices()
    {
        if (_host.VotingPeers().All().empty())
        {
            return;
        }
        const std::size_t count = std::min(_tallyRooms.size(), static_cast<std::size_t>(MapChoicesPacket::MaxChoices));
        auto keys = std::make_shared<std::vector<std::optional<std::string>>>(count);
        auto votes = std::make_shared<std::vector<std::uint8_t>>(count);
        for (std::size_t i = 0; i < count; i++)
        {
            (*keys)[i] = _tallyRooms[i];
            (*votes)[i] = static_cast<std::uint8_t>(std::clamp(_tallyVotes[i], 0, 255));
        }
        MapChoicesPacket packet{};
        packet.Open = static_cast<std::uint8_t>(_ballotOpen ? 1 : 0);
        packet.Count = static_cast<std::uint8_t>(count);
        packet.RoomKeys = keys;
        packet.Votes = votes;
        packet.Eligible = static_cast<std::uint8_t>(std::clamp(static_cast<std::int32_t>(_host.VotingPeers().All().size()), 0, 255));
        packet.Write(_scratch);
        for (const std::shared_ptr<Peer>& peer : _host.VotingPeers().All())
        {
            _host.SendVotePacket(peer, PacketType::MapChoices, First(_scratch, MapChoicesPacket::Size));
        }
    }

    void ServerMapVoting::ReviewPicks()
    {
        if (!_ballotOpen)
        {
            return;
        }
        Recount();
        ApplyLeader();
        BroadcastMapChoices();
    }

    void ServerMapVoting::StartVote(const std::shared_ptr<Peer>& peer, const std::string& roomKey, double now)
    {
        if (_host.VotingPhase() != SessionPhase::InMatch)
        {
            return;
        }
        if (_voteRunning)
        {
            _host.TellVotingPeer(peer, "a vote is already running");
            return;
        }
        if (static_cast<std::int32_t>(_host.VotingPeers().All().size()) < VoteMinimumPlayers)
        {
            _host.TellVotingPeer(peer, "not enough players to hold a vote");
            return;
        }
        // The cooldowns keep one player from spamming the others with votes;
        // nobody is spammed by a player who is alone.
        const bool alone = _host.VotingPeers().All().size() == 1;
        const double sinceVote = now - _voteResolvedAt;
        if (!alone && sinceVote < VoteCooldownSeconds)
        {
            _host.TellVotingPeer(peer, "another vote may be called in " + Runtime::ToString(VoteCooldownSeconds - sinceVote, "0") + " s");
            return;
        }
        const double sinceMine = now - StateFor(*peer).LastProposal;
        if (!alone && sinceMine < ProposalCooldownSeconds)
        {
            _host.TellVotingPeer(peer, "you may propose again in " + Runtime::ToString(ProposalCooldownSeconds - sinceMine, "0") + " s");
            return;
        }
        const std::optional<std::string> resolved = ResolveRoomKey(roomKey);
        if (!resolved.has_value())
        {
            _host.TellVotingPeer(peer, "no map called \"" + roomKey + "\"");
            return;
        }
        if (Runtime::StringEqualsOrdinalIgnoreCase(*resolved, _host.VotingDefinition().RoomKey.value_or("")))
        {
            _host.TellVotingPeer(peer, "that is the map you are on");
            return;
        }
        _voteRunning = true;
        _voteRoom = *resolved;
        _voteMode = ModeForRoom(*resolved);
        _voteProposer = !peer->Profile.Name().empty() ? peer->Profile.Name() : "Player" + std::to_string(peer->SlotIndex + 1);
        _voteProposerSlot = peer->SlotIndex;
        _voteStartedAt = now;
        _voteResult = VoteStatePacket::StateIdle;
        StateFor(*peer).LastProposal = now;
        for (const std::shared_ptr<Peer>& other : _host.VotingPeers().All())
        {
            StateFor(*other).Ballot = 0;
        }
        StateFor(*peer).Ballot = VotePacket::KindYes;
        _host.AnnounceVote(_voteProposer + " proposes " + *resolved + " -- F1 to accept, F2 to deny");
        _host.LogVote("vote started by slot " + std::to_string(peer->SlotIndex) + " for " + *resolved + " ("
            + ::MphRead::ToString(_voteMode) + ")");
        BroadcastVoteState(now);
        Tally(now);
    }

    void ServerMapVoting::Tally(double now)
    {
        if (!_voteRunning)
        {
            return;
        }
        const auto [yes, no, eligible, needed] = CountVotes();
        const std::string count = std::to_string(yes) + " of " + std::to_string(eligible);
        if (MapVoteRules::Passed(yes, needed))
        {
            ResolveVote(now, true, count);
            return;
        }
        if (!MapVoteRules::StillPossible(eligible, no, needed))
        {
            ResolveVote(now, false, count);
            return;
        }
        if (now - _voteStartedAt >= VoteSeconds)
        {
            ResolveVote(now, false, count);
        }
    }

    std::tuple<std::int32_t, std::int32_t, std::int32_t, std::int32_t> ServerMapVoting::CountVotes() const
    {
        std::int32_t yes = 0;
        std::int32_t no = 0;
        for (const std::shared_ptr<Peer>& peer : _host.VotingPeers().All())
        {
            const PeerVoteState* vote = FindStateFor(*peer);
            if (vote == nullptr)
            {
                continue;
            }
            if (vote->Ballot == VotePacket::KindYes)
            {
                yes++;
            }
            else if (vote->Ballot == VotePacket::KindNo)
            {
                no++;
            }
        }
        const std::int32_t eligible = static_cast<std::int32_t>(_host.VotingPeers().All().size());
        const std::int32_t needed = MapVoteRules::RequiredYesVotes(eligible);
        return {yes, no, eligible, needed};
    }

    void ServerMapVoting::ResolveVote(double now, bool passed, const std::string& count)
    {
        const std::string room = _voteRoom;
        const GameMode mode = _voteMode;
        _voteRunning = false;
        _voteResolvedAt = now;
        _voteResult = passed ? VoteStatePacket::StatePassed : VoteStatePacket::StateFailed;
        for (const std::shared_ptr<Peer>& peer : _host.VotingPeers().All())
        {
            StateFor(*peer).Ballot = 0;
        }
        if (passed)
        {
            _host.AnnounceVote("vote passed (" + count + ") -- changing to " + room);
            _host.LogVote("vote passed (" + count + ") for " + room);
            _host.ApplyAcceptedMapVote(now, room, mode);
        }
        else
        {
            _host.AnnounceVote("vote failed (" + count + ") -- staying on " + _host.VotingDefinition().RoomKey.value_or(""));
            _host.LogVote("vote failed (" + count + ") for " + room);
        }
        BroadcastVoteState(now);
    }

    void ServerMapVoting::ReviewVote(double now)
    {
        if (!_voteRunning)
        {
            return;
        }
        if (static_cast<std::int32_t>(_host.VotingPeers().All().size()) < VoteMinimumPlayers)
        {
            ResolveVote(now, false, "not enough players");
            return;
        }
        Tally(now);
    }

    std::optional<std::string> ServerMapVoting::ResolveRoomKey(const std::string& roomKey)
    {
        if (Runtime::StringIsNullOrWhiteSpace(roomKey))
        {
            return std::nullopt;
        }
        const std::string wanted = Runtime::StringTrim(roomKey);
        for (const auto& entry : Metadata::RoomMetadata)
        {
            if (Runtime::RequireReference(entry.second).Multiplayer
                && Runtime::StringEqualsOrdinalIgnoreCase(entry.first, wanted))
            {
                return entry.first;
            }
        }
        return std::nullopt;
    }

    GameMode ServerMapVoting::ModeForRoom(const std::string& roomKey) const
    {
        return _host.VotingModeForRoom(roomKey);
    }

    void ServerMapVoting::BroadcastVoteState(double now)
    {
        if (_host.VotingPeers().All().empty())
        {
            return;
        }
        VoteStatePacket state{};
        state.State = _voteRunning ? VoteStatePacket::StateRunning : _voteResult;
        state.RoomKey = _voteRunning ? _voteRoom : std::string{};
        state.Proposer = _voteRunning ? _voteProposer : std::string{};
        if (_voteRunning)
        {
            const auto [yes, no, eligible, needed] = CountVotes();
            state.Yes = static_cast<std::uint8_t>(yes);
            state.No = static_cast<std::uint8_t>(no);
            state.Eligible = static_cast<std::uint8_t>(eligible);
            state.Needed = static_cast<std::uint8_t>(needed);
            state.Seconds = static_cast<std::uint16_t>(std::max(0.0, VoteSeconds - (now - _voteStartedAt)));
        }
        else if (_host.MapVotesAllowed())
        {
            const double wait = VoteCooldownSeconds - (now - _voteResolvedAt);
            state.Seconds = static_cast<std::uint16_t>(std::clamp(wait, 0.0, 65535.0));
        }
        else
        {
            state.Seconds = 0xFFFF;
        }
        state.Write(_scratch);
        for (const std::shared_ptr<Peer>& peer : _host.VotingPeers().All())
        {
            _host.SendVotePacket(peer, PacketType::VoteState, First(_scratch, VoteStatePacket::Size));
        }
    }


}
