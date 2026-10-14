#include "EnemySpawnEntity.hpp"

#include "../GameState.hpp"
#include "../Scene.hpp"
#include "../Mods/Network/Gameplay/Weapons/EnemyTargetIdentity.hpp"
#include "EnemyInstanceEntity.hpp"
#include "Enemies/00_WarWasp.hpp"
#include "Enemies/01_Zoomer.hpp"
#include "Enemies/02_Temroid.hpp"
#include "Enemies/03_Petrasyl1.hpp"
#include "Enemies/04_Petrasyl2.hpp"
#include "Enemies/05_Petrasyl3.hpp"
#include "Enemies/06_Petrasyl4.hpp"
#include "Enemies/10_BarbedWarWasp.hpp"
#include "Enemies/11_Shriekbat.hpp"
#include "Enemies/12_Geemer.hpp"
#include "Enemies/16_Blastcap.hpp"
#include "Enemies/18_AlimbicTurret.hpp"
#include "Enemies/19_Cretaphid.hpp"
#include "Enemies/20_CretaphidEye.hpp"
#include "Enemies/21_CretaphidCrystal.hpp"
#include "Enemies/23_PsychoBit.hpp"
#include "Enemies/24_Gorea1A.hpp"
#include "Enemies/25_GoreaHead.hpp"
#include "Enemies/26_GoreaArm.hpp"
#include "Enemies/27_GoreaLeg.hpp"
#include "Enemies/28_Gorea1B.hpp"
#include "Enemies/29_GoreaSealSphere1.hpp"
#include "Enemies/30_Trocra.hpp"
#include "Enemies/31_Gorea2.hpp"
#include "Enemies/32_GoreaSealSphere2.hpp"
#include "Enemies/33_GoreaMeteor.hpp"
#include "Enemies/35_Voldrum.hpp"
#include "Enemies/36_Voldrum.hpp"
#include "Enemies/37_Quadtroid.hpp"
#include "Enemies/38_CrashPillar.hpp"
#include "Enemies/39_FireSpawn.hpp"
#include "Enemies/40_EnemySpawner.hpp"
#include "Enemies/41_Slench.hpp"
#include "Enemies/42_SlenchShield.hpp"
#include "Enemies/43_SlenchNest.hpp"
#include "Enemies/44_SlenchSynapse.hpp"
#include "Enemies/45_SlenchTurret.hpp"
#include "Enemies/46_LesserIthrak.hpp"
#include "Enemies/47_GreaterIthrak.hpp"
#include "Enemies/49_ForceFieldLock.hpp"
#include "Enemies/50_HitZone.hpp"
#include "Enemies/51_CarnivorousPlant.hpp"
#include "Players/PlayerEntity.hpp"
#include "../Formats/Collision.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../Formats/Types.hpp"

#include <any>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedSubtract;
using ::MphRead::TestFlag;
using ::OpenTK::Mathematics::DistanceSquared;
using ::OpenTK::Mathematics::Multiply;

namespace MphRead::Entities
{
    namespace
    {
        using OpenTK::Mathematics::Matrix4;
        using OpenTK::Mathematics::Vector3;
        using OpenTK::Mathematics::Vector4;

        template <typename T>
        [[nodiscard]] std::shared_ptr<EnemyInstanceEntity> MakeEnemy(
            EntityBase* spawner,
            MphRead::EnemyType type,
            Formats::Culling::NodeRef nodeRef,
            Scene* scene)
        {
            return std::make_shared<T>(EnemyInstanceEntityData(type, spawner,
                Mods::Network::EnemyTargetIdentity::ForSpawn(spawner, type)), nodeRef, scene);
        }
    }

    EnemySpawnEntity::EnemySpawnEntity(
        EnemySpawnEntityData data, std::string nodeName, Scene* scene)
        : EntityBase(EntityType::EnemySpawn, std::move(nodeName), scene),
          _data(data),
          Data(_data),
          SpawnedCount(_spawnedCount),
          ActiveCount(_activeCount),
          ParentEntCol(_parentEntCol)
    {
        Id = data.Header.EntityId;
        std::string marshaledNodeName = ::MphRead::NativeRuntime::Utf16ToUtf8(::MphRead::MarshalExtensions::MarshalString(data.NodeName));
        _rangeNodeRef = RequireReference(scene).GetNodeRefByName(
            std::move(marshaledNodeName));
        _cooldownTimer = static_cast<std::int32_t>(_data.InitialCooldown) * 2;
        assert(GameState::Mode() == GameMode::SinglePlayer);
        bool active = false;
        const std::int32_t state = RequireReference(::MphRead::GameState::StorySave).InitRoomState(
            RequireReference(_scene).RoomId(), Id, data.Active != 0);
        if (data.AlwaysActive != 0)
        {
            active = data.Active != 0;
        }
        else
        {
            active = state != 0;
        }
        if (active)
        {
            Flags |= SpawnerFlags::Active;
        }
        SetTransform(
            data.Header.FacingVector, data.Header.UpVector, data.Header.Position);
        AddPlaceholderModel();
        Flags |= SpawnerFlags::Suspended;
    }

