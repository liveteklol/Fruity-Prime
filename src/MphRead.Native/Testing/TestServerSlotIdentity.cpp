#include "Mods/Network/Server/Core/ServerSlotIdentity.hpp"

#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
    using MphRead::Mods::Network::ServerSlotIdentity;

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
    ServerSlotIdentity slots;
    bool passed = true;
    constexpr std::size_t Slot = 3;

    passed &= Check(ServerSlotIdentity::SlotCapacity == 8, "wire slot capacity remains eight");
    passed &= Check(slots.Generation(Slot) == 0 && slots.Life(Slot) == 0, "slots begin unassigned");
    passed &= Check(slots.IsValidSlot(Slot) && !slots.IsValidSlot(ServerSlotIdentity::SlotCapacity),
        "slot bounds follow wire capacity");

    slots.EnsureGeneration(Slot);
    slots.SetLife(Slot, 9);
    slots.EnsureGeneration(Slot);
    passed &= Check(slots.Generation(Slot) == 1 && slots.Life(Slot) == 9,
        "ensuring a bot generation is idempotent and preserves its life");

    passed &= Check(slots.AdvanceGeneration(Slot) == 2 && slots.Life(Slot) == 0,
        "a new occupant advances generation and clears its life");
    passed &= Check(slots.MatchesGeneration(Slot, 2) && !slots.MatchesGeneration(Slot, 1),
        "generation matching rejects stale occupants");

    slots.SetLife(Slot, 7);
    slots.ResetLives();
    passed &= Check(slots.Generation(Slot) == 2 && slots.Life(Slot) == 0,
        "match reset clears lives without changing slot generation");
    slots.SetLife(Slot, 12);
    slots.ResetLife(Slot);
    passed &= Check(slots.Life(Slot) == 0, "single-slot life reset is isolated");

    slots.SetLife(Slot, 21);
    slots.EnsureGeneration(Slot);
    passed &= Check(slots.Generation(Slot) == 2 && slots.Life(Slot) == 21,
        "idempotent initialization does not alter an assigned slot");
    slots.AdvanceGeneration(Slot);
    passed &= Check(slots.Generation(Slot) == 3 && slots.Life(Slot) == 0,
        "generation advances independently for each occupant");

    for (std::uint32_t i = 3; i < std::numeric_limits<std::uint16_t>::max(); i++)
    {
        slots.AdvanceGeneration(Slot);
    }
    passed &= Check(slots.Generation(Slot) == std::numeric_limits<std::uint16_t>::max(),
        "generation reaches the last nonzero serial");
    passed &= Check(slots.AdvanceGeneration(Slot) == 1, "generation wraps around reserved zero");

    slots.SetLife(ServerSlotIdentity::SlotCapacity, 5);
    passed &= Check(slots.Life(ServerSlotIdentity::SlotCapacity) == 0
        && !slots.MatchesGeneration(ServerSlotIdentity::SlotCapacity, 0),
        "invalid slots cannot read or mutate identity state");

    if (!passed)
    {
        return 1;
    }
    std::cout << "ServerSlotIdentity PASS\n";
    return 0;
}
