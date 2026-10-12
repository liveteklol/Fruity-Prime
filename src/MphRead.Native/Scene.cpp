#include "Scene.hpp"

#include "NativeRuntime/System/Runtime.hpp"
#include "Formats/Entity.hpp"
#include "Formats/Formats.hpp"
#include "GameState.hpp"
#include "Metadata/Metadata.hpp"
#include "Read.hpp"
#include "Strings.hpp"
#include "NativeRuntime/System/IO.hpp"
#include "NativeRuntime/System/Managed.hpp"
#include "NativeRuntime/OpenTK/Mathematics.hpp"
#include "Formats/Types.hpp"

#include <bit>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ManagedCast;
using ::MphRead::NativeRuntime::ShiftRight;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::OpenTK::Mathematics::Multiply;
using ::OpenTK::Mathematics::WithY;

namespace
{
    using OpenTK::Mathematics::Vector3;

    [[nodiscard]] std::string ToUpperInvariant(std::string value)
    {
        for (char& ch : value)
        {
            const unsigned char byte = static_cast<unsigned char>(ch);
            if (byte >= static_cast<unsigned char>('a')
                && byte <= static_cast<unsigned char>('z'))
            {
                ch = static_cast<char>(byte - static_cast<unsigned char>('a')
                    + static_cast<unsigned char>('A'));
            }
        }
        return value;
    }


}

namespace MphRead
{
    NavMapEntitySymbol::NavMapEntitySymbol(
        EntityType type,
        std::int16_t id,
        std::int32_t subType,
        bool locked,
        Vector3 position,
        Vector3 upVector,
        Vector3 facingVector) noexcept
        : Type(type),
          Id(id),
          SubType(subType),
          Locked(locked),
          Position(position),
          UpVector(upVector),
          FacingVector(facingVector)
    {
    }

    NavMapRoomSymbols::NavMapRoomSymbols(
        std::string name,
        std::int32_t id,
        std::shared_ptr<std::vector<std::shared_ptr<NavMapEntitySymbol>>> symbols)
        : Name(std::move(name)),
          Id(id),
          Symbols(ImmutableArray<NavMapEntitySymbol>::AsImmutableArray(std::move(symbols)))
    {
    }

    Scene::EntityNodeMap Scene::MakeEntityNodeMap()
    {
        EntityNodeMap result;
        result.Add(EntityType::Platform, nullptr);
        result.Add(EntityType::Object, nullptr);
        result.Add(EntityType::Q3Mover, nullptr);
        result.Add(EntityType::PlayerSpawn, nullptr);
        result.Add(EntityType::Door, nullptr);
        result.Add(EntityType::ItemSpawn, nullptr);
        result.Add(EntityType::ItemInstance, nullptr);
        result.Add(EntityType::EnemySpawn, nullptr);
        result.Add(EntityType::TriggerVolume, nullptr);
        result.Add(EntityType::AreaVolume, nullptr);
        result.Add(EntityType::JumpPad, nullptr);
        result.Add(EntityType::PointModule, nullptr);
        result.Add(EntityType::MorphCamera, nullptr);
        result.Add(EntityType::OctolithFlag, nullptr);
        result.Add(EntityType::FlagBase, nullptr);
        result.Add(EntityType::Teleporter, nullptr);
        result.Add(EntityType::NodeDefense, nullptr);
        result.Add(EntityType::LightSource, nullptr);
        result.Add(EntityType::Artifact, nullptr);
        result.Add(EntityType::CameraSequence, nullptr);
        result.Add(EntityType::ForceField, nullptr);
        result.Add(EntityType::BeamEffect, nullptr);
        result.Add(EntityType::Bomb, nullptr);
        result.Add(EntityType::EnemyInstance, nullptr);
        result.Add(EntityType::Halfturret, nullptr);
        result.Add(EntityType::Player, nullptr);
        result.Add(EntityType::BeamProjectile, nullptr);
        result.Add(EntityType::FhUnknown0, nullptr);
        result.Add(EntityType::FhPlayerSpawn, nullptr);
        result.Add(EntityType::FhUnknown2, nullptr);
        result.Add(EntityType::FhDoor, nullptr);
        result.Add(EntityType::FhItemSpawn, nullptr);
        result.Add(EntityType::FhItemInstance, nullptr);
        result.Add(EntityType::FhEnemySpawn, nullptr);
        result.Add(EntityType::FhEffectInstance, nullptr);
        result.Add(EntityType::FhBomb, nullptr);
        result.Add(EntityType::FhTriggerVolume, nullptr);
        result.Add(EntityType::FhAreaVolume, nullptr);
        result.Add(EntityType::FhPlatform, nullptr);
        result.Add(EntityType::FhJumpPad, nullptr);
        result.Add(EntityType::FhPointModule, nullptr);
        result.Add(EntityType::FhMorphCamera, nullptr);
        result.Add(EntityType::FhEnemyInstance, nullptr);
        result.Add(EntityType::FhPlayer, nullptr);
        result.Add(EntityType::FhBeamProjectile, nullptr);
        return result;
    }

