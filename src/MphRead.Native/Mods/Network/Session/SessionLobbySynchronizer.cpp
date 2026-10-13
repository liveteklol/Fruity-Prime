#include "Mods/Network/Session/SessionLobbySynchronizer.hpp"

#include <utility>

namespace MphRead::Mods::Network
{
    SessionLobbyTransition SessionLobbySynchronizer::Apply(ClientLobbyState& lobby,
        const SessionMatchIdentity& matchIdentity, const SessionStatePacket& state)
    {
        const std::optional<MatchStatePacket>& matchState = matchIdentity.State();
        if (!lobby.AcceptsSessionState(state, matchState))
        {
            return {};
        }

        const bool newMatch = lobby.SetSession(state);
        SessionLobbyTransition transition{};
        transition.Accepted = true;
        transition.ResetMatch = state.Policy == ServerSessionPolicy::Lobby
            && (newMatch || (state.Phase == SessionPhase::Lobby
                && (!lobby.HasSession() || lobby.Session()->Phase != SessionPhase::Lobby)));

        if (!matchState.has_value() || matchState->MatchId != state.MatchId
            || matchState->AuthorityEpoch != state.AuthorityEpoch)
        {
            MatchStatePacket match{};
            match.RoomKey = state.Match.RoomKey;
            match.Mode = static_cast<std::uint8_t>(state.Match.Mode);
            match.AuthorityEpoch = state.AuthorityEpoch;
            match.PointGoal = state.Match.PointGoal;
            match.TimeRemaining = state.Match.TimeLimitSeconds;
            match.MatchId = state.MatchId;
            match.Flags = static_cast<std::uint8_t>(MatchStatePacket::FlagInProgress
                | (state.Match.FriendlyFire ? MatchStatePacket::FlagFriendlyFire : 0)
                | (state.Match.ShadowFreeze ? 0 : MatchStatePacket::FlagNoShadowFreeze)
                | MatchStatePacket::RuleFlags(1, state.Match.AffinityWeapons));
            transition.MatchState = std::move(match);
        }
        return transition;
    }
}
