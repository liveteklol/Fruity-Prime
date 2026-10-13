#include "Mods/Network/Gameplay/Claims/Hit/HitClaimDeathState.hpp"

#include <iostream>

namespace
{
    using MphRead::Mods::Network::HitClaimDeathState;
    using MphRead::Mods::Network::HitClaimSlotTransition;

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
    HitClaimDeathState deaths;
    bool passed = true;
    passed &= Check(deaths.UpdateInPlay(2, false, 100) == HitClaimSlotTransition::None,
        "an empty slot remains unchanged");
    passed &= Check(deaths.UpdateInPlay(2, true, 101) == HitClaimSlotTransition::Respawned,
        "first entry into play follows the existing respawn transition");
    passed &= Check(!deaths.WasDead(2) && !deaths.FiredAfterOwnDeath(2, 101),
        "a live slot has no death fence");
    passed &= Check(deaths.UpdateInPlay(2, true, 102) == HitClaimSlotTransition::None,
        "continuous play does not create another lifecycle transition");

    passed &= Check(deaths.UpdateInPlay(2, false, 120) == HitClaimSlotTransition::Died
        && deaths.WasDead(2), "leaving play records the death boundary");
    passed &= Check(!deaths.FiredAfterOwnDeath(2, 119) && deaths.FiredAfterOwnDeath(2, 120),
        "shots from before the death frame remain eligible, and same-frame shots are fenced");
    passed &= Check(deaths.UpdateInPlay(2, false, 121) == HitClaimSlotTransition::None
        && deaths.WasDead(2), "staying out of play preserves the death tombstone");

    passed &= Check(deaths.UpdateInPlay(2, true, 130) == HitClaimSlotTransition::Respawned
        && !deaths.WasDead(2) && !deaths.FiredAfterOwnDeath(2, 130),
        "respawn clears the prior death fence");
    static_cast<void>(deaths.UpdateInPlay(3, true, 199));
    static_cast<void>(deaths.UpdateInPlay(3, false, 0));
    passed &= Check(deaths.WasDead(3) && !deaths.FiredAfterOwnDeath(3, 0)
        && !deaths.FiredAfterOwnDeath(3, 1),
        "frame zero retains the existing no-fence sentinel behavior");
    static_cast<void>(deaths.UpdateInPlay(4, true, 140));
    static_cast<void>(deaths.UpdateInPlay(4, false, 150));
    deaths.ForgetSlot(2);
    passed &= Check(deaths.UpdateInPlay(2, false, 160) == HitClaimSlotTransition::None
        && deaths.WasDead(4), "forgetting one slot does not reset another");
    deaths.Reset();
    passed &= Check(!deaths.WasDead(4) && deaths.UpdateInPlay(4, false, 170) == HitClaimSlotTransition::None,
        "reset clears every slot lifecycle");
    passed &= Check(deaths.UpdateInPlay(-1, true, 180) == HitClaimSlotTransition::None
        && deaths.UpdateInPlay(static_cast<std::int32_t>(HitClaimDeathState::SlotCapacity), true, 180)
            == HitClaimSlotTransition::None
        && !deaths.WasDead(-1), "invalid slots are ignored");

    if (!passed)
    {
        return 1;
    }
    std::cout << "HitClaimDeathState PASS\n";
    return 0;
}
