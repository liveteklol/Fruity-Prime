#pragma once

#include "NativeRuntime/OpenTK/Mathematics.hpp"

#include <cmath>

namespace MphRead::Mods::Network::Domain
{
    inline constexpr float PlayerVectorComponentLimit = 100000.0F;

    [[nodiscard]] inline bool IsFiniteVector3(
        const OpenTK::Mathematics::Vector3& value) noexcept
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

    [[nodiscard]] inline bool IsSanePlayerVector(
        const OpenTK::Mathematics::Vector3& value) noexcept
    {
        return IsFiniteVector3(value)
            && std::fabs(value.X) < PlayerVectorComponentLimit
            && std::fabs(value.Y) < PlayerVectorComponentLimit
            && std::fabs(value.Z) < PlayerVectorComponentLimit;
    }
}
