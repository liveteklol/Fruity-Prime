#include "Mods/Network/Transport/NetFaultQueue.hpp"

#include <iostream>

namespace
{
    using MphRead::Mods::Network::NetFaultQueue;

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
    bool passed = true;

    NetFaultQueue<int> duplicateQueue(301011, 0, 0, 0, 0, 1, nullptr, 4);
    duplicateQueue.Enqueue(0, 17);
    passed &= Check(duplicateQueue.Count() == 2 && duplicateQueue.Duplicated() == 1
        && duplicateQueue.Dropped() == 0, "duplicate policy adds one bounded copy");
    int value = 0;
    passed &= Check(duplicateQueue.TryDequeue(0, value) && value == 17
        && !duplicateQueue.TryDequeue(0, value), "original packet keeps its due time");
    passed &= Check(duplicateQueue.TryDequeue(3, value) && value == 17
        && duplicateQueue.Count() == 0, "duplicate arrives after the original");

    bool observedReorder = false;
    for (std::int32_t seed = 1; seed <= 32 && !observedReorder; seed++)
    {
        NetFaultQueue<int> reorderQueue(seed, 0, 0, 0, 1, 0, nullptr, 4);
        reorderQueue.Enqueue(0, 1);
        reorderQueue.Enqueue(0, 2);
        int first = 0;
        int second = 0;
        observedReorder = reorderQueue.Reordered() == 2
            && reorderQueue.TryDequeue(100, first)
            && reorderQueue.TryDequeue(100, second)
            && first == 2 && second == 1;
    }
    passed &= Check(observedReorder, "seeded queue can deliver a later datagram first");

    NetFaultQueue<int> boundedQueue(7, 0, 0, 0, 0, 1, nullptr, 2);
    boundedQueue.Enqueue(0, 23);
    boundedQueue.Enqueue(0, 29);
    passed &= Check(boundedQueue.Count() == 2 && boundedQueue.Duplicated() == 2
        && boundedQueue.Dropped() == 2, "saturation holds the capacity and counts rejected copies");
    int held = 0;
    int duplicate = 0;
    passed &= Check(boundedQueue.TryDequeue(0, held) && held == 23
        && boundedQueue.TryDequeue(3, duplicate) && duplicate == 23
        && boundedQueue.Count() == 0, "saturation preserves queued packets in their due order");

    NetFaultQueue<int> lossQueue(5, 0, 0, 1, 0, 0, nullptr, 1);
    lossQueue.Enqueue(0, 31);
    passed &= Check(lossQueue.Count() == 0 && lossQueue.Dropped() == 1,
        "loss policy drops without growing the queue");

    if (!passed)
    {
        return 1;
    }
    std::cout << "NetFaultQueue PASS\n";
    return 0;
}
