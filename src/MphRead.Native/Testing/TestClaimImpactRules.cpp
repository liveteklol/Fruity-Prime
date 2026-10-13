#include "Mods/Network/Gameplay/Claims/Hit/ClaimImpactRules.hpp"

#include <iostream>
#include <string_view>

namespace
{
    bool Check(bool condition, std::string_view name)
    {
        if (!condition)
        {
            std::cerr << "FAIL " << name << '\n';
            return false;
        }
        return true;
    }
}

int main()
{
    using namespace MphRead::Mods::Network;
    using OpenTK::Mathematics::Vector3;

    bool passed = true;
    ClaimImpactProfile profile{};
    passed &= Check(ClaimImpactRules::Plausible(profile), "unknown impact leaves the legacy history check in charge");

    profile.Known = true;
    profile.Offset = Vector3{ 7.9F, 0.0F, 0.0F };
    profile.Splash = true;
    passed &= Check(ClaimImpactRules::Plausible(profile), "splash centre inside blast reach is accepted");
    profile.Offset = Vector3{ 8.1F, 0.0F, 0.0F };
    passed &= Check(!ClaimImpactRules::Plausible(profile), "splash centre outside blast reach is refused");

    profile.Splash = false;
    profile.TurretHit = true;
    passed &= Check(ClaimImpactRules::Plausible(profile), "a live turret claim is checked at the turret ray");
    profile.TurretHit = false;

    profile.AltForm = true;
    profile.Offset = Vector3{ 2.4F, 0.0F, 0.0F };
    passed &= Check(ClaimImpactRules::Plausible(profile), "alt-form reach uses its existing radius");
    profile.Offset = Vector3{ 2.6F, 0.0F, 0.0F };
    passed &= Check(!ClaimImpactRules::Plausible(profile), "alt-form impact beyond its radius is refused");

    profile.AltForm = false;
    profile.Radius = 0.5F;
    profile.MinY = -0.5F;
    profile.MaxY = 1.1F;
    profile.Offset = Vector3{ 1.05F, 1.0F, 0.0F };
    passed &= Check(ClaimImpactRules::Plausible(profile), "biped impact keeps the capsule slack");
    profile.Headshot = true;
    profile.Offset = Vector3{ 0.0F, 0.70F, 0.0F };
    passed &= Check(!ClaimImpactRules::Plausible(profile), "headshot impact must be inside the head band");
    profile.Offset.Y = 0.74F;
    passed &= Check(ClaimImpactRules::Plausible(profile), "headshot impact at the band boundary is accepted");

    return passed ? 0 : 1;
}
