#include "PlatformEntity.hpp"

#include "../Features.hpp"
#include "../Formats/Collision.hpp"
#include "../GameState.hpp"
#include "../MemoryArrays.hpp"
#include "../Metadata/Enemies.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Metadata/SoundMeta.hpp"
#include "../Metadata/Weapons.hpp"
#include "../Scene.hpp"
#include "../SceneSetup.hpp"
#include "BeamProjectileEntity.hpp"
#include "ItemSpawnEntity.hpp"
#include "Players/PlayerEntity.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/OpenTK/Mathematics.hpp"
#include "../Formats/Types.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

using ::MphRead::NativeRuntime::ConvertToInt32Net9;
using ::MphRead::NativeRuntime::ManagedAt;
using ::MphRead::NativeRuntime::ManagedListAt;
using ::MphRead::NativeRuntime::RequireReference;
using ::MphRead::NativeRuntime::UInt32ToInt32;
using ::MphRead::NativeRuntime::UncheckedAdd;
using ::MphRead::NativeRuntime::UncheckedMultiply;
using ::MphRead::NativeRuntime::UncheckedSubtract;
using ::MphRead::TestFlag;
using ::OpenTK::Mathematics::AddY;
using ::OpenTK::Mathematics::CreateScale;
using ::OpenTK::Mathematics::Equal;
using ::OpenTK::Mathematics::Length;
using ::OpenTK::Mathematics::Multiply;
using ::OpenTK::Mathematics::Normalize;
using ::OpenTK::Mathematics::ScaleVector;
using ::OpenTK::Mathematics::SetRow3;

namespace
{
    using MphRead::Entities::EntityBase;
    using MphRead::Entities::PlatformEntity;
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    [[nodiscard]] MphRead::Formats::CollisionResult UnboxCollisionResult(
        const MphRead::MessageObject& value)
    {
        if (!value || !value->has_value())
        {
            throw MphRead::Memory::Detail::NullReferenceException();
        }
        try
        {
            return std::any_cast<MphRead::Formats::CollisionResult>(*value);
        }
        catch (const std::bad_any_cast&)
        {
            throw MphRead::Memory::Detail::InvalidCastException();
        }
    }

    [[nodiscard]] std::uint16_t IncrementUInt16(std::uint16_t value) noexcept
    {
        return static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) + 1U);
    }

    [[nodiscard]] Vector3 Row3(Matrix4 value) noexcept
    {
        return Vector3(value.M41, value.M42, value.M43);
    }

    [[nodiscard]] std::int32_t PlatformSfxAt(std::uint32_t modelId, std::int32_t index)
    {
        const auto data = MphRead::Metadata::PlatformSfx();
        const auto& rows = RequireReference(data);
        if (static_cast<std::size_t>(modelId) >= rows.size())
        {
            throw MphRead::Memory::Detail::IndexOutOfRangeException();
        }
        const auto& row = rows[static_cast<std::size_t>(modelId)];
        if (index < 0 || static_cast<std::size_t>(index) >= row.size())
        {
            throw MphRead::Memory::Detail::IndexOutOfRangeException();
        }
        return row[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] const MphRead::ModelMetadata& ModelMetadataAt(const std::string& name)
    {
        const auto it = MphRead::Metadata::ModelMetadata.find(name);
        if (it == MphRead::Metadata::ModelMetadata.end())
        {
            throw MphRead::SceneDetail::KeyNotFoundException();
        }
        return it->second;
    }

    [[nodiscard]] const MphRead::ModelMetadata& FirstHuntModelAt(const std::string& name)
    {
        const auto it = MphRead::Metadata::FirstHuntModels.find(name);
        if (it == MphRead::Metadata::FirstHuntModels.end())
        {
            throw MphRead::SceneDetail::KeyNotFoundException();
        }
        return it->second;
    }

    [[nodiscard]] std::int32_t AnimationIdAt(
        const MphRead::PlatformMetadata& meta, std::int32_t index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= meta.AnimationIds.size())
        {
            throw MphRead::Memory::Detail::ArgumentOutOfRangeException();
        }
        return meta.AnimationIds[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] MphRead::Entities::BeamProjectileEntity* CastBeam(EntityBase* entity)
    {
        if (entity == nullptr)
        {
            return nullptr;
        }
        auto* beam = dynamic_cast<MphRead::Entities::BeamProjectileEntity*>(entity);
        if (beam == nullptr)
        {
            throw MphRead::SceneDetail::InvalidCastException();
        }
        return beam;
    }

    [[nodiscard]] std::shared_ptr<PlatformEntity> CastPlatform(
        const std::shared_ptr<EntityBase>& entity)
    {
        if (!entity)
        {
            return nullptr;
        }
        auto platform = std::dynamic_pointer_cast<PlatformEntity>(entity);
        if (!platform)
        {
            throw MphRead::SceneDetail::InvalidCastException();
        }
        return platform;
    }
}

namespace MphRead::Entities
{
    std::shared_ptr<MphRead::BeamProjectileArray>
        PlatformEntity::_beams{};

    std::array<PlatformEntity::BeamSfxInfo, 4> PlatformEntity::_beamSfx{{
        BeamSfxInfo(140, PlatSfxFlags::None, 0.0F, 0),
        BeamSfxInfo(-1, PlatSfxFlags::None, 0.0F, 0),
        BeamSfxInfo(2, static_cast<PlatSfxFlags>(0x400 | 0x2000), Fixed::ToFloat(5310), 14),
        BeamSfxInfo(0, static_cast<PlatSfxFlags>(0x400 | 0x2000), 0.0F, 37)
    }};

    PlatformEntity::SfxData::SfxData(std::int32_t data) noexcept
        : Id(data & 0xC3FF),
          Flags(static_cast<PlatSfxFlags>(data & 0x3C00))
    {
    }

    PlatformEntity::SfxData::SfxData(
        std::int32_t id, PlatSfxFlags flags) noexcept
        : Id(id),
          Flags(flags)
    {
    }

    PlatformEntity::MoveSfxInfo::MoveSfxInfo(
        SfxData start1, SfxData start2, SfxData stop, SfxData destoryed) noexcept
        : Start1(start1),
          Start2(start2),
          Stop(stop),
          Destoryed(destoryed)
    {
    }

    PlatformEntity::BeamSfxInfo::BeamSfxInfo(
        std::int32_t id, PlatSfxFlags flags, float offset, std::int32_t rangeIndex) noexcept
        : Data(id, flags),
          Offset(offset),
          RangeIndex(rangeIndex)
    {
    }

    PlatformEntity::PlatformEntity(
        PlatformEntityData data, std::string nodeName, Scene* scene)
        : EntityBase(EntityType::Platform, std::move(nodeName), scene),
          _data(data)
    {
        Id = data.Header.EntityId;
        _flags = data.Flags;
        if (TestFlag(_flags, PlatformFlags::Breakable))
        {
            _flags |= PlatformFlags::BeamColEffect;
            _flags |= PlatformFlags::HideOnSleep;
            _flags |= PlatformFlags::BeamTarget;
        }

        _health = UInt32ToInt32(data.Health);
        if (TestFlag(_flags, PlatformFlags::SyluxShip))
        {
            _halfHealth = _health / 2;
        }
        Metadata::LoadEffectiveness(_data.Effectiveness, _beamEffectiveness);

        SetTransform(data.Header.FacingVector, data.Header.UpVector, data.Header.Position);
        _curPosition = Position;
        _posOffset = data.PositionOffset.ToFloatVector();

        for (std::int32_t i = 0; i < static_cast<std::int32_t>(data.PositionCount); ++i)
        {
            _posList.push_back(data.Positions[i].ToFloatVector());
        }
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(data.PositionCount); ++i)
        {
            _rotList.push_back(data.Rotations[i].ToFloatVector());
        }

        const PlatformMetadata* meta = Metadata::GetPlatformById(UInt32ToInt32(data.ModelId));
        if (meta == nullptr)
        {
            AddPlaceholderModel();
            _meta = &Metadata::InvisiblePlat;
        }
        else
        {
            _meta = meta;
            if (_meta->Lighting)
            {
                _anyLighting = true;
            }
            ModelInstance& inst = SetUpModel(_meta->Name);
            const ModelMetadata& modelMeta = ModelMetadataAt(_meta->Name);
            if (meta->Animation)
            {
                _animFlags |= PlatAnimFlags::HasAnim;
            }
            if (modelMeta.CollisionPath.has_value())
            {
                const auto collision = Formats::Collision::Collision::GetCollision(&modelMeta);
                SetCollision(collision, 0, &inst);
            }
        }

