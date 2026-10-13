#pragma once

#include "Mods/Network/Domain/MatchDefinition.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"

#include "Mods/Multiplayer/MatchWorldProfile.hpp"
#include "Mods/Multiplayer/TeamLayout.hpp"

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    struct RosterPacket;

    class LobbyRules final
    {
    public:
        LobbyRules() = delete;

        [[nodiscard]] static Multiplayer::TeamLayout ResolveTeamLayout(const MatchDefinition& match);
        [[nodiscard]] static std::int32_t TeamCount(const MatchDefinition& match);
        [[nodiscard]] static std::int32_t TeamCapacity(const MatchDefinition& match, std::int32_t team);
        [[nodiscard]] static bool ExactTeams(const MatchDefinition& match);
        [[nodiscard]] static Multiplayer::MatchWorldProfile ResolveWorldProfile(
            const MatchDefinition& match, std::int32_t maxPlayers);

        [[nodiscard]] static LobbyResultCode ValidateDefinition(const MatchDefinition& match, std::string& reason);
        [[nodiscard]] static LobbyResultCode Validate(const MatchDefinition& match, const RosterPacket& roster,
            bool requireReady, std::string& reason);
    };
}
