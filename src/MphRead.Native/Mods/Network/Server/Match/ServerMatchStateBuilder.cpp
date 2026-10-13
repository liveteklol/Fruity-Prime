#include "Mods/Network/Server/Match/ServerMatchStateBuilder.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    MatchStatePacket ServerMatchStateBuilder::Build(const MatchDefinition& definition,
        SessionPhase phase, double now, double matchStarted, bool ending,
        std::size_t playerCount, std::uint16_t matchId, std::uint64_t authorityEpoch,
        std::int32_t damageLevel, const std::string& nextRoomKey)
    {
        const float elapsed = phase == SessionPhase::Lobby || phase == SessionPhase::Starting
            ? 0.0F : static_cast<float>(now - matchStarted);
        MatchStatePacket state{};
        state.Mode = static_cast<std::uint8_t>(definition.Mode);
        state.TimeRemaining = ending || definition.TimeLimitSeconds <= 0
            ? 0.0F
            : std::max(0.0F, static_cast<float>(definition.TimeLimitSeconds) - elapsed);
        state.TimeElapsed = elapsed;
        state.PlayerCount = static_cast<std::uint8_t>(playerCount);
        state.Flags = static_cast<std::uint8_t>((ending ? MatchStatePacket::FlagEnding : MatchStatePacket::FlagInProgress)
            | (definition.FriendlyFire ? MatchStatePacket::FlagFriendlyFire : 0)
            | (definition.ShadowFreeze ? 0 : MatchStatePacket::FlagNoShadowFreeze)
            | MatchStatePacket::RuleFlags(damageLevel, definition.AffinityWeapons));
        state.PointGoal = definition.PointGoal;
        state.MatchId = matchId;
        state.AuthorityEpoch = authorityEpoch;
        state.RoomKey = definition.RoomKey;
        state.NextRoomKey = nextRoomKey;
        return state;
    }
}
