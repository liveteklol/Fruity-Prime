#include "Mods/Network/Session/PeerRegistry.hpp"

#include <iostream>

namespace
{
    using MphRead::Mods::Network::PeerRegistry;
    using MphRead::Mods::Network::SessionPeers;
    using MphRead::Mods::Network::IntentPacket;

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
    PeerRegistry peers;
    bool passed = true;
    passed &= Check(peers.Empty() && peers.Count() == 0, "registry starts empty");

    const PeerRegistry::PeerHandle first = peers.AddPeer(nullptr, 1, 101, 0.5);
    passed &= Check(first != nullptr && first->SlotIndex() == 1 && first->ClientId() == 101,
        "registry creates a peer with immutable slot identity");
    passed &= Check(first->LastSeenTime() == 0.5 && peers.FindByClientId(101) == first,
        "client registration stores liveness and supports identity lookup");
    passed &= Check(peers.FindByEndpoint(nullptr) == nullptr && peers.FindSlotByEndpoint(nullptr) == -1,
        "null endpoints never match a peer");
    passed &= Check(peers.Count() == 1 && !peers.Empty() && peers.NextFreeSlot(1, 4) == 2,
        "membership controls the next free slot");

    peers.SetHostGeneration(1, 7);
    passed &= Check(peers.HostGeneration(1) == 7, "host generation belongs to the registry");

    IntentPacket intent{};
    intent.Frame = 24;
    first->RecordLatestIntent(intent, 1.25);
    passed &= Check(first->LastIntentFrame() == 24 && first->LatestIntent().Frame == 24
        && first->LastSeenTime() == 1.25, "intent observation updates peer-owned snapshot and liveness");
    first->MarkSeen(1.5);
    passed &= Check(first->LastSeenTime() == 1.5, "valid peer traffic refreshes liveness");

    peers.ResetIntentFrameForSlot(1);
    passed &= Check(first->LastIntentFrame() == 0 && first->LatestIntent().Frame == 24,
        "slot reset clears the ordering sentinel while retaining the latest snapshot");
    peers.ResetHostGenerations();
    passed &= Check(peers.HostGeneration(1) == 0, "host generations reset independently of membership");

    passed &= Check(peers.Remove(first) && peers.NextFreeSlot(1, 4) == 1,
        "removal releases the peer slot");
    passed &= Check(!peers.Remove(first), "repeated removal is harmless");
    peers.Clear();
    passed &= Check(peers.Empty(), "clear removes all peer membership");

    if (!passed)
    {
        return 1;
    }
    std::cout << "PeerRegistry PASS\n";
    return 0;
}