    LinkedListIterator<Entities::EntityBase> Scene::Entities() noexcept
    {
        return LinkedListIterator<Entities::EntityBase>(_entities);
    }

    void Scene::AddEntity(std::shared_ptr<Entities::EntityBase> entity)
    {
        InsertEntity(entity);
        InitializeEntity(entity);
    }

    void Scene::InsertEntity(std::shared_ptr<Entities::EntityBase> entity)
    {
        InsertEntityByType(entity);
        if (entity->Id != -1)
        {
            // The key is read before the entity is moved into the value
            // parameter: argument evaluation order is unspecified, and GCC
            // on x64 Windows builds the second argument first.
            const std::int32_t id = entity->Id;
            _entityMap.Add(id, std::move(entity));
        }
    }

    void Scene::InsertEntityByType(const std::shared_ptr<Entities::EntityBase>& entity)
    {
        if (!entity)
        {
            throw System::NullReferenceException();
        }

        bool isFirstOfType = true;
        for (EntityNodePtr item = _entities->First(); item; item = item->Next())
        {
            const std::shared_ptr<Entities::EntityBase>& existing = item->Value();
            if (!existing)
            {
                throw System::NullReferenceException();
            }
            if (existing->Type == entity->Type)
            {
                isFirstOfType = false;
            }
            if (existing->Type != EntityType::Room && existing->Type > entity->Type)
            {
                EntityNodePtr newNode = _entities->AddBefore(item, entity);
                if (isFirstOfType)
                {
                    _entityNodesByType.Set(entity->Type, newNode);
                }
                return;
            }
        }

        EntityNodePtr lastNode = _entities->AddLast(entity);
        if (isFirstOfType)
        {
            _entityNodesByType.Set(entity->Type, lastNode);
        }
    }

    void Scene::InitializeEntity(const std::shared_ptr<Entities::EntityBase>& entity)
    {
        if (!entity)
        {
            throw System::NullReferenceException();
        }
        entity->Initialize();
        InitEntity(entity);
    }

    bool Scene::TryGetEntity(
        std::int32_t id,
        std::shared_ptr<Entities::EntityBase>& entity) const
    {
        return _entityMap.TryGetValue(id, entity);
    }

    void Scene::RemoveEntity(const std::shared_ptr<Entities::EntityBase>& entity)
    {
        if (!entity)
        {
            throw System::NullReferenceException();
        }

        (void)_entityMap.Remove(entity->Id);

        EntityNodePtr node;
        if (_entityNodesByType.TryGetValue(entity->Type, node))
        {
            while (node && node->Value() != entity)
            {
                node = node->Next();
            }
        }
        else
        {
            node = _entities->Find(entity);
        }

        if (node)
        {
            if (_entityNodesByType.At(entity->Type) == node)
            {
                const EntityNodePtr& next = node->Next();
                if (next)
                {
                    const std::shared_ptr<Entities::EntityBase>& nextEntity = next->Value();
                    if (!nextEntity)
                    {
                        throw System::NullReferenceException();
                    }
                    _entityNodesByType.Set(
                        entity->Type,
                        nextEntity->Type == entity->Type ? next : nullptr);
                }
                else
                {
                    _entityNodesByType.Set(entity->Type, nullptr);
                }
            }
            _entities->Remove(node);
        }
    }

