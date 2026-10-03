#include "EnemyInstanceEntity.hpp"

#include "../Features.hpp"
#include "../GameState.hpp"
#include "../Metadata/Enemies.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Scene.hpp"
#include "../SceneSetup.hpp"
#include "BeamProjectileEntity.hpp"
#include "BombEntity.hpp"
#include "EnemySpawnEntity.hpp"
#include "ItemSpawnEntity.hpp"
#include "Players/PlayerEntity.hpp"
#include "../NativeRuntime/System/IO.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../Formats/Types.hpp"

#include <any>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

using ::MphRead::NativeRuntime::ManagedCast;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::TestFlag;
using ::OpenTK::Mathematics::AddY;
using ::OpenTK::Mathematics::ClearScale;
using ::OpenTK::Mathematics::IsZero;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::MathHelper::DegreesToRadians;
using ::OpenTK::Mathematics::Normalize;
using ::OpenTK::Mathematics::ScaleVector;
using ::OpenTK::Mathematics::SetRow3;

namespace
{
    using MphRead::Entities::EntityBase;
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;

    template <typename T>
    [[nodiscard]] const T& ArrayBackedReadOnlyListAt(
        const std::vector<T>& values, std::int32_t index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= values.size())
        {
            throw System::ArgumentOutOfRangeException();
        }
        return values[static_cast<std::size_t>(index)];
    }

    template <typename T>
    [[nodiscard]] const T& ManagedVectorArrayAt(
        const std::shared_ptr<std::vector<T>>& values, std::int32_t index)
    {
        const std::vector<T>& array = RequireReference(values);
        if (index < 0 || static_cast<std::size_t>(index) >= array.size())
        {
            throw MphRead::SceneDetail::IndexOutOfRangeException();
        }
        return array[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] MphRead::MessageObject BoxEntity(EntityBase* value)
    {
        return std::make_shared<const std::any>(value);
    }
}

namespace MphRead::Entities
{
    using Formats::CollisionDetection;
    using Formats::CollisionResult;
    using Formats::TestFlags;

    std::shared_ptr<MphRead::BeamProjectileArray>
        EnemyInstanceEntity::_beams{};

    EnemyInstanceEntity::EnemyInstanceEntity(EnemyInstanceEntityData data,
        Formats::Culling::NodeRef nodeRef, Scene* scene)
        : EntityBase(EntityType::EnemyInstance, nodeRef, scene),
          _data(data)
    {
        if (!_beams)
        {
            _beams = SceneSetup::CreateBeamList(64, scene);
        }
    }

    std::uint16_t EnemyInstanceEntity::Health() const noexcept
    {
        return _health;
    }

    std::uint16_t EnemyInstanceEntity::HealthMax() const noexcept
    {
        return _healthMax;
    }

    std::uint8_t EnemyInstanceEntity::StateA() const noexcept
    {
        return _state1;
    }

    std::uint8_t EnemyInstanceEntity::StateB() const noexcept
    {
        return _state2;
    }

    CollisionVolume EnemyInstanceEntity::HurtVolume() const noexcept
    {
        return _hurtVolume;
    }

    MphRead::EnemyType EnemyInstanceEntity::EnemyType() const noexcept
    {
        return _data.Type;
    }

    EntityBase* EnemyInstanceEntity::Owner() const noexcept
    {
        return _owner;
    }

    std::int32_t EnemyInstanceEntity::HealthbarMessageId() const noexcept
    {
        return _healthbarMessageId;
    }

    void EnemyInstanceEntity::DestroyBeams()
    {
        if (_beams)
        {
            _beams->ReleaseReferences();
        }
        _beams.reset();
    }

