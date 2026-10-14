#include "Mods/Network/Gameplay/Weapons/FiringVariant.hpp"

#include "Entities/Players/PlayerEntity.hpp"
#include "GameState.hpp"
#include "Metadata/Weapons.hpp"
#include "Mods/Combat/WeaponVariant.hpp"
#include "Mods/Network/Gameplay/Combat/NetUnlagged.hpp"
#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"
#include "Mods/Network/Gameplay/Weapons/ShotOwnership.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "NativeRuntime/System/Managed.hpp"

#include <optional>

namespace MphRead::Mods::Network
{
    namespace
    {
        std::uint32_t ReceivedRoleFrame = 0;
        std::int32_t ReceivedPrimeSlot = -1;
        // A copy of somebody else's continuous tick: the role the owner
        // declared with it, which the authority checks against who held
        // the role in the frame the owner was looking at.
        [[nodiscard]] std::optional<bool> ContinuousPrimeOfCopy(const Entities::PlayerEntity& shooter)
        {
            const std::int32_t slot = shooter.SlotIndex();
            const auto& weapon = NativeRuntime::RequireReference(shooter.EquipInfo()).Weapon;
            if (ShotOwnership::IsOwner(shooter) || weapon == nullptr
                || !::MphRead::TestFlag(weapon->Flags, WeaponFlags::Continuous)
                || !NetSession::HasRemoteIntent(slot))
            {
                return std::nullopt;
            }
            const IntentPacket& intent = NetSession::RemoteIntent(static_cast<std::size_t>(slot));
            if (!intent.HasContinuousFire)
            {
                return std::nullopt;
            }
            if (ShotOwnership::Judges())
            {
                if (const std::optional<std::int32_t> held = NetUnlagged::PrimeHunterAt(intent.ContinuousRoleFrame))
                {
                    return *held == slot;
                }
                return false;
            }
            return intent.ContinuousPrimeHunter;
        }
    }

    void FiringVariant::ReceiveRole(std::uint32_t frame, std::int32_t slot) noexcept
    {
        ReceivedRoleFrame = frame;
        ReceivedPrimeSlot = slot;
        GameState::PrimeHunter(slot);
    }

    void FiringVariant::ResetRole() noexcept
    {
        ReceivedRoleFrame = 0;
        ReceivedPrimeSlot = -1;
    }

    std::uint32_t FiringVariant::RoleFrame() noexcept
    {
        return ShotOwnership::Judges() ? NetSession::NetFrame() : ReceivedRoleFrame;
    }

    FiringVariant FiringVariant::For(const Entities::PlayerEntity& shooter)
    {
        const std::optional<IntentPacket::ShotEvent> event = NetShotEvents::FiringEvent(shooter);
        bool prime = event.has_value() ? event->PrimeHunterAtFire()
            : ContinuousPrimeOfCopy(shooter).value_or(
                NetSession::Active() && !ShotOwnership::Judges()
                    ? ReceivedPrimeSlot == shooter.SlotIndex() : shooter.IsPrimeHunter());
        if (event.has_value() && event->WeaponVariantIndex != IntentPacket::NoVariant)
        {
            if (!ShotOwnership::Judges() || ShotOwnership::IsOwner(shooter))
            {
                return FiringVariant{event->WeaponVariantIndex, prime};
            }
            const auto held = NetUnlagged::PrimeHunterAt(
                event->RoleFrame != 0 ? event->RoleFrame : event->AckFrame);
            prime = held.has_value() && *held == shooter.SlotIndex();
        }
        const Combat::WeaponVariantSelection resolved = Combat::ResolveWeaponVariantAtFire(
            shooter.CurrentWeapon(), shooter.Hunter(), Weapons::GetAffinityBeam(shooter.Hunter()),
            GameState::SinglePlayer(), prime);
        return FiringVariant{resolved.Index, prime};
    }
}
