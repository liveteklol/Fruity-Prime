#include "Mods/Network/Session/ClientLobbyState.hpp"

namespace MphRead::Mods::Network
{
    SessionPhase ClientLobbyState::Phase() const noexcept
    {
        return _session.has_value() ? _session->Phase : SessionPhase::InMatch;
    }

    std::uint16_t ClientLobbyState::SessionRevision() const noexcept
    {
        return _session.has_value() ? _session->Revision : std::uint16_t{0};
    }

    std::optional<MatchDefinition> ClientLobbyState::ActiveMatchDefinition() const
    {
        return _session.has_value() ? std::optional<MatchDefinition>(_session->Match) : std::nullopt;
    }

    bool ClientLobbyState::IsLocalOwner(std::int32_t localSlot) const noexcept
    {
        return localSlot >= 0 && _session.has_value() && _session->OwnerSlot == localSlot;
    }

    bool ClientLobbyState::IsInLobby() const noexcept { return Phase() == SessionPhase::Lobby; }
    bool ClientLobbyState::IsStarting() const noexcept { return Phase() == SessionPhase::Starting; }
    bool ClientLobbyState::IsPlaying() const noexcept { return Phase() == SessionPhase::InMatch; }
    bool ClientLobbyState::IsPostMatch() const noexcept { return Phase() == SessionPhase::PostMatch; }

    bool ClientLobbyState::CanEditLobby(std::int32_t localSlot) const noexcept
    {
        return IsInLobby() && IsLocalOwner(localSlot);
    }

    bool ClientLobbyState::Persistent() const noexcept
    {
        return _session.has_value() && _session->Policy == ServerSessionPolicy::Lobby;
    }

    bool ClientLobbyState::FreezeGameplay() const noexcept
    {
        return Persistent() && (IsInLobby() || IsStarting());
    }

    bool ClientLobbyState::ShouldLoadMatch(std::int32_t localSlot) const noexcept
    {
        if (!_session.has_value())
            return false;
        if (_session->Phase == SessionPhase::InMatch)
            return true;
        if (_session->Phase != SessionPhase::Starting || localSlot < 0
            || localSlot >= static_cast<std::int32_t>(NetworkSlotCapacity))
        {
            return false;
        }
        return (_session->ExpectedParticipants & (1U << localSlot)) != 0;
    }

    bool ClientLobbyState::AcceptsSessionState(const SessionStatePacket& state,
        const std::optional<MatchStatePacket>& matchState) const
    {
        if (state.MatchId == 0 || state.AuthorityEpoch == 0)
        {
            return false;
        }
        if (matchState.has_value())
        {
            const MatchStatePacket& match = *matchState;
            if (state.AuthorityEpoch < match.AuthorityEpoch
                || (state.AuthorityEpoch == match.AuthorityEpoch && state.MatchId != match.MatchId
                    && !NetLifecycleTracker::Newer(state.MatchId, match.MatchId)))
            {
                return false;
            }
        }
        if (_session.has_value())
        {
            const SessionStatePacket& old = *_session;
            if (state.AuthorityEpoch != old.AuthorityEpoch
                && !NetLifecycleTracker::Newer(state.AuthorityEpoch, old.AuthorityEpoch))
            {
                return false;
            }
            if (state.AuthorityEpoch == old.AuthorityEpoch && state.Revision != old.Revision
                && !SessionStatePacket::IsNewer(state.Revision, old.Revision))
            {
                return false;
            }
        }
        return true;
    }

    bool ClientLobbyState::AcceptsMatchState(const MatchStatePacket& state) const noexcept
    {
        return !Persistent() || (!IsInLobby() && _session.has_value()
            && state.MatchId == _session->MatchId);
    }

    bool ClientLobbyState::SetSession(const SessionStatePacket& state)
    {
        const bool newMatch = !_session.has_value() || _session->MatchId != state.MatchId;
        _session = state;
        if (newMatch)
        {
            _loadedMatch.reset();
        }
        return newMatch;
    }

    std::uint32_t ClientLobbyState::NextCommandId() noexcept
    {
        std::uint32_t id = ++_nextCommandId;
        if (id == 0)
        {
            id = ++_nextCommandId;
        }
        return id;
    }

    void ClientLobbyState::QueueCommand(const LobbyCommandPacket& packet, double now)
    {
        _pendingCommands.emplace(packet.CommandId, PendingCommand{packet, now, 0});
    }

    bool ClientLobbyState::AcknowledgeCommand(std::uint32_t id)
    {
        return _pendingCommands.erase(id) != 0;
    }

    bool ClientLobbyState::IdentityUpdateDue(double now, double interval) noexcept
    {
        if (now - _lastIdentity < interval)
        {
            return false;
        }
        _lastIdentity = now;
        return true;
    }

    bool ClientLobbyState::LoadedMatchRetryDue(double now, double interval, bool starting) const noexcept
    {
        return starting && _session.has_value() && _session->Policy == ServerSessionPolicy::Lobby
            && _loadedMatch.has_value() && *_loadedMatch == _session->MatchId
            && now - _lastLoadAck >= interval;
    }

    std::optional<std::uint16_t> ClientLobbyState::MarkMatchLoaded(double now, double retryInterval) noexcept
    {
        if (!_session.has_value())
        {
            return std::nullopt;
        }
        const std::uint16_t matchId = _session->MatchId;
        if (_loadedMatch.has_value() && *_loadedMatch == matchId && now - _lastLoadAck < retryInterval)
        {
            return std::nullopt;
        }
        _loadedMatch = matchId;
        _lastLoadAck = now;
        return matchId;
    }

    void ClientLobbyState::Reset()
    {
        _session.reset();
        _pendingCommands.clear();
        _loadedMatch.reset();
        _ownerToken = ::MphRead::NativeRuntime::Guid::Empty();
        _rosterRevision = 0;
        _message.clear();
        _lastLoadAck = 0;
        _lastIdentity = 0;
    }
}
