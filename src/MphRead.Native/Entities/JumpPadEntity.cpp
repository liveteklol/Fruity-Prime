#include "JumpPadEntity.hpp"

#include "../GameState.hpp"
#include "../MemoryArrays.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Mods/WorldEvents.hpp"
#include "../Renderer.hpp"
#include "../Scene.hpp"
#include "Players/PlayerEntity.hpp"
#include "TriggerVolumeEntity.hpp"
#include "../Formats/Types.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../NativeRuntime/OpenTK/Mathematics.hpp"

#include <any>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

using ::MphRead::NativeRuntime::RequireReference;
using ::OpenTK::Mathematics::CreateScale;
using ::OpenTK::Mathematics::Determinant;
using ::OpenTK::Mathematics::Equal;
using ::OpenTK::Mathematics::Inverted;
using ::OpenTK::Mathematics::Multiply;
using ::OpenTK::Mathematics::ScaleVector;

namespace
{
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;
    using OpenTK::Mathematics::Vector4;

    constexpr Vector3 UnitX(1.0F, 0.0F, 0.0F);
    constexpr Vector3 UnitY(0.0F, 1.0F, 0.0F);

    [[nodiscard]] const std::string& GetJumpPadName(std::uint32_t modelId)
    {
        const std::int32_t index = std::bit_cast<std::int32_t>(modelId);
        if (index < 0
            || static_cast<std::size_t>(index) >= MphRead::Metadata::JumpPads.size())
        {
            throw System::ArgumentOutOfRangeException();
        }
        return MphRead::Metadata::JumpPads[static_cast<std::size_t>(index)];
    }

}

namespace MphRead::Entities
{
    JumpPadEntity::JumpPadEntity(
        JumpPadEntityData data, std::string nodeName, Scene* scene)
        : EntityBase(EntityType::JumpPad, std::move(nodeName), scene),
          _data(data)
    {
        Id = data.Header.EntityId;
        SetTransform(data.Header.FacingVector, data.Header.UpVector, data.Header.Position);
        _prevPos = Position;
        _volume = CollisionVolume::Move(_data.Volume, Position);

        _hidden = data.ModelId == HiddenModelId;
        _q3 = static_cast<Q3Trigger>(data.Padding81);
        if (_q3 == Q3Trigger::Teleport)
        {
            _q3Target = data.BeamVector.ToFloatVector();
        }
        const std::string modelName = GetJumpPadName(_hidden ? 0U : data.ModelId);
        SetUpModel(modelName);
        ModelInstance& beamInst = SetUpModel("JumpPad_Beam");

        const Vector3 beamVector = data.BeamVector.ToFloatVector().Normalized();
        _beamTransform = GetTransformMatrix(
            beamVector,
            beamVector.X != 0.0F || beamVector.Z != 0.0F ? UnitY : UnitX);
        _beamTransform.M42 = 0.25F;
        const Vector3 transformedBeam = Matrix::Vec3MultMtx3(beamVector, Transform);
        const float beamSpeed = _data.Speed.FloatValue();
        _beamVector = ScaleVector(transformedBeam, beamSpeed);

        if (GameState::Mode() == GameMode::SinglePlayer)
        {
            const std::shared_ptr<StorySave> storySave = GameState::StorySave;
            const std::int32_t roomId = RequireReference(_scene).RoomId();
            const std::int32_t entityId = Id;
            const bool active = data.Active != 0;
            Active = RequireReference(storySave).InitRoomState(
                roomId,
                entityId,
                active) != 0;
        }
        else
        {
            Active = data.Active != 0;
        }
        beamInst.Active = Active;
    }

    JumpPadEntity::Q3Trigger JumpPadEntity::Q3Kind() const noexcept
    {
        return _q3;
    }

    float JumpPadEntity::Q3Value() const noexcept
    {
        return std::bit_cast<float>(_data.Unused28);
    }

    std::shared_ptr<Formats::NodeData3> JumpPadEntity::ClosestNode() const noexcept
    {
        return _closestNode;
    }

    void JumpPadEntity::ClosestNode(std::shared_ptr<Formats::NodeData3> value) noexcept
    {
        _closestNode = std::move(value);
    }

    CollisionVolume JumpPadEntity::ModVolume() const noexcept
    {
        return _volume;
    }

    void JumpPadEntity::Initialize()
    {
        EntityBase::Initialize();
        if (_data.ParentId != -1)
        {
            std::shared_ptr<EntityBase> parent;
            if (RequireReference(_scene).TryGetEntity(_data.ParentId, parent))
            {
                _parent = std::move(parent);
            }
        }
    }

    bool JumpPadEntity::GetTargetable()
    {
        return false;
    }

