#include "Mods/Network/Gameplay/Claims/Pickup/PickupClaimReplayLedger.hpp"

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
    PickupClaimReplayLedger ledger;
    passed &= Check(ledger.Find(-1, 1) == nullptr && ledger.Find(0, 0) == nullptr
        && ledger.Find(8, 1) == nullptr, "rejects invalid slot and claim ids");

    const PickupVerdictPacket::Entry granted{ 17, PickupVerdictPacket::ResultGranted, 9 };
    ledger.Remember(2, granted);
    const auto* replay = ledger.Find(2, 17);
    passed &= Check(replay != nullptr && replay->ClaimId == 17
        && replay->Result == PickupVerdictPacket::ResultGranted && replay->Sequence == 9,
        "returns the exact first authority verdict for a resend");
    passed &= Check(ledger.Find(1, 17) == nullptr, "keeps different shooters isolated");

    const PickupVerdictPacket::Entry refused{ 33, PickupVerdictPacket::ResultOutOfReach, 0 };
    ledger.Remember(2, refused);
    passed &= Check(ledger.Find(2, 17) == nullptr && ledger.Find(2, 33) != nullptr,
        "bounded sequence window replaces the colliding old entry");

    ledger.Remember(8, granted);
    ledger.Remember(2, PickupVerdictPacket::Entry{});
    passed &= Check(ledger.Find(8, 17) == nullptr && ledger.Find(2, 0) == nullptr,
        "does not retain invalid input");
    ledger.Reset();
    passed &= Check(ledger.Find(2, 33) == nullptr, "reset clears all authority replay state");
    return passed ? 0 : 1;
}
