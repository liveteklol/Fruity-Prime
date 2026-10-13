#include "Mods/Network/Gameplay/StatusEffects/TimedEffectSync.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
    using namespace MphRead::Mods::Network;

    struct TracePolicy
    {
        int Starts = 0;
        int Updates = 0;
        int Ends = 0;
        std::uint16_t LastFrames = 0;

        void Start(std::uint16_t frames) { ++Starts; LastFrames = frames; }
        void Update(std::uint16_t frames) { ++Updates; LastFrames = frames; }
        void End() { ++Ends; LastFrames = 0; }
    };

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

    passed &= Check(ReconcileTimedEffect(0, 0, false).Action == TimedEffectSyncAction::Keep,
        "an inactive effect remains inactive");
    passed &= Check(ReconcileTimedEffect(65535, 0, false).Action == TimedEffectSyncAction::Start
        && ReconcileTimedEffect(65535, 0, false).RemainingFrames == 65535,
        "start preserves the full wire timer range");
    passed &= Check(ReconcileTimedEffect(80, 100, false).Action == TimedEffectSyncAction::Update
        && ReconcileTimedEffect(80, 80, false).Action == TimedEffectSyncAction::Keep,
        "an active timer is corrected only when it differs");
    passed &= Check(ReconcileTimedEffect(90, 1, false).Action == TimedEffectSyncAction::Start,
        "a remaining authoritative effect restarts a local timer already entering its end frame");
    passed &= Check(ReconcileTimedEffect(80, 80, true).Action == TimedEffectSyncAction::Update,
        "an equal timer still acknowledges a pending local prediction");
    passed &= Check(ReconcileTimedEffect(0, 100, false).Action == TimedEffectSyncAction::End
        && ReconcileTimedEffect(0, 100, true).Action == TimedEffectSyncAction::Keep,
        "authority end respects a pending prediction");
    passed &= Check(ReconcileTimedEffect(0, 1, false).Action == TimedEffectSyncAction::Keep,
        "an effect already in its one-frame end transition is left to simulation");
    passed &= Check(RemainingAfterSnapshotAge(120, 8) == 112
        && RemainingAfterSnapshotAge(8, 8) == 0
        && RemainingAfterSnapshotAge(4, 9) == 0,
        "snapshot age projects a repeated timer forward and clamps at zero");
    passed &= Check(RemainingAfterSnapshotAge(120, std::numeric_limits<std::uint32_t>::max()) == 120,
        "an unknown snapshot age leaves the authoritative timer unchanged");
    passed &= Check(ReconcileTimedEffect(RemainingAfterSnapshotAge(120, 8), 111, false).Action
            == TimedEffectSyncAction::Update
        && ReconcileTimedEffect(RemainingAfterSnapshotAge(120, 8), 111, false).RemainingFrames == 112,
        "a packet gap advances the received timer instead of restoring consumed frames");

    TracePolicy policy;
    ApplyTimedEffect(120, 0, false, policy);
    ApplyTimedEffect(90, 120, false, policy);
    ApplyTimedEffect(90, 90, false, policy);
    ApplyTimedEffect(0, 90, false, policy);
    passed &= Check(policy.Starts == 1 && policy.Updates == 1 && policy.Ends == 1
        && policy.LastFrames == 0,
        "the shared lifecycle dispatches start, update and end to effect policy");
    return passed ? 0 : 1;
}
