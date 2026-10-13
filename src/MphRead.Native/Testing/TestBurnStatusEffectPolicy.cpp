#include "Mods/Network/Gameplay/StatusEffects/BurnStatusEffectPolicy.hpp"

#include "Entities/Players/PlayerEntity.hpp"

#include <iostream>

int main()
{
    using MphRead::Entities::DamageFlags;
    using MphRead::Mods::Network::BurnStatusEffectPolicy;

    if (!BurnStatusEffectPolicy::IsAuthorityOnlyDamageTick(DamageFlags::Burn))
    {
        std::cerr << "FAIL burn ticks stay authority-resolved\n";
        return 1;
    }
    if (!BurnStatusEffectPolicy::IsAuthorityOnlyDamageTick(
        DamageFlags::Burn | DamageFlags::NoSfx | DamageFlags::NoDmgInvuln))
    {
        std::cerr << "FAIL burn tick flags retain authority ownership\n";
        return 1;
    }
    if (BurnStatusEffectPolicy::IsAuthorityOnlyDamageTick(DamageFlags::None))
    {
        std::cerr << "FAIL ordinary damage remains eligible for normal resolution\n";
        return 1;
    }
    return 0;
}
