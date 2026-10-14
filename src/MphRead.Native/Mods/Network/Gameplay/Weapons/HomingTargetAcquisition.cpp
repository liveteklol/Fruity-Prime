#include "Mods/Network/Gameplay/Weapons/HomingTargetAcquisition.hpp"

#include "Entities/BeamProjectileEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Formats/Types.hpp"
#include "Metadata/Metadata.hpp"
#include "Mods/Network/Gameplay/Weapons/HomingTargetHistory.hpp"
#include "Mods/Network/Gameplay/Weapons/HomingTargetReference.hpp"
#include "Mods/Network/Gameplay/Weapons/HomingTargetSelection.hpp"
#include "Mods/Network/Gameplay/Weapons/NetShotEvents.hpp"
#include "Mods/Network/Gameplay/Weapons/ShotOwnership.hpp"
#include "Mods/Network/Session/NetSession.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "Scene.hpp"

#include <optional>
#include <vector>

namespace MphRead::Mods::Network
{
    using Entities::BeamFlags;
    using Entities::BeamProjectileEntity;
    using Entities::EntityBase;
    using Entities::PlayerEntity;
    using OpenTK::Mathematics::Vector3;
    using TargetReference = IntentPacket::ShotTargetReference;

    namespace
    {
        // What the shot's own record says about its target, when this copy
        // is not the one that chooses.
        struct TargetDecision final
        {
            bool Received = false;
            bool Remote = false;
            bool Trusted = false;
            std::optional<TargetReference> Target{};
            bool HasRay = false;
            Vector3 RayOrigin{};
            Vector3 RayDirection{};
            std::uint32_t Frame = 0;
        };

        [[nodiscard]] TargetDecision ReadDecision(const BeamProjectileEntity& beam, const PlayerEntity* owner)
        {
            TargetDecision decision{};
            if (!NetSession::Active() || owner == nullptr)
            {
                return decision;
            }
            decision.Remote = !ShotOwnership::IsOwner(*owner);
            decision.Trusted = decision.Remote && !ShotOwnership::Judges();
            if (::MphRead::TestFlag(beam.Flags(), BeamFlags::Continuous))
            {
                if (decision.Remote && NetSession::HasRemoteIntent(owner->SlotIndex()))
                {
                    const IntentPacket& intent = NetSession::RemoteIntent(static_cast<std::size_t>(owner->SlotIndex()));
                    decision.Received = true;
                    decision.Frame = intent.AckFrame;
                    decision.HasRay = intent.HasShot;
                    decision.RayOrigin = intent.ShotOrigin;
                    decision.RayDirection = intent.ShotDirection;
                    if (intent.HasContinuousTarget)
                    {
                        decision.Target = intent.ContinuousTarget;
                    }
                }
            }
            else if (const auto event = NetShotEvents::CurrentShotEvent(*owner); event.has_value())
            {
                decision.Received = decision.Remote || event->HasHomingDecision();
                decision.Frame = event->AckFrame;
                decision.HasRay = true;
                decision.RayOrigin = event->Origin;
                decision.RayDirection = event->Direction;
                if (event->HasHomingDecision())
                {
                    decision.Target = event->Target;
                }
            }
            return decision;
        }

        // The authority's check of a remote shooter's choice, against where
        // shooter and target were in the frame the shot was fired from.
        [[nodiscard]] bool ValidAtFire(const TargetDecision& decision, const PlayerEntity& owner,
            HomingTargetSelectionRequest request)
        {
            const auto ownerAtFire = HomingTargetHistory::FindPlayer(decision.Frame, owner.SlotIndex());
            const auto targetAtFire = HomingTargetHistory::Find(decision.Frame, *decision.Target);
            if (!ownerAtFire.has_value() || !ownerAtFire->Targetable || !targetAtFire.has_value())
            {
                return false;
            }
            request.ExcludedTarget = {};
            request.OwnerTeam = ownerAtFire->TeamIndex;
            if (!decision.HasRay)
            {
                return HomingTargetSelection::IsEligible(*targetAtFire, request);
            }
            request.Origin = decision.RayOrigin;
            request.Direction = decision.RayDirection;
            return HomingTargetSelection::CanAcquire(*targetAtFire, request);
        }

        void RecordChoice(const BeamProjectileEntity& beam, const PlayerEntity& owner, const TargetReference& selected)
        {
            if (::MphRead::TestFlag(beam.Flags(), BeamFlags::Continuous))
            {
                NetShotEvents::RecordContinuousTarget(owner.SlotIndex(), selected);
            }
            else if (NetShotEvents::CurrentShotEvent(owner).has_value())
            {
                NetShotEvents::RecordHomingTarget(owner, selected);
            }
        }
    }

