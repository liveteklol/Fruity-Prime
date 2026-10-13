#include "Mods/Network/Server/Core/ServerPeerIntentState.hpp"

#include <cstdint>
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
    ServerPeerIntentState state;
    bool passed = true;
    passed &= Check(state.Observe(10, false) == ServerPeerIntentState::Disposition::Newest,
        "first intent is newest");
    passed &= Check(state.Observe(10, false) == ServerPeerIntentState::Disposition::StaleWithoutShots,
        "duplicate intent without shots is suppressed");
    passed &= Check(state.Observe(9, true) == ServerPeerIntentState::Disposition::StaleWithShots,
        "stale intent carrying shot history is relayed");
    passed &= Check(state.LastFrame() == 10, "stale intent does not move the watermark");
    passed &= Check(state.Observe(11, false) == ServerPeerIntentState::Disposition::Newest,
        "newer intent advances the watermark");
    passed &= Check(state.Observe(0xFFFFFFFEU, false)
        == ServerPeerIntentState::Disposition::StaleWithoutShots,
        "out-of-order frame does not advance the watermark");
    passed &= Check(state.Observe(12, false) == ServerPeerIntentState::Disposition::Newest,
        "newer sequence advances after stale packets");
    state.Reset();
    passed &= Check(state.LastFrame() == 0
        && state.Observe(0xFFFFFFFEU, false) == ServerPeerIntentState::Disposition::Newest
        && state.Observe(1, false) == ServerPeerIntentState::Disposition::Newest,
        "intent sequence comparison remains wrap-safe after reset");
    state.Reset();
    passed &= Check(state.Observe(7, false) == ServerPeerIntentState::Disposition::Newest,
        "match reset clears the intent watermark");
    return passed ? 0 : 1;
}
