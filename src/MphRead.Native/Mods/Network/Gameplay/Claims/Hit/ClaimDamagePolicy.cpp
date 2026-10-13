#include "Mods/Network/Gameplay/Claims/Hit/ClaimDamagePolicy.hpp"

#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"

#include <algorithm>

namespace MphRead::Mods::Network
{
    std::int32_t ClaimDamagePolicy::Allowed(const ClaimDamageProfile& profile,
        ClaimDamageKind kind, bool doubled) noexcept
    {
        DamageScale scale{};
        scale.Doubled = doubled;
        scale.Level = profile.Level;
        scale.PrimeHunter = profile.PrimeHunter;
        scale.Effectiveness = profile.Effectiveness;

        if (!profile.HasWeapon)
        {
            // Not a beam: a Death Alt, a ram, a bomb -- the game's 200 at most.
            return ScaleBound(200.0F, scale);
        }

        ChargeAmounts amounts{};
        switch (kind)
        {
        case ClaimDamageKind::Splash:
            amounts = profile.Splash;
            break;
        case ClaimDamageKind::Headshot:
            amounts = profile.Headshot;
            break;
        case ClaimDamageKind::Direct:
        default:
            amounts = profile.Direct;
            break;
        }

        // The shot's own charge, when its event names it; otherwise whatever
        // charge would hit hardest.
        float base = static_cast<float>(Largest(amounts));
        if (profile.HasShotCharge && profile.ShotCharge < 0xFF)
        {
            base = AmountAt(amounts, ChargePercent(profile.CanCharge, profile.PartialCharge,
                profile.MinCharge, profile.FullCharge, profile.ShotCharge));
        }
        return ScaleBound(base, scale);
    }

    ClaimDamageSettlement ClaimDamagePolicy::Settle(const ClaimDamageProfile& profile,
        ClaimDamageKind kind, std::uint16_t damage, std::uint16_t turretDamage,
        std::uint8_t flags, bool doubleDamageGranted) noexcept
    {
        ClaimDamageSettlement result{};
        result.Damage = damage;
        result.TurretDamage = turretDamage;
        result.Flags = flags;

        if ((flags & HitClaimPacket::FlagDoubled) != 0 && !doubleDamageGranted)
        {
            // Keep the hit but remove an ungranted multiplier.
            result.Damage = static_cast<std::uint16_t>((result.Damage + 1U) / 2U);
            result.TurretDamage = static_cast<std::uint16_t>((result.TurretDamage + 1U) / 2U);
            result.Flags = static_cast<std::uint8_t>(result.Flags & ~HitClaimPacket::FlagDoubled);
            result.Undoubled = true;
        }

        // A granted multiplier applies even when a client omitted its flag;
        // double damage and Prime Hunter damage do not stack.
        result.Allowed = Allowed(profile, kind, doubleDamageGranted);
        result.Claimed = static_cast<std::int32_t>(result.Damage) + result.TurretDamage;
        if (result.Claimed <= result.Allowed)
        {
            return result;
        }

        result.Capped = true;
        result.Damage = static_cast<std::uint16_t>(std::min<std::int32_t>(result.Damage, result.Allowed));
        result.TurretDamage = static_cast<std::uint16_t>(std::min<std::int32_t>(result.TurretDamage,
            std::max(0, result.Allowed - result.Damage)));
        return result;
    }
}