    void Scene::RemoveEntityFromMap(const std::shared_ptr<Entities::EntityBase>& entity)
    {
        if (!entity)
        {
            throw System::NullReferenceException();
        }
        (void)_entityMap.Remove(entity->Id);
    }

    std::optional<ImmutableArray<::MphRead::NavMapRoomSymbols>>
        Scene::NavMapRoomSymbols() const noexcept
    {
        return _navMapRoomSymbols;
    }

    void Scene::LoadMapSymbolEntities(std::int32_t areaId)
    {
        if (_navMapRoomSymbols.has_value() || areaId > 7)
        {
            return;
        }

        if (!GameState::StorySave)
        {
            throw System::NullReferenceException();
        }
        const BossFlags bossFlags = GameState::StorySave->BossFlags;
        const std::int32_t shiftCount = UncheckedMultiply(2, areaId);
        const std::int32_t layerId
            = ShiftRight(static_cast<std::int32_t>(bossFlags), shiftCount) & 3;

        std::vector<std::shared_ptr<::MphRead::NavMapRoomSymbols>> rooms;
        for (std::int32_t i = 1; i <= 35; ++i)
        {
            const std::int32_t id = UncheckedAdd(UncheckedMultiply(areaId / 2, 100), i);
            std::shared_ptr<StringTableEntry> roomEntry
                = Text::Strings::GetEntry('R', id, Text::StringTables::LocationNames);
            if (!roomEntry)
            {
                break;
            }

            if (!roomEntry->String1.starts_with("Con"))
            {
                auto [meta, roomId] = Metadata::GetRoomByName(
                    ToUpperInvariant(roomEntry->String1));
                (void)roomId;
                if (meta == nullptr || !meta->EntityPath.has_value())
                {
                    ::MphRead::NativeRuntime::DebuggerBreak();
                    continue;
                }

                const auto entities = Read::GetEntities(
                    *meta->EntityPath, layerId, false, false);
                if (!entities)
                {
                    throw System::NullReferenceException();
                }

                std::int32_t count = 0;
                for (const auto& entity : *entities)
                {
                    if (!entity)
                    {
                        throw System::NullReferenceException();
                    }
                    if (entity->Type == EntityType::Door
                        || entity->Type == EntityType::Teleporter)
                    {
                        ++count;
                    }
                }
                if (count == 0)
                {
                    continue;
                }

                auto symbols = std::make_shared<
                    std::vector<std::shared_ptr<NavMapEntitySymbol>>>(
                        static_cast<std::size_t>(count));

                count = 0;
                for (const auto& entity : *entities)
                {
                    if (!entity)
                    {
                        throw System::NullReferenceException();
                    }
                    if (entity->Type == EntityType::Door)
                    {
                        const auto door = ManagedCast<EntityOf<DoorEntityData>>(entity);
                        const DoorEntityData& data = door->Data;
                        float heightFactor = 1.0F;
                        if (meta->Name == "UNIT2_C2")
                        {
                            heightFactor = 2.05F;
                        }
                        else if (meta->Name == "UNIT2_C3")
                        {
                            heightFactor = 1.305F;
                        }

                        const std::int32_t doorIndex = std::bit_cast<std::int32_t>(
                            static_cast<std::uint32_t>(data.DoorType));
                        if (doorIndex < 0
                            || static_cast<std::size_t>(doorIndex) >= Metadata::Doors.size())
                        {
                            throw SceneDetail::IndexOutOfRangeException();
                        }
                        const auto& doorMeta = Metadata::Doors[static_cast<std::size_t>(doorIndex)];
                        Vector3 lockPos = entity->Position
                            + Multiply(entity->UpVector, doorMeta.LockOffset);
                        lockPos = WithY(lockPos, lockPos.Y * heightFactor);
                        if (count < 0
                            || static_cast<std::size_t>(count) >= symbols->size())
                        {
                            throw SceneDetail::IndexOutOfRangeException();
                        }
                        (*symbols)[static_cast<std::size_t>(count++)]
                            = std::make_shared<NavMapEntitySymbol>(
                                EntityType::Door,
                                entity->EntityId,
                                std::bit_cast<std::int32_t>(data.PaletteId),
                                data.Locked != 0,
                                lockPos,
                                entity->UpVector,
                                entity->FacingVector);
                    }
                    else if (entity->Type == EntityType::Teleporter)
                    {
                        const auto teleporter
                            = ManagedCast<EntityOf<TeleporterEntityData>>(entity);
                        const TeleporterEntityData& data = teleporter->Data;
                        const std::int32_t type
                            = data.ArtifactId < 8 && data.Invisible == 0 ? 1 : 0;
                        if (count < 0
                            || static_cast<std::size_t>(count) >= symbols->size())
                        {
                            throw SceneDetail::IndexOutOfRangeException();
                        }
                        (*symbols)[static_cast<std::size_t>(count++)]
                            = std::make_shared<NavMapEntitySymbol>(
                                EntityType::Teleporter,
                                entity->EntityId,
                                type,
                                false,
                                entity->Position,
                                entity->UpVector,
                                entity->FacingVector);
                    }
                }

                rooms.push_back(std::make_shared<::MphRead::NavMapRoomSymbols>(
                    meta->Name, meta->Id, std::move(symbols)));
            }
        }

        if (!rooms.empty())
        {
            _navMapRoomSymbols
                = ImmutableArray<::MphRead::NavMapRoomSymbols>::ToImmutableArray(rooms);
        }
    }

