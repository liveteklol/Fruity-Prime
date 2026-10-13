#include "Mods/Network/Gameplay/Claims/Hit/HitClaimRateLimiter.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    bool HitClaimRateLimiter::Allows(std::int32_t shooterSlot, std::uint8_t beam,
        std::uint32_t launchFrame, std::int32_t cooldownFrames) noexcept
    {
        if (shooterSlot < 0 || static_cast<std::size_t>(shooterSlot) >= SlotCapacity
            || beam >= WeaponCapacity || launchFrame == 0 || cooldownFrames <= 0)
        {
            return true;
        }

        const std::uint32_t floor = static_cast<std::uint32_t>(cooldownFrames);
        RecentLaunches& recent
            = _recent[static_cast<std::size_t>(shooterSlot)][static_cast<std::size_t>(beam)];
        for (const std::uint32_t seen : recent)
        {
            if (seen == 0 || seen == launchFrame)
            {
                continue;
            }
            const std::uint32_t gap = seen > launchFrame
                ? seen - launchFrame
                : launchFrame - seen;
            if (gap < floor)
            {
                return false;
            }
        }
        if (std::find(recent.begin(), recent.end(), launchFrame) == recent.end())
        {
            std::rotate(recent.rbegin(), recent.rbegin() + 1, recent.rend());
            recent[0] = launchFrame;
        }
        return true;
    }

    void HitClaimRateLimiter::ForgetSlot(std::int32_t slot) noexcept
    {
        if (slot < 0 || static_cast<std::size_t>(slot) >= SlotCapacity)
        {
            return;
        }
        _recent[static_cast<std::size_t>(slot)] = {};
    }

    void HitClaimRateLimiter::Reset() noexcept
    {
        _recent = {};
    }
}
