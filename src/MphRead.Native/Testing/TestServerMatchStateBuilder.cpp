#include "Mods/Network/Server/Match/ServerMatchStateBuilder.hpp"

#include <iostream>
#include <string_view>

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
}

int main()
{
    using namespace MphRead;
    using namespace MphRead::Mods::Network;

    MatchDefinition definition{};
    definition.Mode = GameMode::Battle;
    definition.TimeLimitSeconds = 120;
    definition.PointGoal = 8;
    definition.RoomKey = "arena";
    definition.FriendlyFire = true;
    definition.AffinityWeapons = true;
    definition.ShadowFreeze = true;

    const MatchStatePacket inMatch = ServerMatchStateBuilder::Build(definition,
        SessionPhase::InMatch, 145.0, 100.0, false, 3, 23, 456, 1, "outpost");
    bool passed = Check(inMatch.Mode == static_cast<std::uint8_t>(GameMode::Battle)
        && inMatch.TimeElapsed == 45.0F && inMatch.TimeRemaining == 75.0F,
        "match time is derived from the fixed match start")
        && Check(inMatch.PlayerCount == 3 && inMatch.MatchId == 23
            && inMatch.AuthorityEpoch == 456 && inMatch.RoomKey == "arena"
            && inMatch.NextRoomKey == "outpost" && inMatch.PointGoal == 8,
            "snapshot copies authoritative match identity and map fields")
        && Check(inMatch.Flags == static_cast<std::uint8_t>(MatchStatePacket::FlagInProgress
            | MatchStatePacket::FlagFriendlyFire | MatchStatePacket::RuleFlags(1, true)),
            "snapshot encodes match rules without changing the protocol flags");

    const MatchStatePacket starting = ServerMatchStateBuilder::Build(definition,
        SessionPhase::Starting, 145.0, 100.0, false, 3, 23, 456, 1, "outpost");
    passed &= Check(starting.TimeElapsed == 0.0F && starting.TimeRemaining == 120.0F,
        "lobby load barrier phases keep the match clock at zero");

    definition.TimeLimitSeconds = 0;
    const MatchStatePacket ended = ServerMatchStateBuilder::Build(definition,
        SessionPhase::PostMatch, 145.0, 100.0, true, 3, 23, 456, 1, "outpost");
    passed &= Check(ended.TimeRemaining == 0.0F
        && ended.Flags == static_cast<std::uint8_t>(MatchStatePacket::FlagEnding
            | MatchStatePacket::FlagFriendlyFire | MatchStatePacket::RuleFlags(1, true)),
        "ended matches publish zero remaining time and the ending flag");

    return passed ? 0 : 1;
}