        if (EntityCollision[0] == nullptr)
        {
            const Matrix4 transform = GetTransform();
            auto entCol = std::make_shared<Formats::Collision::EntityCollision>(nullptr, this);
            EntityCollision[0] = entCol;
            UpdateCollisionTransform(0, transform);
            UpdateLinkedInverse(0);
        }

        _beamInterval = UncheckedMultiply(UInt32ToInt32(data.BeamInterval), 2);
        if (!_beams)
        {
            _beams = SceneSetup::CreateBeamList(64, scene);
        }
        if (data.BeamId > -1)
        {
            _ammo = 1000;
            assert(static_cast<std::size_t>(data.BeamId)
                < RequireReference(Weapons::PlatformWeapons).size());
            const auto weapon = ManagedListAt(
                RequireReference(Weapons::PlatformWeapons), data.BeamId);
            _equipInfo = std::make_shared<EquipInfo>(weapon, _beams);
            _equipInfo->SetGetAmmo([this]() { return _ammo; });
            _equipInfo->SetSetAmmo([this](std::int32_t newAmmo) { _ammo = newAmmo; });
            _beamSpawnPos = data.BeamSpawnPos.ToFloatVector();
            _beamSpawnDir = data.BeamSpawnDir.ToFloatVector();
            _beamIntervalIndex = 15;
        }

        _delay = static_cast<std::int32_t>(data.Delay) * 2;
        _moveTimer = _delay;
        _recoilTimer = 0;
        _forwardSpeed = data.ForwardSpeed.FloatValue() / 2.0F;
        _backwardSpeed = data.BackwardSpeed.FloatValue() / 2.0F;
        UpdatePosition();

        _soundSource.Volume = 0.0F;
        _moveSfx = MoveSfxInfo(
            SfxData(PlatformSfxAt(_data.ModelId, 0)),
            SfxData(PlatformSfxAt(_data.ModelId, 1)),
            SfxData(PlatformSfxAt(_data.ModelId, 2)),
            SfxData(PlatformSfxAt(_data.ModelId, 3)));

        _animFlags |= PlatAnimFlags::Draw;
        assert(GameState::Mode() == GameMode::SinglePlayer);

        if (TestFlag(_flags, PlatformFlags::UseRoomState)
            && !TestFlag(_flags, PlatformFlags::PersistRoomState))
        {
            const std::int32_t state = RequireReference(::MphRead::GameState::StorySave).GetRoomState(
                RequireReference(scene).RoomId(), Id);
            if (state == 1)
            {
                _fromIndex = static_cast<std::int32_t>(_data.PositionCount) - 1;
                UpdatePosition();
            }
            else if (state == 2)
            {
                _animFlags &= ~PlatAnimFlags::Draw;
            }
        }

        if (TestFlag(_flags, PlatformFlags::SamusShip) && !Cheats::SkipPlanetIntros())
        {
            SleepWake(true, true);
            _currentAnimState = -2;
            if (RequireReference(::MphRead::GameState::StorySave).CheckVisitedRoom(RequireReference(scene).RoomId()))
            {
                SetPlatAnimation(PlatAnimId::InstantWake, AnimFlags::None);
            }
            else
            {
                SetPlatAnimation(PlatAnimId::Wake, AnimFlags::NoLoop);
                _currentAnimState = GetAnimation(PlatAnimId::InstantWake);
            }
            if (data.Active != 0)
            {
                _animFlags |= PlatAnimFlags::Active;
            }
        }
        else
        {
            if (TestFlag(_flags, PlatformFlags::StartSleep))
            {
                SleepWake(false, true);
            }
            else
            {
                SleepWake(true, true);
            }

            if (TestFlag(_flags, PlatformFlags::PersistRoomState))
            {
                if (RequireReference(::MphRead::GameState::StorySave).InitRoomState(
                    RequireReference(_scene).RoomId(), Id, _data.Active != 0) != 0)
                {
                    _animFlags |= PlatAnimFlags::Active;
                }
            }
            else if (_data.Active != 0)
            {
                _animFlags |= PlatAnimFlags::Active;
            }

            if (TestFlag(_animFlags, PlatAnimFlags::Active))
            {
                Activate();
            }
            else
            {
                Deactivate();
            }

            _currentAnimState = -2;
            if (TestFlag(_animFlags, PlatAnimFlags::HasAnim))
            {
                if (TestFlag(_stateFlags, PlatStateFlags::Awake))
                {
                    SetPlatAnimation(PlatAnimId::InstantWake, AnimFlags::None);
                }
                else
                {
                    SetPlatAnimation(PlatAnimId::InstantSleep, AnimFlags::None);
                }
            }
        }

