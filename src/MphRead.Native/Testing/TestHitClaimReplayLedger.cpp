#include "Mods/Network/Gameplay/Claims/Hit/HitClaimReplayLedger.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

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
    HitClaimReplayLedger ledger;
    passed &= Check(!ledger.Seen(2, 1), "new claim id is unseen");

    const ShotKey key{7, 9, 2, 3, 4, 11};
    ledger.Begin(2, 1, 255, MphRead::BeamType::Judicator, key);
    passed &= Check(ledger.Seen(2, 1) && ledger.Result(2, 1) == 255,
        "begin records a pending claim");
    passed &= Check(ledger.Beam(2, 1) == MphRead::BeamType::Judicator && ledger.Key(2, 1) == key,
        "pending claim retains its diagnostic shot identity");
    ledger.Remember(2, 1, 3);
    passed &= Check(ledger.Result(2, 1) == 3, "claim result replaces pending state");

    for (std::uint16_t id = 2; id <= HitClaimReplayLedger::Capacity + 1; id++)
    {
        ledger.Remember(2, id, static_cast<std::uint8_t>(id));
    }
    passed &= Check(ledger.Seen(2, 1) && !ledger.Result(2, 1).has_value(),
        "overwritten id stays recognized as too old without returning another result");
    passed &= Check(ledger.Result(2, 2) == 2, "other remembered ids retain their exact results");
    passed &= Check(ledger.Seen(0, 0) && ledger.Seen(-1, 50),
        "zero ids and invalid slots fail closed as seen");

    ledger.Reset();
    ledger.Remember(4, 65534, 1);
    ledger.Remember(4, 1, 2);
    passed &= Check(ledger.Seen(4, 65534) && ledger.Result(4, 1) == 2,
        "newest sequence comparison handles 16-bit wraparound");
    ledger.ForgetSlot(4);
    passed &= Check(!ledger.Seen(4, 1) && !ledger.Result(4, 1).has_value(),
        "slot cleanup clears only that slot's replay window");
    passed &= Check(ledger.Result(2, 2) == std::nullopt, "reset clears every replay window");
    return passed ? 0 : 1;
}
