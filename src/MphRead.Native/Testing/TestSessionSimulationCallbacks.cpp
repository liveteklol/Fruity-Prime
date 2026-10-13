#include "Mods/Network/Session/SessionSimulationCallbacks.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>

namespace
{
    using MphRead::Mods::Network::SessionSimulationCallbacks;

    bool Check(bool condition, const char* name)
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
    SessionSimulationCallbacks callbacks;
    bool passed = true;
    std::int32_t snapshots = 0;
    std::int32_t matchEnds = 0;
    callbacks.Configure(
        [&snapshots](std::span<const std::uint8_t> payload)
        {
            if (payload.size() == 3 && payload[0] == 1 && payload[2] == 3)
            {
                ++snapshots;
            }
        },
        [&matchEnds]() { ++matchEnds; });

    const std::array<std::uint8_t, 3> payload{1, 2, 3};
    passed &= Check(callbacks.HasSnapshotSink(), "authority config exposes a snapshot sink");
    callbacks.PublishSnapshot(payload);
    callbacks.NotifyMatchEnded();
    passed &= Check(snapshots == 1 && matchEnds == 1,
        "snapshot and match-end callbacks reach their configured consumers");

    callbacks.Reset();
    callbacks.PublishSnapshot(payload);
    callbacks.NotifyMatchEnded();
    passed &= Check(!callbacks.HasSnapshotSink() && snapshots == 1 && matchEnds == 1,
        "session reset releases callbacks and makes stale consumers unreachable");

    if (!passed)
    {
        return 1;
    }
    std::cout << "SessionSimulationCallbacks PASS\n";
    return 0;
}
