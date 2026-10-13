#pragma once

#include <cmath>
#include <cstdint>

namespace MphRead::Mods::Network
{
    // The most damage the authority lets one claimed hit do, worked out from
    // what it knows -- the weapon, the shot's own charge, the kind of hit,
    // the victim's resistance and the power-ups it granted -- and never from
    // the number the claim carries. A claim above it is held to it.

    // One weapon's damage at no charge, at the least charge and at full.
    struct ChargeAmounts
    {
        std::int32_t Uncharged = 0;
        std::int32_t MinCharge = 0;
        std::int32_t Charged = 0;
    };

    // How charged a shot was, as BeamProjectileEntity::Spawn reads it: 0 is
    // uncharged, 1 full; a partial-charge weapon in between.
    [[nodiscard]] constexpr float ChargePercent(bool canCharge, bool partialCharge, std::int32_t minCharge,
        std::int32_t fullCharge, std::int32_t chargeLevel) noexcept
    {
        if (!canCharge)
        {
            return 0.0F;
        }
        if (partialCharge)
        {
            if (chargeLevel < minCharge * 2 || fullCharge <= minCharge)
            {
                return 0.0F;
            }
            const float percent = static_cast<float>(chargeLevel - minCharge * 2)
                / static_cast<float>(fullCharge * 2 - minCharge * 2);
            return percent > 1.0F ? 1.0F : percent;
        }
        return chargeLevel >= fullCharge * 2 ? 1.0F : 0.0F;
    }

    // The amount at that charge, exactly as the projectile takes it.
    [[nodiscard]] constexpr float AmountAt(ChargeAmounts amounts, float percent) noexcept
    {
        return percent <= 0.0F
            ? static_cast<float>(amounts.Uncharged)
            : static_cast<float>(amounts.MinCharge) + static_cast<float>(amounts.Charged - amounts.MinCharge) * percent;
    }

    // The most it can be at any charge: for a shot whose charge is not known.
    [[nodiscard]] constexpr std::int32_t Largest(ChargeAmounts amounts) noexcept
    {
        const std::int32_t larger = amounts.Uncharged > amounts.MinCharge ? amounts.Uncharged : amounts.MinCharge;
        return larger > amounts.Charged ? larger : amounts.Charged;
    }

    // What multiplies a hit's base damage on its way to the victim.
    struct DamageScale
    {
        // The victim's resistance to the beam (Metadata::DamageMultipliers).
        float Effectiveness = 1.0F;
        // The match's damage level (Metadata::DamageLevels).
        float Level = 1.0F;
        // Double damage the authority granted, running when the shot was fired.
        bool Doubled = false;
        // A Prime Hunter's shot (x1.5), never stacked with double damage.
        bool PrimeHunter = false;
    };

    [[nodiscard]] inline std::int32_t ScaleBound(float base, DamageScale scale) noexcept
    {
        const float power = scale.Doubled ? 2.0F : scale.PrimeHunter ? 1.5F : 1.0F;
        // Every step truncates in the game; one point covers a float that
        // lands a hair over.
        return static_cast<std::int32_t>(std::ceil(base * scale.Effectiveness * power * scale.Level)) + 1;
    }
}