    bool JumpPadEntity::Process()
    {
        if (_parent)
        {
            if (!_invSetUp)
            {
                _invPos = Matrix::Vec3MultMtx4(
                    Position,
                    Inverted(_parent->CollisionTransform()));
                _invSetUp = true;
            }
            Position = Matrix::Vec3MultMtx4(_invPos, _parent->CollisionTransform());
        }

        if (!Equal(_prevPos, static_cast<Vector3>(Position)))
        {
            _volume = CollisionVolume::Move(
                _data.Volume,
                static_cast<Vector3>(Position));
            _prevPos = static_cast<Vector3>(Position);
        }

        if (Active && _cooldownTimer == 0)
        {
            auto enumerator = RequireReference(_scene).GetPlayerEntities().GetEnumerator();
            while (enumerator.MoveNext())
            {
                PlayerEntity& player = RequireReference(enumerator.Current());
                if (player.Health() == 0)
                {
                    continue;
                }

                Vector3 position;
                if (player.IsAltForm())
                {
                    if (!TypeExtensions::TestFlag(
                        _data.TriggerFlags, TriggerFlags::PlayerAlt))
                    {
                        continue;
                    }
                    position = player.Volume().SpherePosition;
                    // Quake's trigger is touched from the feet up; the
                    // ball's are the bottom of the sphere, not its centre,
                    // which stands above a thin trigger altogether.
                    if (_q3 != Q3Trigger::None)
                    {
                        position.Y -= player.Volume().SphereRadius;
                    }
                }
                else
                {
                    if (!TypeExtensions::TestFlag(
                        _data.TriggerFlags, TriggerFlags::PlayerBiped))
                    {
                        continue;
                    }
                    position = player.Position;
                }

                if (!_volume.TestPoint(position))
                {
                    continue;
                }
                if (_q3 == Q3Trigger::Teleport)
                {
                    const float yaw = Q3Value();
                    player.Q3Teleport(_q3Target,
                        Vector3(std::sin(yaw), 0.0F, std::cos(yaw)),
                        NodeRef, _data.Speed.FloatValue());
                    Mods::WorldEvents::NoteTeleport(player, Id);
                    continue;
                }
                player.ActivateJumpPad(this, _beamVector, _data.ControlLockTime);
                Mods::WorldEvents::NoteJumpPad(player, Id);
                if (_q3 == Q3Trigger::None)
                {
                    _cooldownTimer = static_cast<std::uint16_t>(
                        static_cast<std::uint32_t>(_data.CooldownTime) * 2U);
                }
            }
        }

        if (_cooldownTimer > 0)
        {
            --_cooldownTimer;
        }
        return EntityBase::Process();
    }

    void JumpPadEntity::HandleMessage(MessageInfo info)
    {
        if (info.Message == Message::Activate)
        {
            Active = true;
            if (GameState::Mode() == GameMode::SinglePlayer)
            {
                const std::shared_ptr<StorySave> storySave = GameState::StorySave;
                const std::int32_t roomId = RequireReference(_scene).RoomId();
                const std::int32_t entityId = Id;
                RequireReference(storySave).SetRoomState(roomId, entityId, 3);
            }
        }
        else if (info.Message == Message::SetActive)
        {
            if (UnboxInt32(info.Param1) != 0)
            {
                Active = true;
                if (GameState::Mode() == GameMode::SinglePlayer)
                {
                    const std::shared_ptr<StorySave> storySave = GameState::StorySave;
                    const std::int32_t roomId = RequireReference(_scene).RoomId();
                    const std::int32_t entityId = Id;
                    RequireReference(storySave).SetRoomState(roomId, entityId, 3);
                }
            }
            else
            {
                Active = false;
                if (GameState::Mode() == GameMode::SinglePlayer)
                {
                    const std::shared_ptr<StorySave> storySave = GameState::StorySave;
                    const std::int32_t roomId = RequireReference(_scene).RoomId();
                    const std::int32_t entityId = Id;
                    RequireReference(storySave).SetRoomState(roomId, entityId, 1);
                }
            }
        }
        _models[1].Active = Active;
    }

    Matrix4 JumpPadEntity::GetModelTransform(ModelInstance& inst, std::int32_t index)
    {
        if (index == 1)
        {
            const std::shared_ptr<Model> model = inst.Model();
            const Matrix4 scale = CreateScale(RequireReference(model).Scale);
            const Matrix4 scaleBeam = Multiply(scale, _beamTransform);
            return Multiply(scaleBeam, _transform);
        }
        return EntityBase::GetModelTransform(inst, index);
    }

    void JumpPadEntity::GetDrawInfo()
    {
        if (!_hidden && IsVisible(NodeRef))
        {
            EntityBase::GetDrawInfo();
        }
    }

    void JumpPadEntity::GetDisplayVolumes()
    {
        if (RequireReference(_scene).ShowVolumes() == VolumeDisplay::JumpPad)
        {
            AddVolumeItem(_volume, UnitY);
        }
    }

    void JumpPadEntity::SetActive(bool active)
    {
        EntityBase::SetActive(active);
        _models[1].Active = Active;
    }

    FhJumpPadEntity::FhJumpPadEntity(FhJumpPadEntityData data, Scene* scene)
        : EntityBase(EntityType::FhJumpPad, scene),
          _data(data)
    {
        Id = data.Header.EntityId;
        SetTransform(data.Header.FacingVector, data.Header.UpVector, data.Header.Position);
        _volume = CollisionVolume::Move(_data.ActiveVolume(), Position);

        std::string name = data.ModelId == 1U ? "balljump" : "jumppad_base";
        SetUpModel(name, 0, AnimFlags::None, true);
        name = data.ModelId == 1U ? "balljump_ray" : "jumppad_ray";
        SetUpModel(name, 0, AnimFlags::None, true);

        const Vector3 beamVector = data.BeamVector.ToFloatVector().Normalized();
        _beamTransform = GetTransformMatrix(
            beamVector,
            beamVector.X != 0.0F || beamVector.Z != 0.0F ? UnitY : UnitX);
    }

    Matrix4 FhJumpPadEntity::GetModelTransform(ModelInstance& inst, std::int32_t index)
    {
        if (index == 1)
        {
            const std::shared_ptr<Model> model = inst.Model();
            const Matrix4 scale = CreateScale(RequireReference(model).Scale);
            Matrix4 transform = Multiply(scale, _beamTransform);
            const Vector3 position = Position;
            transform.M41 = position.X;
            transform.M42 = position.Y + 0.25F;
            transform.M43 = position.Z;
            return transform;
        }
        return EntityBase::GetModelTransform(inst, index);
    }

    void FhJumpPadEntity::GetDisplayVolumes()
    {
        if (RequireReference(_scene).ShowVolumes() == VolumeDisplay::JumpPad)
        {
            AddVolumeItem(_volume, UnitY);
        }
    }
}
