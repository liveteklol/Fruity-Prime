#include "Mods/Network/Server/Lobby/ServerSessionStateBuilder.hpp"

#include "Mods/Multiplayer/MatchWorldProfile.hpp"
#include "Mods/Network/Server/Lobby/LobbyRules.hpp"

#include <iostream>
#include <memory>
#include <string_view>

namespace
{
    using namespace MphRead::Mods::Network;

    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }

    MatchDefinition Definition()
    {
        MatchDefinition match{};
        match.RoomKey = "MP1 SANCTORUS";
        match.Mode = MphRead::GameMode::Battle;
        match.Format = MatchFormat::FreeForAll;
        match.TimeLimitSeconds = 420;
        match.PointGoal = 7;
        return match;
    }
}

int main()
{
    using namespace MphRead::Mods::Network;
    using MphRead::Mods::Multiplayer::MatchWorldProfile;
    using MphRead::Mods::Multiplayer::ResourceSpawnProfile;

    ServerLobbyState lobby;
    lobby.SetPolicy(ServerSessionPolicy::Lobby);
    lobby.SetPhase(SessionPhase::Lobby);
    lobby.SetLobbyMatch(Definition());
    lobby.SetLobbyOwnerClientId(42);
    lobby.SetSessionOptions(true, true);
    lobby.SetLockTeams(true);
    (void)lobby.AdvanceSessionRevision();
    (void)lobby.AdvanceSessionRevision();
    lobby.ExpectLoadedSlot(0);
    lobby.MarkLoadedSlot(0);

    ServerPeerRegistry peers;
    auto owner = std::make_shared<ServerPeerRegistry::Peer>();
    owner->ClientId = 42;
    owner->SlotIndex = 0;
    peers.Add(owner);

    const SessionStatePacket lobbyPacket = ServerSessionStateBuilder::Build(
        lobby, peers, lobby.LobbyMatch(), 23, 456, 4);
    const SessionRules expectedRules = lobby.LobbyMatch().Rules() | SessionRules::RequireReady
        | SessionRules::AllowJoinInProgress | SessionRules::LockTeams;

    bool passed = Check(lobbyPacket.Phase == SessionPhase::Lobby
        && lobbyPacket.Policy == ServerSessionPolicy::Lobby
        && lobbyPacket.Revision == 3 && lobbyPacket.MatchId == 23
        && lobbyPacket.AuthorityEpoch == 456 && lobbyPacket.OwnerSlot == 0
        && lobbyPacket.MaxPlayers == 4,
        "the session snapshot carries identity and preserves owner slot zero")
        && Check(lobbyPacket.RuleFlags == expectedRules
            && lobbyPacket.ExpectedParticipants == 1 && lobbyPacket.LoadedParticipants == 1,
            "the snapshot projects lobby rules and load-barrier masks")
        && Check(lobbyPacket.WorldProfile == LobbyRules::ResolveWorldProfile(lobby.LobbyMatch(), 4),
            "the lobby profile is resolved from current match policy");

    lobby.SetLobbyOwnerClientId(99);
    const SessionStatePacket disconnectedOwner = ServerSessionStateBuilder::Build(
        lobby, peers, lobby.LobbyMatch(), 23, 456, 4);
    passed &= Check(disconnectedOwner.OwnerSlot == 255,
        "a missing owner maps to the protocol no-owner sentinel");

    lobby.FreezeMatch(lobby.LobbyMatch());
    lobby.FreezeWorldProfile(MatchWorldProfile(3, ResourceSpawnProfile::Standard));
    lobby.SetPhase(SessionPhase::Starting);
    const SessionStatePacket starting = ServerSessionStateBuilder::Build(
        lobby, peers, lobby.FrozenMatch(), 24, 457, 4);
    passed &= Check(starting.WorldProfile == lobby.FrozenWorldProfile(),
        "starting phases publish the frozen world profile");

    lobby.SetPolicy(ServerSessionPolicy::Continuous);
    lobby.SetMapWorldProfile(MatchWorldProfile(4, ResourceSpawnProfile::High));
    const SessionStatePacket continuous = ServerSessionStateBuilder::Build(
        lobby, peers, Definition(), 25, 458, 8);
    passed &= Check(continuous.WorldProfile == lobby.MapWorldProfile(),
        "continuous sessions publish the map profile when one is valid");

    return passed ? 0 : 1;
}
