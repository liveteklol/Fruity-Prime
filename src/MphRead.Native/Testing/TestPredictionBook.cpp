#include "Mods/Network/Gameplay/Pickups/PredictionBook.hpp"

#include <cstdint>
#include <functional>
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
    PredictionBook book;
    book.Add(1, PowerUp::DoubleDamage, 12, 3, 4, 100, PowerUpFrames);
    passed &= Check(book.Holds(12, 3) && book.Awaiting(PowerUp::DoubleDamage),
        "tracks the predicted contact until the authority answers");
    passed &= Check(!book.Settle(1, true, 7, 100, 4, true).has_value(),
        "grant waits for its snapshot confirmation");

    bool askedForSpawn = false;
    book.Tick(100 + PredictionBook::UnseenFrames + 1,
        [&askedForSpawn](std::int16_t spawnId, std::uint16_t spawnCount)
        {
            askedForSpawn = spawnId == 12 && spawnCount == 3;
            return false;
        });
    passed &= Check(askedForSpawn && book.Holds(12, 3),
        "unseen grant ages into confirmation but waits for item sync");
    book.Tick(100 + PredictionBook::UnseenFrames + 2,
        [](std::int16_t, std::uint16_t) { return true; });
    passed &= Check(!book.Holds(12, 3), "settled item sync retires the confirmed prediction");

    book.Add(2, PowerUp::Cloak, 18, 1, 4, 500, 300);
    passed &= Check(!book.Settle(2, true, 9, 500, 4, true).has_value(), "accepts a second power-up grant");
    book.Seen(PowerUp::Cloak, 9);
    const std::function<bool(std::int16_t, std::uint16_t)> noItemSync{};
    book.Tick(500 + PredictionBook::HoldFrames, noItemSync);
    passed &= Check(book.Holds(18, 1), "retains a confirmed spawn through the hold boundary");
    book.Tick(500 + PredictionBook::HoldFrames + 1, noItemSync);
    passed &= Check(!book.Holds(18, 1), "empty optional callback remains safe and hold age expires the spawn");

    book.Add(3, PowerUp::Deathalt, 25, 2, 4, 900, 200);
    book.Reset();
    passed &= Check(!book.Holds(25, 2) && !book.Awaiting(PowerUp::Deathalt), "reset clears prediction state");

    PredictionBook refused;
    refused.Seen(PowerUp::DoubleDamage, 7);
    refused.Add(10, PowerUp::DoubleDamage, 31, 1, 4, 100, 700);
    const auto rollback = refused.Settle(10, false, 0, 150, 4, true);
    passed &= Check(rollback.has_value() && rollback->Kind == PowerUp::DoubleDamage
        && rollback->Frames == 650,
        "a refusal restores the prior effect after elapsed time");

    PredictionBook orderedRefusals;
    orderedRefusals.Seen(PowerUp::Cloak, 9);
    orderedRefusals.Add(11, PowerUp::Cloak, 32, 1, 5, 100, 700);
    orderedRefusals.Add(12, PowerUp::Cloak, 33, 1, 5, 130, 1770);
    passed &= Check(!orderedRefusals.Settle(11, false, 0, 150, 5, true).has_value(),
        "an earlier refusal transfers its rollback baseline to a later pending pickup");
    const auto orderedRollback = orderedRefusals.Settle(12, false, 0, 150, 5, true);
    passed &= Check(orderedRollback.has_value() && orderedRollback->Frames == 650,
        "ordered refusals restore the original effect once the final prediction is rejected");

    PredictionBook reorderedRefusals;
    reorderedRefusals.Seen(PowerUp::Cloak, 9);
    reorderedRefusals.Add(21, PowerUp::Cloak, 42, 1, 5, 100, 700);
    reorderedRefusals.Add(22, PowerUp::Cloak, 43, 1, 5, 130, 1770);
    const auto laterRollback = reorderedRefusals.Settle(22, false, 0, 150, 5, true);
    const auto earlierRollback = reorderedRefusals.Settle(21, false, 0, 150, 5, true);
    passed &= Check(laterRollback.has_value() && laterRollback->Frames == 1750
        && earlierRollback.has_value() && earlierRollback->Frames == 650,
        "reordered refusals unwind newer then older predictions without losing the baseline");

    PredictionBook superseded;
    superseded.Seen(PowerUp::Deathalt, 3);
    superseded.Add(30, PowerUp::Deathalt, 51, 1, 6, 200, 700);
    superseded.Seen(PowerUp::Deathalt, 4);
    passed &= Check(!superseded.Settle(30, false, 0, 210, 6, true).has_value(),
        "an authoritative snapshot sequence supersedes a late refusal rollback");

    PredictionBook dead;
    dead.Seen(PowerUp::DoubleDamage, 2);
    dead.Add(40, PowerUp::DoubleDamage, 61, 1, 8, 300, 700);
    passed &= Check(!dead.Settle(40, false, 0, 310, 8, false).has_value(),
        "a refusal cannot roll an effect back onto a dead player");
    return passed ? 0 : 1;
}
