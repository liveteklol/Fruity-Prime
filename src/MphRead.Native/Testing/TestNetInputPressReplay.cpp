#include "Mods/Network/Gameplay/Player/NetInputPressReplay.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

using namespace MphRead::Mods::Network;

namespace
{
    bool Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
        }
        return condition;
    }

    IntentPacket MakeIntent(std::uint32_t frame, std::initializer_list<std::uint32_t> history)
    {
        IntentPacket intent{};
        intent.Frame = frame;
        intent.SlotGeneration = 10;
        intent.LifeId = 20;
        intent.Presses = std::make_shared<std::vector<std::uint32_t>>(
            IntentPacket::PressHistory, 0);
        std::size_t index = 0;
        for (const std::uint32_t press : history)
        {
            if (index >= intent.Presses->size())
            {
                break;
            }
            (*intent.Presses)[index++] = press;
        }
        return intent;
    }
}

int main()
{
    NetInputPressReplay::Reset();
    bool passed = true;

    const IntentPacket first = MakeIntent(100, {
        static_cast<std::uint32_t>(IntentButtons::AltAttack),
        0, 0, static_cast<std::uint32_t>(IntentButtons::Shoot)});
    const NetInputPressReplayResult firstReplay = NetInputPressReplay::Consume(2, first);
    passed &= Check(firstReplay.Pressed == (IntentButtons::AltAttack | IntentButtons::Shoot),
        "the first validated life intent replays its bounded press history");
    passed &= Check(firstReplay.AltAttackAge == 0 && firstReplay.ShootAge == 3,
        "the first replay preserves each action's event age");

    const NetInputPressReplayResult duplicate = NetInputPressReplay::Consume(2, first);
    passed &= Check(duplicate.Pressed == IntentButtons::None,
        "re-reading the same intent never replays an edge twice");

    const IntentPacket next = MakeIntent(101, {
        0, static_cast<std::uint32_t>(IntentButtons::AltAttack),
        0, 0, static_cast<std::uint32_t>(IntentButtons::Shoot)});
    const NetInputPressReplayResult overlap = NetInputPressReplay::Consume(2, next);
    passed &= Check(overlap.Pressed == IntentButtons::None,
        "overlapping history frames are deduplicated across consecutive intents");

    const IntentPacket later = MakeIntent(104, {
        static_cast<std::uint32_t>(IntentButtons::Morph),
        0, static_cast<std::uint32_t>(IntentButtons::AltAttack)});
    const NetInputPressReplayResult recovered = NetInputPressReplay::Consume(2, later);
    passed &= Check(recovered.Pressed == (IntentButtons::Morph | IntentButtons::AltAttack)
        && recovered.AltAttackAge == 2,
        "new presses survive a skipped intent and retain their age");

    const IntentPacket stale = MakeIntent(103, { static_cast<std::uint32_t>(IntentButtons::Shoot) });
    passed &= Check(NetInputPressReplay::Consume(2, stale).Pressed == IntentButtons::None,
        "an out-of-order intent cannot rewind the replay cursor");

    const IntentPacket otherSlot = MakeIntent(100, { static_cast<std::uint32_t>(IntentButtons::Jump) });
    passed &= Check(NetInputPressReplay::Consume(3, otherSlot).Pressed == IntentButtons::Jump,
        "each slot owns an independent replay cursor");

    NetInputPressReplay::ForgetSlot(2);
    passed &= Check(NetInputPressReplay::Consume(2, first).Pressed
        == (IntentButtons::AltAttack | IntentButtons::Shoot),
        "a new occupant life can consume its first validated history window");

    IntentPacket nextLife = MakeIntent(200, { static_cast<std::uint32_t>(IntentButtons::Morph) });
    nextLife.LifeId = 21;
    passed &= Check(NetInputPressReplay::Consume(2, nextLife).Pressed == IntentButtons::Morph,
        "a new life resets deduplication even when the slot was not explicitly forgotten");

    IntentPacket nextOccupant = MakeIntent(250, { static_cast<std::uint32_t>(IntentButtons::Shoot) });
    nextOccupant.SlotGeneration = 11;
    nextOccupant.LifeId = 1;
    passed &= Check(NetInputPressReplay::Consume(2, nextOccupant).Pressed == IntentButtons::Shoot,
        "a new slot generation starts an independent press history");

    NetInputPressReplay::ForgetSlot(2);
    const IntentPacket multipleShots = MakeIntent(300, {
        static_cast<std::uint32_t>(IntentButtons::Shoot), 0, 0,
        static_cast<std::uint32_t>(IntentButtons::Shoot)});
    passed &= Check(NetInputPressReplay::Consume(2, multipleShots).ShootAge == 3,
        "multiple recovered shots preserve the oldest-edge rewind age");

    NetInputPressReplay::Reset();
    const IntentPacket beforeFrameWrap = MakeIntent(
        std::numeric_limits<std::uint32_t>::max() - 1,
        { static_cast<std::uint32_t>(IntentButtons::AltAttack) });
    passed &= Check(NetInputPressReplay::Consume(2, beforeFrameWrap).Pressed == IntentButtons::AltAttack,
        "press replay accepts events immediately before the frame counter wraps");
    const IntentPacket afterFrameWrap = MakeIntent(1, {
        0, 0, static_cast<std::uint32_t>(IntentButtons::Shoot) });
    passed &= Check(NetInputPressReplay::Consume(2, afterFrameWrap).Pressed == IntentButtons::Shoot
        && NetInputPressReplay::Consume(2, afterFrameWrap).Pressed == IntentButtons::None,
        "press replay remains ordered and deduplicated across frame counter wrap");

    const IntentPacket withoutHistory{};
    passed &= Check(NetInputPressReplay::Consume(4, withoutHistory).Pressed == IntentButtons::None,
        "an intent without press history is ignored");
    passed &= Check(NetInputPressReplay::Consume(-1, first).Pressed == IntentButtons::None
        && NetInputPressReplay::Consume(static_cast<std::int32_t>(NetworkSlotCapacity), first).Pressed == IntentButtons::None,
        "invalid slots cannot access replay state");

    if (!passed)
    {
        return 1;
    }
    std::cout << "Network input press replay checks passed.\n";
    return 0;
}
