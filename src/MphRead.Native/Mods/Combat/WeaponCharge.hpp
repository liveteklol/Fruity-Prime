#pragma once

#include "Metadata/Metadata.hpp"

#include <cstdint>

namespace MphRead::Mods::Combat
{
    struct WeaponChargeState final
    {
        bool Charged = false;
        float Percent = 0.0F;
    };

    [[nodiscard]] inline float InterpolateWeaponChargeAmount(
        std::int32_t uncharged, std::int32_t minimum, std::int32_t charged,
        WeaponChargeState state) noexcept
    {
        return state.Percent <= 0.0F
            ? static_cast<float>(uncharged)
            : static_cast<float>(minimum)
                + static_cast<float>(charged - minimum) * state.Percent;
    }

    [[nodiscard]] inline WeaponChargeState ResolveWeaponCharge(
        WeaponFlags flags, std::int32_t minCharge, std::int32_t fullCharge,
        std::uint16_t chargeLevel) noexcept
    {
        if (!TestFlag(flags, WeaponFlags::CanCharge))
        {
            return {};
        }
        if (TestFlag(flags, WeaponFlags::PartialCharge))
        {
            const std::int32_t minimum = minCharge * 2;
            const std::int32_t full = fullCharge * 2;
            if (chargeLevel < minimum)
            {
                return {};
            }
            if (full <= minimum)
            {
                return WeaponChargeState{true, 1.0F};
            }
            return WeaponChargeState{true,
                (static_cast<float>(chargeLevel) - static_cast<float>(minimum))
                    / static_cast<float>(full - minimum)};
        }
        return chargeLevel >= fullCharge * 2
            ? WeaponChargeState{true, 1.0F} : WeaponChargeState{};
    }
}