    std::optional<Vector4> EnemySpawnEntity::OverrideColor() const
    {
        return ColorRgb(0x00, 0x00, 0x8B).AsVector4();
    }

    void EnemySpawnEntity::Initialize()
    {
        EntityBase::Initialize();
        if (_data.EntityId1 != -1)
        {
            (void)RequireReference(_scene).TryGetEntity(_data.EntityId1, _entity1);
        }
        if (_data.EntityId2 != -1)
        {
            (void)RequireReference(_scene).TryGetEntity(_data.EntityId2, _entity2);
        }
        if (_data.EntityId3 != -1)
        {
            (void)RequireReference(_scene).TryGetEntity(_data.EntityId3, _entity3);
        }
        if (_data.LinkedEntityId != -1)
        {
            (void)RequireReference(_scene).TryGetEntity(_data.LinkedEntityId, _parent);
            if (_parent)
            {
                _parentEntCol = _parent->EntityCollision[0];
                if (_parentEntCol)
                {
                    _invTransform = Multiply(_transform, _parentEntCol->Inverse2);
                }
            }
        }
        if (_data.SpawnerHealth > 0)
        {
            std::shared_ptr<EnemyInstanceEntity> enemy = SpawnEnemy(
                this, MphRead::EnemyType::Spawner, NodeRef, _scene);
            if (enemy)
            {
                RequireReference(_scene).AddEntity(enemy);
            }
        }
    }

    void EnemySpawnEntity::SetActive(bool active)
    {
        EntityBase::SetActive(active);
        Flags |= SpawnerFlags::Active;
    }

    bool EnemySpawnEntity::Process()
    {
        if (!TestFlag(Flags, SpawnerFlags::Active))
        {
            return EntityBase::Process();
        }
        if (_parentEntCol)
        {
            Transform = Multiply(_invTransform, _parentEntCol->Transform);
        }
        if (_cooldownTimer > 0)
        {
            --_cooldownTimer;
        }
        if (TestFlag(Flags, SpawnerFlags::Suspended) && _cooldownTimer == 0)
        {
            if (_rangeNodeRef != Formats::Culling::NodeRef::None
                && RequireReference(_scene).CameraMode() == MphRead::CameraMode::Player)
            {
                auto enumerator = RequireReference(_scene).GetPlayerEntities().GetEnumerator();
                while (enumerator.MoveNext())
                {
                    std::shared_ptr<PlayerEntity> player = enumerator.Current();
                    PlayerEntity& playerRef = RequireReference(player);
                    if (playerRef.Health() > 0 && playerRef.NodeRef == _rangeNodeRef)
                    {
                        Flags &= ~SpawnerFlags::Suspended;
                    }
                }
            }
            else
            {
                Flags &= ~SpawnerFlags::Suspended;
            }
        }
        if (TestFlag(Flags, SpawnerFlags::Suspended))
        {
            return EntityBase::Process();
        }

        float distSqr = _data.ActiveDistance.FloatValue();
        distSqr *= distSqr;
        bool inRange = false;
        if (_data.EnemyType != MphRead::EnemyType::CarnivorousPlant
            && RequireReference(_scene).CameraMode() == MphRead::CameraMode::Player)
        {
            auto enumerator = RequireReference(_scene).GetPlayerEntities().GetEnumerator();
            while (enumerator.MoveNext())
            {
                std::shared_ptr<PlayerEntity> player = enumerator.Current();
                PlayerEntity& playerRef = RequireReference(player);
                if (DistanceSquared(Position, playerRef.Position) < distSqr)
                {
                    inRange = true;
                    break;
                }
            }
        }
        else
        {
            inRange = true;
        }

        if (!inRange)
        {
            Flags |= SpawnerFlags::Suspended;
        }
        else if (_activeCount < _data.SpawnLimit
            && _cooldownTimer == 0
            && _data.SpawnCount > 0
            && (_data.SpawnTotal == 0 || _spawnedCount < _data.SpawnTotal))
        {
            for (std::int32_t i = 0; i < _data.SpawnCount; ++i)
            {
                if (_data.EnemyType == MphRead::EnemyType::Hunter)
                {
                    PlayerEntity* spawned = SpawnHunter();
                    if (spawned == nullptr)
                    {
                        break;
                    }
                }
                else
                {
                    std::shared_ptr<EnemyInstanceEntity> spawned = SpawnEnemy(
                        this, _data.EnemyType, NodeRef, _scene);
                    if (!spawned)
                    {
                        break;
                    }
                    RequireReference(_scene).AddEntity(spawned);
                }

                if (TestFlag(Flags, SpawnerFlags::HasModel))
                {
                    Flags |= SpawnerFlags::PlayAnimation;
                }
                _activeCount = UncheckedAdd(_activeCount, 1);
                _spawnedCount = UncheckedAdd(_spawnedCount, 1);
                _cooldownTimer = static_cast<std::int32_t>(_data.CooldownTime) * 2;
                if (_activeCount >= _data.SpawnLimit
                    || (_data.SpawnTotal > 0 && _spawnedCount >= _data.SpawnTotal))
                {
                    break;
                }
            }
        }

        if (_data.SpawnTotal > 0
            && _spawnedCount >= _data.SpawnTotal
            && _activeCount == 0)
        {
            DeactivateAndSendMessages();
        }
        return EntityBase::Process();
    }

