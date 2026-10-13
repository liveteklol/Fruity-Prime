#include "Mods/Network/Session/SessionTrafficMetrics.hpp"

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
    using MphRead::Mods::Network::SessionTrafficMetrics;

    bool passed = true;
    SessionTrafficMetrics metrics;
    passed &= Check(metrics.SnapshotsReceived() == 0 && metrics.SnapshotsSent() == 0
        && metrics.StatesApplied() == 0 && metrics.IntentsReceived() == 0
        && metrics.IntentsSent() == 0 && metrics.IntentBytesSent() == 0,
        "traffic counters start empty");

    metrics.RecordSnapshotReceived();
    metrics.RecordSnapshotReceived();
    metrics.RecordSnapshotSent();
    metrics.RecordStateApplied();
    metrics.RecordStateApplied();
    metrics.RecordStateApplied();
    metrics.RecordIntentReceived();
    metrics.RecordIntentSent(17);
    metrics.RecordIntentSent(31);
    passed &= Check(metrics.SnapshotsReceived() == 2 && metrics.SnapshotsSent() == 1
        && metrics.StatesApplied() == 3 && metrics.IntentsReceived() == 1
        && metrics.IntentsSent() == 2 && metrics.IntentBytesSent() == 48,
        "event recording keeps independent counts and byte totals");

    metrics.Reset();
    passed &= Check(metrics.SnapshotsReceived() == 0 && metrics.SnapshotsSent() == 0
        && metrics.StatesApplied() == 0 && metrics.IntentsReceived() == 0
        && metrics.IntentsSent() == 0 && metrics.IntentBytesSent() == 0,
        "session reset clears all traffic metrics together");

    return passed ? 0 : 1;
}