    void EnemyInstanceEntity::Initialize()
    {
        EntityBase::Initialize();
        _owner = _data.Spawner;
        _scanId = ArrayBackedReadOnlyListAt(
            Metadata::EnemyScanIds, static_cast<std::int32_t>(_data.Type));
        Metadata::LoadEffectiveness(_data.Type, BeamEffectiveness);
        Flags = EnemyFlags::CollidePlayer | EnemyFlags::CollideBeam;
        EnemyInitialize();
        _prevPos = static_cast<Vector3>(Position);
        if (_data.Type == MphRead::EnemyType::Gorea1A
            || _data.Type == MphRead::EnemyType::GoreaHead
            || _data.Type == MphRead::EnemyType::GoreaArm
            || _data.Type == MphRead::EnemyType::GoreaLeg
            || _data.Type == MphRead::EnemyType::Gorea1B
            || _data.Type == MphRead::EnemyType::GoreaSealSphere1
            || _data.Type == MphRead::EnemyType::Trocra
            || _data.Type == MphRead::EnemyType::Gorea2
            || _data.Type == MphRead::EnemyType::GoreaSealSphere2)
        {
            _onlyMoveHurtVolume = true;
        }
        if (_data.Type == MphRead::EnemyType::Cretaphid
            || _data.Type == MphRead::EnemyType::CretaphidEye
            || _data.Type == MphRead::EnemyType::CretaphidCrystal
            || _data.Type == MphRead::EnemyType::Unknown22
            || _data.Type == MphRead::EnemyType::Gorea1A
            || _data.Type == MphRead::EnemyType::GoreaHead
            || _data.Type == MphRead::EnemyType::GoreaArm
            || _data.Type == MphRead::EnemyType::GoreaLeg
            || _data.Type == MphRead::EnemyType::Gorea1B
            || _data.Type == MphRead::EnemyType::GoreaSealSphere1
            || _data.Type == MphRead::EnemyType::Trocra
            || _data.Type == MphRead::EnemyType::Gorea2
            || _data.Type == MphRead::EnemyType::GoreaSealSphere2
            || _data.Type == MphRead::EnemyType::GoreaMeteor
            || _data.Type == MphRead::EnemyType::Slench
            || _data.Type == MphRead::EnemyType::SlenchShield
            || _data.Type == MphRead::EnemyType::SlenchNest
            || _data.Type == MphRead::EnemyType::FireSpawn
            || _data.Type == MphRead::EnemyType::HitZone)
        {
            _noIneffectiveEffect = true;
        }
    }

    void EnemyInstanceEntity::Destroy()
    {
        _soundSource.StopAllSfx(true);
        EntityBase::Destroy();
    }

    void EnemyInstanceEntity::GetPosition(Vector3& position)
    {
        position = _hurtVolume.GetCenter();
    }

    void EnemyInstanceEntity::GetVectors(Vector3& position, Vector3& up, Vector3& facing)
    {
        position = _hurtVolume.GetCenter();
        up = UpVector();
        facing = FacingVector();
    }

    bool EnemyInstanceEntity::GetTargetable()
    {
        return _health != 0;
    }

    void EnemyInstanceEntity::ClearHitPlayers()
    {
        HitPlayers[0] = false;
        HitPlayers[1] = false;
        HitPlayers[2] = false;
        HitPlayers[3] = false;
    }

    Vector3 EnemyInstanceEntity::FixParallelVectors(Vector3 facing, Vector3 up) const
    {
        Vector3 right = Vector3::Cross(up, facing);
        if (!IsZero(right))
        {
            return up;
        }
        if (facing.Y != 0.0F || facing.Z != 0.0F)
        {
            right = Vector3::Cross(facing, Vector3(1.0F, 0.0F, 0.0F));
        }
        else
        {
            right = Vector3::Cross(facing, Vector3(0.0F, 1.0F, 0.0F));
        }
        right = Normalize(right);
        up = Vector3::Cross(facing, right);
        return up;
    }