    PlayerEntity* EnemySpawnEntity::SpawnHunter()
    {
        PlayerEntity* player = nullptr;
        for (std::int32_t i = 0; i < 4; ++i)
        {
            player = PlayerEntity::Players()[static_cast<std::size_t>(i)].get();
            PlayerEntity& playerRef = RequireReference(player);
            if (playerRef.Health() == 0 && playerRef.EnemySpawner().get() == this)
            {
                playerRef.Spawn(
                    Position, FacingVector(), UpVector(), NodeRef, true);
                playerRef.InitEnemyHunter();
                break;
            }
        }
        return player;
    }

    void EnemySpawnEntity::DeactivateAndSendMessages()
    {
        bool updateSave = false;
        Flags &= ~SpawnerFlags::Active;
        Scene& scene = RequireReference(_scene);
        RequireReference(::MphRead::GameState::StorySave).SetRoomState(scene.RoomId(), Id, 1);

        if ((_data.EnemyType != MphRead::EnemyType::Hunter
                || _data.Fields.S09().EncounterType == 1)
            && scene.AreaId() < 8)
        {
            const std::int32_t type = static_cast<std::int32_t>(_data.EnemyType);
            if (type >= 0 && (type >> 3) < 8)
            {
                StorySave& storySave = RequireReference(::MphRead::GameState::StorySave);
                const std::int32_t areaId = scene.AreaId();
                StorySave::ByteArray& encounterRow
                    = ManagedAt(storySave.EnemyEncounters, areaId);
                std::uint8_t& encounter = ManagedAt(encounterRow, type >> 3);
                encounter |= static_cast<std::uint8_t>(1U << (type & 7));
            }
        }

        if (_data.EnemyType == MphRead::EnemyType::Cretaphid)
        {
            RequireReference(::MphRead::GameState::StorySave).Areas |= 3;
            GameState::UpdateBossFlags(scene.AreaId());
            updateSave = true;
        }
        else if (_data.EnemyType == MphRead::EnemyType::Slench)
        {
            RequireReference(::MphRead::GameState::StorySave).Areas |= 0xF0;
            GameState::UpdateBossFlags(scene.AreaId());
            updateSave = true;
        }
        else if (_data.EnemyType == MphRead::EnemyType::Gorea1A)
        {
            GameState::UpdateBossFlags(scene.AreaId());
            updateSave = true;
        }

        if (_entity1)
        {
            MessageObject param1 = BoxInt32(-1);
            MessageObject param2 = BoxInt32(0);
            scene.SendMessage(
                _data.Message1, this, _entity1.get(), std::move(param1), std::move(param2));
        }
        if (_entity2)
        {
            MessageObject param1 = BoxInt32(-1);
            MessageObject param2 = BoxInt32(0);
            scene.SendMessage(
                _data.Message2, this, _entity2.get(), std::move(param1), std::move(param2));
        }
        if (_entity3)
        {
            MessageObject param1 = BoxInt32(-1);
            MessageObject param2 = BoxInt32(0);
            scene.SendMessage(
                _data.Message3, this, _entity3.get(), std::move(param1), std::move(param2));
        }
        if (updateSave)
        {
            GameState::UpdateCleanSave(false);
        }
    }

