#include "Mods/Network/Gameplay/Claims/Hit/HitClaimRateLimiter.hpp"

#include <iostream>

namespace
{
    using MphRead::Mods::Network::HitClaimRateLimiter;

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
    HitClaimRateLimiter limiter;
    bool passed = true;

    passed &= Check(limiter.Allows(0, 0, 100, 5),
        "the first shot for a shooter and weapon is allowed");
    passed &= Check(limiter.Allows(0, 0, 100, 5),
        "multiple victims from one launch share the launch frame");
    passed &= Check(!limiter.Allows(0, 0, 104, 5),
        "distinct shots inside the weapon cooldown are refused");
    passed &= Check(limiter.Allows(0, 0, 105, 5),
        "the exact cooldown boundary is allowed");
    passed &= Check(limiter.Allows(0, 1, 101, 5)
        && limiter.Allows(1, 0, 101, 5),
        "weapon and shooter histories are isolated");
    passed &= Check(limiter.Allows(-1, 0, 101, 5)
        && limiter.Allows(0, 9, 101, 5)
        && limiter.Allows(0, 0, 0, 5)
        && limiter.Allows(0, 0, 101, 0),
        "invalid or inactive rate-check inputs remain permissive");

    limiter.ForgetSlot(0);
    passed &= Check(limiter.Allows(0, 0, 101, 5),
        "forgetting a reused slot clears its prior occupant's launch history");
    limiter.Reset();
    passed &= Check(limiter.Allows(0, 0, 101, 5)
        && limiter.Allows(1, 0, 101, 5),
        "reset clears histories for every slot");

    if (!passed)
    {
        return 1;
    }
    std::cout << "HitClaimRateLimiter PASS\n";
    return 0;
}
