#include "Mods/Network/Session/SessionLobbySynchronizer.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
    using MphRead::Mods::Network::ClientLobbyState;
    using MphRead::Mods::Network::MatchStatePacket;
    using MphRead::Mods::Network::ServerSessionPolicy;
    using MphRead::Mods::Network::SessionLobbySynchronizer;
    using MphRead::Mods::Network::SessionPhase;
    using MphRead::Mods::Network::SessionStatePacket;
    using MphRead::Mods::Network::SessionMatchIdentity;

    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }

    SessionStatePacket State(std::uint16_t matchId, std::uint64_t epoch, std::uint16_t revision,
        SessionPhase phase)
    {
        SessionStatePacket state{};
        state.MatchId = matchId;
        state.AuthorityEpoch = epoch;
        state.Revision = revision;
        state.Policy = ServerSessionPolicy::Lobby;
        state.Phase = phase;
        state.Match.RoomKey = "MP1 SANCTORUS";
        state.Match.TimeLimitSeconds = 420;
        state.Match.PointGoal = 7;
        state.Match.AffinityWeapons = true;
        return state;
    }
}

int main()
{
    bool passed = true;
    ClientLobbyState lobby;
    SessionMatchIdentity identity;

    const SessionStatePacket starting = State(7, 3, 4, SessionPhase::Starting);
    const auto first = SessionLobbySynchronizer::Apply(lobby, identity, starting);
    passed &= Check(first.Accepted && first.ResetMatch && first.MatchState.has_value(),
        "new authoritative lobby session resets match state and projects match controls");
    if (first.MatchState.has_value())
    {
        passed &= Check(first.MatchState->MatchId == 7 && first.MatchState->AuthorityEpoch == 3
            && first.MatchState->RoomKey == "MP1 SANCTORUS" && first.MatchState->TimeRemaining == 420,
            "session match definition projects into the matching wire state");
    }

    MatchStatePacket current{};
    current.MatchId = 7;
    current.AuthorityEpoch = 3;
    identity.SetAuthoritative(current);
    const SessionStatePacket newerRevision = State(7, 3, 5, SessionPhase::InMatch);
    const auto revision = SessionLobbySynchronizer::Apply(lobby, identity, newerRevision);
    passed &= Check(revision.Accepted && !revision.ResetMatch && !revision.MatchState.has_value(),
        "same match revision updates lobby state without replaying match control");

    const SessionStatePacket stale = State(7, 3, 4, SessionPhase::Lobby);
    const auto rejected = SessionLobbySynchronizer::Apply(lobby, identity, stale);
    passed &= Check(!rejected.Accepted && lobby.Session()->Phase == SessionPhase::InMatch,
        "stale lobby revision cannot mutate the accepted phase");

    const SessionStatePacket nextMatch = State(8, 4, 1, SessionPhase::Lobby);
    const auto next = SessionLobbySynchronizer::Apply(lobby, identity, nextMatch);
    passed &= Check(next.Accepted && next.ResetMatch && next.MatchState.has_value()
        && lobby.Session()->MatchId == 8,
        "authority and match transition resets match state and projects new controls");

    if (!passed)
    {
        return 1;
    }
    std::cout << "SessionLobbySynchronizer PASS\n";
    return 0;
}