    std::string ToString(HomingTargetAcquisition::Outcome value)
    {
        using Outcome = HomingTargetAcquisition::Outcome;
        switch (value)
        {
        case Outcome::NotAttempted: return "-";
        case Outcome::Chosen: return "chosen";
        case Outcome::NoneInCone: return "none";
        case Outcome::Received: return "received";
        case Outcome::ReceivedNone: return "received-none";
        case Outcome::ReceivedIneligible: return "received-ineligible";
        case Outcome::Refused: return "refused";
        }
        return std::to_string(static_cast<std::int32_t>(value));
    }

    HomingTargetAcquisition::Outcome HomingTargetAcquisition::ConsumeOutcome() noexcept
    {
        const Outcome outcome = _outcome;
        _outcome = Outcome::NotAttempted;
        return outcome;
    }

    bool HomingTargetAcquisition::Finish(Outcome outcome, bool result) noexcept
    {
        _outcome = outcome;
        return result;
    }

    bool HomingTargetAcquisition::Acquire(BeamProjectileEntity& beam, const EquipInfo& equip, Scene& scene)
    {
        const std::shared_ptr<EntityBase> owner = beam.Owner();
        const PlayerEntity* ownerPlayer = owner != nullptr && owner->Type == EntityType::Player
            ? static_cast<const PlayerEntity*>(owner.get()) : nullptr;
        const bool continuous = ::MphRead::TestFlag(beam.Flags(), BeamFlags::Continuous);
        const TargetDecision decision = ReadDecision(beam, ownerPlayer);
        if (decision.Received && (!decision.Target.has_value()
            || decision.Target->Kind == IntentPacket::ShotTargetKind::None))
        {
            return Finish(Outcome::ReceivedNone, false);
        }

        const WeaponInfo& weapon = NativeRuntime::RequireReference(equip.Weapon);
        HomingTargetSelectionRequest request{};
        request.ExcludedTarget = HomingTargetReference::Encode(owner.get());
        request.Origin = static_cast<Vector3>(beam.Position);
        request.Direction = beam.Velocity();
        request.OwnerType = NativeRuntime::RequireReference(owner).Type;
        request.Beam = beam.BeamKind();
        request.OwnerSlot = ownerPlayer != nullptr ? ownerPlayer->SlotIndex() : -1;
        request.OwnerTeam = ownerPlayer != nullptr ? ownerPlayer->TeamIndex() : -1;
        request.Range = Fixed::ToFloat(weapon.HomingRange);
        request.Tolerance = Fixed::ToFloat(equip.HomingTolerance());
        request.Continuous = ::MphRead::TestFlag(weapon.Flags, WeaponFlags::Continuous);

        bool validated = false;
        if (decision.Remote && decision.Target.has_value() && ShotOwnership::Judges())
        {
            if (!ValidAtFire(decision, *ownerPlayer, request))
            {
                return Finish(Outcome::Refused, false);
            }
            validated = true;
        }

        std::vector<HomingTargetSnapshot> snapshots{};
        std::vector<std::shared_ptr<EntityBase>> entities{};
        for (const EntityType type : HomingTargetHistory::TargetTypes)
        {
            auto enumerator = scene.Entities().GetEnumerator();
            while (enumerator.MoveNext())
            {
                const std::shared_ptr<EntityBase> entity = enumerator.Current();
                if (!entity || entity->Type != type)
                {
                    continue;
                }
                const HomingTargetSnapshot snapshot = HomingTargetHistory::Capture(*entity);
                if (!snapshot.Reference.IsValid() || (decision.Received && snapshot.Reference != *decision.Target))
                {
                    continue;
                }
                if (decision.Received && (decision.Trusted || validated))
                {
                    // Somebody else chose: take that entity as it stands now.
                    if (HomingTargetSelection::IsEligible(snapshot, request))
                    {
                        beam.SetTarget(entity);
                        return Finish(Outcome::Received, continuous);
                    }
                    continue;
                }
                snapshots.push_back(snapshot);
                entities.push_back(entity);
            }
        }

        if (decision.Received && (decision.Trusted || validated))
        {
            return Finish(Outcome::ReceivedIneligible, false);
        }
        const TargetReference selected = HomingTargetSelection::Select(snapshots, request);
        bool acquired = false;
        for (std::size_t i = 0; selected.IsValid() && i < snapshots.size(); ++i)
        {
            if (snapshots[i].Reference == selected)
            {
                beam.SetTarget(entities[i]);
                acquired = true;
                break;
            }
        }
        if (!decision.Received && ownerPlayer != nullptr)
        {
            RecordChoice(beam, *ownerPlayer, selected);
        }
        return Finish(acquired ? Outcome::Chosen : decision.Received ? Outcome::ReceivedIneligible
            : Outcome::NoneInCone, acquired && continuous);
    }
}
