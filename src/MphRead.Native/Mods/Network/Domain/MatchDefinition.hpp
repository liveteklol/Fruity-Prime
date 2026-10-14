#pragma once

#include "Mods/Multiplayer/TeamLayout.hpp"
#include "Formats/Formats.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace MphRead::Mods::Network
{
    enum class SessionPhase : std::uint8_t { Lobby, Starting, InMatch, PostMatch };
    // SessionPhase.ToString().
    [[nodiscard]] std::string ToString(SessionPhase value);
    enum class ServerSessionPolicy : std::uint8_t { Continuous, Lobby };
    enum class MatchFormat : std::uint8_t
    {
        Auto, FreeForAll, OneVsOne, TwoVsTwo, ThreeVsThree, FourVsFour, TwoVsTwoVsTwoVsTwo, Custom
    };
    // MatchFormat.ToString().
    [[nodiscard]] std::string ToString(MatchFormat value);

    // The wire keeps one ushort for a mode's win condition. Point-scored modes
    // use it directly, Survival stores spare lives, and Defender / Prime
    // Hunter store the required control time in seconds.
    class MatchGoalRules final
    {
    public:
        MatchGoalRules() = delete;

        [[nodiscard]] static bool UsesLives(::MphRead::GameMode mode) noexcept;
        [[nodiscard]] static bool UsesTimeTarget(::MphRead::GameMode mode) noexcept;
        [[nodiscard]] static std::uint16_t DefaultValue(::MphRead::GameMode mode) noexcept;
    };

    // [Flags]
    enum class SessionRules : std::uint16_t
    {
        None = 0, FriendlyFire = 1, AffinityWeapons = 2, ShadowFreeze = 4,
        RequireReady = 8, AllowJoinInProgress = 16, LockTeams = 32,
        HideOpponentHealth = 64, Instagib = 128
    };

    [[nodiscard]] constexpr SessionRules operator|(SessionRules left, SessionRules right) noexcept
    {
        return static_cast<SessionRules>(
            static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr SessionRules operator&(SessionRules left, SessionRules right) noexcept
    {
        return static_cast<SessionRules>(
            static_cast<std::uint16_t>(left) & static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr SessionRules operator~(SessionRules value) noexcept
    {
        return static_cast<SessionRules>(static_cast<std::uint16_t>(~static_cast<std::uint16_t>(value)));
    }

    constexpr SessionRules& operator|=(SessionRules& left, SessionRules right) noexcept
    {
        return left = left | right;
    }

    constexpr SessionRules& operator&=(SessionRules& left, SessionRules right) noexcept
    {
        return left = left & right;
    }

    struct MatchDefinition final
    {
        std::optional<std::string> RoomKey{};
        ::MphRead::GameMode Mode = ::MphRead::GameMode::None;
        MatchFormat Format = MatchFormat::Auto;
        Multiplayer::TeamLayout CustomTeams{};
        std::uint16_t TimeLimitSeconds = 0;
        std::uint16_t PointGoal = 0;
        bool FriendlyFire = false;
        bool AffinityWeapons = false;
        bool ShadowFreeze = false;
        bool HideOpponentHealth = false;
        bool Instagib = false;

        [[nodiscard]] SessionRules Rules() const noexcept;

        [[nodiscard]] friend bool operator==(
            const MatchDefinition& left, const MatchDefinition& right) = default;
    };
}
