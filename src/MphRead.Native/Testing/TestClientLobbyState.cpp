#include "Mods/Network/Session/ClientLobbyState.hpp"

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
    using MphRead::Mods::Network::ClientLobbyState;
    using MphRead::Mods::Network::LobbyCommandPacket;
    using MphRead::Mods::Network::MatchStatePacket;
    using MphRead::Mods::Network::NetworkSlotCapacity;
    using MphRead::Mods::Network::ServerSessionPolicy;
    using MphRead::Mods::Network::SessionPhase;
    using MphRead::Mods::Network::SessionStatePacket;

    bool passed = true;
    ClientLobbyState state;
    passed &= Check(!state.HasSession() && state.Session() == std::nullopt
        && state.Message().empty() && !state.HasPendingCommand()
        && state.Phase() == SessionPhase::InMatch && state.SessionRevision() == 0
        && !state.Persistent() && !state.FreezeGameplay() && !state.ShouldLoadMatch(0),
        "lobby owner begins empty");

    SessionStatePacket lobby{};
    lobby.MatchId = 7;
    lobby.AuthorityEpoch = 3;
    lobby.Revision = 4;
    lobby.Policy = ServerSessionPolicy::Lobby;
    lobby.Phase = SessionPhase::Starting;
    lobby.OwnerSlot = 0;
    lobby.ExpectedParticipants = 1;
    passed &= Check(state.AcceptsSessionState(lobby, std::nullopt)
        && state.SetSession(lobby) && state.Session()->MatchId == 7
        && state.Phase() == SessionPhase::Starting && state.SessionRevision() == 4
        && state.ActiveMatchDefinition() == lobby.Match && state.Persistent()
        && state.FreezeGameplay() && state.IsStarting() && !state.IsInLobby()
        && !state.CanEditLobby(0) && state.IsLocalOwner(0)
        && state.ShouldLoadMatch(0) && !state.ShouldLoadMatch(1)
        && !state.ShouldLoadMatch(-1)
        && !state.ShouldLoadMatch(static_cast<std::int32_t>(NetworkSlotCapacity)),
        "valid lobby state is accepted and owned");
    MatchStatePacket lobbyMatch{};
    lobbyMatch.MatchId = lobby.MatchId;
    passed &= Check(state.AcceptsMatchState(lobbyMatch),
        "starting lobby accepts only its current match packet");

    SessionStatePacket editing = lobby;
    editing.Revision = 5;
    editing.Phase = SessionPhase::Lobby;
    static_cast<void>(state.SetSession(editing));
    passed &= Check(state.IsInLobby()
        && state.CanEditLobby(0) && !state.CanEditLobby(1) && state.FreezeGameplay()
        && !state.AcceptsMatchState(lobbyMatch),
        "lobby phase and owner policy are derived by the lobby state owner");

    SessionStatePacket playing = editing;
    playing.Revision = 6;
    playing.Phase = SessionPhase::InMatch;
    static_cast<void>(state.SetSession(playing));
    passed &= Check(state.IsPlaying()
        && state.ShouldLoadMatch(1) && !state.FreezeGameplay() && !state.IsPostMatch()
        && state.AcceptsMatchState(lobbyMatch),
        "playing state is loadable and unfreezes gameplay");
    MatchStatePacket staleMatch = lobbyMatch;
    staleMatch.MatchId++;
    passed &= Check(!state.AcceptsMatchState(staleMatch),
        "persistent lobby drops control state before its session projection");

    SessionStatePacket postMatch = playing;
    postMatch.Revision = 7;
    postMatch.Phase = SessionPhase::PostMatch;
    postMatch.Policy = ServerSessionPolicy::Continuous;
    static_cast<void>(state.SetSession(postMatch));
    passed &= Check(state.IsPostMatch()
        && !state.Persistent() && !state.IsLocalOwner(-1)
        && state.AcceptsMatchState(staleMatch),
        "post-match and continuous policy are derived by the lobby state owner");

    static_cast<void>(state.SetSession(lobby));

    SessionStatePacket staleRevision = lobby;
    staleRevision.Revision = 3;
    passed &= Check(!state.AcceptsSessionState(staleRevision, std::nullopt),
        "older lobby revision is rejected by the owner");

    MatchStatePacket newerMatch{};
    newerMatch.MatchId = 8;
    newerMatch.AuthorityEpoch = 4;
    passed &= Check(!state.AcceptsSessionState(lobby, newerMatch),
        "session behind current match control is rejected");

    std::uint32_t commandSends = 0;
    LobbyCommandPacket command{};
    command.CommandId = state.NextCommandId();
    state.QueueCommand(command, 0.0);
    state.RetryPendingCommands(0.24, [&commandSends](const LobbyCommandPacket&) { ++commandSends; });
    state.RetryPendingCommands(0.25, [&commandSends](const LobbyCommandPacket&) { ++commandSends; });
    passed &= Check(commandSends == 1 && state.HasPendingCommand(),
        "pending command retries only when due");
    passed &= Check(state.AcknowledgeCommand(command.CommandId) && !state.HasPendingCommand()
        && !state.AcknowledgeCommand(command.CommandId),
        "command acknowledgement removes exactly one pending entry");

    command.CommandId = state.NextCommandId();
    state.QueueCommand(command, 0.0);
    for (double now : {0.25, 0.75, 1.5, 2.5})
    {
        state.RetryPendingCommands(now, [&commandSends](const LobbyCommandPacket&) { ++commandSends; });
    }
    state.RetryPendingCommands(3.5, [&commandSends](const LobbyCommandPacket&) { ++commandSends; });
    passed &= Check(commandSends == 5 && !state.HasPendingCommand()
        && state.Message() == "The server did not acknowledge the command. Check the current lobby and try again.",
        "retry limit reports timeout and releases pending command");

    passed &= Check(!state.IdentityUpdateDue(0.99, 1.0) && state.IdentityUpdateDue(1.0, 1.0)
        && !state.IdentityUpdateDue(1.99, 1.0) && state.IdentityUpdateDue(2.0, 1.0),
        "identity update cadence is owned by lobby state");

    const auto firstLoaded = state.MarkMatchLoaded(10.0, 0.25);
    passed &= Check(firstLoaded == 7 && !state.MarkMatchLoaded(10.1, 0.25).has_value()
        && !state.LoadedMatchRetryDue(10.24, 0.25, true)
        && state.LoadedMatchRetryDue(10.25, 0.25, true),
        "loaded match acknowledgement is rate limited");

    SessionStatePacket nextMatch = lobby;
    nextMatch.MatchId = 8;
    nextMatch.Revision = 5;
    passed &= Check(state.SetSession(nextMatch) && !state.LoadedMatchRetryDue(20.0, 0.25, true)
        && state.MarkMatchLoaded(20.0, 0.25) == 8,
        "new match clears old loaded acknowledgement");

    state.SetRosterSessionRevision(12);
    state.SetMessage("result");
    state.Reset();
    passed &= Check(!state.HasSession() && !state.HasPendingCommand() && state.Message().empty()
        && state.RosterSessionRevision() == 0,
        "lobby reset clears owned session command and message state");

    return passed ? 0 : 1;
}