    bool EnemyInstanceEntity::Process()
    {
        bool inRange = false;
        if (_data.Type == MphRead::EnemyType::Spawner
            || TestFlag(Flags(), EnemyFlags::NoMaxDistance)
            || RequireReference(_scene).CameraMode() != MphRead::CameraMode::Player)
        {
            inRange = true;
        }
        else
        {
            float distSqr = 35.0F * 35.0F;
            if (_owner != nullptr && _owner->Type == EntityType::EnemySpawn)
            {
                EnemySpawnEntity& spawner
                    = RequireReference(ManagedCast<EnemySpawnEntity>(_owner));
                distSqr = spawner.Data.EnemyActiveDistance.FloatValue();
                distSqr *= distSqr;
            }
            const auto checkInRange = [this, distSqr](Vector3 pos)
            {
                const Vector3 between = static_cast<Vector3>(Position) - pos;
                return LengthSquared(between) < distSqr
                    && between.Y > -15.0F && between.Y < 15.0F;
            };
            if (GameState::Multiplayer())
            {
                auto enumerator = RequireReference(_scene).GetPlayerEntities().GetEnumerator();
                while (enumerator.MoveNext())
                {
                    PlayerEntity& player = RequireReference(enumerator.Current());
                    if (player.Health() > 0
                        && checkInRange(static_cast<Vector3>(player.Position)))
                    {
                        inRange = true;
                        break;
                    }
                }
            }
            else
            {
                PlayerEntity& main = RequireReference(PlayerEntity::Main());
                inRange = checkInRange(static_cast<Vector3>(main.Position));
            }
        }

        if (inRange)
        {
            if (_timeSinceDamage < 510)
            {
                ++_timeSinceDamage;
            }
            if (_health > 0)
            {
                _state1 = _state2;
                _subId = _state1;
                if (!TestFlag(Flags(), EnemyFlags::Static))
                {
                    DoMovement();
                }
                const std::int32_t rangeIndex = ArrayBackedReadOnlyListAt(
                    Metadata::EnemyAudioRangeIndices,
                    static_cast<std::int32_t>(EnemyType()));
                _soundSource.Update(static_cast<Vector3>(Position), rangeIndex);
                UpdateNodeRefVolume();
                ClearHitPlayers();
                auto enumerator = RequireReference(_scene).GetPlayerEntities().GetEnumerator();
                while (enumerator.MoveNext())
                {
                    PlayerEntity& player = RequireReference(enumerator.Current());
                    CollisionResult hitRes{};
                    if (player.Health() > 0)
                    {
                        bool hit = player.CheckAltAttackHitEnemy1(this);
                        if (!hit)
                        {
                            hit = player.CheckAltAttackHitEnemy2(this);
                        }
                        CollisionVolume playerVolume = player.Volume();
                        CollisionVolume hurtVolume = HurtVolume();
                        if (!hit && CollisionDetection::CheckVolumesOverlap(
                            &playerVolume, &hurtVolume, hitRes))
                        {
                            const std::int32_t slot = player.SlotIndex();
                            if (slot < 0 || static_cast<std::size_t>(slot) >= HitPlayers.size())
                            {
                                throw SceneDetail::IndexOutOfRangeException();
                            }
                            HitPlayers[static_cast<std::size_t>(slot)] = true;
                            if (TestFlag(Flags(), EnemyFlags::CollidePlayer))
                            {
                                player.HandleCollision(hitRes);
                            }
                        }
                    }
                }
                EnemyProcess();
                if (!TestFlag(Flags(), EnemyFlags::Static))
                {
                    UpdateHurtVolume();
                }
                if (NodeRef != Formats::Culling::NodeRef::None)
                {
                    NodeRef = RequireReference(_scene).UpdateNodeRef(
                        NodeRef, _prevPos, static_cast<Vector3>(Position));
                }
                return BaseProcess();
            }
            RequireReference(_scene).SendMessage(
                Message::Destroyed, this, _owner, BoxInt32(0), BoxInt32(0));
            if (_owner != nullptr && _owner->Type == EntityType::EnemySpawn)
            {
                EnemySpawnEntity& spawner
                    = RequireReference(ManagedCast<EnemySpawnEntity>(_owner));
                const Vector3 pos = AddY(_hurtVolume.GetCenter(), 0.5F);
                ItemSpawnEntity::SpawnItemDrop(
                    spawner.Data.ItemType, pos, NodeRef,
                    spawner.Data.ItemChance, _scene);
            }
            return false;
        }
        RequireReference(_scene).SendMessage(
            Message::Destroyed, this, _owner, BoxInt32(1), BoxInt32(0));
        return false;
    }

