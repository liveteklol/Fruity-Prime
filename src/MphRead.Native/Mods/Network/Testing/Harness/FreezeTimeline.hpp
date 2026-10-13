#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // Per-slot, allocation-free observations for the netcheck freeze gate.
    // Episode lengths are diagnostic: network delivery can shift the first
    // locally observed frozen frame. Timer acceptance compares only live
    // states; dead/corpse timer values are retained as diagnostic evidence.
    class FreezeTimeline final
    {
    public:
        static constexpr std::size_t SlotCapacity = NetworkSlotCapacity;

        struct SlotStats final
        {
            std::uint64_t AppliedFrozenFrames = 0;
            std::uint32_t AppliedEpisodesStarted = 0;
            std::uint32_t AppliedEpisodesCompleted = 0;
            std::uint32_t AppliedLocalEpisodesCompleted = 0;
            std::uint32_t AppliedDurationMin = 0;
            std::uint32_t AppliedDurationMax = 0;
            std::uint64_t AppliedDurationTotal = 0;
            std::uint32_t AppliedLocalDurationMin = 0;
            std::uint32_t AppliedLocalDurationMax = 0;
            std::uint64_t AppliedLocalDurationTotal = 0;
            std::uint16_t AppliedLastRemaining = 0;
            std::uint32_t AppliedEpisodeStartFrame = 0;
            std::uint32_t AppliedAuthorityEpisodeStartFrame = 0;
            std::uint32_t AppliedLastThawFrame = 0;
            std::uint32_t AppliedAuthorityLastThawFrame = 0;
            std::uint32_t AppliedEpisodeOpenFrames = 0;
            std::uint32_t AppliedEpisodesInterrupted = 0;

            std::uint64_t AuthoritySnapshots = 0;
            std::uint32_t AuthorityEpisodesStarted = 0;
            std::uint32_t AuthorityThaws = 0;
            std::uint32_t AuthorityDurationMin = 0;
            std::uint32_t AuthorityDurationMax = 0;
            std::uint64_t AuthorityDurationTotal = 0;
            std::uint32_t AuthorityFrozenSamples = 0;
            std::uint32_t AuthorityRemainingDeltaMax = 0;
            std::uint32_t AuthorityFrozenExcludedSamples = 0;
            std::uint32_t AuthorityRemainingDeltaMaxExcluded = 0;
            std::uint32_t AuthorityLastThawFrame = 0;
            std::uint32_t AuthorityEpisodesInterrupted = 0;
            std::uint32_t OccupantChanges = 0;
            std::uint16_t HighestOccupantGeneration = 0;
            std::uint32_t AuthorityEpisodeStartFrame = 0;
            std::uint32_t AuthorityLastFrame = 0;
            std::uint16_t AuthorityLastRemaining = 0;
            bool AuthorityEpisodeOpen = false;
        };

        void Reset() noexcept;
        void Observe(std::int32_t slot, bool appliedInPlay, std::uint16_t appliedRemaining,
            bool hasAuthorityState, bool authorityInPlay, std::uint16_t authorityRemaining,
            std::uint64_t snapshotSequence, std::uint32_t authorityFrame,
            std::uint32_t localFrame, std::uint16_t occupantGeneration,
            std::uint32_t snapshotAgeFrames = 0) noexcept;
        [[nodiscard]] const SlotStats& At(std::size_t slot) const noexcept;
        [[nodiscard]] bool HasObservations(std::size_t slot) const noexcept;

    private:
        struct SlotState final
        {
            SlotStats Stats{};
            std::uint32_t AppliedEpisodeFrames = 0;
            std::uint32_t AppliedEpisodeAuthorityStartFrame = 0;
            std::uint64_t LastSnapshotSequence = 0;
            std::uint32_t LastAuthorityFrame = 0;
            std::uint32_t ClockAuthorityFrame = 0;
            std::uint32_t ClockLocalFrame = 0;
            std::uint16_t OccupantGeneration = 0;
            bool HasAuthoritySample = false;
            bool HasAuthorityClock = false;
            bool HasOccupantGeneration = false;
        };

        std::array<SlotState, SlotCapacity> _slots{};
    };
}
