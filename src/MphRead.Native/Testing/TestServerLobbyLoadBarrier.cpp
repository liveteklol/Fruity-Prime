#include "Mods/Network/Server/Lobby/ServerLobbyLoadBarrier.hpp"

#include <iostream>
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
}

int main()
{
    using namespace MphRead::Mods::Network;

    ServerLobbyState state;
    state.SetPhase(SessionPhase::Starting);
    state.SetStartDeadline(30.0);
    state.ExpectLoadedSlot(0);
    state.ExpectLoadedSlot(1);

    bool passed = true;
    passed &= Check(!ServerLobbyLoadBarrier::AcceptLoaded(state, 0, 6, 7)
        && !state.IsLoadedSlot(0), "stale match acknowledgements do not mutate the barrier");
    passed &= Check(!ServerLobbyLoadBarrier::AcceptLoaded(state, 2, 7, 7)
        && !state.IsLoadedSlot(2), "unexpected slots cannot satisfy the barrier");
    passed &= Check(ServerLobbyLoadBarrier::AcceptLoaded(state, 0, 7, 7)
        && state.IsLoadedSlot(0), "the expected slot is recorded once");
    passed &= Check(!ServerLobbyLoadBarrier::AcceptLoaded(state, 0, 7, 7),
        "duplicate acknowledgements do not advance the barrier");

    const ServerLobbyLoadDecision waiting = ServerLobbyLoadBarrier::Evaluate(state, 29.99);
    passed &= Check(waiting.NextAction == ServerLobbyLoadDecision::Action::Wait,
        "an incomplete barrier waits before its deadline");
    const ServerLobbyLoadDecision timeout = ServerLobbyLoadBarrier::Evaluate(state, 30.0);
    passed &= Check(timeout.NextAction == ServerLobbyLoadDecision::Action::StartMatch
        && timeout.DeadlineElapsed, "the exact deadline releases late clients into the match");

    ServerLobbyLoadBarrier::AcceptLoaded(state, 1, 7, 7);
    const ServerLobbyLoadDecision complete = ServerLobbyLoadBarrier::Evaluate(state, 29.0);
    passed &= Check(complete.NextAction == ServerLobbyLoadDecision::Action::StartMatch
        && !complete.DeadlineElapsed, "all expected slots release the barrier before timeout");
    state.SetPhase(SessionPhase::Lobby);
    passed &= Check(ServerLobbyLoadBarrier::Evaluate(state, 40.0).NextAction
        == ServerLobbyLoadDecision::Action::Ignore, "other phases do not run the start barrier");

    return passed ? 0 : 1;
}