    bool EnemyInstanceEntity::ContactDamagePlayer(std::uint32_t damage, bool knockback)
    {
        PlayerEntity& main = RequireReference(PlayerEntity::Main());
        const std::int32_t slot = main.SlotIndex();
        if (slot < 0 || static_cast<std::size_t>(slot) >= HitPlayers.size())
        {
            throw SceneDetail::IndexOutOfRangeException();
        }
        if (!HitPlayers[static_cast<std::size_t>(slot)])
        {
            return false;
        }
        main.TakeDamage(damage, DamageFlags::None, _speed, this);
        if (knockback)
        {
            const Vector3 between
                = main.Volume().SpherePosition - static_cast<Vector3>(Position);
            const float factor = Length(between) * 5.0F;
            Vector3 speed = main.Speed();
            speed.X += between.X / factor;
            speed.Z += between.Z / factor;
            main.SetSpeed(speed);
        }
        return true;
    }

    bool EnemyInstanceEntity::BaseProcess()
    {
        return EntityBase::Process();
    }

    void EnemyInstanceEntity::DoMovement()
    {
        _prevPos = static_cast<Vector3>(Position);
        Position = static_cast<Vector3>(Position) + _speed;
        UpdateHurtVolume();
    }

    void EnemyInstanceEntity::UpdateHurtVolume()
    {
        if (_onlyMoveHurtVolume)
        {
            _hurtVolume = CollisionVolume::Move(
                _hurtVolumeInit, static_cast<Vector3>(Position));
        }
        else
        {
            _hurtVolume = CollisionVolume::Transform(
                _hurtVolumeInit, static_cast<Matrix4>(Transform));
        }
    }

    void EnemyInstanceEntity::GetDrawInfo()
    {
        if (_health > 0 && TestFlag(Flags(), EnemyFlags::Visible))
        {
            if (!EnemyGetDrawInfo())
            {
                DrawGeneric();
            }
        }
    }

    void EnemyInstanceEntity::DrawGeneric()
    {
        if (!TestFlag(Flags(), EnemyFlags::NoMaxDistance) && !IsVisible(NodeRef))
        {
            return;
        }
        if (_timeSinceDamage < 5 * 2)
        {
            SetPaletteOverride(Metadata::RedPalette);
        }
        EntityBase::GetDrawInfo();
        SetPaletteOverride(std::nullopt);
    }

    void EnemyInstanceEntity::EnemyInitialize()
    {
    }

    void EnemyInstanceEntity::EnemyProcess()
    {
    }

    bool EnemyInstanceEntity::EnemyGetDrawInfo()
    {
        return false;
    }

    void EnemyInstanceEntity::SetHealth(std::uint16_t health) noexcept
    {
        _health = health;
    }

    void EnemyInstanceEntity::Detach()
    {
    }

    bool EnemyInstanceEntity::CheckHitByBomb(BombEntity* bomb)
    {
        if (TestFlag(Flags(), EnemyFlags::Invincible))
        {
            return false;
        }
        BombEntity& bombRef = RequireReference(bomb);
        const Vector3 between
            = static_cast<Vector3>(Position) - static_cast<Vector3>(bombRef.Position);
        const float radius = bombRef.Radius();
        if (LengthSquared(between) > radius * radius)
        {
            return false;
        }
        TakeDamage(bombRef.EnemyDamage(), bomb);
        RequireReference(_scene).SendMessage(
            Message::Impact, bomb, bombRef.Owner(), BoxEntity(this), BoxInt32(0));
        return true;
    }

