#include "Mods/Network/Server/Core/ServerPeerRegistry.hpp"

#include <iostream>
#include <memory>

namespace
{
    using MphRead::Mods::Network::ServerPeerRegistry;

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
    ServerPeerRegistry peers;
    bool passed = true;
    const auto reserved = [](std::int32_t slot) { return slot == 1 || slot == 3; };

    auto first = std::make_shared<ServerPeerRegistry::Peer>();
    first->SlotIndex = 0;
    first->ClientId = 1001;
    auto second = std::make_shared<ServerPeerRegistry::Peer>();
    second->SlotIndex = 2;
    second->ClientId = 2002;
    peers.Add(first);
    peers.Add(second);
    passed &= Check(peers.Count() == 2 && peers.All().size() == 2, "membership count follows add");
    passed &= Check(peers.FindByClientId(1001) == first && peers.FindByClientId(2002) == second,
        "connection identity lookup returns the registered peer");
    passed &= Check(peers.FindByClientId(0) == nullptr && peers.FindByClientId(3003) == nullptr,
        "zero and unknown client identities do not match a peer");
    passed &= Check(!peers.SlotFree(0, reserved), "an occupied slot is not free");
    passed &= Check(!peers.SlotFree(1, reserved), "a bot-reserved slot is not free");
    passed &= Check(peers.SlotFree(4, reserved), "an unoccupied, unreserved slot is free");
    passed &= Check(peers.NextFreeSlot(5, reserved) == 4, "slot allocation skips peers and reserved bot slots");
    passed &= Check(peers.SelectFreeSlot(4, 5, reserved) == 4,
        "slot allocation honors a preferred free slot");
    passed &= Check(peers.SelectFreeSlot(1, 5, reserved) == 4,
        "slot allocation falls back when the preferred slot belongs to a bot");
    passed &= Check(peers.SelectFreeSlot(0, 5, reserved) == 4,
        "slot allocation falls back when the preferred slot belongs to a human");
    passed &= Check(peers.SelectFreeSlot(4, 4, reserved) == -1,
        "slot allocation refuses when every in-capacity slot is occupied or reserved");
    passed &= Check(peers.Remove(first), "remove finds a registered peer");
    passed &= Check(peers.Count() == 1 && peers.SlotFree(0, reserved), "removed slots can be reused");
    passed &= Check(peers.SelectFreeSlot(0, 5, reserved) == 0,
        "a leaving human's preferred slot can be reclaimed");
    passed &= Check(!peers.Remove(first) && peers.Count() == 1, "repeated remove leaves membership unchanged");
    peers.Add(nullptr);
    passed &= Check(peers.Count() == 1, "null peers cannot enter the registry");
    passed &= Check(peers.NextFreeSlot(4, reserved) == 0, "allocation returns the first available slot");

    if (!passed)
    {
        return 1;
    }
    std::cout << "ServerPeerRegistry PASS\n";
    return 0;
}
