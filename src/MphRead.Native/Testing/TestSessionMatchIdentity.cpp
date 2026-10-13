#include "Mods/Network/Session/SessionMatchIdentity.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }

    MphRead::Mods::Network::MatchStatePacket Match(
        std::uint16_t matchId, std::uint64_t epoch, bool ending = false, std::string room = "MP3")
    {
        MphRead::Mods::Network::MatchStatePacket state{};
        state.MatchId = matchId;
        state.AuthorityEpoch = epoch;
        state.RoomKey = std::move(room);
        state.Flags = ending ? MphRead::Mods::Network::MatchStatePacket::FlagEnding : 0;
        return state;
    }
}

int main()
{
    using MphRead::Mods::Network::SessionMatchIdentity;

    bool passed = true;
    SessionMatchIdentity identity;
    passed &= Check(!identity.HasMatch() && identity.MatchId(false) == 0
        && identity.AuthorityEpoch(false) == 0 && identity.MatchId(true) == 1
        && identity.AuthorityEpoch(true) == 1, "empty and host-fallback identities");

    passed &= Check(identity.Apply(Match(0, 1)).Rejected == SessionMatchIdentity::Rejection::InvalidIdentity
        && !identity.HasMatch(), "invalid identities are refused without changing state");

    const auto first = identity.Apply(Match(4, 100));
    passed &= Check(first.Accepted() && first.NewMatch && first.NewAuthority && first.RoomChanged
        && identity.MatchId(false) == 4 && identity.AuthorityEpoch(false) == 100,
        "first accepted identity reports initial stream transition");
    const auto same = identity.Apply(Match(4, 100));
    passed &= Check(same.Accepted() && !same.NewMatch && !same.NewAuthority && !same.RoomChanged,
        "same identity update does not report a stream transition");

    passed &= Check(identity.Apply(Match(4, 99)).Rejected == SessionMatchIdentity::Rejection::StaleAuthority
        && identity.Apply(Match(3, 100)).Rejected == SessionMatchIdentity::Rejection::StaleMatch
        && identity.MatchId(false) == 4 && identity.AuthorityEpoch(false) == 100,
        "stale authority and match updates leave identity unchanged");
    passed &= Check(identity.Apply(Match(4, 100, true)).Accepted(), "ending state is accepted");
    passed &= Check(identity.Apply(Match(4, 100)).Rejected == SessionMatchIdentity::Rejection::EndingRegression,
        "an ending match cannot return to an active state");

    const auto nextMatch = identity.Apply(Match(5, 100));
    passed &= Check(nextMatch.Accepted() && nextMatch.NewMatch && !nextMatch.NewAuthority,
        "new match in the same authority reports only a match transition");
    const auto migrated = identity.Apply(Match(5, 101));
    passed &= Check(migrated.Accepted() && !migrated.NewMatch && migrated.NewAuthority
        && !migrated.RoomChanged, "authority migration preserves same-match identity and room");
    const auto changedRoom = identity.Apply(Match(5, 101, false, "MP4"));
    passed &= Check(changedRoom.Accepted() && !changedRoom.NewMatch && !changedRoom.NewAuthority
        && changedRoom.RoomChanged, "room transition is reported independently");

    identity.SetAuthoritative(Match(1, 200));
    passed &= Check(identity.MatchId(false) == 1 && identity.AuthorityEpoch(false) == 200,
        "local authority can set its state directly");
    identity.Reset();
    passed &= Check(!identity.HasMatch() && identity.MatchId(false) == 0,
        "session stop clears owned match identity");
    return passed ? 0 : 1;
}