        _sfxRangeIndex = 14;
        if (_data.ModelId == 7)
        {
            _sfxRangeIndex = 26;
        }
        else if (_data.ModelId == 34)
        {
            _sfxRangeIndex = 33;
        }
    }

    PlatformEntityData PlatformEntity::Data() const
    {
        return _data;
    }

    PlatformFlags PlatformEntity::Flags() const noexcept
    {
        return _flags;
    }

    PlatStateFlags PlatformEntity::StateFlags() const noexcept
    {
        return _stateFlags;
    }

    Vector3 PlatformEntity::Velocity() const noexcept
    {
        return _velocity;
    }

    std::optional<Vector4> PlatformEntity::OverrideColor() const
    {
        return _overrideColor;
    }

    void PlatformEntity::DestroyBeams()
    {
        if (_beams)
        {
            _beams->ReleaseReferences();
        }
        _beams.reset();
    }

    void PlatformEntity::Initialize()
    {
        EntityBase::Initialize();

        if (TestFlag(_flags, PlatformFlags::SamusShip))
        {
            ModelInstance& instance = _models[0];
            Model& model = RequireReference(instance.Model());
            _effectNodeIds[0] = model.GetNodeIndexByName("R_Turret");
            _effectNodeIds[1] = model.GetNodeIndexByName("R_Turret1");
            _effectNodeIds[2] = model.GetNodeIndexByName("R_Turret2");
            _effectNodeIds[3] = model.GetNodeIndexByName("R_Turret3");
            if (_effectNodeIds[0] != -1
                || _effectNodeIds[1] != -1
                || _effectNodeIds[2] != -1
                || _effectNodeIds[3] != -1)
            {
                RequireReference(_scene).LoadEffect(_nozzleEffectId, false);
            }
        }

        if (_data.ResistEffectId != 0)
        {
            RequireReference(_scene).LoadEffect(_data.ResistEffectId, false);
        }
        if (_data.DamageEffectId != 0)
        {
            RequireReference(_scene).LoadEffect(_data.DamageEffectId, false);
        }
        if (_data.DeadEffectId != 0)
        {
            RequireReference(_scene).LoadEffect(_data.DeadEffectId, false);
        }
        if (_data.BeamId == 0 && TestFlag(_flags, PlatformFlags::BeamSpawner))
        {
            RequireReference(_scene).LoadEffect(183, false);
            RequireReference(_scene).LoadEffect(184, false);
            RequireReference(_scene).LoadEffect(185, false);
        }

        std::shared_ptr<EntityBase> target{};
        (void)RequireReference(_scene).TryGetEntity(_data.ScanMsgTarget, target);
        _scanMessageTarget = target;
        target.reset();
        (void)RequireReference(_scene).TryGetEntity(_data.BeamHitMsgTarget, target);
        _hitMessageTarget = target;
        target.reset();
        (void)RequireReference(_scene).TryGetEntity(_data.PlayerColMsgTarget, target);
        _playerColMessageTarget = target;
        target.reset();
        (void)RequireReference(_scene).TryGetEntity(_data.DeadMsgTarget, target);
        _deathMessageTarget = target;

        std::shared_ptr<EntityBase> lifetimeTarget{};
        (void)RequireReference(_scene).TryGetEntity(_data.LifetimeMsg1Target, lifetimeTarget);
        _lifetimeMessageTargets[0] = lifetimeTarget;
        lifetimeTarget.reset();
        (void)RequireReference(_scene).TryGetEntity(_data.LifetimeMsg2Target, lifetimeTarget);
        _lifetimeMessageTargets[1] = lifetimeTarget;
        lifetimeTarget.reset();
        (void)RequireReference(_scene).TryGetEntity(_data.LifetimeMsg3Target, lifetimeTarget);
        _lifetimeMessageTargets[2] = lifetimeTarget;
        lifetimeTarget.reset();
        (void)RequireReference(_scene).TryGetEntity(_data.LifetimeMsg4Target, lifetimeTarget);
        _lifetimeMessageTargets[3] = lifetimeTarget;

        _lifetimeMessages[0] = _data.LifetimeMessage1;
        _lifetimeMessages[1] = _data.LifetimeMessage2;
        _lifetimeMessages[2] = _data.LifetimeMessage3;
        _lifetimeMessages[3] = _data.LifetimeMessage4;
        _lifetimeMessageParam1s[0] = _data.LifetimeMsg1Param1;
        _lifetimeMessageParam1s[1] = _data.LifetimeMsg2Param1;
        _lifetimeMessageParam1s[2] = _data.LifetimeMsg3Param1;
        _lifetimeMessageParam1s[3] = _data.LifetimeMsg4Param1;
        _lifetimeMessageParam2s[0] = _data.LifetimeMsg1Param2;
        _lifetimeMessageParam2s[1] = _data.LifetimeMsg2Param2;
        _lifetimeMessageParam2s[2] = _data.LifetimeMsg3Param2;
        _lifetimeMessageParam2s[3] = _data.LifetimeMsg4Param2;
        _lifetimeMessageIndices[0] = _data.LifetimeMsg1Index;
        _lifetimeMessageIndices[1] = _data.LifetimeMsg2Index;
        _lifetimeMessageIndices[2] = _data.LifetimeMsg3Index;
        _lifetimeMessageIndices[3] = _data.LifetimeMsg4Index;

        if (_data.ParentId != -1)
        {
            std::shared_ptr<EntityBase> parent{};
            if (RequireReference(_scene).TryGetEntity(_data.ParentId, parent)
                && RequireReference(parent).Type == EntityType::Platform)
            {
                _parent = CastPlatform(parent);
                _parentEntCol = RequireReference(_parent).EntityCollision[0];
            }
        }

        const Matrix4 transform = GetTransform();
        _visiblePosition = Row3(transform);
    }

    void PlatformEntity::Destroy()
    {
        _soundSource.StopAllSfx(true);
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(_effects.size()); ++i)
        {
            const auto& effect = _effects[static_cast<std::size_t>(i)];
            if (effect)
            {
                RequireReference(_scene).UnlinkEffectEntry(effect);
            }
        }
        EntityBase::Destroy();
    }

    void PlatformEntity::GetPosition(Vector3& position)
    {
        Vector3 up{};
        Vector3 facing{};
        GetVectors(position, up, facing);
    }

    void PlatformEntity::GetVectors(Vector3& position, Vector3& up, Vector3& facing)
    {
        if (TestFlag(_flags, PlatformFlags::SamusShip))
        {
            const Matrix4 transform = GetTransform();
            const Vector3 offset = Matrix::Vec3MultMtx3(
                Vector3(0.0F, 0.8F, 4.2F), transform);
            position = Row3(transform) + offset;
        }
        else if (TestFlag(_flags, PlatformFlags::SyluxShip) && _parentEntCol != nullptr)
        {
            const Matrix4 transform = GetTransform();
            position = Matrix::Vec3MultMtx4(_beamSpawnPos, transform);
        }
        else
        {
            position = _visiblePosition;
        }
        up = UpVector();
        facing = FacingVector();
    }

    std::int32_t PlatformEntity::GetScanId(bool alternate)
    {
        static_cast<void>(alternate);
        bool awake = TestFlag(_stateFlags, PlatStateFlags::Awake);
        if (TestFlag(_flags, PlatformFlags::SyluxShip)
            && _parentEntCol != nullptr && _parent != nullptr)
        {
            if (!TestFlag(_parent->StateFlags(), PlatStateFlags::Awake)
                && !TestFlag(_parent->StateFlags(), PlatStateFlags::WasAwake))
            {
                awake = false;
            }
        }
        return awake
            ? static_cast<std::int32_t>(_data.ScanData1)
            : static_cast<std::int32_t>(_data.ScanData2);
    }

    void PlatformEntity::OnScanned()
    {
        if (_data.ScanMessage != Message::None
            && _scanMessageTarget != nullptr
            && !RequireReference(::MphRead::GameState::StorySave).CheckLogbook(GetScanId()))
        {
            RequireReference(_scene).SendMessage(
                _data.ScanMessage, this, _scanMessageTarget.get(),
                BoxInt32(-1), BoxInt32(0));
        }
    }

    void PlatformEntity::SetPlatAnimation(PlatAnimId id, AnimFlags flags)
    {
        SetPlatAnimation(
            AnimationIdAt(RequireReference(_meta), static_cast<std::int32_t>(id)),
            flags);
    }

    void PlatformEntity::SetPlatAnimation(std::int32_t index, AnimFlags flags)
    {
        _currentAnimId = index;
        if (index >= 0)
        {
            assert(!_models[0].IsPlaceholder);
            _models[0].SetAnimation(index, flags);
        }
    }

    std::int32_t PlatformEntity::GetAnimation(PlatAnimId id) const
    {
        return AnimationIdAt(
            RequireReference(_meta), static_cast<std::int32_t>(id));
    }

    void PlatformEntity::SleepWake(bool wake, bool instant)
    {
        if (wake)
        {
            _stateFlags |= PlatStateFlags::Awake;
            if (instant)
            {
                SetPlatAnimation(PlatAnimId::InstantWake, AnimFlags::None);
            }
            else
            {
                SetPlatAnimation(PlatAnimId::Wake, AnimFlags::NoLoop);
                _currentAnimState = GetAnimation(PlatAnimId::InstantWake);
            }
        }
        else
        {
            if (TestFlag(_stateFlags, PlatStateFlags::Awake))
            {
                _stateFlags |= PlatStateFlags::WasAwake;
            }
            _stateFlags &= ~PlatStateFlags::Awake;
            if (instant)
            {
                SetPlatAnimation(PlatAnimId::InstantSleep, AnimFlags::None);
            }
            else
            {
                SetPlatAnimation(PlatAnimId::Sleep, AnimFlags::NoLoop);
                _currentAnimState = GetAnimation(PlatAnimId::InstantSleep);
            }

            if (TestFlag(_flags, PlatformFlags::HideOnSleep))
            {
                for (std::size_t i = 0; i < EntityCollision.size(); ++i)
                {
                    const auto& entCol = EntityCollision[i];
                    if (entCol != nullptr && entCol->Collision != nullptr)
                    {
                        entCol->Collision->Active = false;
                    }
                }
                _animFlags &= ~PlatAnimFlags::Draw;
                if (TestFlag(_flags, PlatformFlags::BeamSpawner))
                {
                    _soundSource.StopAllSfx();
                }
                RequireReference(::MphRead::GameState::StorySave).SetRoomState(
                    RequireReference(_scene).RoomId(), Id, 3);
            }
        }
    }

    void PlatformEntity::Activate()
    {
        _animFlags |= PlatAnimFlags::Active;
        if (TestFlag(_flags, PlatformFlags::UseRoomState)
            && TestFlag(_flags, PlatformFlags::PersistRoomState))
        {
            RequireReference(::MphRead::GameState::StorySave).SetRoomState(
                RequireReference(_scene).RoomId(), Id, 3);
        }

        if (_state == PlatformState::Inactive)
        {
            if (TestFlag(_flags, PlatformFlags::DripMoat))
            {
                RequireReference(_scene).SendMessage(
                    Message::DripMoatPlatform, this, PlayerEntity::Main().get(),
                    BoxInt32(1), BoxInt32(0));
            }
            if (_data.PositionCount >= 2)
            {
                UpdateMovement();
                _state = PlatformState::Waiting;
                _moveTimer = 0;
                _stateFlags |= PlatStateFlags::Activated;
                if (_fromIndex == static_cast<std::int32_t>(_data.PositionCount) - 1)
                {
                    _stateFlags |= PlatStateFlags::Reverse;
                }
                else
                {
                    _stateFlags &= ~PlatStateFlags::Reverse;
                }

                if (_data.NoPort != 0)
                {
                    SfxData startSfx = _moveSfx.Start1;
                    if (_posList.size() == 2 && _data.ForCutscene == 1)
                    {
                        startSfx = _moveSfx.Start2;
                    }
                    PlaySfx(startSfx);
                }
            }
        }
    }

    void PlatformEntity::Deactivate()
    {
        if (_state != PlatformState::Inactive)
        {
            _state = PlatformState::Inactive;
            _animFlags &= ~PlatAnimFlags::Active;
            if (TestFlag(_flags, PlatformFlags::UseRoomState)
                && TestFlag(_flags, PlatformFlags::PersistRoomState))
            {
                RequireReference(::MphRead::GameState::StorySave).SetRoomState(
                    RequireReference(_scene).RoomId(), Id, 1);
            }
            if (TestFlag(_flags, PlatformFlags::DripMoat))
            {
                RequireReference(_scene).SendMessage(
                    Message::DripMoatPlatform, this, PlayerEntity::Main().get(),
                    BoxInt32(0), BoxInt32(0));
            }
        }
    }

    void PlatformEntity::SetActive(bool active)
    {
        if (active)
        {
            _stateFlags |= PlatStateFlags::Activated;
            Activate();
        }
        else
        {
            _stateFlags &= ~PlatStateFlags::Activated;
            Deactivate();
        }
    }

    void PlatformEntity::PlaySfx(SfxData data)
    {
        const bool environment = TestFlag(data.Flags, PlatSfxFlags::Environment);
        if ((data.Id == 0 && !environment) || (data.Id & 0x8000) != 0)
        {
            return;
        }

        float recency = -1.0F;
        const bool sourceOnly = true;
        const bool loop = TestFlag(data.Flags, PlatSfxFlags::Loop) || environment;
        if (!loop)
        {
            recency = std::numeric_limits<float>::max();
        }
        const bool noUpdate = TestFlag(data.Flags, PlatSfxFlags::NoUpdate);
        if (environment)
        {
            _soundSource.PlayEnvironmentSfx(data.Id);
        }
        else
        {
            _soundSource.PlaySfx(data.Id, loop, noUpdate, recency, sourceOnly);
        }
    }

    bool PlatformEntity::Process()
    {
        UpdateLinkedInverse(0);
        _prevVisiblePosition = _visiblePosition;

        _timeSincePlayerCol = IncrementUInt16(_timeSincePlayerCol);
        if (_timeSincePlayerCol >= 3 * 2)
        {
            _timeSincePlayerCol = 3 * 2;
            _playerCol = false;
        }

        if (!TestFlag(_animFlags, PlatAnimFlags::DisableReflect))
        {
            bool isTurret = false;
            bool turretAiming = false;
            if (TestFlag(_flags, PlatformFlags::SyluxShip) && _parentEntCol != nullptr)
            {
                isTurret = true;
                if (TestFlag(_stateFlags, PlatStateFlags::Awake)
                    && (*_models[0].AnimInfo->Index)[0]
                        == GetAnimation(PlatAnimId::InstantWake))
                {
                    turretAiming = true;
                }
            }

            if (TestFlag(_flags, PlatformFlags::SyluxShip)
                && (isTurret
                    || TestFlag(_stateFlags, PlatStateFlags::Awake)
                    || TestFlag(_stateFlags, PlatStateFlags::WasAwake)))
            {
                Vector3 target = Vector3::Zero;
                if (isTurret)
                {
                    assert(_parentEntCol != nullptr);
                    if (turretAiming)
                    {
                        PlayerEntity& mainPlayer = RequireReference(PlayerEntity::Main());
                        target = Vector3(
                            mainPlayer.Position.X - _visiblePosition.X,
                            mainPlayer.Position.Y + 1.0F - _visiblePosition.Y,
                            mainPlayer.Position.Z - _visiblePosition.Z);
                    }
                    else
                    {
                        target = Matrix::Vec3MultMtx3(
                            Vector3(0.0F, 0.0F, 1.0F), _parentEntCol->Transform);
                    }
                    target = target.Normalized();
                }
                else
                {
                    PlayerEntity& mainPlayer = RequireReference(PlayerEntity::Main());
                    target = Vector3(
                        mainPlayer.Position.X - _visiblePosition.X,
                        0.0F,
                        mainPlayer.Position.Z - _visiblePosition.Z).Normalized();
                }

                const Vector3 cross1 = Vector3::Cross(
                    Vector3(0.0F, 1.0F, 0.0F), target).Normalized();
                const Vector3 cross2 = Vector3::Cross(target, cross1).Normalized();
                Vector4 rotation = ChooseVectors(cross1, cross2, target);
                if (!_models[0].IsPlaceholder)
                {
                    const float pct = Fixed::ToFloat(
                        _parentEntCol == nullptr ? 64 : 256) / 2.0F;
                    rotation = ComputeRotationSin(_curRotation, rotation, pct);
                }
                _curRotation = rotation;

                if (_data.MovementType == 0)
                {
                    UpdateState();
                    _curPosition = _curPosition + _velocity;
                }
            }
            else
            {
                if (_data.MovementType == 1)
                {
                    _curPosition = _curPosition + _velocity;
                    _movePercent += _moveIncrement;
                    _curRotation = ComputeRotationLinear(
                        _fromRotation, _toRotation, _movePercent);
                }
                else if (_data.MovementType == 0)
                {
                    UpdateState();
                    _curPosition = _curPosition + _velocity;
                    _movePercent += _moveIncrement;
                    _curRotation = ComputeRotationSin(
                        _fromRotation, _toRotation, _movePercent);
                }

                if (TestFlag(_animFlags, PlatAnimFlags::SeekPlayerHeight)
                    && PlayerEntity::PlayerCount() > 0)
                {
                    PlayerEntity& mainPlayer = RequireReference(PlayerEntity::Main());
                    const float offset
                        = (mainPlayer.Position.Y - _curPosition.Y) * Fixed::ToFloat(20);
                    _curPosition.Y += offset;
                }
            }
        }

        _visiblePosition = Row3(GetTransform());

        bool spawnBeam = true;
        if (!_models[0].IsPlaceholder && TestFlag(_flags, PlatformFlags::SyluxShip))
        {
            if (_currentAnimState != -2)
            {
                spawnBeam = false;
            }
            else if (_parent != nullptr
                && !TestFlag(_parent->StateFlags(), PlatStateFlags::Awake)
                && !TestFlag(_parent->StateFlags(), PlatStateFlags::WasAwake))
            {
                spawnBeam = false;
            }
        }
        if (TestFlag(_flags, PlatformFlags::NoBeamIfCull) && !IsVisible(NodeRef))
        {
            spawnBeam = false;
        }

        bool soundUpdated = false;
        if (spawnBeam
            && TestFlag(_animFlags, PlatAnimFlags::Draw)
            && !TestFlag(_animFlags, PlatAnimFlags::DisableReflect)
            && TestFlag(_stateFlags, PlatStateFlags::Awake)
            && TestFlag(_flags, PlatformFlags::BeamSpawner)
            && _data.BeamId > -1)
        {
            _beamIntervalTimer = UncheckedAdd(_beamIntervalTimer, -1);
            if (_beamIntervalTimer <= 0)
            {
                _beamIntervalIndex = UncheckedAdd(_beamIntervalIndex, 1);
                _beamIntervalIndex %= 16;
                _beamActive = (_data.BeamOnIntervals
                    & (std::uint32_t{1}
                        << static_cast<std::uint32_t>(_beamIntervalIndex))) != 0;
                if (!_beamActive)
                {
                    _soundSource.StopAllSfx();
                }
                _beamIntervalTimer = _beamInterval;
            }

            if (_beamActive)
            {
                EquipInfo& equip = RequireReference(_equipInfo);
                const Matrix4 transform = GetTransform();
                const Vector3 spawnPos = Matrix::Vec3MultMtx4(
                    _beamSpawnPos, transform);
                const Vector3 spawnDir = Matrix::Vec3MultMtx3(
                    _beamSpawnDir, transform).Normalized();

                BeamSpawnFlags spawnFlags = BeamSpawnFlags::None;
                const auto weapon = equip.Weapon;
                if (TestFlag(RequireReference(weapon).Flags, WeaponFlags::Continuous))
                {
                    spawnFlags = BeamSpawnFlags::DestroyMuzzle;
                }

                (void)BeamProjectileEntity::Spawn(
                    SharedFrom<EntityBase>(this), _equipInfo,
                    spawnPos, spawnDir, spawnFlags, NodeRef, _scene);

                const BeamSfxInfo& sfxInfo = ManagedAt(_beamSfx, _data.BeamId);
                if (sfxInfo.Data.Id != -1)
                {
                    const Vector3 sfxPos = spawnPos + ScaleVector(spawnDir, sfxInfo.Offset);
                    _soundSource.Update(sfxPos, sfxInfo.RangeIndex);
                    PlaySfx(sfxInfo.Data);
                    soundUpdated = true;
                }

                if (!TestFlag(
                    RequireReference(weapon).Flags, WeaponFlags::RepeatFire))
                {
                    _beamActive = false;
                }
            }
        }

        if (!TestFlag(_flags, PlatformFlags::SkipNodeRef)
            && NodeRef != Formats::Culling::NodeRef::None)
        {
            NodeRef = RequireReference(_scene).UpdateNodeRef(
                NodeRef, _prevVisiblePosition, _visiblePosition);
        }

        if (!_models[0].IsPlaceholder
            && TestFlag(_animFlags, PlatAnimFlags::HasAnim)
            && _currentAnimId >= 0)
        {
            UpdateAnimFrames(_models[0]);
        }

        if (_currentAnimState != -2
            && TestFlag((*_models[0].AnimInfo->Flags)[0], AnimFlags::Ended))
        {
            SetPlatAnimation(_currentAnimState, AnimFlags::None);
            _currentAnimState = -2;
            _stateFlags &= ~PlatStateFlags::WasAwake;
        }

        if (!soundUpdated)
        {
            _soundSource.Update(_visiblePosition, _sfxRangeIndex);
            if (_data.ModelId == 5)
            {
                _soundSource.Volume = 1.0F;
            }
            else
            {
                UpdateNodeRefVolume();
            }
        }

        Model& model = RequireReference(_models[0].Model());
        for (std::int32_t i = 0; i < 4; ++i)
        {
            auto effect = _effects[static_cast<std::size_t>(i)];
            if (_effectNodeIds[static_cast<std::size_t>(i)] >= 0 && !effect)
            {
                const Matrix4 transform = Matrix::GetTransform4(
                    Vector3(1.0F, 0.0F, 0.0F),
                    Vector3(0.0F, 1.0F, 0.0F),
                    Vector3(0.0F, 2.0F, 0.0F));
                effect = RequireReference(_scene).SpawnEffectGetEntry(
                    _nozzleEffectId, transform);
            }

            if (effect)
            {
                effect->SetElementExtension(true);
                const auto& nodes = RequireReference(model.Nodes);
                const std::int32_t nodeIndex
                    = _effectNodeIds[static_cast<std::size_t>(i)];
                if (nodeIndex < 0
                    || static_cast<std::size_t>(nodeIndex) >= nodes.size())
                {
                    throw Memory::Detail::IndexOutOfRangeException();
                }
                Node& node = RequireReference(nodes[static_cast<std::size_t>(nodeIndex)]);
                Matrix4 transform = node.Animation;
                const Vector3 position(
                    transform.M31 * 1.5F + transform.M41,
                    transform.M32 * 1.5F + transform.M42,
                    transform.M33 * 1.5F + transform.M43);
                transform = Matrix::GetTransform4(
                    Vector3(transform.M21, transform.M22, transform.M23),
                    Vector3(transform.M31, transform.M32, transform.M33),
                    position);
                effect->Transform(position, transform);
                _effects[static_cast<std::size_t>(i)] = effect;
            }
        }

        if (_data.PositionCount > 0)
        {
            Transform = GetTransform();
        }

        if (TestFlag(_animFlags, PlatAnimFlags::WasDrawn) && _colAttachNode != nullptr)
        {
            UpdateCollisionTransform(0, _colAttachNode->Animation);
        }
        else
        {
            UpdateCollisionTransform(0, Transform);
        }
        return true;
    }

    void PlatformEntity::GetDrawInfo()
    {
        _animFlags &= ~PlatAnimFlags::WasDrawn;

        if (_models[0].IsPlaceholder)
        {
            EntityBase::GetDrawInfo();
        }
        else if (TestFlag(_animFlags, PlatAnimFlags::Draw))
        {
            bool draw = false;
            if (TestFlag(_flags, PlatformFlags::DrawAlways))
            {
                draw = true;
            }
            else if (IsVisible(NodeRef))
            {
                draw = true;
            }

            if (TestFlag(_flags, PlatformFlags::SyluxShip)
                && _parentEntCol != nullptr)
            {
                assert(_parent != nullptr);
                if (!TestFlag(RequireReference(_parent).StateFlags(), PlatStateFlags::Awake)
                    && !TestFlag(
                        RequireReference(_parent).StateFlags(), PlatStateFlags::WasAwake))
                {
                    draw = false;
                }
            }

            if (TestFlag(_animFlags, PlatAnimFlags::HasAnim)
                && _currentAnimId < 0)
            {
                draw = false;
            }

            if (draw)
            {
                EntityBase::GetDrawInfo();
                _animFlags |= PlatAnimFlags::WasDrawn;
            }

            if (TestFlag(_flags, PlatformFlags::SamusShip))
            {
                for (std::int32_t i = 0; i < 4; ++i)
                {
                    const auto& effect = _effects[static_cast<std::size_t>(i)];
                    if (effect)
                    {
                        effect->SetDrawEnabled(draw);
                    }
                }
            }
        }
    }

    void PlatformEntity::UpdatePosition()
    {
        if (_data.PositionCount > 0)
        {
            _curPosition = ManagedListAt(_posList, _fromIndex);
            _curRotation = ManagedListAt(_rotList, _fromIndex);
            _fromRotation = _curRotation;
        }
    }

    void PlatformEntity::UpdateMovement()
    {
        UpdatePosition();
        const Vector3 velocity
            = ManagedListAt(_posList, _toIndex) - ManagedListAt(_posList, _fromIndex);
        const float speed = TestFlag(_stateFlags, PlatStateFlags::Reverse)
            ? _backwardSpeed
            : _forwardSpeed;

        float factor;
        if (_data.ForCutscene != 0)
        {
            _moveTimer = ConvertToInt32Net9(30.0F / (speed * 2.0F));
            factor = 1.0F / static_cast<float>(UncheckedAdd(_moveTimer, 1));
            _moveTimer = UncheckedMultiply(_moveTimer, 2);
            factor /= 2.0F;
        }
        else
        {
            const float distance = Length(velocity);
            if (distance == 0.0F)
            {
                factor = 0.0F;
            }
            else
            {
                _moveTimer = ConvertToInt32Net9(distance / speed);
                factor = speed / distance;
            }
        }

        _velocity = ScaleVector(velocity, factor);
        _toRotation = ManagedListAt(_rotList, _toIndex);
        _movePercent = 0.0F;
        _moveIncrement = factor;
    }

    Vector4 PlatformEntity::ChooseVectors(
        Vector3 vec1, Vector3 vec2, Vector3 vec3)
    {
        float sqrt;
        float inv;
        if (vec3.Z + vec1.X + vec2.Y >= 0.0F)
        {
            sqrt = std::sqrt(vec3.Z + vec1.X + vec2.Y + 1.0F);
            inv = 1.0F / sqrt / 2.0F;
            return Vector4(
                (vec2.Z - vec3.Y) * inv,
                (vec3.X - vec1.Z) * inv,
                (vec1.Y - vec2.X) * inv,
                sqrt / 2.0F);
        }

        if (vec2.Y <= vec1.X)
        {
            if (vec3.Z <= vec1.X)
            {
                sqrt = std::sqrt(vec1.X - (vec2.Y + vec3.Z) + 1.0F);
                inv = 1.0F / sqrt / 2.0F;
                return Vector4(
                    sqrt / 2.0F,
                    (vec2.X + vec1.Y) * inv,
                    (vec1.Z + vec3.X) * inv,
                    (vec2.Z - vec3.Y) * inv);
            }
        }
        else if (vec3.Z <= vec2.Y)
        {
            sqrt = std::sqrt(vec2.Y - (vec3.Z + vec1.X) + 1.0F);
            inv = 1.0F / sqrt / 2.0F;
            return Vector4(
                (vec2.X + vec1.Y) * inv,
                sqrt / 2.0F,
                (vec3.Y + vec2.Z) * inv,
                (vec3.X - vec1.Z) * inv);
        }

        sqrt = std::sqrt(vec3.Z - (vec1.X + vec2.Y) + 1.0F);
        inv = 1.0F / sqrt / 2.0F;
        return Vector4(
            (vec1.Z + vec3.X) * inv,
            (vec3.Y + vec2.Z) * inv,
            sqrt / 2.0F,
            (vec1.Y - vec2.X) * inv);
    }

    Vector4 PlatformEntity::ComputeRotationLinear(
        Vector4 fromRot, Vector4 toRot, float pct)
    {
        if (pct <= 0.0F)
        {
            return fromRot;
        }
        if (pct >= 1.0F)
        {
            return toRot;
        }
        return Vector4(
            ((1.0F - pct) * fromRot.X) + (pct * toRot.X),
            ((1.0F - pct) * fromRot.Y) + (pct * toRot.Y),
            ((1.0F - pct) * fromRot.Z) + (pct * toRot.Z),
            ((1.0F - pct) * fromRot.W) + (pct * toRot.W));
    }

    Vector4 PlatformEntity::ComputeRotationSin(
        Vector4 fromRot, Vector4 toRot, float pct)
    {
        pct = std::clamp(pct, 0.0F, 1.0F);
        float dot = Vector4::Dot(fromRot, toRot);
        const bool negDot = dot < 0.0F;
        dot = std::fabs(dot);

        float factor;
        if (1.0F - dot >= Fixed::ToFloat(16))
        {
            const float angle1
                = std::atan(std::sqrt((1.0F - dot) / (dot + 1.0F))) * 2.0F;
            const float angle2 = pct * angle1;
            const float sin = std::sin(angle1);
            factor = std::sin(angle1 - angle2) / sin;
            pct = std::sin(angle2) / sin;
        }
        else
        {
            factor = 1.0F - pct;
        }

        if (negDot)
        {
            pct *= -1.0F;
        }

        return Normalize(Vector4(
            factor * fromRot.X + pct * toRot.X,
            factor * fromRot.Y + pct * toRot.Y,
            factor * fromRot.Z + pct * toRot.Z,
            factor * fromRot.W + pct * toRot.W));
    }

    void PlatformEntity::Recoil()
    {
        if (_state == PlatformState::Moving
            && !TestFlag(_flags, PlatformFlags::NoRecoil)
            && _data.ForCutscene == 0)
        {
            _recoilTimer = 31 * 2;
            _moveTimer = UncheckedAdd(_moveTimer, 60 * 2);
            _velocity.Y *= -1.0F;
        }
    }

    void PlatformEntity::UpdateState()
    {
        bool cutscene = false;
        if (_posList.size() == 2 && _data.ForCutscene == 1)
        {
            cutscene = true;
        }

        if (_state == PlatformState::Moving)
        {
            if (_recoilTimer > 0)
            {
                _recoilTimer = UncheckedAdd(_recoilTimer, -1);
                if (_recoilTimer == 0)
                {
                    _velocity.Y *= -1.0F;
                }
            }

            if (_moveTimer > 0)
            {
                _moveTimer = UncheckedAdd(_moveTimer, -1);
                const SfxData startSfx = _moveSfx.Start1;
                if (TestFlag(startSfx.Flags, PlatSfxFlags::Environment))
                {
                    PlaySfx(startSfx);
                }
            }
            else
            {
                const bool reverse = TestFlag(_stateFlags, PlatStateFlags::Reverse);
                const std::int32_t count = static_cast<std::int32_t>(_posList.size());
                if (_data.ModelId != 5
                    || (!reverse && _fromIndex >= count - 2)
                    || (reverse && _fromIndex <= 1))
                {
                    const SfxData startSfx
                        = cutscene ? _moveSfx.Start2 : _moveSfx.Start1;
                    if (TestFlag(startSfx.Flags, PlatSfxFlags::Loop)
                        && (startSfx.Id & 0x8000) == 0)
                    {
                        _soundSource.StopAllSfx();
                    }
                    if (!cutscene)
                    {
                        PlaySfx(_moveSfx.Stop);
                    }
                }

                _fromIndex = _toIndex;
                for (std::int32_t i = 0;
                    i < static_cast<std::int32_t>(_lifetimeMessages.size()); ++i)
                {
                    if (_lifetimeMessageIndices[static_cast<std::size_t>(i)]
                        == _fromIndex)
                    {
                        const Message message
                            = _lifetimeMessages[static_cast<std::size_t>(i)];
                        const auto& target
                            = _lifetimeMessageTargets[static_cast<std::size_t>(i)];
                        if (message != Message::None && target != nullptr)
                        {
                            RequireReference(_scene).SendMessage(
                                message, this, target.get(),
                                BoxInt32(_lifetimeMessageParam1s[static_cast<std::size_t>(i)]),
                                BoxInt32(_lifetimeMessageParam2s[static_cast<std::size_t>(i)]));
                        }
                    }
                }

                if (TestFlag(_flags, PlatformFlags::UseRoomState)
                    && !TestFlag(_flags, PlatformFlags::PersistRoomState))
                {
                    if (_fromIndex == 0)
                    {
                        RequireReference(::MphRead::GameState::StorySave).SetRoomState(
                            RequireReference(_scene).RoomId(), Id, 1);
                    }
                    else if (_fromIndex
                        == static_cast<std::int32_t>(_data.PositionCount) - 1)
                    {
                        RequireReference(::MphRead::GameState::StorySave).SetRoomState(
                            RequireReference(_scene).RoomId(), Id, 2);
                    }
                }

                if (_state != PlatformState::Inactive)
                {
                    _state = PlatformState::Waiting;
                    _moveTimer = _delay;
                }
                _velocity = Vector3::Zero;
                _moveIncrement = 0.0F;
                _movePercent = 0.0F;
                UpdatePosition();
            }
        }
        else if (_state == PlatformState::Waiting)
        {
            if (_moveTimer > 0)
            {
                _moveTimer = UncheckedAdd(_moveTimer, -1);
            }
            else
            {
                if (TestFlag(_stateFlags, PlatStateFlags::Activated))
                {
                    if (_data.ReverseType == 0)
                    {
                        if (TestFlag(_stateFlags, PlatStateFlags::Reverse))
                        {
                            if (_fromIndex == 0)
                            {
                                _stateFlags &= ~PlatStateFlags::Reverse;
                            }
                        }
                        else if (_fromIndex
                            == static_cast<std::int32_t>(_data.PositionCount) - 1)
                        {
                            _stateFlags |= PlatStateFlags::Reverse;
                        }
                        _toIndex = UncheckedAdd(
                            _fromIndex,
                            TestFlag(_stateFlags, PlatStateFlags::Reverse) ? -1 : 1);
                        _state = PlatformState::Moving;
                    }
                    else if (_data.ReverseType == 1)
                    {
                        const std::int32_t index = UncheckedAdd(
                            _fromIndex,
                            TestFlag(_stateFlags, PlatStateFlags::Reverse) ? -1 : 1);
                        _toIndex = index
                            % static_cast<std::int32_t>(_data.PositionCount);
                        _state = PlatformState::Moving;
                    }
                    else if (_data.ReverseType == 2)
                    {
                        if ((TestFlag(_stateFlags, PlatStateFlags::Reverse)
                                && _fromIndex == 0)
                            || (!TestFlag(_stateFlags, PlatStateFlags::Reverse)
                                && _fromIndex
                                    == static_cast<std::int32_t>(_data.PositionCount) - 1))
                        {
                            Deactivate();
                            if (TestFlag(_flags, PlatformFlags::SleepAtEnd))
                            {
                                SleepWake(false, false);
                            }
                        }

                        if (_state != PlatformState::Inactive)
                        {
                            _toIndex = UncheckedAdd(
                                _fromIndex,
                                TestFlag(_stateFlags, PlatStateFlags::Reverse) ? -1 : 1);
                            _state = PlatformState::Moving;
                        }
                    }
                }

                if (_state == PlatformState::Moving)
                {
                    const SfxData startSfx
                        = cutscene ? _moveSfx.Start2 : _moveSfx.Start1;
                    if (TestFlag(startSfx.Flags, PlatSfxFlags::Loop))
                    {
                        PlaySfx(startSfx);
                    }
                    UpdateMovement();
                }
            }
        }
        else if (_state == PlatformState::Inactive)
        {
            if (_moveTimer > 0)
            {
                _moveTimer = UncheckedAdd(_moveTimer, -1);
            }
            else if (TestFlag(_animFlags, PlatAnimFlags::Active))
            {
                Activate();
            }
        }

        if ((_moveSfx.Start1.Id & 0x8000) != 0)
        {
            const float speed = TestFlag(_stateFlags, PlatStateFlags::Reverse)
                ? _backwardSpeed
                : _forwardSpeed;
            const float amount = 65535.0F * speed * 2.0F / Fixed::ToFloat(1640);
            if (_state == PlatformState::Moving && amount > 0.0F)
            {
                _moveSfxAmount = amount;
            }
            else
            {
                _moveSfxAmount = ExponentialDecay(0.875F, _moveSfxAmount);
            }
            _soundSource.PlaySfx(
                _moveSfx.Start1.Id, true, false, -1.0F, false, false,
                _moveSfxAmount);
        }
    }

    Matrix4 PlatformEntity::GetTransform() const
    {
        Matrix4 transform{};
        if (_data.PositionCount > 0)
        {
            transform = GetTransformMatrix();
            if (!Equal(_posOffset, Vector3::Zero))
            {
                SetRow3(
                    transform,
                    Row3(transform) + Matrix::Vec3MultMtx3(_posOffset, transform));
            }

            if (_parentEntCol != nullptr)
            {
                if (TestFlag(_flags, PlatformFlags::SyluxShip))
                {
                    SetRow3(
                        transform,
                        Matrix::Vec3MultMtx4(Row3(transform), _parentEntCol->Transform));
                }
                else
                {
                    transform = Multiply(transform, _parentEntCol->Transform);
                }
            }
        }
        else
        {
            assert(_parentEntCol == nullptr);
            transform = Transform;
        }
        return transform;
    }

    Matrix4 PlatformEntity::GetModelTransform(
        ModelInstance& inst, std::int32_t index)
    {
        static_cast<void>(index);
        const Model& model = RequireReference(inst.Model());
        return Multiply(CreateScale(model.Scale), GetTransform());
    }

    Matrix4 PlatformEntity::GetTransformMatrix() const
    {
        const float v3 = _curRotation.Y * _curRotation.Y;
        const float v4 = _curRotation.Z * _curRotation.Z;
        const float v5 = 2.0F * _curRotation.X * _curRotation.Z;
        const float v6 = 2.0F * _curRotation.Y * _curRotation.Z;
        const float v7 = _curRotation.W * _curRotation.Z;
        const float v8 = _curRotation.W * _curRotation.X;
        const float v9 = _curRotation.W * _curRotation.Y;
        const float v10 = 1.0F - 2.0F * _curRotation.X * _curRotation.X;
        const float v13 = 2.0F * _curRotation.X * _curRotation.Y;
        const float m11 = 1.0F - 2.0F * v3 - 2.0F * v4;
        const float m12 = v13 + 2.0F * v7;
        const float m13 = v5 - 2.0F * v9;
        const float m21 = v13 - 2.0F * v7;
        const float m22 = v10 - 2.0F * v4;
        const float m23 = v6 + 2.0F * v8;
        const float m31 = v5 + 2.0F * v9;
        const float m32 = v6 - 2.0F * v8;
        const float m33 = v10 - 2.0F * v3;

        return Matrix4(
            Vector4(m11, m12, m13, 0.0F),
            Vector4(m21, m22, m23, 0.0F),
            Vector4(m31, m32, m33, 0.0F),
            Vector4(_curPosition.X, _curPosition.Y, _curPosition.Z, 1.0F));
    }

    void PlatformEntity::CheckContactDamage(DamageResult& result)
    {
        result.Damage = _data.ContactDamage;
        if (!TestFlag(_flags, PlatformFlags::Hazard))
        {
            result.TakeDamage = false;
        }
        else if (TestFlag(_flags, PlatformFlags::ContactDamage))
        {
            result.TakeDamage = true;
        }
    }

    void PlatformEntity::CheckBeamReflection(bool& result)
    {
        if (!TestFlag(_flags, PlatformFlags::BeamReflection)
            || TestFlag(_animFlags, PlatAnimFlags::DisableReflect))
        {
            if (!TestFlag(_flags, PlatformFlags::DamageReflect1))
            {
                result = false;
            }
            else if (TestFlag(_flags, PlatformFlags::DamageReflect2))
            {
                result = true;
            }
        }
        else
        {
            result = true;
        }
    }

    void PlatformEntity::HandleMessage(MessageInfo info)
    {
        const auto onActivate = [this]()
        {
            Activate();
            _beamIntervalTimer = _beamInterval;
            _beamIntervalIndex = 15;
            _beamActive = false;
        };

        if (!TestFlag(_flags, PlatformFlags::SamusShip))
        {
            if (TestFlag(_stateFlags, PlatStateFlags::Awake))
            {
                if (info.Message == Message::Activate)
                {
                    onActivate();
                }
                else if (info.Message == Message::SetActive)
                {
                    if (UnboxInt32(info.Param1) != 0)
                    {
                        onActivate();
                    }
                    else
                    {
                        Deactivate();
                    }
                }
                else if (info.Message == Message::Damage)
                {
                    _health = UnboxInt32(info.Param1);
                }
                else if (info.Message == Message::SetSeekPlayerY)
                {
                    _animFlags &= ~PlatAnimFlags::SeekPlayerHeight;
                    if (UnboxInt32(info.Param1) != 0)
                    {
                        _animFlags |= PlatAnimFlags::SeekPlayerHeight;
                    }
                }
                else if (info.Message == Message::SetBeamReflection)
                {
                    _animFlags &= ~PlatAnimFlags::DisableReflect;
                    if (UnboxInt32(info.Param1) != 0)
                    {
                        _animFlags |= PlatAnimFlags::DisableReflect;
                    }
                }
                else if (info.Message == Message::PlatformSleep)
                {
                    SleepWake(false, false);
                    Deactivate();
                }
                else if (info.Message == Message::BeamCollideWith)
                {
                    if (_health > 0)
                    {
                        if (_data.BeamHitMessage != Message::None
                            && _hitMessageTarget != nullptr)
                        {
                            RequireReference(_scene).SendMessage(
                                _data.BeamHitMessage, this, _hitMessageTarget.get(),
                                BoxInt32(_data.BeamHitMsgParam1),
                                BoxInt32(_data.BeamHitMsgParam2));
                        }

                        std::int32_t effectId = 0;
                        if (!TestFlag(_flags, PlatformFlags::BeamColEffect)
                            || (TestFlag(_flags, PlatformFlags::BeamReflection)
                                && !TestFlag(
                                    _animFlags, PlatAnimFlags::DisableReflect)))
                        {
                            effectId = _data.ResistEffectId;
                        }
                        else
                        {
                            BeamProjectileEntity& beam
                                = RequireReference(CastBeam(info.Sender));
                            const std::int32_t index
                                = static_cast<std::int32_t>(beam.BeamKind());
                            if (index >= static_cast<std::int32_t>(_beamEffectiveness.size())
                                || ManagedAt(_beamEffectiveness, index)
                                    == Effectiveness::Zero)
                            {
                                effectId = _data.ResistEffectId;
                            }
                            else
                            {
                                effectId = _data.DamageEffectId;
                                _health = UncheckedSubtract(
                                    _health, ConvertToInt32Net9(beam.Damage()));

                                if (_health <= _halfHealth)
                                {
                                    if (_halfHealth > 0)
                                    {
                                        for (std::int32_t i = 0;
                                            i < static_cast<std::int32_t>(
                                                _lifetimeMessages.size());
                                            ++i)
                                        {
                                            const Message message
                                                = _lifetimeMessages[
                                                    static_cast<std::size_t>(i)];
                                            const auto& target
                                                = _lifetimeMessageTargets[
                                                    static_cast<std::size_t>(i)];
                                            if (message != Message::None
                                                && target != nullptr)
                                            {
                                                RequireReference(_scene).SendMessage(
                                                    message, this, target.get(),
                                                    BoxInt32(_lifetimeMessageParam1s[
                                                        static_cast<std::size_t>(i)]),
                                                    BoxInt32(_lifetimeMessageParam2s[
                                                        static_cast<std::size_t>(i)]));
                                            }
                                        }
                                        _halfHealth = 0;
                                    }
                                    else
                                    {
                                        effectId = _data.DeadEffectId;
                                        if (TestFlag(_flags, PlatformFlags::Breakable))
                                        {
                                            RequireReference(_scene).SendMessage(
                                                Message::PlatformSleep, this, this,
                                                BoxInt32(0), BoxInt32(0));
                                            PlaySfx(_moveSfx.Destoryed);
                                        }

                                        const Vector3 spawnPos
                                            = AddY(_visiblePosition, 1.0F);
                                        (void)ItemSpawnEntity::SpawnItemDrop(
                                            _data.ItemType, spawnPos,
                                            NodeRef, _data.ItemChance, _scene);

                                        if (_data.DeadMessage != Message::None
                                            && _deathMessageTarget != nullptr)
                                        {
                                            RequireReference(_scene).SendMessage(
                                                _data.DeadMessage,
                                                this,
                                                _deathMessageTarget.get(),
                                                BoxInt32(_data.DeadMsgParam1),
                                                BoxInt32(_data.DeadMsgParam2));
                                        }
                                        _health = 0;
                                    }
                                }
                            }

                            if (effectId != 0)
                            {
                                const Formats::CollisionResult collisionResult
                                    = UnboxCollisionResult(info.Param1);
                                assert(collisionResult.EntityCollision != nullptr);
                                const auto& entCol
                                    = collisionResult.EntityCollision;
                                Formats::Collision::EntityCollision& entColRef
                                    = RequireReference(entCol);
                                Vector3 spawnPos = collisionResult.Position;
                                Vector3 spawnUp = collisionResult.Plane.Xyz();
                                spawnPos = Matrix::Vec3MultMtx4(
                                    spawnPos, entColRef.Inverse1);
                                spawnUp = Matrix::Vec3MultMtx4(
                                    spawnUp, entColRef.Inverse1);

                                Vector3 spawnFacing;
                                if (spawnUp.Z <= -0.9F || spawnUp.Z >= 0.9F)
                                {
                                    spawnFacing = Vector3::Cross(
                                        Vector3(1.0F, 0.0F, 0.0F),
                                        spawnUp).Normalized();
                                }
                                else
                                {
                                    spawnFacing = Vector3::Cross(
                                        Vector3(0.0F, 0.0F, 1.0F),
                                        spawnUp).Normalized();
                                }
                                RequireReference(_scene).SpawnEffect(
                                    effectId, spawnFacing, spawnUp,
                                    spawnPos, false, entCol);
                            }
                        }
                    }
                }
                else if (info.Message == Message::PlayerCollideWith)
                {
                    const bool alreadyColliding = _playerCol;
                    _timeSincePlayerCol = 0;
                    _playerCol = true;

                    if (_data.PlayerColMessage != Message::None
                        && _playerColMessageTarget != nullptr
                        && !alreadyColliding
                        && !(TestFlag(_flags, PlatformFlags::StandingColOnly)
                            && UnboxInt32(info.Param2) == 0))
                    {
                        RequireReference(_scene).SendMessage(
                            _data.PlayerColMessage,
                            this,
                            _playerColMessageTarget.get(),
                            BoxInt32(_data.PlayerColMsgParam1),
                            BoxInt32(_data.PlayerColMsgParam2));
                    }
                }
            }
            else if (info.Message == Message::PlatformWakeup)
            {
                SleepWake(true, false);
                if (UnboxInt32(info.Param1) != 0)
                {
                    onActivate();
                }
            }

            if (info.Message == Message::SetPlatformIndex)
            {
                const std::int32_t index = UncheckedAdd(
                    UnboxInt32(info.Param1), -1);
                if (_fromIndex != index)
                {
                    if (_state == PlatformState::Inactive)
                    {
                        _fromIndex = index;
                        UpdatePosition();
                        Deactivate();
                        _velocity = Vector3::Zero;
                        _movePercent = 0.0F;
                        _moveIncrement = 0.0F;
                    }
                    else if (UnboxInt32(info.Param2) != 0)
                    {
                        _fromIndex = index;
                        UpdatePosition();
                    }
                }
            }
        }
    }

    FhPlatformEntity::FhPlatformEntity(
        FhPlatformEntityData data, Scene* scene)
        : EntityBase(EntityType::FhPlatform, scene),
          _data(data)
    {
        Id = data.Header.EntityId;
        SetTransform(data.Header.FacingVector, data.Header.UpVector, data.Header.Position);

        const std::string name = "platform";
        SetUpModel(name, 0, AnimFlags::None, true);
        const ModelMetadata& modelMeta = FirstHuntModelAt(name);
        assert(modelMeta.CollisionPath.has_value());
        const auto collision = Formats::Collision::Collision::GetCollision(&modelMeta);
        SetCollision(collision);

        _speed = data.Speed.FloatValue() / 2.0F;
        assert(data.PositionCount >= 2 && data.PositionCount < 8);
        for (std::int32_t i = 0;
            i < static_cast<std::int32_t>(data.PositionCount); ++i)
        {
            _posList.push_back(data.Positions[i].ToFloatVector());
        }
        _delay = static_cast<std::int32_t>(data.Delay) * 2;
        _moveTimer = _delay;
    }

    bool FhPlatformEntity::Process()
    {
        Vector3 position = _position;
        position = position + _velocity;

        if (_moveTimer > 0)
        {
            _moveTimer = UncheckedAdd(_moveTimer, -1);
        }
        else
        {
            if (_state == MoveState::Sleep)
            {
                _state = MoveState::MoveForward;
                UpdateMovement();
                _fromIndex = UncheckedAdd(_fromIndex, 1);
            }
            else if (_state == MoveState::MoveForward)
            {
                _state = MoveState::Wait;
                _velocity = Vector3::Zero;
                position = ManagedListAt(_posList, _toIndex);
                if (_fromIndex == static_cast<std::int32_t>(_posList.size()) - 1)
                {
                    _toIndex = UncheckedAdd(_fromIndex, -1);
                }
                else
                {
                    _fromIndex = UncheckedAdd(_fromIndex, 1);
                    _toIndex = UncheckedAdd(_fromIndex, 1);
                }
                _moveTimer = _delay;
            }
            else if (_state == MoveState::Wait)
            {
                _state = _toIndex >= _fromIndex
                    ? MoveState::MoveForward
                    : MoveState::MoveBackward;
                UpdateMovement();
            }
            else if (_state == MoveState::MoveBackward)
            {
                _velocity = Vector3::Zero;
                position = ManagedListAt(_posList, _toIndex);
                if (_toIndex > 0)
                {
                    _state = MoveState::Wait;
                    _fromIndex = UncheckedAdd(_fromIndex, -1);
                    _toIndex = UncheckedAdd(_fromIndex, -1);
                }
                else
                {
                    _state = MoveState::Sleep;
                    _fromIndex = 0;
                    _toIndex = 1;
                }
                _moveTimer = _delay;
            }
        }

        Position = position;
        UpdateCollisionTransform(0, Transform);
        return true;
    }

    void FhPlatformEntity::UpdateMovement()
    {
        const Vector3 velocity
            = ManagedListAt(_posList, _toIndex) - ManagedListAt(_posList, _fromIndex);
        const float distance = Length(velocity);
        _moveTimer = ConvertToInt32Net9(distance / _speed);
        const float factor = _speed / distance;
        _velocity = ScaleVector(velocity, factor);
    }
}
