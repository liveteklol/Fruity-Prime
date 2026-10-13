#include "Mods/Network/Gameplay/Player/NetInputHistory.hpp"

#include <cstdint>
#include <iostream>
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
}

int main()
{
    NetInputHistory::Reset();
    NetInputHistory::Record(3, IntentButtons::AltAttack);
    NetInputHistory::Record(3, IntentButtons::None);

    IntentPacket botIntent{};
    botIntent.Presses = std::make_shared<std::vector<std::uint32_t>>(
        IntentPacket::PressHistory, 0);
    NetInputHistory::Attach(botIntent, 3);
    bool passed = Check(botIntent.Presses->size() == IntentPacket::PressHistory,
        "history uses protocol press-history length");
    passed &= Check((*botIntent.Presses)[0] == 0
        && (*botIntent.Presses)[1] == static_cast<std::uint32_t>(IntentButtons::AltAttack),
        "alt-attack edge survives the following held/release frame");

    IntentPacket otherSlot{};
    NetInputHistory::Attach(otherSlot, 2);
    passed &= Check(otherSlot.Presses != nullptr
        && (*otherSlot.Presses)[1] == 0,
        "another player slot cannot inherit the bot edge");

    for (std::int32_t i = 0; i < IntentPacket::PressHistory - 2; ++i)
    {
        NetInputHistory::Record(3, IntentButtons::None);
    }
    NetInputHistory::Attach(botIntent, 3);
    passed &= Check((*botIntent.Presses)[IntentPacket::PressHistory - 1]
        == static_cast<std::uint32_t>(IntentButtons::AltAttack),
        "edge remains available throughout the loss-recovery history");

    NetInputHistory::Record(3, IntentButtons::None);
    NetInputHistory::Attach(botIntent, 3);
    passed &= Check((*botIntent.Presses)[IntentPacket::PressHistory - 1] == 0,
        "edge expires after leaving the recovery window");

    NetInputHistory::ForgetSlot(3);
    NetInputHistory::Attach(botIntent, 3);
    for (const std::uint32_t press : *botIntent.Presses)
    {
        passed &= Check(press == 0, "slot reset clears its press history");
    }

    if (!passed)
    {
        return 1;
    }
    std::cout << "Network input history checks passed.\n";
    return 0;
}