    void EnemySpawnEntity::HandleMessage(MessageInfo info)
    {
        if (info.Message == Message::Destroyed)
        {
            if (UnboxInt32(info.Param1) != 0)
            {
                _activeCount = UncheckedSubtract(_activeCount, 1);
                _spawnedCount = UncheckedSubtract(_spawnedCount, 1);
                _cooldownTimer = 0;
            }
            else
            {
                EntityBase& sender = RequireReference(info.Sender);
                if (sender.Type == EntityType::EnemyInstance)
                {
                    auto* enemy = dynamic_cast<EnemyInstanceEntity*>(&sender);
                    if (enemy == nullptr)
                    {
                        throw SceneDetail::InvalidCastException();
                    }
                    if (enemy->EnemyType() == MphRead::EnemyType::Spawner)
                    {
                        Flags &= ~SpawnerFlags::Active;
                        RequireReference(::MphRead::GameState::StorySave).SetRoomState(
                            RequireReference(_scene).RoomId(), Id, 1);
                    }
                    else
                    {
                        _activeCount = UncheckedSubtract(_activeCount, 1);
                        _cooldownTimer
                            = static_cast<std::int32_t>(_data.CooldownTime) * 2;
                    }
                }
                else
                {
                    _activeCount = UncheckedSubtract(_activeCount, 1);
                    _cooldownTimer
                        = static_cast<std::int32_t>(_data.CooldownTime) * 2;
                }
            }
            if (!TestFlag(Flags, SpawnerFlags::Active) && _activeCount == 0)
            {
                DeactivateAndSendMessages();
            }
        }
        else if (info.Message == Message::Activate)
        {
            Flags |= SpawnerFlags::Active;
            RequireReference(::MphRead::GameState::StorySave).SetRoomState(
                RequireReference(_scene).RoomId(), Id, 3);
        }
        else if (info.Message == Message::SetActive)
        {
            if (UnboxInt32(info.Param1) != 0)
            {
                Flags |= SpawnerFlags::Active;
                RequireReference(::MphRead::GameState::StorySave).SetRoomState(
                    RequireReference(_scene).RoomId(), Id, 3);
            }
            else
            {
                Flags &= ~SpawnerFlags::Active;
                RequireReference(::MphRead::GameState::StorySave).SetRoomState(
                    RequireReference(_scene).RoomId(), Id, 1);
            }
        }
        else if (info.Message == Message::Gorea2Trigger)
        {
            auto enumerator = RequireReference(_scene)
                .GetEnemyInstanceEntities().GetEnumerator();
            while (enumerator.MoveNext())
            {
                std::shared_ptr<EnemyInstanceEntity> enemy = enumerator.Current();
                EnemyInstanceEntity& enemyRef = RequireReference(enemy);
                if (enemyRef.EnemyType() == MphRead::EnemyType::Gorea2)
                {
                    std::shared_ptr<Enemies::Enemy31Entity> gorea2
                        = std::dynamic_pointer_cast<Enemies::Enemy31Entity>(enemy);
                    if (!gorea2)
                    {
                        throw SceneDetail::InvalidCastException();
                    }
                    gorea2->HandleMessage(info);
                    return;
                }
            }
        }
    }

