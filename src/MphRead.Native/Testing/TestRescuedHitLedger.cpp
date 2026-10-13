#include "Mods/Network/Gameplay/Claims/Hit/RescuedHitLedger.hpp"

#include <cstdint>
#include <iostream>

namespace
{
    using MphRead::Mods::Network::RescuedHitLedger;
    using MphRead::Mods::Network::ShotKey;

    int failures = 0;

    void Check(bool condition, const char* name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            failures++;
        }
    }

    [[nodiscard]] ShotKey Key(std::uint32_t frame)
    {
        return ShotKey{1, 2, 3, 4, 5, frame};
    }
}

int main()
{
    {
        RescuedHitLedger ledger;
        ledger.Record(3, 4, Key(100), 6, 7, 1000);
        ledger.Record(3, 4, Key(100), 6, 7, 1100);
        Check(ledger.Consume(Key(100), 4, 6, 7, 1820), "consume is valid at the inclusive age limit");
        Check(ledger.Consume(Key(100), 4, 6, 7, 1820), "duplicate rescues keep their owed count");
        Check(!ledger.Consume(Key(100), 4, 6, 7, 1820), "each rescued copy is consumed once");
    }
    {
        RescuedHitLedger ledger;
        ledger.Record(3, 4, Key(100), 6, 7, 1000);
        Check(!ledger.Consume(Key(100), 4, 6, 7, 1721), "entries expire beyond the age limit");
        Check(!ledger.Consume(Key(100), 4, 6, 7, 1721), "expired entries cannot be consumed twice");
        ledger.Record(3, 4, Key(100), 6, 7, 1000);
        Check(!ledger.Consume(Key(100), 4, 6, 8, 1000), "a later victim life cannot consume an earlier rescue");
        Check(ledger.Consume(Key(100), 4, 6, 7, 1000), "the matching victim life remains consumable");
    }
    {
        RescuedHitLedger ledger;
        ledger.Record(3, 4, Key(100), 6, 7, 1000);
        ledger.ForgetSlot(3, true);
        Check(ledger.Consume(Key(100), 4, 6, 7, 1000), "preserved in-flight attacker shot survives slot removal");
        ledger.Record(3, 4, Key(100), 6, 7, 1000);
        ledger.ForgetSlot(3, false);
        Check(!ledger.Consume(Key(100), 4, 6, 7, 1000), "ordinary attacker removal clears its flight");
        ledger.Record(3, 4, Key(100), 6, 7, 1000);
        ledger.ForgetSlot(4, true);
        Check(!ledger.Consume(Key(100), 4, 6, 7, 1000), "victim removal always clears its rescue");
    }
    {
        RescuedHitLedger ledger;
        for (std::uint32_t frame = 1; frame <= RescuedHitLedger::Capacity + 1; frame++)
        {
            ledger.Record(3, 4, Key(frame), 6, 7, 1000);
        }
        Check(!ledger.Consume(Key(1), 4, 6, 7, 1000), "the ring overwrites its oldest entry at capacity");
        Check(ledger.Consume(Key(RescuedHitLedger::Capacity + 1), 4, 6, 7, 1000), "the newest ring entry is retained");
        ledger.Clear();
        Check(!ledger.Consume(Key(2), 4, 6, 7, 1000), "clear retains no pending rescue");
        ledger.Record(3, 4, Key(2), 6, 7, 1000);
        ledger.Reset();
        Check(!ledger.Consume(Key(2), 4, 6, 7, 1000), "reset clears pending entries");
    }

    if (failures != 0)
    {
        return 1;
    }
    std::cout << "RescuedHitLedger PASS\n";
    return 0;
}
