#include "Mods/Network/Gameplay/Weapons/HomingTargetReference.hpp"

#include "Entities/EntityBase.hpp"
#include "Entities/DoorEntity.hpp"
#include "Entities/EnemyInstanceEntity.hpp"
#include "Entities/PlatformEntity.hpp"
#include "Entities/Players/HalfturretEntity.hpp"
#include "Entities/Players/PlayerEntity.hpp"
#include "Mods/Network/Gameplay/Player/NetPlayerLifecycle.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "Scene.hpp"

namespace MphRead::Mods::Network
{
    namespace
    {
        using TargetKind = IntentPacket::ShotTargetKind;
        using TargetReference = IntentPacket::ShotTargetReference;

        [[nodiscard]] TargetReference PlayerReference(const Entities::PlayerEntity& player, TargetKind kind) noexcept
        {
            const std::int32_t slot = player.SlotIndex();
            if (slot < 0 || slot >= Entities::PlayerEntity::SlotCapacity)
            {
                return {};
            }
            return TargetReference{ kind, 0xFF, static_cast<std::uint8_t>(slot),
                NetPlayerLifecycle::Generation(slot), NetPlayerLifecycle::Get(slot), -1, 0 };
        }

        [[nodiscard]] bool PlayerIdentityMatches(const TargetReference& reference) noexcept
        {
            return reference.Slot < Entities::PlayerEntity::SlotCapacity
                && NetPlayerLifecycle::Matches(reference.Slot, reference.Generation, reference.LifeId);
        }
    }

    IntentPacket::ShotTargetReference HomingTargetReference::Encode(
        const Entities::EntityBase* target) noexcept
    {
        if (target == nullptr)
        {
            return {};
        }
        switch (target->Type)
        {
        case EntityType::Player:
            return PlayerReference(*static_cast<const Entities::PlayerEntity*>(target), TargetKind::Player);
        case EntityType::Halfturret:
        {
            const std::shared_ptr<Entities::PlayerEntity> owner
                = static_cast<const Entities::HalfturretEntity*>(target)->Owner();
            return owner ? PlayerReference(*owner, TargetKind::Halfturret) : TargetReference{};
        }
        case EntityType::EnemyInstance:
        {
            const std::uint64_t stableId
                = static_cast<const Entities::EnemyInstanceEntity*>(target)->ModStableTargetId();
            return stableId == 0 ? TargetReference{} : TargetReference{
                TargetKind::EnemyInstance, 0xFF, 0xFF, 0, 0, -1, stableId };
        }
        case EntityType::Door:
        case EntityType::Platform:
            return target->Id < 0 ? TargetReference{} : TargetReference{
                TargetKind::SceneEntity, static_cast<std::uint8_t>(target->Type), 0xFF, 0, 0, target->Id, 0 };
        default:
            return {};
        }
    }

    std::shared_ptr<Entities::EntityBase> HomingTargetReference::Resolve(
        const IntentPacket::ShotTargetReference& reference, Scene* scene)
    {
        switch (reference.Kind)
        {
        case TargetKind::Player:
        case TargetKind::Halfturret:
        {
            if (!PlayerIdentityMatches(reference))
            {
                return {};
            }
            const std::shared_ptr<Entities::PlayerEntity> player
                = NativeRuntime::ManagedAt(Entities::PlayerEntity::Players(), reference.Slot);
            if (reference.Kind == TargetKind::Player)
            {
                return player;
            }
            return player ? player->Halfturret() : std::shared_ptr<Entities::EntityBase>{};
        }
        case TargetKind::SceneEntity:
        {
            if (scene == nullptr || reference.EntityId < 0)
            {
                return {};
            }
            std::shared_ptr<Entities::EntityBase> entity{};
            if (!scene->TryGetEntity(reference.EntityId, entity) || !entity
                || static_cast<std::uint8_t>(entity->Type) != reference.EntityType
                || (entity->Type != EntityType::Door && entity->Type != EntityType::Platform))
            {
                return {};
            }
            return entity;
        }
        case TargetKind::EnemyInstance:
        {
            if (scene == nullptr || reference.StableId == 0)
            {
                return {};
            }
            auto enemies = scene->GetEnemyInstanceEntities().GetEnumerator();
            while (enemies.MoveNext())
            {
                const std::shared_ptr<Entities::EnemyInstanceEntity> enemy = enemies.Current();
                if (enemy && enemy->ModStableTargetId() == reference.StableId)
                {
                    return enemy;
                }
            }
            return {};
        }
        default:
            return {};
        }
    }
}
