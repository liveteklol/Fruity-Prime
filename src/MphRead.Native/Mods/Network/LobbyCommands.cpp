#include "DedicatedServer.hpp"

#include "LobbyRules.hpp"
#include "MapRotation.hpp"
#include "NetLifecycleTracker.hpp"
#include "NetSession.hpp"
#include "NetTransport.hpp"
#include "ServerSim.hpp"
#include "../Multiplayer/TeamLayout.hpp"
#include "../../NativeRuntime/System/Globalization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;

    MatchDefinition DedicatedServer::CurrentDefinition() const
    {
        return _sessionPolicy == ServerSessionPolicy::Lobby
            ? (_phase == SessionPhase::Lobby ? _lobbyMatch : _frozenMatch)
            : DefinitionFor(*_rotation->Current());
    }

    MatchDefinition DedicatedServer::DefinitionFor(const RotationEntry& entry) const
    {
        MatchDefinition definition{};
        definition.RoomKey = entry.RoomKey;
        definition.Mode = entry.Mode;
        definition.Format = _format;
        const float clampedTimeLimit = std::clamp(entry.TimeLimit, 0.0F, 65535.0F);
        // Math.Clamp preserves NaN, and C#'s unchecked float-to-integer
        // conversion leaves that case unspecified. Pick a stable value here
        // instead of invoking C++'s undefined float-to-integer conversion.
        definition.TimeLimitSeconds = std::isnan(clampedTimeLimit)
            ? 0 : static_cast<std::uint16_t>(clampedTimeLimit);
        definition.PointGoal = static_cast<std::uint16_t>(std::clamp(entry.PointGoal, 0, 0xFFFF));
        definition.FriendlyFire = _friendlyFire;
        definition.AffinityWeapons = _affinityWeapons;
        definition.ShadowFreeze = _shadowFreeze;
        return definition;
    }

    void DedicatedServer::InvalidateLobbyReady()
    {
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            peer->LobbyReady = false;
        }
    }

    void DedicatedServer::TouchLobbyRevision(const std::string& reason)
    {
        const std::uint16_t previous = _sessionRevision++;
        Log("[lobby] revision " + std::to_string(previous) + " -> " + std::to_string(_sessionRevision) + ": " + reason);
        BroadcastSessionState();
        BroadcastRoster();
    }

    void DedicatedServer::SetPhase(SessionPhase phase)
    {
        Log("[lobby] phase " + ToString(_phase) + " -> " + ToString(phase) + ", match " + std::to_string(_matchId));
        _phase = phase;
        TouchLobbyRevision("phase changed");
    }

    SessionStatePacket DedicatedServer::BuildSessionState() const
    {
        const MatchDefinition definition = CurrentDefinition();
        SessionStatePacket state{};
        state.Phase = _phase;
        state.Policy = _sessionPolicy;
        state.Revision = _sessionRevision;
        state.MatchId = _matchId;
        state.AuthorityEpoch = _authorityEpoch;
        std::uint8_t owner = 255;
        if (_lobbyOwnerClientId != 0)
        {
            for (const std::shared_ptr<Peer>& peer : _peers)
            {
                if (peer->ClientId == _lobbyOwnerClientId)
                {
                    owner = static_cast<std::uint8_t>(peer->SlotIndex);
                    break;
                }
            }
        }
        state.OwnerSlot = owner;
        state.MaxPlayers = static_cast<std::uint8_t>(_maxPlayers);
        state.Match = definition;
        state.WorldProfile = _sessionPolicy == ServerSessionPolicy::Lobby
            ? (_phase != SessionPhase::Lobby ? _frozenWorldProfile : LobbyRules::ResolveWorldProfile(definition, _maxPlayers))
            : _mapWorldProfile.IsValid() ? _mapWorldProfile : LobbyRules::ResolveWorldProfile(definition, 2);
        SessionRules rules = definition.Rules();
        if (_requireReady)
        {
            rules = rules | SessionRules::RequireReady;
        }
        if (_allowJoinInProgress)
        {
            rules = rules | SessionRules::AllowJoinInProgress;
        }
        if (_lockTeams)
        {
            rules = rules | SessionRules::LockTeams;
        }
        state.RuleFlags = rules;
        state.ExpectedParticipants = _expectedLoadedSlots;
        state.LoadedParticipants = _loadedSlots;
        return state;
    }

    void DedicatedServer::BroadcastSessionState()
    {
        const SessionStatePacket state = BuildSessionState();
        if (_sim != nullptr)
        {
            NetSession::ApplySessionState(state);
        }
        state.Write(_scratch);
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (_transport != nullptr)
            {
                _transport->Send(peer->EndPoint, PacketType::SessionState,
                    std::span<const std::uint8_t>(_scratch.data(), SessionStatePacket::Size));
            }
        }
    }

    std::int8_t DedicatedServer::ChooseTeam(const MatchDefinition& match, const Peer* exclude) const
    {
        const Multiplayer::TeamLayout layout = LobbyRules::ResolveTeamLayout(match);
        std::array<std::int32_t, 4> counts{};
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            if (peer.get() != exclude && peer->TeamIndex >= 0 && peer->TeamIndex < layout.TeamCount)
            {
                counts[static_cast<std::size_t>(peer->TeamIndex)]++;
            }
        }
        return Multiplayer::TeamRules::ChooseTeam(layout, counts);
    }

    void DedicatedServer::NormalizeTeams()
    {
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            peer->TeamIndex = -1;
        }
        for (const std::shared_ptr<Peer>& peer : _peers)
        {
            peer->TeamIndex = ChooseTeam(CurrentDefinition(), peer.get());
        }
    }

    void DedicatedServer::ClaimOwner(const std::shared_ptr<Peer>& peer, std::span<const std::uint8_t> hello)
    {
        if (_sessionPolicy != ServerSessionPolicy::Lobby || _lobbyOwnerClientId != 0 || peer->ClientId == 0)
        {
            return;
        }
        const bool tokenMatches = _ownerToken != Runtime::Guid::Empty() && hello.size() == 22
            && Runtime::Guid(hello.subspan(6, 16)) == _ownerToken;
        if (_ownerToken != Runtime::Guid::Empty() && !tokenMatches)
        {
            return;
        }
        _lobbyOwnerClientId = peer->ClientId;
        _ownerToken = Runtime::Guid::Empty();
        TouchLobbyRevision("owner = slot " + std::to_string(peer->SlotIndex));
    }

    void DedicatedServer::HandleLobbyCommand(const ReceivedPacket& packet, double now)
    {
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        LobbyCommandPacket command{};
        if (peer == nullptr || !LobbyCommandPacket::TryRead(packet.Payload(), command))
        {
            return;
        }
        peer->LastSeen = now;
        auto found = peer->Commands.find(command.CommandId);
        LobbyCommandResultPacket result{};
        if (found == peer->Commands.end())
        {
            std::string reason;
            const LobbyResultCode code = ExecuteLobbyCommand(peer, command, reason);
            result.CommandId = command.CommandId;
            result.ResultCode = code;
            result.CurrentRevision = _sessionRevision;
            result.Reason = reason;
            if (peer->Commands.size() >= 64)
            {
                peer->Commands.erase(peer->CommandOrder.front());
                peer->CommandOrder.pop_front();
            }
            peer->Commands.emplace(command.CommandId, result);
            peer->CommandOrder.push_back(command.CommandId);
            if (code != LobbyResultCode::Ok)
            {
                Log("[lobby] slot " + std::to_string(peer->SlotIndex) + " " + ToString(command.Type)
                    + " denied: " + reason);
            }
        }
        else
        {
            result = found->second;
        }
        result.Write(_scratch);
        if (_transport != nullptr)
        {
            _transport->Send(peer->EndPoint, PacketType::LobbyCommandResult,
                std::span<const std::uint8_t>(_scratch.data(), LobbyCommandResultPacket::Size));
        }
        BroadcastSessionState();
        BroadcastRoster();
    }

    LobbyResultCode DedicatedServer::ExecuteLobbyCommand(const std::shared_ptr<Peer>& peer,
        const LobbyCommandPacket& command, std::string& reason)
    {
        reason.clear();
        if (_sessionPolicy != ServerSessionPolicy::Lobby || _phase != SessionPhase::Lobby)
        {
            reason = "Wait until the server returns to the lobby.";
            return LobbyResultCode::InvalidPhase;
        }
        const bool owner = peer->ClientId == _lobbyOwnerClientId && peer->ClientId != 0;
        if (command.Type != LobbyCommandType::SetReady && command.Type != LobbyCommandType::SetTeam && !owner)
        {
            reason = "Only the lobby owner can do that.";
            return LobbyResultCode::NotOwner;
        }
        if (command.ExpectedRevision != _sessionRevision)
        {
            reason = "The lobby changed. Review the updated settings and try again.";
            return LobbyResultCode::StaleRevision;
        }
        switch (command.Type)
        {
        case LobbyCommandType::SetReady:
            peer->LobbyReady = command.Ready;
            break;
        case LobbyCommandType::SetTeam:
        {
            std::shared_ptr<Peer> target{};
            for (const std::shared_ptr<Peer>& candidate : _peers)
            {
                if (candidate->SlotIndex == command.TargetSlot)
                {
                    target = candidate;
                    break;
                }
            }
            if (target == nullptr)
            {
                reason = "That player has left.";
                return LobbyResultCode::TargetNotFound;
            }
            if (target != peer && !owner)
            {
                reason = "Only the owner can move another player.";
                return LobbyResultCode::NotOwner;
            }
            if (_lockTeams && !owner)
            {
                reason = "Team changes are locked by the owner.";
                return LobbyResultCode::NotOwner;
            }
            const std::int8_t requestedTeam = command.TeamIndex == -1 ? ChooseTeam(_lobbyMatch, target.get()) : command.TeamIndex;
            if (command.TeamIndex < -1 || requestedTeam < 0 || requestedTeam >= LobbyRules::TeamCount(_lobbyMatch))
            {
                reason = "Choose a team for the current format.";
                return LobbyResultCode::InvalidTeam;
            }
            const auto onTeam = std::count_if(_peers.begin(), _peers.end(),
                [&target, requestedTeam](const std::shared_ptr<Peer>& p) { return p != target && p->TeamIndex == requestedTeam; });
            if (onTeam >= LobbyRules::TeamCapacity(_lobbyMatch, requestedTeam))
            {
                reason = "That team is full.";
                return LobbyResultCode::TeamFull;
            }
            target->TeamIndex = requestedTeam;
            target->LobbyReady = false;
            break;
        }
        case LobbyCommandType::UpdateMatch:
        {
            const MatchDefinition proposed = command.Configuration.Match;
            const LobbyResultCode valid = LobbyRules::ValidateDefinition(proposed, reason);
            if (valid != LobbyResultCode::Ok)
            {
                return valid;
            }
            const std::optional<std::string> room = ResolveRoomKey(proposed.RoomKey.value_or(""));
            if (!room.has_value())
            {
                reason = "The server does not have that map.";
                return LobbyResultCode::MapUnavailable;
            }
            const Multiplayer::TeamLayout proposedLayout = LobbyRules::ResolveTeamLayout(proposed);
            if (proposedLayout.TeamCount > 0 && (proposedLayout.TotalPlayers() < static_cast<std::int32_t>(_peers.size())
                || (LobbyRules::ExactTeams(proposed) && proposedLayout.TotalPlayers() > _maxPlayers)))
            {
                reason = "The layout must fit the connected roster and server player limit.";
                return LobbyResultCode::InvalidConfiguration;
            }
            const bool topologyChanged = !(proposedLayout == LobbyRules::ResolveTeamLayout(_lobbyMatch));
            _lobbyMatch = proposed;
            _lobbyMatch.RoomKey = *room;
            _requireReady = command.Configuration.RequireReady();
            _allowJoinInProgress = command.Configuration.AllowJoinInProgress();
            _lockTeams = command.Configuration.LockTeams();
            if (topologyChanged)
            {
                NormalizeTeams();
            }
            InvalidateLobbyReady();
            break;
        }
        case LobbyCommandType::StartMatch:
        {
            const LobbyResultCode start = LobbyRules::Validate(_lobbyMatch, BuildRoster(), _requireReady, reason);
            if (start != LobbyResultCode::Ok)
            {
                return start;
            }
            _frozenMatch = _lobbyMatch;
            _frozenWorldProfile = LobbyRules::ResolveWorldProfile(_frozenMatch,
                std::max<std::int32_t>(2, static_cast<std::int32_t>(_peers.size())));
            const double buildStarted = NetSession::Clock();
            const std::uint16_t previousMatch = _matchId;
            _matchId = NetLifecycleTracker::Next(_matchId);
            try
            {
                StartSimulation();
            }
            catch (const std::exception& ex)
            {
                _matchId = previousMatch;
                if (_sim != nullptr)
                {
                    _sim->Stop();
                }
                _sim.reset();
                Log(std::string("[lobby] map load failed: ") + ex.what());
                reason = "The server could not load this map.";
                return LobbyResultCode::MapUnavailable;
            }
            _snapshotSeen = false;
            _slotLives.fill(0);
            for (const std::shared_ptr<Peer>& connected : _peers)
            {
                connected->LastIntentFrame = 0;
            }
            _matchEndedAt = -1;
            _lastSnapshot = nullptr;
            _expectedLoadedSlots = 0;
            _loadedSlots = 0;
            for (const std::shared_ptr<Peer>& participant : _peers)
            {
                _expectedLoadedSlots = static_cast<std::uint8_t>(_expectedLoadedSlots | (1 << participant->SlotIndex));
                participant->PostMatchReady = false;
            }
            _startDeadline = _now + (NetSession::Clock() - buildStarted) + 15;
            SetPhase(SessionPhase::Starting);
            SyncSimulationState(_now);
            char mask[3];
            std::snprintf(mask, sizeof(mask), "%02X", _expectedLoadedSlots);
            Log(std::string("[lobby] waiting for slots mask ") + mask);
            return LobbyResultCode::Ok;
        }
        case LobbyCommandType::KickPlayer:
        case LobbyCommandType::TransferOwner:
        {
            std::shared_ptr<Peer> selected{};
            for (const std::shared_ptr<Peer>& candidate : _peers)
            {
                if (candidate->SlotIndex == command.TargetSlot)
                {
                    selected = candidate;
                    break;
                }
            }
            if (selected == nullptr || selected == peer)
            {
                reason = "Choose another connected player.";
                return LobbyResultCode::TargetNotFound;
            }
            if (command.Type == LobbyCommandType::TransferOwner)
            {
                _lobbyOwnerClientId = selected->ClientId;
            }
            else
            {
                SendRefusal(selected->EndPoint, RefusedPacket::ReasonKicked);
                Remove(selected, "removed by lobby owner");
            }
            break;
        }
        }
        TouchLobbyRevision("slot " + std::to_string(peer->SlotIndex) + ": " + ToString(command.Type));
        return LobbyResultCode::Ok;
    }

    void DedicatedServer::HandleMatchLoaded(const ReceivedPacket& packet, double now)
    {
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        MatchLoadedPacket loaded{};
        if (peer == nullptr || !MatchLoadedPacket::TryRead(packet.Payload(), loaded) || loaded.MatchId != _matchId)
        {
            return;
        }
        peer->LastSeen = now;
        const auto mask = static_cast<std::uint8_t>(1 << peer->SlotIndex);
        if (_phase != SessionPhase::Starting || (_expectedLoadedSlots & mask) == 0 || (_loadedSlots & mask) != 0)
        {
            return;
        }
        _loadedSlots = static_cast<std::uint8_t>(_loadedSlots | mask);
        TouchLobbyRevision("slot " + std::to_string(peer->SlotIndex) + " loaded match " + std::to_string(_matchId));
        CheckLoadBarrier(now);
    }

    void DedicatedServer::CheckLoadBarrier(double now)
    {
        if (_phase != SessionPhase::Starting)
        {
            return;
        }
        if ((_loadedSlots & _expectedLoadedSlots) != _expectedLoadedSlots)
        {
            if (now < _startDeadline)
            {
                return;
            }
        }
        Log(now >= _startDeadline ? "[lobby] load timeout; late clients may join in progress" : "[lobby] all clients loaded");
        _matchStarted = now;
        SetPhase(SessionPhase::InMatch);
        BroadcastMatchState(now);
    }

    void DedicatedServer::HandleMatchLoadFailed(const ReceivedPacket& packet)
    {
        const std::shared_ptr<Peer> peer = Find(packet.Sender);
        MatchLoadFailedPacket failed{};
        if (peer == nullptr || !MatchLoadFailedPacket::TryRead(packet.Payload(), failed) || failed.MatchId != _matchId)
        {
            return;
        }
        Remove(peer, "could not load match: " + failed.Reason);
    }

    void DedicatedServer::ReturnToLobby()
    {
        if (_sim != nullptr)
        {
            _sim->Stop();
        }
        _sim.reset();
        _lastSnapshot = nullptr;
        MatchDefinition next = DefinitionFor(*_rotation->Advance());
        next.Format = _frozenMatch.Format;
        next.CustomTeams = _frozenMatch.CustomTeams;
        next.FriendlyFire = _frozenMatch.FriendlyFire;
        next.AffinityWeapons = _frozenMatch.AffinityWeapons;
        next.ShadowFreeze = _frozenMatch.ShadowFreeze;
        _lobbyMatch = next;
        std::string ignored;
        if (LobbyRules::ValidateDefinition(_lobbyMatch, ignored) != LobbyResultCode::Ok)
        {
            _lobbyMatch.Format = MatchFormat::Auto;
        }
        _matchEndedAt = -1;
        _expectedLoadedSlots = _loadedSlots = 0;
        CloseBallot();
        BroadcastMapChoices();
        InvalidateLobbyReady();
        const SessionPhase previous = _phase;
        _phase = SessionPhase::Lobby;
        NormalizeTeams();
        _phase = previous;
        SetPhase(SessionPhase::Lobby);
    }

    void DedicatedServer::LobbyPeerRemoved(const std::shared_ptr<Peer>& peer)
    {
        _expectedLoadedSlots = static_cast<std::uint8_t>(_expectedLoadedSlots & ~(1 << peer->SlotIndex));
        _loadedSlots = static_cast<std::uint8_t>(_loadedSlots & ~(1 << peer->SlotIndex));
        if (_lobbyOwnerClientId == peer->ClientId)
        {
            _lobbyOwnerClientId = !_peers.empty() ? _peers[0]->ClientId : 0;
        }
        TouchLobbyRevision("slot " + std::to_string(peer->SlotIndex) + " left; owner " + std::to_string(_lobbyOwnerClientId));
        if (_phase == SessionPhase::Starting)
        {
            std::string why;
            if (LobbyRules::Validate(_frozenMatch, BuildRoster(), false, why) != LobbyResultCode::Ok)
            {
                Log("[lobby] start cancelled: " + why);
                if (_sim != nullptr)
                {
                    _sim->Stop();
                }
                _sim.reset();
                _lastSnapshot = nullptr;
                InvalidateLobbyReady();
                SetPhase(SessionPhase::Lobby);
            }
            else
            {
                CheckLoadBarrier(_now);
            }
        }
    }
}
