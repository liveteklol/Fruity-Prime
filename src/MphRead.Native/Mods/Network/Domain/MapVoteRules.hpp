#pragma once

#include <algorithm>
#include <cstdint>

namespace MphRead::Mods::Network::MapVoteRules
{
    [[nodiscard]] constexpr std::int32_t RequiredYesVotes(std::int32_t eligible) noexcept
    {
        // ceil(70% of the electorate), with one vote required for an empty
        // or invalid electorate to avoid making a vote pass by default.
        const std::int64_t nonnegative = std::max<std::int64_t>(0, eligible);
        const std::int64_t threshold = (nonnegative * 70 + 99) / 100;
        return static_cast<std::int32_t>(std::max<std::int64_t>(1, threshold));
    }

    [[nodiscard]] constexpr bool Passed(std::int32_t yes, std::int32_t needed) noexcept
    {
        return yes >= needed;
    }

    [[nodiscard]] constexpr bool StillPossible(
        std::int32_t eligible, std::int32_t no, std::int32_t needed) noexcept
    {
        return eligible >= no && eligible - no >= needed;
    }
}
