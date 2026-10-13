#pragma once

#include "Mods/Network/Domain/NetworkCapacity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // Remembers recent launch frames per shooter and weapon so one weapon's
    // cooldown bounds how quickly distinct hit claims may name shots.
    class HitClaimRateLimiter final
    {
    public:
        static constexpr std::size_t SlotCapacity = NetworkSlotCapacity;
        static constexpr std::size_t WeaponCapacity = 9;
        static constexpr std::size_t RecentLaunchCapacity = 8;

        [[nodiscard]] bool Allows(std::int32_t shooterSlot, std::uint8_t beam,
            std::uint32_t launchFrame, std::int32_t cooldownFrames) noexcept;
        void ForgetSlot(std::int32_t slot) noexcept;
        void Reset() noexcept;

    private:
        using RecentLaunches = std::array<std::uint32_t, RecentLaunchCapacity>;
        std::array<std::array<RecentLaunches, WeaponCapacity>, SlotCapacity> _recent{};
    };
}
