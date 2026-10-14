#include "BeamEffectEntity.hpp"

#include "../Program.hpp"
#include "../Scene.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../Formats/Types.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

using ::MphRead::NativeRuntime::RequireReference;
using ::OpenTK::Mathematics::ClearScale;
using ::OpenTK::Mathematics::Length;

namespace
{
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;

    [[nodiscard]] std::int32_t ManagedFrameLifespan(std::int32_t frameCount) noexcept
    {
        const std::uint32_t value
            = (std::bit_cast<std::uint32_t>(frameCount) - 1U) * 2U;
        return std::bit_cast<std::int32_t>(value);
    }

    [[nodiscard]] std::int32_t ManagedPostDecrement(std::int32_t& value) noexcept
    {
        const std::int32_t previous = value;
        const std::uint32_t decremented = std::bit_cast<std::uint32_t>(value) - 1U;
        value = std::bit_cast<std::int32_t>(decremented);
        return previous;
    }
}

namespace MphRead::Entities
{
    BeamEffectEntityData::BeamEffectEntityData(
        std::int32_t type,
        bool noSplat,
        Matrix4 transform,
        std::shared_ptr<::MphRead::Formats::Collision::EntityCollision> entCol) noexcept
        : Type(type),
          NoSplat(noSplat),
          Transform(transform),
          EntityCollision(std::move(entCol))
    {
    }

    BeamEffectEntityData& BeamEffectEntityData::operator=(
        const BeamEffectEntityData& other) noexcept
    {
        if (this != std::addressof(other))
        {
            this->~BeamEffectEntityData();
            ::new (static_cast<void*>(this)) BeamEffectEntityData(other);
        }
        return *this;
    }

    BeamEffectEntity::BeamEffectEntity(Scene* scene)
        : EntityBase(EntityType::BeamEffect, scene)
    {
    }

    void BeamEffectEntity::Spawn(BeamEffectEntityData data)
    {
        _models = ModelList{};
        ModelInstance* model;
        if (data.Type == 0)
        {
            model = std::addressof(SetUpModel("iceWave", 0, AnimFlags::NoLoop));
        }
        else if (data.Type == 1)
        {
            model = std::addressof(SetUpModel("sniperBeam", 0, AnimFlags::NoLoop));
        }
        else if (data.Type == 2)
        {
            model = std::addressof(SetUpModel("cylBossLaserBurn"));
        }
        else
        {
            throw ProgramException("Invalid beam effect type.");
        }

        _lifespan = 0;
        const std::shared_ptr<MphRead::Model> modelDefinition = model->Model();
        const MphRead::Model& definition = RequireReference(modelDefinition);
        const MphRead::AnimationGroups& groups = RequireReference(definition.AnimationGroups);
        const auto& nodeGroups = RequireReference(groups.Node);
        if (!nodeGroups.empty() && data.Type != 2)
        {
            const MphRead::NodeAnimationGroup& group = RequireReference(nodeGroups[0]);
            _lifespan = ManagedFrameLifespan(group.FrameCount);
        }
        else
        {
            const auto& materialGroups = RequireReference(groups.Material);
            if (!materialGroups.empty())
            {
                const MphRead::MaterialAnimationGroup& group
                    = RequireReference(materialGroups[0]);
                _lifespan = ManagedFrameLifespan(group.FrameCount);
            }
        }

        Transform = data.Transform;
        if (data.Type == 0)
        {
            RequireReference(_scene).SpawnEffect(78, ClearScale(data.Transform));
        }
    }

    void BeamEffectEntity::Reposition(Vector3 offset)
    {
        Position = static_cast<Vector3>(Position) + offset;
    }

    bool BeamEffectEntity::Process()
    {
        if (ManagedPostDecrement(_lifespan) <= 0)
        {
            return false;
        }
        return EntityBase::Process();
    }

    void BeamEffectEntity::Destroy()
    {
        RequireReference(_scene).UnlinkBeamEffect(this);
        EntityBase::Destroy();
    }

    std::shared_ptr<BeamEffectEntity> BeamEffectEntity::Create(
        BeamEffectEntityData data, Scene* scene, std::optional<Vector3> tint)
    {
        if (data.Type >= 3)
        {
            std::int32_t effectId = data.Type - 3;
            if (data.NoSplat)
            {
                if (effectId == 1)
                {
                    effectId = 2;
                }
                else if (effectId == 92)
                {
                    effectId = 98;
                }
            }
            if (tint.has_value())
            {
                RequireReference(scene).SpawnTintedEffect(
                    effectId, data.Transform, *tint, data.EntityCollision);
            }
            else
            {
                RequireReference(scene).SpawnEffect(
                    effectId, data.Transform, false, data.EntityCollision);
            }
            return nullptr;
        }
        return RequireReference(scene).InitBeamEffect(data);
    }
}
