#include "Mods/Network/Session/SessionRuntimeState.hpp"

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
    using MphRead::Mods::Network::NetRole;
    using MphRead::Mods::Network::SessionRuntimeState;

    bool passed = true;
    SessionRuntimeState state;
    passed &= Check(state.Role() == NetRole::Offline && state.LocalSlot() == 0
        && state.Frame() == 0 && !state.IsPlayback() && !state.LastError().has_value(),
        "initial session runtime is offline and clear");

    state.SetRole(NetRole::Client);
    state.SetLocalSlot(-1);
    state.SetPlayback(true);
    state.AdvanceFrame();
    state.AdvanceFrame();
    passed &= Check(state.Role() == NetRole::Client && state.LocalSlot() == -1
        && state.Frame() == 2 && state.IsPlayback(),
        "playback client state and frame advancement are owned together");

    state.SetLocalSlot(3);
    state.ResetFrame();
    state.ClearLastError();
    passed &= Check(state.LocalSlot() == 3 && state.Frame() == 0
        && !state.LastError().has_value(), "slot assignment and frame reset");

    state.SetLastError("join failed");
    state.SetRole(NetRole::Offline);
    state.SetPlayback(false);
    passed &= Check(state.Role() == NetRole::Offline && !state.IsPlayback()
        && state.LastError() == std::optional<std::string>("join failed"),
        "stopping preserves the last failure until the next successful start clears it");

    state.ClearLastError();
    state.SetRole(NetRole::Server);
    state.SetLocalSlot(-1);
    state.AdvanceFrame();
    passed &= Check(state.Role() == NetRole::Server && state.LocalSlot() == -1
        && state.Frame() == 1 && !state.LastError().has_value(),
        "server run can begin after a cleared failure");

    return passed ? 0 : 1;
}