    void EnemyInstanceEntity::TakeDamage(std::uint32_t damage, EntityBase* source)
    {
        const std::uint16_t prevHealth = _health;
        Effectiveness effectiveness = Effectiveness::Normal;
        BeamProjectileEntity* beamSource = nullptr;
        if (source != nullptr && source->Type == EntityType::BeamProjectile)
        {
            beamSource = ManagedCast<BeamProjectileEntity>(source);
        }
        if (beamSource != nullptr)
        {
            const std::shared_ptr<EntityBase> beamOwner = beamSource->Owner();
            if (beamOwner && beamOwner->Type == EntityType::EnemyInstance)
            {
                return;
            }
            effectiveness = GetEffectiveness(beamSource->Beam());
        }
        bool unaffected = effectiveness == Effectiveness::Zero
            || TestFlag(Flags(), EnemyFlags::Invincible)
            || (source != nullptr && source->Type == EntityType::Bomb
                && TestFlag(Flags(), EnemyFlags::NoBombDamage));
        bool dead = false;
        bool doubleDead = false;
        if (!unaffected)
        {
            if (beamSource != nullptr)
            {
                const std::shared_ptr<EntityBase> beamOwner = beamSource->Owner();
                if (beamOwner && beamOwner->Type == EntityType::Player)
                {
                    damage = static_cast<std::uint32_t>(
                        damage * Metadata::GetDamageMultiplier(effectiveness));
                    if (damage == 0)
                    {
                        damage = 1;
                    }
                }
            }
            if (damage >= _health)
            {
                dead = true;
                if (_health <= 0)
                {
                    doubleDead = true;
                }
                _health = 0;
            }
            else
            {
                _health -= static_cast<std::uint16_t>(damage);
            }
        }
        if (EnemyTakeDamage(source))
        {
            _health = prevHealth;
            unaffected = true;
            dead = false;
        }
        if (unaffected)
        {
            if (effectiveness == Effectiveness::Zero && !_noIneffectiveEffect)
            {
                Matrix4 transform = GetTransformMatrix(
                    Vector3(1.0F, 0.0F, 0.0F), Vector3(0.0F, 1.0F, 0.0F));
                SetRow3(transform, _hurtVolume.GetCenter());
                auto effect = RequireReference(_scene).SpawnEffectGetEntry(115, transform);
                if (effect)
                {
                    effect->SetReadOnlyField(0, _boundingRadius);
                    RequireReference(_scene).DetachEffectEntry(effect, false);
                }
            }
        }
        else
        {
            if (doubleDead && Bugfixes::NoDoubleEnemyDeath())
            {
                return;
            }
            if (beamSource != nullptr)
            {
                beamSource->SpawnDamageEffect(effectiveness);
            }
            if (dead)
            {
                if (_data.Type != MphRead::EnemyType::CarnivorousPlant
                    && _data.Type != MphRead::EnemyType::CretaphidEye)
                {
                    StorySave& storySave = RequireReference(GameState::StorySave);
                    StorySave::SaveStats& stats = RequireReference(storySave.Stats);
                    if (stats.EnemyKills != std::numeric_limits<std::uint32_t>::max())
                    {
                        ++stats.EnemyKills;
                    }
                }
                if (_data.Type == MphRead::EnemyType::Temroid)
                {
                    Detach();
                }
                _soundSource.StopAllSfx();
                const std::int32_t deathSfx = ManagedVectorArrayAt(
                    Metadata::EnemyDeathSfx(), static_cast<std::int32_t>(EnemyType()));
                PlayEnemySfx(deathSfx, true);
                std::int32_t effectId = 0;
                if (EnemyType() == MphRead::EnemyType::FireSpawn)
                {
                    assert(_owner != nullptr && _owner->Type == EntityType::EnemySpawn);
                    EnemySpawnEntity& spawner
                        = RequireReference(ManagedCast<EnemySpawnEntity>(_owner));
                    effectId = spawner.Data.Fields.S06().EnemySubtype == 1 ? 217 : 218;
                }
                else
                {
                    effectId = Metadata::GetEnemyDeathEffect(EnemyType());
                }
                if (effectId > 0)
                {
                    const Matrix4 transform = ClearScale(static_cast<Matrix4>(Transform));
                    RequireReference(_scene).SpawnEffect(effectId, transform);
                }
            }
            else
            {
                _timeSinceDamage = 0;
                const std::int32_t damageSfx = ManagedVectorArrayAt(
                    Metadata::EnemyDamageSfx(), static_cast<std::int32_t>(EnemyType()));
                PlayEnemySfx(damageSfx, false);
                switch (_data.Type)
                {
                case MphRead::EnemyType::Zoomer:
                case MphRead::EnemyType::Petrasyl1:
                case MphRead::EnemyType::Petrasyl2:
                case MphRead::EnemyType::Petrasyl3:
                case MphRead::EnemyType::Petrasyl4:
                    if (_models.Size() == 0)
                    {
                        throw System::ArgumentOutOfRangeException();
                    }
                    _models[0].SetAnimation(1, AnimFlags::NoLoop);
                    break;
                case MphRead::EnemyType::Blastcap:
                {
                    if (_models.Size() == 0)
                    {
                        throw System::ArgumentOutOfRangeException();
                    }
                    _models[0].SetAnimation(0, AnimFlags::NoLoop);
                    Matrix4 transform = GetTransformMatrix(
                        Vector3(1.0F, 0.0F, 0.0F), Vector3(0.0F, 1.0F, 0.0F));
                    SetRow3(transform, static_cast<Vector3>(Position));
                    RequireReference(_scene).SpawnEffect(3, transform);
                    break;
                }
                default:
                    break;
                }
            }
        }
    }

