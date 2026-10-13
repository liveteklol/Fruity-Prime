#include "Mods/Network/Transport/BoundedInbox.hpp"
#include "Mods/Network/Transport/NetFaultQueue.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <optional>
#include <thread>
#include <vector>

using namespace MphRead::Mods::Network;

namespace
{
    bool Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
        }
        return condition;
    }

    [[nodiscard]] PacketType PacketTypeOf(PacketType packet) noexcept
    {
        return packet;
    }

    bool CriticalQueuePreservesImportantPackets()
    {
        constexpr std::size_t capacity = 2048;
        BoundedInbox<PacketType> inbox(capacity);
        bool passed = true;
        for (std::size_t i = 0; i < capacity; ++i)
        {
            passed &= Check(inbox.TryEnqueue(PacketType::HitVerdict, PacketTypeOf)
                == InboxEnqueueResult::Enqueued,
                "a packet is accepted while the inbox has room");
        }

        passed &= Check(inbox.TryEnqueue(PacketType::Ping, PacketTypeOf)
            == InboxEnqueueResult::RejectedSuperseded,
            "a replaceable incoming ping is rejected when every queued packet is important");
        const BoundedInboxStatistics stats = inbox.Statistics();
        passed &= Check(stats.Depth == capacity && stats.Capacity == capacity,
            "the inbox never exceeds its configured capacity");
        passed &= Check(stats.IncomingSupersededDrops == 1
            && stats.ExistingSupersededDrops == 0 && stats.CriticalOverflowDrops == 0,
            "incoming replaceable drops are counted separately");

        std::optional<PacketType> packet;
        std::size_t drained = 0;
        while (inbox.TryDequeue(packet))
        {
            passed &= Check(*packet == PacketType::HitVerdict,
                "full critical queues retain every queued verdict");
            ++drained;
        }
        passed &= Check(drained == capacity,
            "dequeue count matches the bounded queue depth");
        return passed;
    }

    bool ExistingSupersededPacketsAreReplacedFirst()
    {
        constexpr std::size_t capacity = 2048;
        BoundedInbox<PacketType> inbox(capacity);
        (void)inbox.TryEnqueue(PacketType::Welcome, PacketTypeOf);
        (void)inbox.TryEnqueue(PacketType::Snapshot, PacketTypeOf);
        (void)inbox.TryEnqueue(PacketType::HitVerdict, PacketTypeOf);
        for (std::size_t i = 3; i < capacity; ++i)
        {
            (void)inbox.TryEnqueue(PacketType::MatchState, PacketTypeOf);
        }
        bool passed = true;
        passed &= Check(inbox.TryEnqueue(PacketType::MapChange, PacketTypeOf)
            == InboxEnqueueResult::ReplacedSuperseded,
            "an important arrival replaces an older superseded packet when full");
        const BoundedInboxStatistics stats = inbox.Statistics();
        passed &= Check(stats.Depth == capacity && stats.ExistingSupersededDrops == 1
            && stats.IncomingSupersededDrops == 0 && stats.CriticalOverflowDrops == 0,
            "replacement leaves depth bounded and records its victim reason");

        std::optional<PacketType> packet;
        std::vector<PacketType> remaining;
        while (inbox.TryDequeue(packet))
        {
            remaining.push_back(*packet);
        }
        passed &= Check(remaining.size() == capacity
            && remaining.front() == PacketType::Welcome
            && remaining[1] == PacketType::HitVerdict
            && remaining.back() == PacketType::MapChange
            && std::count(remaining.begin(), remaining.end(), PacketType::Snapshot) == 0,
            "replacement preserves critical FIFO order, removes the old snapshot, and appends the new packet");
        return passed;
    }

    bool CriticalOverflowRejectsNewestAndHeldPromotionStaysBounded()
    {
        BoundedInbox<PacketType> critical(2);
        (void)critical.TryEnqueue(PacketType::Welcome, PacketTypeOf);
        (void)critical.TryEnqueue(PacketType::HitVerdict, PacketTypeOf);
        bool passed = true;
        passed &= Check(critical.TryEnqueue(PacketType::MapChange, PacketTypeOf)
            == InboxEnqueueResult::RejectedCriticalOverflow,
            "a full all-important inbox explicitly rejects the newest important packet");
        passed &= Check(critical.Statistics().CriticalOverflowDrops == 1,
            "important overflow has its own drop counter");

        constexpr std::size_t capacity = 2048;
        BoundedInbox<PacketType> promoted(capacity);
        for (std::size_t i = 0; i < 100; ++i)
        {
            (void)promoted.TryEnqueue(PacketType::Welcome, PacketTypeOf);
        }
        for (std::size_t i = 0; i < capacity; ++i)
        {
            (void)promoted.TryEnqueue(PacketType::Snapshot, PacketTypeOf);
            passed &= Check(promoted.Statistics().Depth <= capacity,
                "a held-arrival promotion burst never grows the inbox beyond its limit");
        }
        const BoundedInboxStatistics stats = promoted.Statistics();
        passed &= Check(stats.Depth == capacity && stats.ExistingSupersededDrops == 100,
            "held promotion uses the same capacity-aware replacement policy");
        std::optional<PacketType> packet;
        std::size_t welcomes = 0;
        while (promoted.TryDequeue(packet))
        {
            welcomes += *packet == PacketType::Welcome ? 1U : 0U;
        }
        passed &= Check(welcomes == 100,
            "held promotion retains all earlier important welcome packets");
        return passed;
    }

    bool FaultLossAndSchedulerCapacityHaveSeparateCounters()
    {
        NetFaultQueue<PacketType> lossQueue(87, 0, 0, 1.0, 0, 0, nullptr, 4);
        lossQueue.Enqueue(0, PacketType::Welcome);
        bool passed = true;
        passed &= Check(lossQueue.Count() == 0 && lossQueue.Dropped() == 1
            && lossQueue.DroppedByLoss() == 1 && lossQueue.DroppedByCapacity() == 0,
            "configured network loss is separated from delayed-scheduler overflow");

        NetFaultQueue<PacketType> capacityQueue(88, 0, 0, 0, 0, 0, nullptr, 2);
        capacityQueue.Enqueue(0, PacketType::Welcome);
        capacityQueue.Enqueue(0, PacketType::HitVerdict);
        capacityQueue.Enqueue(0, PacketType::MapChange);
        passed &= Check(capacityQueue.Count() == 2 && capacityQueue.Capacity() == 2
            && capacityQueue.Dropped() == 1 && capacityQueue.DroppedByLoss() == 0
            && capacityQueue.DroppedByCapacity() == 1,
            "delayed scheduler capacity has a distinct limit and drop metric");

        NetFaultQueue<PacketType> faulted(2151, 0, 5, 0.05, 0.10, 0.05, nullptr, 2048);
        BoundedInbox<PacketType> inbox(2048);
        for (std::size_t i = 0; i < 100; ++i)
        {
            (void)inbox.TryEnqueue(PacketType::Welcome, PacketTypeOf);
        }
        for (std::size_t i = 0; i < 4096; ++i)
        {
            faulted.Enqueue(0, i % 10 == 0 ? PacketType::HitVerdict : PacketType::Snapshot);
        }
        PacketType packet{};
        while (faulted.TryDequeue(10000, packet))
        {
            (void)inbox.TryEnqueue(packet, PacketTypeOf);
        }
        const BoundedInboxStatistics inboxStats = inbox.Statistics();
        passed &= Check(faulted.DroppedByLoss() > 0 && faulted.DroppedByCapacity() > 0,
            "loss, reorder, duplication, and scheduler saturation are exercised together");
        passed &= Check(inboxStats.Depth == inboxStats.Capacity
            && inboxStats.ExistingSupersededDrops > 0 && inboxStats.CriticalOverflowDrops == 0,
            "a mixed faulted stream stays bounded while important verdicts displace stale snapshots");
        std::optional<PacketType> queued;
        bool verdictReachedInbox = false;
        while (inbox.TryDequeue(queued))
        {
            verdictReachedInbox |= *queued == PacketType::HitVerdict;
        }
        passed &= Check(verdictReachedInbox,
            "a hit verdict remains reachable through a faulted saturated inbox");
        return passed;
    }

    bool ConcurrentIngressAndDrainStayBounded()
    {
        constexpr std::size_t capacity = 128;
        constexpr std::size_t producerCount = 4;
        constexpr std::size_t packetsPerProducer = 10000;
        BoundedInbox<PacketType> inbox(capacity);
        std::atomic<std::size_t> producersDone{0};
        std::atomic<bool> withinCapacity{true};
        std::thread consumer([&]()
        {
            std::optional<PacketType> packet;
            while (producersDone.load(std::memory_order_acquire) != producerCount)
            {
                if (!inbox.TryDequeue(packet))
                {
                    std::this_thread::yield();
                }
                if (inbox.Statistics().Depth > capacity)
                {
                    withinCapacity.store(false, std::memory_order_release);
                }
            }
            while (inbox.TryDequeue(packet))
            {
            }
        });

        std::vector<std::thread> producers;
        producers.reserve(producerCount);
        for (std::size_t producer = 0; producer < producerCount; ++producer)
        {
            producers.emplace_back([producer, &inbox, &producersDone]()
            {
                for (std::size_t i = 0; i < packetsPerProducer; ++i)
                {
                    const PacketType packet = (i + producer) % 4 == 0
                        ? PacketType::HitVerdict : PacketType::Snapshot;
                    (void)inbox.TryEnqueue(packet, PacketTypeOf);
                }
                producersDone.fetch_add(1, std::memory_order_release);
            });
        }
        for (std::thread& producer : producers)
        {
            producer.join();
        }
        consumer.join();
        const BoundedInboxStatistics stats = inbox.Statistics();
        return Check(withinCapacity.load(std::memory_order_acquire)
            && stats.Depth == 0 && stats.Capacity == capacity,
            "concurrent socket, held-arrival, and drain operations share one bounded inbox");
    }
}

int main()
{
    bool passed = true;
    passed &= CriticalQueuePreservesImportantPackets();
    passed &= ExistingSupersededPacketsAreReplacedFirst();
    passed &= CriticalOverflowRejectsNewestAndHeldPromotionStaysBounded();
    passed &= FaultLossAndSchedulerCapacityHaveSeparateCounters();
    passed &= ConcurrentIngressAndDrainStayBounded();
    if (!passed)
    {
        return 1;
    }
    std::cout << "Bounded network inbox checks passed.\n";
    return 0;
}
