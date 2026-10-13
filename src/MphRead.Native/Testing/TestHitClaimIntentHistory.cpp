#include "Mods/Network/Gameplay/Claims/Hit/HitClaimIntentHistory.hpp"

#include <cmath>
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

    MphRead::Mods::Network::IntentPacket MakeIntent(
        std::uint32_t frame, std::uint32_t ack, bool hasShot = false)
    {
        MphRead::Mods::Network::IntentPacket intent{};
        intent.Frame = frame;
        intent.AckFrame = ack;
        intent.HasShot = hasShot;
        intent.ShotOrigin = { static_cast<float>(frame), 2.0F, 3.0F };
        intent.ShotDirection = { 3.0F, 4.0F, 0.0F };
        return intent;
    }
}

int main()
{
    using namespace MphRead::Mods::Network;
    bool passed = true;
    HitClaimIntentHistory history;

    passed &= Check(!history.HasSlot(-1) && !history.HasSlot(8), "rejects slots outside the fixed player range");
    history.Record(-1, MakeIntent(1, 50, true));
    history.Record(8, MakeIntent(1, 50, true));
    history.Record(2, MakeIntent(5, 50));
    history.Record(2, MakeIntent(6, 48));
    passed &= Check(history.LatestAck(2) == 50 && history.LatestAck(1) == 0,
        "keeps the newest acknowledgement per shooter");
    passed &= Check(history.AckForFrame(2, 5) == 50 && history.AckForFrame(2, 6) == 48
        && !history.AckForFrame(2, 69).has_value(), "looks up acknowledgement by exact intent frame");

    history.Record(2, MakeIntent(69, 53));
    passed &= Check(!history.AckForFrame(2, 5).has_value() && history.AckForFrame(2, 69) == 53,
        "a ring collision replaces only the same acknowledgement bucket");

    history.Record(2, MakeIntent(70, 54, true));
    const ClaimShotRay* closest = history.NearestRay(2, 55, 3);
    passed &= Check(closest != nullptr && closest->Ack == 54 && closest->Origin.X == 70.0F,
        "records shot origin and selects the closest ray within the exclusive window");
    passed &= Check(std::abs(OpenTK::Mathematics::Length(closest->Direction) - 1.0F) < 0.0001F,
        "stores a normalized shot direction");
    passed &= Check(history.NearestRay(2, 57, 3) == nullptr && history.NearestRay(8, 54, 3) == nullptr,
        "rejects rays outside the window and from another slot");

    for (std::uint32_t i = 0; i < HitClaimIntentHistory::ShotRayCapacity; i++)
    {
        history.Record(4, MakeIntent(100 + i, 100 + i, true));
    }
    history.Record(4, MakeIntent(132, 132, true));
    passed &= Check(history.NearestRay(4, 100, 1) == nullptr
        && history.NearestRay(4, 132, 3) != nullptr, "overwrites the oldest ray in bounded ring order");

    history.Reset();
    passed &= Check(history.LatestAck(2) == 0 && !history.AckForFrame(2, 69).has_value()
        && history.NearestRay(4, 132, 3) == nullptr, "reset clears all shooter evidence");
    return passed ? 0 : 1;
}
