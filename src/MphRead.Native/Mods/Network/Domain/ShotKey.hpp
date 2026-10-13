#pragma once

#include <cstdint>
#include <string>

namespace MphRead::Mods::Network
{
    // Stable gameplay identity for one launch. The diagnostic module owns its
    // presentation; the value itself is shared by claim and weapon ledgers.
    struct ShotKey final
    {
        std::uint64_t AuthorityEpoch = 0;
        std::uint16_t MatchId = 0;
        std::int32_t ShooterSlot = 0;
        std::uint16_t Generation = 0;
        std::uint16_t LifeId = 0;
        std::uint32_t LaunchFrame = 0;

        constexpr ShotKey() noexcept = default;
        constexpr ShotKey(std::uint64_t authorityEpoch, std::uint16_t matchId, std::int32_t shooterSlot,
            std::uint16_t generation, std::uint16_t lifeId, std::uint32_t launchFrame) noexcept
            : AuthorityEpoch(authorityEpoch), MatchId(matchId), ShooterSlot(shooterSlot),
              Generation(generation), LifeId(lifeId), LaunchFrame(launchFrame)
        {
        }

        [[nodiscard]] static ShotKey For(std::int32_t slot, std::uint32_t frame);
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] friend constexpr bool operator==(const ShotKey& left, const ShotKey& right) noexcept = default;
    };
}
