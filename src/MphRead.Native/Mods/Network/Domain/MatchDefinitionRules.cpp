#include "Mods/Network/Domain/MatchDefinitionRules.hpp"

#include "NativeRuntime/System/Globalization.hpp"

namespace MphRead::Mods::Network
{
    using Multiplayer::TeamLayout;

    bool MatchDefinitionRules::IsTeamMode(::MphRead::GameMode mode) noexcept
    {
        return mode == ::MphRead::GameMode::BattleTeams
            || mode == ::MphRead::GameMode::SurvivalTeams
            || mode == ::MphRead::GameMode::Capture
            || mode == ::MphRead::GameMode::BountyTeams
            || mode == ::MphRead::GameMode::NodesTeams
            || mode == ::MphRead::GameMode::DefenderTeams;
    }

    TeamLayout MatchDefinitionRules::ResolveTeamLayout(const MatchDefinition& match)
    {
        if (!IsTeamMode(match.Mode))
        {
            return {};
        }
        switch (match.Format)
        {
        case MatchFormat::OneVsOne: return TeamLayout(2, 1, 1);
        case MatchFormat::TwoVsTwo: return TeamLayout(2, 2, 2);
        case MatchFormat::ThreeVsThree: return TeamLayout(2, 3, 3);
        case MatchFormat::TwoVsTwoVsTwoVsTwo: return TeamLayout(4, 2, 2, 2, 2);
        case MatchFormat::Custom: return match.CustomTeams;
        default: return TeamLayout(2, 4, 4);
        }
    }

    std::int32_t MatchDefinitionRules::TeamCount(const MatchDefinition& match)
    {
        return ResolveTeamLayout(match).TeamCount;
    }

    std::int32_t MatchDefinitionRules::TeamCapacity(const MatchDefinition& match, std::int32_t team)
    {
        return ResolveTeamLayout(match).Capacity(team);
    }

    bool MatchDefinitionRules::ExactTeams(const MatchDefinition& match)
    {
        return IsTeamMode(match.Mode) && match.Format != MatchFormat::Auto;
    }

    Multiplayer::MatchWorldProfile MatchDefinitionRules::ResolveWorldProfile(
        const MatchDefinition& match, std::int32_t maxPlayers)
    {
        return Multiplayer::MatchWorldProfile::Resolve(
            ExactTeams(match) ? ResolveTeamLayout(match).TotalPlayers() : maxPlayers);
    }

    bool MatchDefinitionRules::Validate(const MatchDefinition& match, std::string& reason)
    {
        reason.clear();
        const bool formatDefined = static_cast<std::uint8_t>(match.Format) <= static_cast<std::uint8_t>(MatchFormat::Custom);
        if (::MphRead::NativeRuntime::StringIsNullOrWhiteSpace(match.RoomKey)
            || ::MphRead::NativeRuntime::Utf16Length(*match.RoomKey) > MaxRoomKeyUnits
            || !::MphRead::IsDefinedGameMode(static_cast<std::uint64_t>(match.Mode))
            || match.Mode == ::MphRead::GameMode::SinglePlayer
            || match.Mode == ::MphRead::GameMode::None
            || match.Mode == ::MphRead::GameMode::Unknown15
            || !formatDefined)
        {
            reason = "Choose a multiplayer map and mode.";
        }
        else if (match.Format != MatchFormat::Auto
            && IsTeamMode(match.Mode) != (match.Format != MatchFormat::FreeForAll))
        {
            reason = "Choose a team mode for a team format, or a free-for-all mode for FFA.";
        }
        else if (IsTeamMode(match.Mode) && !ResolveTeamLayout(match).IsValid())
        {
            reason = "Use 2 to 4 nonempty teams, zero inactive capacities, and at most 8 players.";
        }
        else if (match.Mode == ::MphRead::GameMode::Capture && TeamCount(match) != 2)
        {
            reason = "Capture requires exactly two teams because maps have two bases.";
        }
        return reason.empty();
    }
}
