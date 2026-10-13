#include "Mods/Network/Server/Lobby/ServerSessionStateBuilder.hpp"

#include "Mods/Network/Server/Lobby/LobbyRules.hpp"

namespace MphRead::Mods::Network
{
    SessionStatePacket ServerSessionStateBuilder::Build(
        const ServerLobbyState& lobby,
        const ServerPeerRegistry& peers,
        const MatchDefinition& currentMatch,
        std::uint16_t matchId,
        std::uint64_t authorityEpoch,
        std::int32_t maxPlayers)
    {
        SessionStatePacket state{};
        state.Phase = lobby.Phase();
        state.Policy = lobby.Policy();
        state.Revision = lobby.SessionRevision();
        state.MatchId = matchId;
        state.AuthorityEpoch = authorityEpoch;
        state.OwnerSlot = 255;
        if (lobby.LobbyOwnerClientId() != 0)
        {
            for (const ServerPeerRegistry::PeerHandle& peer : peers.All())
            {
                if (peer->ClientId == lobby.LobbyOwnerClientId())
                {
                    state.OwnerSlot = static_cast<std::uint8_t>(peer->SlotIndex);
                    break;
                }
            }
        }
        state.MaxPlayers = static_cast<std::uint8_t>(maxPlayers);
        state.Match = currentMatch;
        state.WorldProfile = lobby.Policy() == ServerSessionPolicy::Lobby
            ? (lobby.Phase() != SessionPhase::Lobby ? lobby.FrozenWorldProfile()
                : LobbyRules::ResolveWorldProfile(currentMatch, maxPlayers))
            : lobby.MapWorldProfile().IsValid() ? lobby.MapWorldProfile()
                : LobbyRules::ResolveWorldProfile(currentMatch, 2);

        SessionRules rules = currentMatch.Rules();
        if (lobby.RequireReady())
        {
            rules = rules | SessionRules::RequireReady;
        }
        if (lobby.AllowJoinInProgress())
        {
            rules = rules | SessionRules::AllowJoinInProgress;
        }
        if (lobby.LockTeams())
        {
            rules = rules | SessionRules::LockTeams;
        }
        state.RuleFlags = rules;
        state.ExpectedParticipants = lobby.ExpectedLoadedSlots();
        state.LoadedParticipants = lobby.LoadedSlots();
        return state;
    }
}