    void EnemyInstanceEntity::PlayEnemySfx(std::int32_t sfx, bool noUpdate)
    {
        if (sfx != -1)
        {
            float recency = -1.0F;
            bool sourceOnly = false;
            if ((sfx & 0x20000) != 0)
            {
                recency = std::numeric_limits<float>::max();
                sourceOnly = true;
            }
            else if ((sfx & 0x80000) != 0)
            {
                recency = 0.0F;
            }
            _soundSource.PlaySfx(
                sfx & ~0xA0000, false, noUpdate, recency, sourceOnly);
        }
    }

    Effectiveness EnemyInstanceEntity::GetEffectiveness(BeamType beam) const
    {
        const std::int32_t index = static_cast<std::int32_t>(beam);
        assert(index < static_cast<std::int32_t>(BeamEffectiveness.size()));
        if (index < 0 || static_cast<std::size_t>(index) >= BeamEffectiveness.size())
        {
            throw SceneDetail::IndexOutOfRangeException();
        }
        return BeamEffectiveness[static_cast<std::size_t>(index)];
    }

    bool EnemyInstanceEntity::EnemyTakeDamage(EntityBase*)
    {
        return false;
    }

    void EnemyInstanceEntity::SetHealthbarMessageId(std::int32_t value) noexcept
    {
        _healthbarMessageId = value;
    }

    void EnemyInstanceEntity::CallStateProcess()
    {
        assert(_stateProcesses != nullptr
            && static_cast<std::size_t>(_state1) < _stateProcesses->Length());
        ManagedArray<std::function<void()>>& processes = RequireReference(_stateProcesses);
        if (static_cast<std::size_t>(_state1) >= processes.Length())
        {
            throw SceneDetail::IndexOutOfRangeException();
        }
        std::function<void()>& process = processes[_state1];
        if (!process)
        {
            throw System::NullReferenceException();
        }
        process();
    }

    bool EnemyInstanceEntity::HandleBlockingCollision(
        Vector3 position, CollisionVolume volume, bool updateSpeed)
    {
        bool a = false;
        bool b = false;
        return HandleBlockingCollision(position, volume, updateSpeed, a, b);
    }

