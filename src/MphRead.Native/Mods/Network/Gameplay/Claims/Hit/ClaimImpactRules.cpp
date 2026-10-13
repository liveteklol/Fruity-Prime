#include "Mods/Network/Gameplay/Claims/Hit/ClaimImpactRules.hpp"

namespace MphRead::Mods::Network
{
    bool ClaimImpactRules::Plausible(const ClaimImpactProfile& profile) noexcept
    {
        if (!profile.Known || profile.TurretHit)
        {
            return true;
        }
        const OpenTK::Mathematics::Vector3& at = profile.Offset;
        if (profile.Splash)
        {
            return OpenTK::Mathematics::Length(at) <= 8.0F;
        }
        if (profile.AltForm)
        {
            return OpenTK::Mathematics::Length(at) <= 2.5F;
        }
        constexpr float slack = 0.6F;
        if (at.X * at.X + at.Z * at.Z > (profile.Radius + slack) * (profile.Radius + slack)
            || at.Y < profile.MinY - slack || at.Y > profile.MaxY + slack)
        {
            return false;
        }
        // The headshot band is the top 0.3 of the capsule, with a quarter of
        // that again for the rounding of two machines' collision.
        return !profile.Headshot || at.Y >= profile.MaxY - 0.3F - 0.075F;
    }
}
