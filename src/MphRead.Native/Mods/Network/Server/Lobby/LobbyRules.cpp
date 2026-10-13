#include "Mods/Network/Server/Lobby/LobbyRules.hpp"

#include "Mods/Network/Domain/MatchDefinitionRules.hpp"
#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"

#include "NativeRuntime/System/Managed.hpp"

#include <array>

namespace MphRead::Mods::Network
{
    namespace Runtime = ::MphRead::NativeRuntime;
    using Multiplayer::TeamLayout;

    TeamLayout LobbyRules::ResolveTeamLayout(const MatchDefinition& match)
    {
        return MatchDefinitionRules::ResolveTeamLayout(match);
    }

    std::int32_t LobbyRules::TeamCount(const MatchDefinition& match)
    {
        return ResolveTeamLayout(match).TeamCount;
    }

    std::int32_t LobbyRules::TeamCapacity(const MatchDefinition& match, std::int32_t team)
    {
        return ResolveTeamLayout(match).Capacity(team);
    }

    bool LobbyRules::ExactTeams(const MatchDefinition& match)
    {
        return MatchDefinitionRules::ExactTeams(match);
    }

    Multiplayer::MatchWorldProfile LobbyRules::ResolveWorldProfile(const MatchDefinition& match, std::int32_t maxPlayers)
    {
        return MatchDefinitionRules::ResolveWorldProfile(match, maxPlayers);
    }

    LobbyResultCode LobbyRules::ValidateDefinition(const MatchDefinition& match, std::string& reason)
    {
        return MatchDefinitionRules::Validate(match, reason)
            ? LobbyResultCode::Ok : LobbyResultCode::InvalidConfiguration;
    }

    LobbyResultCode LobbyRules::Validate(const MatchDefinition& match, const RosterPacket& roster,
        bool requireReady, std::string& reason)
    {
        const LobbyResultCode result = ValidateDefinition(match, reason);
        if (result != LobbyResultCode::Ok)
        {
            return result;
        }
        const TeamLayout layout = ResolveTeamLayout(match);
        const bool exact = MatchDefinitionRules::ExactTeams(match);
        const std::int32_t required = exact ? layout.TotalPlayers() : match.Format == MatchFormat::FreeForAll ? 2 : 1;
        if (roster.Count < required || (exact && roster.Count != required))
        {
            reason = exact ? layout.ToString() + " requires exactly " + Runtime::ToString(required) + " players."
                : "At least " + Runtime::ToString(required) + " players must join.";
            return LobbyResultCode::NotEnoughPlayers;
        }
        std::array<std::int32_t, 4> counts{};
        for (std::int32_t i = 0; i < roster.Count; i++)
        {
            if (layout.TeamCount > 0)
            {
                const auto& teams = Runtime::RequireReference(roster.Teams);
                const std::int32_t team = Runtime::ManagedAt(teams, i);
                if (team < 0 || team >= layout.TeamCount)
                {
                    reason = "Every player needs a valid team.";
                    return LobbyResultCode::InvalidTeam;
                }
                counts[static_cast<std::size_t>(team)]++;
            }
            if (requireReady)
            {
                const auto& ready = Runtime::RequireReference(roster.LobbyReady);
                if (!Runtime::ManagedAt(ready, i))
                {
                    const auto& names = Runtime::RequireReference(roster.Names);
                    reason = "Waiting for " + Runtime::ManagedAt(names, i).value_or("") + " to ready.";
                    return LobbyResultCode::PlayersNotReady;
                }
            }
        }
        for (std::int32_t team = 0; team < layout.TeamCount; team++)
        {
            const std::int32_t capacity = layout.Capacity(team);
            const std::int32_t count = counts[static_cast<std::size_t>(team)];
            if (count > capacity || (exact && count != capacity))
            {
                reason = std::string("Team ") + static_cast<char>('A' + team) + " needs " + Runtime::ToString(capacity)
                    + " players (currently " + Runtime::ToString(count) + ").";
                return LobbyResultCode::InvalidTeam;
            }
        }
        reason = "Ready to start.";
        return LobbyResultCode::Ok;
    }
}
