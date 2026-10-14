#pragma once

#include "Formats/Enums.hpp"

#include <cstdint>

namespace MphRead::Mods::Combat
{
    static constexpr std::int32_t BaseWeaponVariantCount = 9;
    static constexpr std::int32_t WeaponVariantCount = BaseWeaponVariantCount * 2;

    struct WeaponVariantSelection final
    {
        std::uint8_t Index = 0xFF;
        bool PrimeHunter = false;
    };

    [[nodiscard]] constexpr WeaponVariantSelection ResolveWeaponVariantAtFire(
        BeamType beam, Hunter hunter, BeamType affinityBeam, bool singlePlayer, bool primeHolder) noexcept
    {
        const std::int32_t beamIndex = static_cast<std::int32_t>(beam);
        if (beamIndex < 0 || beamIndex >= BaseWeaponVariantCount)
        {
            return {};
        }
        const bool prime = primeHolder;
        const bool affinity = beam == affinityBeam
            || (singlePlayer
                && ((hunter == Hunter::Samus
                        && (beam == BeamType::PowerBeam || beam == BeamType::OmegaCannon))
                    || (hunter == Hunter::Guardian && beam == BeamType::VoltDriver)));
        return WeaponVariantSelection{
            static_cast<std::uint8_t>(beamIndex + ((prime || affinity) ? 9 : 0)), prime };
    }

    // The row a weapon is equipped with: what it charges and costs while held.
    // The Prime Hunter role is not part of it -- that is decided per shot, as
    // it leaves the gun (ResolveWeaponVariantAtFire).
    [[nodiscard]] constexpr WeaponVariantSelection ResolveEquippedWeaponVariant(
        BeamType beam, Hunter hunter, BeamType affinityBeam, bool singlePlayer) noexcept
    {
        return ResolveWeaponVariantAtFire(beam, hunter, affinityBeam, singlePlayer, false);
    }
}
