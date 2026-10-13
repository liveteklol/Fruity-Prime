#pragma once

#include <cstdint>

namespace MphRead::Mods::Network
{
    // Shared role eligibility for client-originated Hit and Pickup claims.
    // Feature-specific gates such as HitClaims::Enabled remain with the owner.
    [[nodiscard]] constexpr bool CanSubmitClientClaim(bool sessionActive,
        bool authority, bool host, std::int32_t localSlot) noexcept
    {
        return sessionActive && !authority && !host && localSlot >= 0;
    }
}
