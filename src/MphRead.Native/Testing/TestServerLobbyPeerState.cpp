#include "Mods/Network/Server/Lobby/LobbyCommandHistory.hpp"
#include "Mods/Network/Server/Lobby/ServerLobbyPeerState.hpp"

#include <iostream>
#include <string>
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
    using MphRead::Mods::Network::LobbyCommandHistory;
    using MphRead::Mods::Network::LobbyCommandResultPacket;
    using MphRead::Mods::Network::ServerLobbyPeerState;

    bool passed = true;
    ServerLobbyPeerState state;
    passed &= Check(!state.LobbyReady() && !state.PostMatchReady() && state.TeamIndex() == -1,
        "peer lobby state starts unready and unassigned");

    state.SetLobbyReady(true);
    state.SetPostMatchReady(true);
    state.SetTeamIndex(1);
    passed &= Check(state.LobbyReady() && state.PostMatchReady() && state.TeamIndex() == 1,
        "lobby state is read and changed through its owner");

    state.MoveToTeam(2);
    passed &= Check(!state.LobbyReady() && state.TeamIndex() == 2 && state.PostMatchReady(),
        "moving a peer clears lobby-ready without changing post-match readiness");
    state.InvalidateLobbyReady();
    state.ClearPostMatchReady();
    passed &= Check(!state.LobbyReady() && !state.PostMatchReady(),
        "lobby and post-match readiness reset independently");

    LobbyCommandHistory history;
    LobbyCommandResultPacket first{};
    first.CommandId = 7;
    first.Reason = "original";
    passed &= Check(history.Remember(first) && history.Size() == 1
        && history.Find(7).has_value() && history.Find(7)->Reason == "original"
        && !history.Find(8).has_value(),
        "command result is replayable by command id");

    LobbyCommandResultPacket duplicate = first;
    duplicate.Reason = "replacement";
    passed &= Check(!history.Remember(duplicate) && history.Find(7)->Reason == "original",
        "duplicate command ids cannot replace the original result");

    const std::uint32_t lastId = static_cast<std::uint32_t>(LobbyCommandHistory::Capacity + 7);
    for (std::uint32_t id = 8; id <= lastId; ++id)
    {
        LobbyCommandResultPacket result{};
        result.CommandId = id;
        result.Reason = std::to_string(id);
        passed &= Check(history.Remember(result), "new command result enters bounded history");
    }
    passed &= Check(history.Size() == LobbyCommandHistory::Capacity
        && !history.Find(7).has_value() && history.Find(8).has_value()
        && history.Find(lastId).has_value(),
        "history evicts only the oldest result at its capacity");

    return passed ? 0 : 1;
}
