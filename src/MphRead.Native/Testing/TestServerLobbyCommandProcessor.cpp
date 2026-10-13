#include "Mods/Network/Server/Lobby/ServerLobbyCommandProcessor.hpp"

#include "Mods/Network/Protocol/Packets/SessionPackets.hpp"
#include "Mods/Network/Protocol/SessionProtocol.hpp"

#include <iostream>
#include <memory>
#include <optional>
#include <string>
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

    MatchDefinition Definition(MphRead::GameMode mode, MatchFormat format)
    {
        MatchDefinition definition{};
        definition.RoomKey = "MP1 SANCTORUS";
        definition.Mode = mode;
        definition.Format = format;
        definition.TimeLimitSeconds = 420;
        definition.PointGoal = 7;
        return definition;
    }

    std::optional<std::string> ResolveRoom(const std::string& room)
    {
        if (room == "MP1 SANCTORUS" || room == "MP3 PROVING GROUND")
        {
            return room;
        }
        return std::nullopt;
    }
}

int main()
{
    using namespace MphRead::Mods::Network;

    ServerLobbyState state;
    state.SetPolicy(ServerSessionPolicy::Lobby);
    state.SetPhase(SessionPhase::Lobby);
    state.SetLobbyMatch(Definition(MphRead::GameMode::Battle, MatchFormat::FreeForAll));
    state.SetLobbyOwnerClientId(1);

    ServerPeerRegistry peers;
    auto owner = std::make_shared<ServerPeerRegistry::Peer>();
    owner->ClientId = 1;
    owner->SlotIndex = 0;
    auto guest = std::make_shared<ServerPeerRegistry::Peer>();
    guest->ClientId = 2;
    guest->SlotIndex = 1;
    peers.Add(owner);
    peers.Add(guest);

    bool passed = true;
    LobbyCommandPacket ready{};
    ready.ExpectedRevision = state.SessionRevision();
    ready.Type = LobbyCommandType::SetReady;
    ready.Ready = true;
    ServerLobbyCommandExecution result = ServerLobbyCommandProcessor::Execute(
        state, peers, guest, ready, 8, RosterPacket{}, &ResolveRoom);
    passed &= Check(result.Result == LobbyResultCode::Ok && guest->Lobby.LobbyReady(),
        "a guest may set their own ready state");

    LobbyCommandPacket stale = ready;
    stale.ExpectedRevision = static_cast<std::uint16_t>(state.SessionRevision() - 1);
    stale.Ready = false;
    result = ServerLobbyCommandProcessor::Execute(
        state, peers, guest, stale, 8, RosterPacket{}, &ResolveRoom);
    passed &= Check(result.Result == LobbyResultCode::StaleRevision && guest->Lobby.LobbyReady(),
        "a stale command is rejected without mutating peer state");

    LobbyCommandPacket update{};
    update.ExpectedRevision = state.SessionRevision();
    update.Type = LobbyCommandType::UpdateMatch;
    update.Configuration.Match = Definition(MphRead::GameMode::Battle, MatchFormat::FreeForAll);
    update.Configuration.Match.RoomKey = "MP3 PROVING GROUND";
    result = ServerLobbyCommandProcessor::Execute(
        state, peers, owner, update, 8, RosterPacket{}, &ResolveRoom);
    passed &= Check(result.Result == LobbyResultCode::Ok
        && state.LobbyMatch().RoomKey == std::optional<std::string>("MP3 PROVING GROUND")
        && !guest->Lobby.LobbyReady(),
        "a validated match update normalizes its room and invalidates lobby readiness");

    LobbyCommandPacket start{};
    start.ExpectedRevision = state.SessionRevision();
    start.Type = LobbyCommandType::StartMatch;
    result = ServerLobbyCommandProcessor::Execute(
        state, peers, owner, start, 8, RosterPacket{}, &ResolveRoom);
    passed &= Check(result.Result == LobbyResultCode::NotEnoughPlayers
        && result.NextAction == ServerLobbyCommandExecution::Action::None,
        "match policy rejects an undersized roster before requesting server effects");

    state.SetLobbyMatch(Definition(MphRead::GameMode::BattleTeams, MatchFormat::TwoVsTwo));
    owner->Lobby.SetLobbyReady(true);
    guest->Lobby.SetLobbyReady(true);
    LobbyCommandPacket team{};
    team.ExpectedRevision = state.SessionRevision();
    team.Type = LobbyCommandType::SetTeam;
    team.TargetSlot = static_cast<std::uint8_t>(guest->SlotIndex);
    team.TeamIndex = 0;
    result = ServerLobbyCommandProcessor::Execute(
        state, peers, owner, team, 8, RosterPacket{}, &ResolveRoom);
    passed &= Check(result.Result == LobbyResultCode::Ok
        && guest->Lobby.TeamIndex() == 0 && !guest->Lobby.LobbyReady(),
        "team assignment updates the peer owner and clears that peer's ready state");

    return passed ? 0 : 1;
}
