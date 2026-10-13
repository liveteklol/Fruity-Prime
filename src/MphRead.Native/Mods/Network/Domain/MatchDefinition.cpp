#include "Mods/Network/Domain/MatchDefinition.hpp"

namespace MphRead::Mods::Network
{
    std::string ToString(MatchFormat value)
    {
        switch (value)
        {
        case MatchFormat::Auto: return "Auto";
        case MatchFormat::FreeForAll: return "FreeForAll";
        case MatchFormat::OneVsOne: return "OneVsOne";
        case MatchFormat::TwoVsTwo: return "TwoVsTwo";
        case MatchFormat::ThreeVsThree: return "ThreeVsThree";
        case MatchFormat::FourVsFour: return "FourVsFour";
        case MatchFormat::TwoVsTwoVsTwoVsTwo: return "TwoVsTwoVsTwoVsTwo";
        case MatchFormat::Custom: return "Custom";
        }
        return std::to_string(static_cast<std::int32_t>(value));
    }

    std::string ToString(SessionPhase value)
    {
        switch (value)
        {
        case SessionPhase::Lobby: return "Lobby";
        case SessionPhase::Starting: return "Starting";
        case SessionPhase::InMatch: return "InMatch";
        case SessionPhase::PostMatch: return "PostMatch";
        }
        return std::to_string(static_cast<std::int32_t>(value));
    }

    bool MatchGoalRules::UsesLives(::MphRead::GameMode mode) noexcept
    {
        return mode == ::MphRead::GameMode::Survival || mode == ::MphRead::GameMode::SurvivalTeams;
    }

    bool MatchGoalRules::UsesTimeTarget(::MphRead::GameMode mode) noexcept
    {
        return mode == ::MphRead::GameMode::Defender || mode == ::MphRead::GameMode::DefenderTeams
            || mode == ::MphRead::GameMode::PrimeHunter;
    }

    std::uint16_t MatchGoalRules::DefaultValue(::MphRead::GameMode mode) noexcept
    {
        using ::MphRead::GameMode;
        switch (mode)
        {
        case GameMode::Battle:
        case GameMode::BattleTeams:
            return 7;
        case GameMode::Survival:
        case GameMode::SurvivalTeams:
            return 2; // two spare lives = three total
        case GameMode::Bounty:
        case GameMode::BountyTeams:
            return 3;
        case GameMode::Capture:
            return 5;
        case GameMode::Defender:
        case GameMode::DefenderTeams:
            return 90;
        case GameMode::Nodes:
        case GameMode::NodesTeams:
            return 70;
        case GameMode::PrimeHunter:
            return 90;
        default:
            return 0;
        }
    }

    SessionRules MatchDefinition::Rules() const noexcept
    {
        return (FriendlyFire ? SessionRules::FriendlyFire : SessionRules::None)
            | (AffinityWeapons ? SessionRules::AffinityWeapons : SessionRules::None)
            | (ShadowFreeze ? SessionRules::ShadowFreeze : SessionRules::None)
            | (HideOpponentHealth ? SessionRules::HideOpponentHealth : SessionRules::None);
    }
}
