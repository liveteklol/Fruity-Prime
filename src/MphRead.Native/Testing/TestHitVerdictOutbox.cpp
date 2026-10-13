#include "Mods/Network/Gameplay/Claims/Hit/HitVerdictOutbox.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

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

    bool passed = true;
    HitVerdictOutbox outbox;
    passed &= Check(outbox.PendingCount(0) == 0, "starts empty");
    passed &= Check(!outbox.Enqueue(-1, 1, 2) && !outbox.Enqueue(8, 1, 2), "rejects invalid slots");
    passed &= Check(outbox.Enqueue(0, 7, 1) && outbox.Enqueue(0, 9, 2)
        && outbox.Enqueue(3, 4, 5), "queues independent slot batches");
    passed &= Check(outbox.PendingCount(0) == 2 && outbox.PendingCount(3) == 1,
        "reports per-slot queue size");

    std::vector<std::pair<std::int32_t, std::vector<HitVerdictOutbox::Entry>>> batches;
    outbox.Sink([&batches](std::int32_t slot, std::span<const HitVerdictOutbox::Entry> entries)
    {
        batches.emplace_back(slot, std::vector<HitVerdictOutbox::Entry>(entries.begin(), entries.end()));
    });
    outbox.Flush();
    passed &= Check(batches.size() == 2 && batches[0].first == 0 && batches[1].first == 3,
        "flushes non-empty slots in slot order");
    passed &= Check(batches[0].second == std::vector<HitVerdictOutbox::Entry>{{7, 1}, {9, 2}}
        && batches[1].second == std::vector<HitVerdictOutbox::Entry>{{4, 5}},
        "preserves verdict order and values");
    passed &= Check(outbox.PendingCount(0) == 0 && outbox.PendingCount(3) == 0,
        "clears a batch before invoking its writer");

    std::size_t callbacks = 0;
    outbox.Sink([&outbox, &callbacks](std::int32_t slot, std::span<const HitVerdictOutbox::Entry>)
    {
        callbacks++;
        (void)outbox.Enqueue(slot, 20, 6);
    });
    (void)outbox.Enqueue(2, 10, 4);
    outbox.Flush();
    passed &= Check(callbacks == 1 && outbox.PendingCount(2) == 1,
        "callback-enqueued verdict remains for the next flush");
    outbox.Flush();
    passed &= Check(callbacks == 2 && outbox.PendingCount(2) == 1,
        "reentrant batches are delivered on the following flush");

    outbox.Sink({});
    outbox.Flush();
    passed &= Check(outbox.PendingCount(2) == 0, "discards queued results when no sink is installed");

    for (std::size_t i = 0; i < HitVerdictOutbox::BatchCapacity; i++)
    {
        passed &= Check(outbox.Enqueue(5, static_cast<std::uint16_t>(i), 1), "accepts within packet capacity");
    }
    passed &= Check(!outbox.Enqueue(5, 999, 1)
        && outbox.PendingCount(5) == HitVerdictOutbox::BatchCapacity, "bounds each batch to one wire packet");
    outbox.ClearSlot(5);
    passed &= Check(outbox.PendingCount(5) == 0, "clears one departing slot");

    (void)outbox.Enqueue(1, 1, 1);
    outbox.Clear();
    passed &= Check(outbox.PendingCount(1) == 0, "clears all pending batches");
    return passed ? 0 : 1;
}
