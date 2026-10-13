#include "Mods/Network/Gameplay/Claims/Hit/ClaimDamagePolicy.hpp"
#include "Mods/Network/Protocol/Packets/ClaimPackets.hpp"

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
    bool passed = true;

    ClaimDamageProfile profile{};
    profile.HasWeapon = true;
    profile.Direct = { 6, 12, 36 };
    profile.Splash = { 4, 8, 20 };
    profile.Headshot = { 10, 20, 60 };
    profile.CanCharge = true;
    profile.PartialCharge = true;
    profile.MinCharge = 6;
    profile.FullCharge = 24;
    profile.HasShotCharge = true;
    profile.ShotCharge = 30;

    auto settled = ClaimDamagePolicy::Settle(profile, ClaimDamageKind::Direct, 18, 0, 0, false);
    passed &= Check(settled.Allowed == 25, "known partial charge uses the named shot charge");
    passed &= Check(!settled.Undoubled && !settled.Capped && settled.Damage == 18,
        "an honest claim under its authority bound is unchanged");

    settled = ClaimDamagePolicy::Settle(profile, ClaimDamageKind::Splash, 50, 0, 0, false);
    passed &= Check(settled.Allowed == 15 && settled.Capped && settled.Damage == 15,
        "splash damage uses splash charge values and is capped");

    profile.HasShotCharge = false;
    settled = ClaimDamagePolicy::Settle(profile, ClaimDamageKind::Headshot, 101, 11,
        HitClaimPacket::FlagDoubled, false);
    passed &= Check(settled.Undoubled && (settled.Flags & HitClaimPacket::FlagDoubled) == 0,
        "an ungranted double flag is removed");
    passed &= Check(settled.Damage == 51 && settled.TurretDamage == 6 && settled.Allowed == 61
        && !settled.Capped, "ungranted double damage is rounded per split before the authority bound");

    settled = ClaimDamagePolicy::Settle(profile, ClaimDamageKind::Headshot, 200, 20, 0, true);
    passed &= Check(settled.Allowed == 121 && settled.Capped && settled.Damage == 121
        && settled.TurretDamage == 0, "granted double damage bounds the complete body/turret split");

    ClaimDamageProfile noBeam{};
    settled = ClaimDamagePolicy::Settle(noBeam, ClaimDamageKind::Direct, 201, 0, 0, false);
    passed &= Check(settled.Allowed == 201 && !settled.Capped,
        "non-beam damage keeps the existing 200-point base bound plus rounding point");

    return passed ? 0 : 1;
}
