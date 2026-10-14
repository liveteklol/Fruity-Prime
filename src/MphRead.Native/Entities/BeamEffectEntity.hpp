#pragma once

#include "EntityBase.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace MphRead::Formats::Collision
{
    class EntityCollision;
}

namespace MphRead::Entities
{
    struct BeamEffectEntityData final
    {
        const std::int32_t Type = 0;
        const bool NoSplat = false;
        const ::OpenTK::Mathematics::Matrix4 Transform{};
        const std::shared_ptr<::MphRead::Formats::Collision::EntityCollision> EntityCollision{};

        BeamEffectEntityData() noexcept = default;
        BeamEffectEntityData(
            std::int32_t type,
            bool noSplat,
            ::OpenTK::Mathematics::Matrix4 transform,
            std::shared_ptr<::MphRead::Formats::Collision::EntityCollision> entCol = nullptr) noexcept;

        BeamEffectEntityData(const BeamEffectEntityData&) noexcept = default;
        BeamEffectEntityData& operator=(const BeamEffectEntityData& other) noexcept;
    };

    class BeamEffectEntity : public EntityBase
    {
    public:
        explicit BeamEffectEntity(Scene* scene);

        BeamEffectEntity(const BeamEffectEntity&) = delete;
        BeamEffectEntity& operator=(const BeamEffectEntity&) = delete;
        BeamEffectEntity(BeamEffectEntity&&) = delete;
        BeamEffectEntity& operator=(BeamEffectEntity&&) = delete;

        void Spawn(BeamEffectEntityData data);
        void Reposition(::OpenTK::Mathematics::Vector3 offset);
        [[nodiscard]] bool Process() override;
        void Destroy() override;

        // `tint`: an effect (type 3 and up) drawn in one colour, as Instagib's
        // hits are in the shooter's rail colour. The models (types 0-2) ignore it.
        [[nodiscard]] static std::shared_ptr<BeamEffectEntity> Create(
            BeamEffectEntityData data, Scene* scene,
            std::optional<::OpenTK::Mathematics::Vector3> tint = std::nullopt);

    private:
        std::int32_t _lifespan = 0;
    };
}
