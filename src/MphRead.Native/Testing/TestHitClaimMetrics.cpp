#include "Mods/Network/Gameplay/Claims/Hit/HitClaimMetrics.hpp"

#include <iostream>

namespace
{
    using MphRead::Mods::Network::HitClaimMetrics;

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
    using Counter = HitClaimMetrics::Counter;

    HitClaimMetrics metrics;
    bool passed = true;
    passed &= Check(metrics.Value(Counter::Applied) == 0,
        "counters start empty");
    metrics.Increment(Counter::Applied);
    metrics.Add(Counter::RescuedDamage, 9);
    passed &= Check(metrics.Value(Counter::Applied) == 1
        && metrics.Value(Counter::RescuedDamage) == 9,
        "named counters retain their own values");

    passed &= Check(!metrics.RecordDamageAgreement(3, 4, 0),
        "unresolved samples do not enter the agreement totals");
    passed &= Check(!metrics.RecordDamageAgreement(3, 8, 8),
        "matching damage records an agreement without a disagreement");
    passed &= Check(metrics.RecordDamageAgreement(3, 5, 4),
        "different damage records a disagreement");
    const HitClaimMetrics::DamageAgreement agreement = metrics.AgreementAt(3);
    passed &= Check(agreement.Agreed == 1 && agreement.Differed == 1
        && agreement.Claimed == 13 && agreement.Resolved == 12,
        "damage totals and sample counts are accumulated together");
    passed &= Check(!metrics.RecordDamageAgreement(HitClaimMetrics::DamageBucketCount, 1, 1)
        && metrics.AgreementAt(HitClaimMetrics::DamageBucketCount).Agreed == 0,
        "out-of-range buckets are ignored safely");

    bool allLogSlotsAvailable = true;
    for (int i = 0; i < 20; ++i)
    {
        allLogSlotsAvailable &= metrics.TakeDisagreementLogSlot();
    }
    passed &= Check(allLogSlotsAvailable && !metrics.TakeDisagreementLogSlot(),
        "disagreement diagnostics keep the original twenty-entry cap");

    metrics.Reset();
    passed &= Check(metrics.Value(Counter::Applied) == 0
        && metrics.Value(Counter::RescuedDamage) == 0
        && metrics.AgreementAt(3).Agreed == 0
        && metrics.TakeDisagreementLogSlot(),
        "reset clears counters, agreement data, and the diagnostic cap");

    if (!passed)
    {
        return 1;
    }
    std::cout << "HitClaimMetrics PASS\n";
    return 0;
}
