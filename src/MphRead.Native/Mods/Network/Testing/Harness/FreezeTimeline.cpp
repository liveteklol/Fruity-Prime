#include "Mods/Network/Testing/Harness/FreezeTimeline.hpp"

#include "Mods/Network/Gameplay/StatusEffects/TimedEffectSync.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    namespace
    {
        void RecordDuration(std::uint32_t duration, std::uint32_t& sampleCount,
            std::uint32_t& minimum, std::uint32_t& maximum, std::uint64_t& total) noexcept
        {
            if (sampleCount == 0 || duration < minimum)
            {
                minimum = duration;
            }
            if (duration > maximum)
            {
                maximum = duration;
            }
            total += duration;
            ++sampleCount;
        }
    }

    void FreezeTimeline::Reset() noexcept
    {
        _slots = {};
    }

    void FreezeTimeline::Observe(std::int32_t slot, bool appliedInPlay,
        std::uint16_t appliedRemaining, bool hasAuthorityState,
        bool authorityInPlay, std::uint16_t authorityRemaining,
        std::uint64_t snapshotSequence,
        std::uint32_t authorityFrame, std::uint32_t localFrame,
        std::uint16_t occupantGeneration, std::uint32_t snapshotAgeFrames) noexcept
    {
        if (slot < 0 || static_cast<std::size_t>(slot) >= _slots.size())
        {
            return;
        }

        SlotState& state = _slots[static_cast<std::size_t>(slot)];
        SlotStats& stats = state.Stats;
        stats.AppliedLastRemaining = appliedInPlay ? appliedRemaining : 0;
        bool occupantChanged = false;
        if (hasAuthorityState && authorityFrame != 0
            && (!state.HasAuthorityClock || state.ClockAuthorityFrame != authorityFrame))
        {
            state.ClockAuthorityFrame = authorityFrame;
            state.ClockLocalFrame = localFrame;
            state.HasAuthorityClock = true;
        }
        const std::uint32_t appliedAuthorityFrame = state.HasAuthorityClock
            ? state.ClockAuthorityFrame + (localFrame - state.ClockLocalFrame)
            : localFrame;
        if (!state.HasOccupantGeneration)
        {
            state.OccupantGeneration = occupantGeneration;
            stats.HighestOccupantGeneration = occupantGeneration;
            state.HasOccupantGeneration = true;
        }
        else if (state.OccupantGeneration != occupantGeneration)
        {
            // The latest authority snapshot may still belong to the previous
            // occupant when the local roster has already advanced. Do not
            // compare those two lifetimes as if they were the same player.
            occupantChanged = true;
            ++stats.OccupantChanges;
            if (state.AppliedEpisodeFrames != 0)
            {
                ++stats.AppliedEpisodesInterrupted;
                state.AppliedEpisodeFrames = 0;
                stats.AppliedEpisodeOpenFrames = 0;
            }
            if (stats.AuthorityEpisodeOpen)
            {
                ++stats.AuthorityEpisodesInterrupted;
                stats.AuthorityEpisodeOpen = false;
            }
            state.HasAuthoritySample = false;
            state.OccupantGeneration = occupantGeneration;
            stats.HighestOccupantGeneration = std::max(stats.HighestOccupantGeneration, occupantGeneration);
        }

        if (appliedInPlay && appliedRemaining > 0)
        {
            if (state.AppliedEpisodeFrames == 0)
            {
                ++stats.AppliedEpisodesStarted;
                stats.AppliedEpisodeStartFrame = localFrame;
                stats.AppliedAuthorityEpisodeStartFrame = appliedAuthorityFrame;
                state.AppliedEpisodeAuthorityStartFrame = appliedAuthorityFrame;
            }
            ++state.AppliedEpisodeFrames;
            ++stats.AppliedFrozenFrames;
            stats.AppliedEpisodeOpenFrames = state.AppliedEpisodeFrames;
        }
        else if (state.AppliedEpisodeFrames != 0)
        {
            if (appliedInPlay)
            {
                RecordDuration(state.AppliedEpisodeFrames, stats.AppliedLocalEpisodesCompleted,
                    stats.AppliedLocalDurationMin, stats.AppliedLocalDurationMax, stats.AppliedLocalDurationTotal);
                RecordDuration(appliedAuthorityFrame - state.AppliedEpisodeAuthorityStartFrame,
                    stats.AppliedEpisodesCompleted, stats.AppliedDurationMin,
                    stats.AppliedDurationMax, stats.AppliedDurationTotal);
                stats.AppliedLastThawFrame = localFrame;
                stats.AppliedAuthorityLastThawFrame = appliedAuthorityFrame;
            }
            else
            {
                ++stats.AppliedEpisodesInterrupted;
            }
            state.AppliedEpisodeFrames = 0;
            stats.AppliedEpisodeOpenFrames = 0;
        }

        if (!hasAuthorityState)
        {
            if (stats.AuthorityEpisodeOpen)
            {
                ++stats.AuthorityEpisodesInterrupted;
                stats.AuthorityEpisodeOpen = false;
            }
            state.HasAuthoritySample = false;
            stats.AuthorityLastRemaining = 0;
            return;
        }
        if (state.HasAuthoritySample && state.LastSnapshotSequence == snapshotSequence
            && state.LastAuthorityFrame == authorityFrame)
        {
            return;
        }

        ++stats.AuthoritySnapshots;
        const std::uint16_t projectedAuthorityRemaining
            = RemainingAfterSnapshotAge(authorityRemaining, snapshotAgeFrames);
        if (appliedInPlay && authorityInPlay && !occupantChanged
            && (authorityRemaining > 0 || appliedRemaining > 0))
        {
            // Compare both timers at the local observation frame. The latest
            // authority timer is projected forward by time since its arrival;
            // without that projection a repeated snapshot looks like timer
            // drift and can hide a genuine delayed thaw.
            ++stats.AuthorityFrozenSamples;
            const std::uint32_t localRemaining = appliedRemaining;
            const std::uint32_t reportedRemaining = projectedAuthorityRemaining;
            const std::uint32_t delta = localRemaining >= reportedRemaining
                ? localRemaining - reportedRemaining : reportedRemaining - localRemaining;
            stats.AuthorityRemainingDeltaMax = std::max(stats.AuthorityRemainingDeltaMax, delta);
        }
        else if (authorityRemaining > 0)
        {
            ++stats.AuthorityFrozenExcludedSamples;
            const std::uint32_t localRemaining = appliedRemaining;
            const std::uint32_t reportedRemaining = projectedAuthorityRemaining;
            const std::uint32_t delta = localRemaining >= reportedRemaining
                ? localRemaining - reportedRemaining : reportedRemaining - localRemaining;
            stats.AuthorityRemainingDeltaMaxExcluded
                = std::max(stats.AuthorityRemainingDeltaMaxExcluded, delta);
        }

        if (!authorityInPlay)
        {
            if (stats.AuthorityEpisodeOpen)
            {
                ++stats.AuthorityEpisodesInterrupted;
                stats.AuthorityEpisodeOpen = false;
            }
            stats.AuthorityLastRemaining = 0;
            state.LastSnapshotSequence = snapshotSequence;
            state.LastAuthorityFrame = authorityFrame;
            state.HasAuthoritySample = true;
            return;
        }

        const bool frozenByAuthority = authorityRemaining > 0;
        if (frozenByAuthority && !stats.AuthorityEpisodeOpen)
        {
            ++stats.AuthorityEpisodesStarted;
            stats.AuthorityEpisodeStartFrame = authorityFrame;
            stats.AuthorityEpisodeOpen = true;
        }
        else if (!frozenByAuthority && stats.AuthorityEpisodeOpen)
        {
            const std::uint32_t duration = authorityFrame - stats.AuthorityEpisodeStartFrame;
            RecordDuration(duration, stats.AuthorityThaws, stats.AuthorityDurationMin,
                stats.AuthorityDurationMax, stats.AuthorityDurationTotal);
            stats.AuthorityLastThawFrame = authorityFrame;
            stats.AuthorityEpisodeOpen = false;
        }
        stats.AuthorityLastFrame = authorityFrame;
        stats.AuthorityLastRemaining = authorityRemaining;
        state.LastSnapshotSequence = snapshotSequence;
        state.LastAuthorityFrame = authorityFrame;
        state.HasAuthoritySample = true;
    }

    const FreezeTimeline::SlotStats& FreezeTimeline::At(std::size_t slot) const noexcept
    {
        return _slots[slot].Stats;
    }

    bool FreezeTimeline::HasObservations(std::size_t slot) const noexcept
    {
        if (slot >= _slots.size())
        {
            return false;
        }
        const SlotState& state = _slots[slot];
        return state.Stats.AppliedEpisodesStarted != 0
            || state.Stats.AuthoritySnapshots != 0
            || state.Stats.AppliedEpisodesInterrupted != 0
            || state.Stats.AuthorityEpisodesInterrupted != 0
            || state.Stats.OccupantChanges != 0
            || (state.HasOccupantGeneration && state.OccupantGeneration != 0)
            || state.AppliedEpisodeFrames != 0;
    }
}