    bool EnemyInstanceEntity::HandleBlockingCollision(
        Vector3 position, CollisionVolume volume, bool updateSpeed,
        bool& withGround, bool& withWall)
    {
        std::int32_t count = 0;
        ManagedArray<CollisionResult> results(30);
        Vector3 pointOne = Vector3::Zero;
        Vector3 pointTwo = Vector3::Zero;
        if (volume.Type == VolumeType::Cylinder)
        {
            pointOne = AddY(_prevPos, 0.5F);
            pointTwo = AddY(static_cast<Vector3>(Position), 0.5F);
            count = CollisionDetection::CheckSphereBetweenPoints(
                pointOne, pointTwo, volume.CylinderRadius, 30,
                false, TestFlags::None, _scene, &results);
        }
        else
        {
            count = CollisionDetection::CheckInRadius(
                position, _boundingRadius, 30,
                false, TestFlags::None, _scene, &results);
        }
        withGround = false;
        if (count == 0)
        {
            return false;
        }
        for (std::int32_t i = 0; i < count; ++i)
        {
            const CollisionResult result = results[static_cast<std::size_t>(i)];
            float v18 = 0.0F;
            if (result.Field0 != 0)
            {
                v18 = _boundingRadius - result.Field14;
            }
            else if (volume.Type == VolumeType::Cylinder)
            {
                v18 = _boundingRadius + result.Plane.W
                    - Vector3::Dot(pointTwo, result.Plane.Xyz());
            }
            else
            {
                v18 = _boundingRadius + result.Plane.W
                    - Vector3::Dot(position, result.Plane.Xyz());
            }
            if (v18 > 0.0F)
            {
                if (result.Plane.Y < 0.1F && result.Plane.Y > -0.1F)
                {
                    withWall = true;
                }
                else
                {
                    withGround = true;
                }
                Position = static_cast<Vector3>(Position) + ScaleVector(result.Plane.Xyz(), v18);
                if (updateSpeed)
                {
                    const float dot = Vector3::Dot(_speed, result.Plane.Xyz());
                    if (dot < 0.0F)
                    {
                        _speed = _speed + ScaleVector(result.Plane.Xyz(), -dot);
                    }
                }
            }
        }
        return true;
    }

    void EnemyInstanceEntity::GetDisplayVolumes()
    {
        if (RequireReference(_scene).ShowVolumes() == VolumeDisplay::EnemyHurt)
        {
            AddVolumeItem(_hurtVolume, Vector3(1.0F, 0.0F, 0.0F));
        }
    }

    Vector3 EnemyInstanceEntity::RotateVector(Vector3 vec, Vector3 axis, float angle)
    {
        axis = Normalize(axis);
        const float radians = DegreesToRadians(angle);
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        const float oneMinusCosine = 1.0F - cosine;
        const Vector3 cross = Vector3::Cross(axis, vec);
        const float dot = Vector3::Dot(axis, vec);
        return ScaleVector(vec, cosine)
            + ScaleVector(cross, sine)
            + ScaleVector(axis, dot * oneMinusCosine);
    }

    bool EnemyInstanceEntity::SeekTargetVector(
        Vector3 target, Vector3& current, Vector3 axis,
        std::uint16_t& steps, float angle)
    {
        if (steps > 0
            && Vector3::Dot(target, current) < std::cos(DegreesToRadians(angle)))
        {
            current = Normalize(RotateVector(current, axis, angle));
            --steps;
            return false;
        }
        current = target;
        return true;
    }

    bool EnemyInstanceEntity::SeekTargetFacing(
        Vector3 target, Vector3 up, std::uint16_t& steps, float angle)
    {
        bool result = false;
        Vector3 facing = FacingVector();
        const float radians = DegreesToRadians(angle);
        if (steps > 0 && Vector3::Dot(target, facing) < std::cos(radians))
        {
            const Vector3 cross = Vector3::Cross(target, facing);
            const float signedAngle = angle * (cross.Y <= 0.0F ? 1.0F : -1.0F);
            facing = Normalize(RotateVector(
                facing, Vector3(0.0F, 1.0F, 0.0F), signedAngle));
            --steps;
        }
        else
        {
            facing = target;
            result = true;
        }
        SetTransform(facing, up, static_cast<Vector3>(Position));
        return result;
    }
}