    std::shared_ptr<EnemyInstanceEntity> EnemySpawnEntity::SpawnEnemy(
        EntityBase* spawner,
        MphRead::EnemyType type,
        Formats::Culling::NodeRef nodeRef,
        Scene* scene)
    {
        using namespace Enemies;

        switch (type)
        {
        case MphRead::EnemyType::WarWasp:
            return MakeEnemy<Enemy00Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Zoomer:
            return MakeEnemy<Enemy01Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Temroid:
            return MakeEnemy<Enemy02Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Petrasyl1:
            return MakeEnemy<Enemy03Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Petrasyl2:
            return MakeEnemy<Enemy04Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Petrasyl3:
            return MakeEnemy<Enemy05Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Petrasyl4:
            return MakeEnemy<Enemy06Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::BarbedWarWasp:
            return MakeEnemy<Enemy10Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Shriekbat:
            return MakeEnemy<Enemy11Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Geemer:
            return MakeEnemy<Enemy12Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Blastcap:
            return MakeEnemy<Enemy16Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::AlimbicTurret:
            return MakeEnemy<Enemy18Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Cretaphid:
            return MakeEnemy<Enemy19Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::CretaphidEye:
            return MakeEnemy<Enemy20Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::CretaphidCrystal:
            return MakeEnemy<Enemy21Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::PsychoBit1:
            return MakeEnemy<Enemy23Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Gorea1A:
            return MakeEnemy<Enemy24Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::GoreaHead:
            return MakeEnemy<Enemy25Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::GoreaArm:
            return MakeEnemy<Enemy26Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::GoreaLeg:
            return MakeEnemy<Enemy27Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Gorea1B:
            return MakeEnemy<Enemy28Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::GoreaSealSphere1:
            return MakeEnemy<Enemy29Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Trocra:
        {
            Scene& sceneRef = RequireReference(scene);
            if (sceneRef.RoomId() == 91)
            {
                std::int32_t enemyCount = 0;
                bool isGorea1 = false;
                auto enumerator = sceneRef.GetEnemyInstanceEntities().GetEnumerator();
                while (enumerator.MoveNext())
                {
                    std::shared_ptr<EnemyInstanceEntity> enemy = enumerator.Current();
                    const MphRead::EnemyType enemyType
                        = RequireReference(enemy).EnemyType();
                    if (enemyType == MphRead::EnemyType::Gorea1A)
                    {
                        enemyCount = UncheckedAdd(enemyCount, 1);
                        isGorea1 = true;
                    }
                    else if (enemyType == MphRead::EnemyType::Trocra
                        || enemyType == MphRead::EnemyType::GoreaHead
                        || enemyType == MphRead::EnemyType::GoreaArm
                        || enemyType == MphRead::EnemyType::GoreaLeg
                        || enemyType == MphRead::EnemyType::Gorea1B
                        || enemyType == MphRead::EnemyType::GoreaSealSphere1)
                    {
                        enemyCount = UncheckedAdd(enemyCount, 1);
                    }
                }
                if (isGorea1 && enemyCount >= 14)
                {
                    return nullptr;
                }
            }
            return MakeEnemy<Enemy30Entity>(spawner, type, nodeRef, scene);
        }
        case MphRead::EnemyType::Gorea2:
            return MakeEnemy<Enemy31Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::GoreaSealSphere2:
            return MakeEnemy<Enemy32Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::GoreaMeteor:
            return MakeEnemy<Enemy33Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Voldrum2:
            return MakeEnemy<Enemy35Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Voldrum1:
            return MakeEnemy<Enemy36Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Quadtroid:
            return MakeEnemy<Enemy37Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::CrashPillar:
            return MakeEnemy<Enemy38Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::FireSpawn:
            return MakeEnemy<Enemy39Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Spawner:
            return MakeEnemy<Enemy40Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::Slench:
            return MakeEnemy<Enemy41Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::SlenchShield:
            return MakeEnemy<Enemy42Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::SlenchNest:
            return MakeEnemy<Enemy43Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::SlenchSynapse:
            return MakeEnemy<Enemy44Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::SlenchTurret:
            return MakeEnemy<Enemy45Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::LesserIthrak:
            return MakeEnemy<Enemy46Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::GreaterIthrak:
            return MakeEnemy<Enemy47Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::ForceFieldLock:
            return MakeEnemy<Enemy49Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::HitZone:
            return MakeEnemy<Enemy50Entity>(spawner, type, nodeRef, scene);
        case MphRead::EnemyType::CarnivorousPlant:
            return MakeEnemy<Enemy51Entity>(spawner, type, nodeRef, scene);
        default:
            return nullptr;
        }
    }

    FhEnemySpawnEntity::FhEnemySpawnEntity(
        FhEnemySpawnEntityData data, Scene* scene)
        : EntityBase(EntityType::FhEnemySpawn, scene),
          _data(data)
    {
        Id = data.Header.EntityId;
        SetTransform(
            data.Header.FacingVector, data.Header.UpVector, data.Header.Position);
        AddPlaceholderModel();
    }

    std::optional<Vector4> FhEnemySpawnEntity::OverrideColor() const
    {
        return ColorRgb(0x00, 0x00, 0x8B).AsVector4();
    }
}
