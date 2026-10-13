#include "Mods/Network/Gameplay/StatusEffects/FreezePredictionLedger.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
    using namespace MphRead::Mods::Network;

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

    FreezePredictionLedger ledger;
    bool passed = true;
    passed &= Check(!ledger.HasPrediction(0) && !ledger.Awaiting(0, 100, 0),
        "unpredicted slots do not wait for authority state");
    ledger.NotePredicted(0, 100);
    passed &= Check(ledger.HasPrediction(0), "a local prediction is recorded by slot");
    passed &= Check(ledger.Awaiting(0, 120, 0), "the prediction remains pending through the arrival slack");
    passed &= Check(!ledger.Awaiting(0, 121, 0), "the prediction expires after the arrival slack");
    passed &= Check(ledger.Awaiting(0, 125, 5), "the round trip extends the expected snapshot window");
    passed &= Check(!ledger.Awaiting(-1, 100, 0)
        && !ledger.Awaiting(static_cast<std::int32_t>(NetworkSlotCapacity), 100, 0),
        "invalid player slots are ignored");

    ledger.NotePredicted(1, 0);
    passed &= Check(!ledger.Awaiting(1, 0, 0), "frame zero keeps the existing nonzero sentinel policy");
    ledger.NotePredicted(2, 0xFFFFFFFEU);
    passed &= Check(ledger.Awaiting(2, 2, 0), "frame wrap preserves the unsigned elapsed-frame window");
    ledger.ForgetSlot(0);
    passed &= Check(!ledger.HasPrediction(0) && !ledger.Awaiting(0, 100, 100),
        "slot cleanup forgets its predicted freeze");
    ledger.Reset();
    passed &= Check(!ledger.HasPrediction(2) && !ledger.Awaiting(2, 2, 100),
        "session reset clears all pending predictions");

    return passed ? 0 : 1;
}