    LinkedListIteratorSpecialized<Entities::PlatformEntity> Scene::GetPlatformEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::PlatformEntity>(
            _entityNodesByType.At(EntityType::Platform));
    }

    LinkedListIteratorSpecialized<Entities::ObjectEntity> Scene::GetObjectEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::ObjectEntity>(
            _entityNodesByType.At(EntityType::Object));
    }

    LinkedListIteratorSpecialized<Entities::PlayerSpawnEntity> Scene::GetPlayerSpawnEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::PlayerSpawnEntity>(
            _entityNodesByType.At(EntityType::PlayerSpawn));
    }

    LinkedListIteratorSpecialized<Entities::DoorEntity> Scene::GetDoorEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::DoorEntity>(
            _entityNodesByType.At(EntityType::Door));
    }

    LinkedListIteratorSpecialized<Entities::ItemSpawnEntity> Scene::GetItemSpawnEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::ItemSpawnEntity>(
            _entityNodesByType.At(EntityType::ItemSpawn));
    }

    LinkedListIteratorSpecialized<Entities::ItemInstanceEntity> Scene::GetItemInstanceEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::ItemInstanceEntity>(
            _entityNodesByType.At(EntityType::ItemInstance));
    }

    LinkedListIteratorSpecialized<Entities::EnemySpawnEntity> Scene::GetEnemySpawnEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::EnemySpawnEntity>(
            _entityNodesByType.At(EntityType::EnemySpawn));
    }

    LinkedListIteratorSpecialized<Entities::TriggerVolumeEntity> Scene::GetTriggerVolumeEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::TriggerVolumeEntity>(
            _entityNodesByType.At(EntityType::TriggerVolume));
    }

    LinkedListIteratorSpecialized<Entities::AreaVolumeEntity> Scene::GetAreaVolumeEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::AreaVolumeEntity>(
            _entityNodesByType.At(EntityType::AreaVolume));
    }

    LinkedListIteratorSpecialized<Entities::JumpPadEntity> Scene::GetJumpPadEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::JumpPadEntity>(
            _entityNodesByType.At(EntityType::JumpPad));
    }

    LinkedListIteratorSpecialized<Entities::PointModuleEntity> Scene::GetPointModuleEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::PointModuleEntity>(
            _entityNodesByType.At(EntityType::PointModule));
    }

    LinkedListIteratorSpecialized<Entities::MorphCameraEntity> Scene::GetMorphCameraEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::MorphCameraEntity>(
            _entityNodesByType.At(EntityType::MorphCamera));
    }

    LinkedListIteratorSpecialized<Entities::OctolithFlagEntity> Scene::GetOctolithFlagEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::OctolithFlagEntity>(
            _entityNodesByType.At(EntityType::OctolithFlag));
    }

    LinkedListIteratorSpecialized<Entities::FlagBaseEntity> Scene::GetFlagBaseEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::FlagBaseEntity>(
            _entityNodesByType.At(EntityType::FlagBase));
    }

    LinkedListIteratorSpecialized<Entities::TeleporterEntity> Scene::GetTeleporterEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::TeleporterEntity>(
            _entityNodesByType.At(EntityType::Teleporter));
    }

    LinkedListIteratorSpecialized<Entities::NodeDefenseEntity> Scene::GetNodeDefenseEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::NodeDefenseEntity>(
            _entityNodesByType.At(EntityType::NodeDefense));
    }

    LinkedListIteratorSpecialized<Entities::LightSourceEntity> Scene::GetLightSourceEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::LightSourceEntity>(
            _entityNodesByType.At(EntityType::LightSource));
    }

    LinkedListIteratorSpecialized<Entities::ArtifactEntity> Scene::GetArtifactEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::ArtifactEntity>(
            _entityNodesByType.At(EntityType::Artifact));
    }

    LinkedListIteratorSpecialized<Entities::CamSeqEntity> Scene::GetCamSeqEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::CamSeqEntity>(
            _entityNodesByType.At(EntityType::CameraSequence));
    }

    LinkedListIteratorSpecialized<Entities::ForceFieldEntity> Scene::GetForceFieldEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::ForceFieldEntity>(
            _entityNodesByType.At(EntityType::ForceField));
    }

    LinkedListIteratorSpecialized<Entities::BeamEffectEntity> Scene::GetBeamEffectEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::BeamEffectEntity>(
            _entityNodesByType.At(EntityType::BeamEffect));
    }

    LinkedListIteratorSpecialized<Entities::BombEntity> Scene::GetBombEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::BombEntity>(
            _entityNodesByType.At(EntityType::Bomb));
    }

    LinkedListIteratorSpecialized<Entities::EnemyInstanceEntity> Scene::GetEnemyInstanceEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::EnemyInstanceEntity>(
            _entityNodesByType.At(EntityType::EnemyInstance));
    }

    LinkedListIteratorSpecialized<Entities::HalfturretEntity> Scene::GetHalfturretEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::HalfturretEntity>(
            _entityNodesByType.At(EntityType::Halfturret));
    }

    LinkedListIteratorSpecialized<Entities::PlayerEntity> Scene::GetPlayerEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::PlayerEntity>(
            _entityNodesByType.At(EntityType::Player));
    }

    LinkedListIteratorSpecialized<Entities::BeamProjectileEntity> Scene::GetBeamProjectileEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::BeamProjectileEntity>(
            _entityNodesByType.At(EntityType::BeamProjectile));
    }

    LinkedListIteratorSpecialized<Entities::FhDoorEntity> Scene::GetFhDoorEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::FhDoorEntity>(
            _entityNodesByType.At(EntityType::FhDoor));
    }

    LinkedListIteratorSpecialized<Entities::FhItemSpawnEntity> Scene::GetFhItemSpawnEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::FhItemSpawnEntity>(
            _entityNodesByType.At(EntityType::FhItemSpawn));
    }

    LinkedListIteratorSpecialized<Entities::FhEnemySpawnEntity> Scene::GetFhEnemySpawnEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::FhEnemySpawnEntity>(
            _entityNodesByType.At(EntityType::FhEnemySpawn));
    }

    LinkedListIteratorSpecialized<Entities::FhTriggerVolumeEntity> Scene::GetFhTriggerVolumeEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::FhTriggerVolumeEntity>(
            _entityNodesByType.At(EntityType::FhTriggerVolume));
    }

    LinkedListIteratorSpecialized<Entities::FhAreaVolumeEntity> Scene::GetFhAreaVolumeEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::FhAreaVolumeEntity>(
            _entityNodesByType.At(EntityType::FhAreaVolume));
    }

    LinkedListIteratorSpecialized<Entities::FhPlatformEntity> Scene::GetFhPlatformEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::FhPlatformEntity>(
            _entityNodesByType.At(EntityType::FhPlatform));
    }

    LinkedListIteratorSpecialized<Entities::FhJumpPadEntity> Scene::GetFhJumpPadEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::FhJumpPadEntity>(
            _entityNodesByType.At(EntityType::FhJumpPad));
    }

    LinkedListIteratorSpecialized<Entities::FhMorphCameraEntity> Scene::GetFhMorphCameraEntities() const
    {
        return LinkedListIteratorSpecialized<Entities::FhMorphCameraEntity>(
            _entityNodesByType.At(EntityType::FhMorphCamera));
    }
}
