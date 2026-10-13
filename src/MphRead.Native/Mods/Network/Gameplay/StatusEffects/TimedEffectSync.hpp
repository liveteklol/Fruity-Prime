#pragma once

#include <cstdint>
#include <limits>

namespace MphRead::Mods::Network
{
    enum class TimedEffectSyncAction : std::uint8_t
    {
        Keep,
        Start,
        Update,
        End
    };

    struct TimedEffectSyncDecision
    {
        TimedEffectSyncAction Action = TimedEffectSyncAction::Keep;
        std::uint16_t RemainingFrames = 0;
    };

    // A repeated snapshot is not a fresh timer sample. Project its timer to
    // the current local simulation frame so packet gaps cannot restore time
    // that the local effect already consumed. An unknown age leaves the wire
    // value intact until the first accepted snapshot establishes an arrival.
    [[nodiscard]] constexpr std::uint16_t RemainingAfterSnapshotAge(
        std::uint16_t reportedFrames, std::uint32_t snapshotAgeFrames) noexcept
    {
        if (snapshotAgeFrames == std::numeric_limits<std::uint32_t>::max())
        {
            return reportedFrames;
        }
        if (snapshotAgeFrames >= reportedFrames)
        {
            return 0;
        }
        return static_cast<std::uint16_t>(reportedFrames - snapshotAgeFrames);
    }

    // Shared lifecycle rule for a timer received in an authoritative player
    // snapshot. Policies own how each effect starts, updates, and ends.
    [[nodiscard]] constexpr TimedEffectSyncDecision ReconcileTimedEffect(
        std::uint16_t reportedFrames, std::uint16_t localFrames, bool awaitingPrediction) noexcept
    {
        if (reportedFrames > 0)
        {
            if (localFrames == 0 || (localFrames == 1 && reportedFrames > 1))
            {
                return { TimedEffectSyncAction::Start, reportedFrames };
            }
            if (reportedFrames != localFrames || awaitingPrediction)
            {
                return { TimedEffectSyncAction::Update, reportedFrames };
            }
            return {};
        }
        if (localFrames > 1 && !awaitingPrediction)
        {
            return { TimedEffectSyncAction::End, 0 };
        }
        return {};
    }

    template<typename Policy>
    void ApplyTimedEffect(std::uint16_t reportedFrames, std::uint16_t localFrames,
        bool awaitingPrediction, Policy& policy)
    {
        const TimedEffectSyncDecision decision
            = ReconcileTimedEffect(reportedFrames, localFrames, awaitingPrediction);
        switch (decision.Action)
        {
        case TimedEffectSyncAction::Keep:
            break;
        case TimedEffectSyncAction::Start:
            policy.Start(decision.RemainingFrames);
            break;
        case TimedEffectSyncAction::Update:
            policy.Update(decision.RemainingFrames);
            break;
        case TimedEffectSyncAction::End:
            policy.End();
            break;
        }
    }
}
