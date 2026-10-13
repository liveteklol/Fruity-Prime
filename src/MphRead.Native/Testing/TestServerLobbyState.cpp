#include "Mods/Network/Server/Lobby/ServerLobbyState.hpp"

#include <iostream>
#include <string_view>

namespace
{
    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    using MphRead::Mods::Network::MatchDefinition;
    using MphRead::Mods::Network::MatchFormat;
    using MphRead::Mods::Network::NetworkSlotCapacity;
    using MphRead::Mods::Network::ServerLobbyState;
    using MphRead::Mods::Network::ServerSessionPolicy;
    using MphRead::Mods::Network::SessionPhase;

    bool passed = true;
    ServerLobbyState state;
    passed &= Check(state.Policy() == ServerSessionPolicy::Continuous
        && state.Format() == MatchFormat::Auto
        && state.Phase() == SessionPhase::InMatch
        && state.SessionRevision() == 1,
        "lobby state starts with the continuous-session defaults");

    state.SetPolicy(ServerSessionPolicy::Lobby);
    state.SetFormat(MatchFormat::TwoVsTwo);
    state.SetSessionOptions(false, true);
    state.SetLockTeams(true);
    state.SetPhase(SessionPhase::Lobby);
    MatchDefinition lobbyMatch{};
    lobbyMatch.Format = MatchFormat::TwoVsTwo;
    state.SetLobbyMatch(lobbyMatch);
    state.FreezeMatch(lobbyMatch);
    passed &= Check(state.Policy() == ServerSessionPolicy::Lobby
        && state.Format() == MatchFormat::TwoVsTwo
        && !state.RequireReady() && state.AllowJoinInProgress() && state.LockTeams()
        && state.Phase() == SessionPhase::Lobby
        && state.LobbyMatch() == lobbyMatch && state.FrozenMatch() == lobbyMatch,
        "configuration and match snapshots change through semantic owner methods");

    passed &= Check(state.AdvanceSessionRevision() == 1 && state.SessionRevision() == 2
        && state.AdvanceSessionRevision() == 2 && state.SessionRevision() == 3,
        "revision increments are owned and expose the previous value for diagnostics");

    state.ResetLoadBarrier();
    state.ExpectLoadedSlot(0);
    state.ExpectLoadedSlot(2);
    state.ExpectLoadedSlot(static_cast<std::int32_t>(NetworkSlotCapacity) - 1);
    state.ExpectLoadedSlot(-1);
    state.ExpectLoadedSlot(static_cast<std::int32_t>(NetworkSlotCapacity));
    passed &= Check(state.ExpectedLoadedSlots() == 0x85 && state.LoadedSlots() == 0
        && state.IsExpectedLoadedSlot(0) && state.IsExpectedLoadedSlot(2)
        && state.IsExpectedLoadedSlot(7) && !state.IsExpectedLoadedSlot(-1)
        && !state.IsExpectedLoadedSlot(8) && !state.LoadBarrierComplete(),
        "load barrier accepts only valid slot indices");

    state.MarkLoadedSlot(0);
    state.MarkLoadedSlot(2);
    passed &= Check(state.IsLoadedSlot(0) && state.IsLoadedSlot(2)
        && !state.LoadBarrierComplete(),
        "load barrier stays open until every expected player reports");
    state.MarkLoadedSlot(7);
    passed &= Check(state.LoadBarrierComplete(),
        "load barrier closes when every expected player reports");
    state.RemoveLoadedSlot(2);
    passed &= Check(state.ExpectedLoadedSlots() == 0x81 && state.LoadedSlots() == 0x81
        && state.LoadBarrierComplete(),
        "removing a player clears both expected and loaded bits");
    state.ResetLoadBarrier();
    passed &= Check(state.ExpectedLoadedSlots() == 0 && state.LoadedSlots() == 0
        && state.LoadBarrierComplete(),
        "barrier reset clears both masks");

    state.SetStartDeadline(12.5);
    state.SetMapWorldProfile({});
    state.FreezeWorldProfile({});
    passed &= Check(state.StartDeadline() == 12.5,
        "start deadline is controlled by the lobby owner");

    return passed ? 0 : 1;
}
