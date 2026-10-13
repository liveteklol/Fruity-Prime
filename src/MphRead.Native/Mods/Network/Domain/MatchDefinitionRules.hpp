#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Multiplayer/MatchWorldProfile.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    class MatchDefinitionRules final
    {
    public:
        static constexpr std::size_t MaxRoomKeyUnits = 40;

        MatchDefinitionRules() = delete;

        [[nodiscard]] static bool IsTeamMode(::MphRead::GameMode mode) noexcept;
        [[nodiscard]] static Multiplayer::TeamLayout ResolveTeamLayout(const MatchDefinition& match);
        [[nodiscard]] static std::int32_t TeamCount(const MatchDefinition& match);
        [[nodiscard]] static std::int32_t TeamCapacity(const MatchDefinition& match, std::int32_t team);
        [[nodiscard]] static bool ExactTeams(const MatchDefinition& match);
        [[nodiscard]] static Multiplayer::MatchWorldProfile ResolveWorldProfile(
            const MatchDefinition& match, std::int32_t maxPlayers);
        [[nodiscard]] static bool Validate(const MatchDefinition& match, std::string& reason);
    };
}
