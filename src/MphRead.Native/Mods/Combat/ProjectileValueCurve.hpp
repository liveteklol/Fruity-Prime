#pragma once

#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MphRead::Mods::Combat
{
    // How a projectile value moves from one end to the other as a ratio goes
    // from 0 to 1, by the weapon table's interpolation type: speed over the
    // decay time, damage over distance. One curve for the physical flight,
    // the drawn flight and the 30 Hz reference alike.
    [[nodiscard]] inline float InterpolateProjectileValue(
        std::int32_t type, float value1, float value2, float ratio) noexcept
    {
        using ::OpenTK::Mathematics::MathHelper::DegreesToRadians;
        if (type == 3)
        {
            return ratio > 1.0F ? value2 : value1;
        }
        ratio = std::clamp(ratio, 0.0F, 1.0F);
        if (type == 0)
        {
            return value1 + (value2 - value1) * ratio;
        }
        if (type == 1)
        {
            return value1 + (value2 - value1)
                * ((std::sin(DegreesToRadians(270.0F - 180.0F * ratio)) + 1.0F) / 2.0F);
        }
        if (type == 2)
        {
            return value1 + (value2 - value1)
                * (std::sin(DegreesToRadians(270.0F - 90.0F * ratio)) + 1.0F);
        }
        return 0.0F;
    }
}
