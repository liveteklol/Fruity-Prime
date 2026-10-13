#include "Mods/Network/Session/RemotePlayerChannel.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
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
    using namespace MphRead::Mods::Network;

    RemotePlayerChannel channel;
    bool passed = true;
    passed &= Check(!channel.HasState(2), "initial state is invalid");
    passed &= Check(!channel.HasIntent(2), "initial intent is invalid");

    PlayerState state{};
    state.SlotIndex = 2;
    state.Health = 37;
    channel.StoreState(state);
    passed &= Check(channel.HasState(2), "stored state becomes valid");
    passed &= Check(channel.StateAt(2).Health == 37, "state is read from its slot");

    PlayerState outOfRange{};
    outOfRange.SlotIndex = static_cast<std::int32_t>(RemotePlayerChannel::SlotCapacity);
    channel.StoreState(outOfRange);
    passed &= Check(!channel.HasState(outOfRange.SlotIndex), "out-of-range state is ignored");

    IntentPacket intent{};
    intent.Frame = 120;
    channel.StoreIntent(3, intent, 100);
    passed &= Check(channel.HasIntent(3), "stored intent becomes valid");
    passed &= Check(channel.IntentAt(3).Frame == 120, "intent is read from its slot");
    passed &= Check(channel.IntentAge(3, 112) == 12, "intent age uses its arrival frame");
    passed &= Check(channel.IntentAge(-1, 112) == std::numeric_limits<std::uint32_t>::max(),
        "invalid intent slot has sentinel age");

    channel.InvalidateIntent(3);
    passed &= Check(!channel.HasIntent(3), "single-slot intent invalidation");
    channel.InvalidateStates();
    passed &= Check(!channel.HasState(2), "state invalidation");
    channel.Reset();
    passed &= Check(!channel.HasIntent(3) && !channel.HasState(2), "reset invalidates every slot");
    passed &= Check(channel.IntentAge(3, 200) == std::numeric_limits<std::uint32_t>::max(),
        "reset clears intent arrival time");
    return passed ? 0 : 1;
}
