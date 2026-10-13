#pragma once

#include "Mods/Network/Domain/NetworkVectorRules.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cmath>

namespace MphRead::Mods::Network::ShotRayValidation
{
    inline constexpr float MinimumDirectionLengthSquared = 0.25F;
    inline constexpr float MaximumDirectionLengthSquared = 4.0F;
    inline constexpr float MaximumOriginOffset = 6.0F;

    [[nodiscard]] inline bool IsFinite(const OpenTK::Mathematics::Vector3& value) noexcept
    {
        return Domain::IsFiniteVector3(value);
    }

    [[nodiscard]] inline bool IsValidDirection(const OpenTK::Mathematics::Vector3& direction) noexcept
    {
        if (!IsFinite(direction))
        {
            return false;
        }
        const float lengthSquared = direction.LengthSquared();
        return std::isfinite(lengthSquared)
            && lengthSquared > MinimumDirectionLengthSquared
            && lengthSquared < MaximumDirectionLengthSquared;
    }

    [[nodiscard]] inline bool IsPlausibleOrigin(
        const OpenTK::Mathematics::Vector3& origin,
        const OpenTK::Mathematics::Vector3& shooterPosition,
        float maximumOffset = MaximumOriginOffset) noexcept
    {
        if (!IsFinite(origin) || !IsFinite(shooterPosition)
            || !std::isfinite(maximumOffset) || maximumOffset < 0.0F)
        {
            return false;
        }
        const float offsetSquared = (origin - shooterPosition).LengthSquared();
        const float maximumOffsetSquared = maximumOffset * maximumOffset;
        return std::isfinite(offsetSquared) && std::isfinite(maximumOffsetSquared)
            && offsetSquared <= maximumOffsetSquared;
    }
}
