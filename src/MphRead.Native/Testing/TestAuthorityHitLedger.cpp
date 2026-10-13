#include "Mods/Network/Gameplay/Claims/Hit/AuthorityHitLedger.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
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

    MphRead::Mods::Network::ShotKey Key(std::uint32_t launch)
    {
        return {7, 9, 2, 3, 4, launch};
    }
}

int main()
{
    using namespace MphRead::Mods::Network;

    bool passed = true;
    AuthorityHitLedger ledger;
    ledger.Record(2, 4, 100, 90, 95, 37, false, MphRead::BeamType::Judicator, Key(95));
    auto match = ledger.Take(2, 4, 90, 97, 101, 10);
    passed &= Check(match.Kind == AuthorityHitLedger::MatchKind::Launch && match.Damage == 37,
        "launch proximity pairs a claim with its authority hit");
    passed &= Check(!ledger.Take(2, 4, 90, 97, 101, 10), "a resolved authority hit can only be consumed once");

    ledger.Record(2, 4, 110, 51, 0, 22, false, MphRead::BeamType::VoltDriver, Key(0));
    match = ledger.Take(2, 4, 52, 0, 112, 5);
    passed &= Check(match.Kind == AuthorityHitLedger::MatchKind::AckWindow && match.Damage == 22,
        "claim without a launch uses the nearest ack inside the arrival window");

    ledger.Record(2, 4, 120, 60, 0, 19, false, MphRead::BeamType::Judicator, Key(0));
    passed &= Check(!ledger.Take(2, 4, 60, 0, 121, 5, Key(1), MphRead::BeamType::Judicator),
        "strict obstruction key refuses a nearby unrelated shot");
    match = ledger.Take(2, 4, 60, 0, 121, 5, Key(0), MphRead::BeamType::Judicator);
    passed &= Check(match.Kind == AuthorityHitLedger::MatchKind::Launch && match.Damage == 19,
        "strict obstruction key and weapon match the exact authority shot");

    ledger.Record(1, 3, 200, 70, 0, 41, true, MphRead::BeamType::OmegaCannon, Key(0));
    passed &= Check(!ledger.Take(1, 3, 70, 0, 200, 1), "already-used authority hits are not paired twice");

    ledger.Reset();
    ledger.Record(0, 1, std::numeric_limits<std::uint32_t>::max() - 1U, 0, 0, 1, false,
        MphRead::BeamType::None, Key(0));
    passed &= Check(ledger.NearestOffset(0, 1, 1) == 3, "nearest-frame diagnostic preserves uint32 wraparound");
    ledger.ClearPair(0, 1);
    passed &= Check(ledger.NearestOffset(0, 1, 1) == std::numeric_limits<std::int32_t>::min(),
        "clearing one attacker-victim pair removes its history");

    ledger.Record(1, 2, 10, 9, 0, 8, false, MphRead::BeamType::None, Key(0));
    ledger.Record(1, 5, 11, 10, 0, 9, false, MphRead::BeamType::None, Key(0));
    ledger.ClearSlot(2);
    passed &= Check(!ledger.Take(1, 2, 9, 0, 10, 1), "slot cleanup clears records where slot was victim");
    passed &= Check(static_cast<bool>(ledger.Take(1, 5, 10, 0, 11, 1)), "slot cleanup preserves unrelated records");

    return passed ? 0 : 1;
}
