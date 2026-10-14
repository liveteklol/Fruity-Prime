#include "Mods/Network/Gameplay/Weapons/ShotOwnership.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Session/NetSession.hpp"

namespace MphRead::Mods::Network
{
    bool ShotOwnership::IsOwner(const Entities::PlayerEntity& shooter) noexcept
    {
        if (!NetSession::Active())
        {
            return true;
        }
        return shooter.IsBot() ? NetSession::IsAuthority() : shooter.SlotIndex() == NetSession::LocalSlot();
    }

    bool ShotOwnership::Judges() noexcept
    {
        return NetSession::IsAuthority();
    }
}
