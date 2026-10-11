#pragma once

#include "../Formats/Entity.hpp"
#include "../Formats/Formats.hpp"
#include "EntityBase.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace MphRead::Formats
{
    class NodeData3;
}

namespace MphRead::Entities
{
    class JumpPadEntity : public EntityBase
    {
    public:
        JumpPadEntity(JumpPadEntityData data, std::string nodeName, Scene* scene);

        // A ModelId no cartridge pad uses: the pad works and draws nothing,
        // for a custom map whose own geometry already shows where it is.
        static constexpr std::uint32_t HiddenModelId = 0xFFFFFFFFU;

        JumpPadEntity(const JumpPadEntity&) = delete;
        JumpPadEntity& operator=(const JumpPadEntity&) = delete;
        JumpPadEntity(JumpPadEntity&&) = delete;
        JumpPadEntity& operator=(JumpPadEntity&&) = delete;

        [[nodiscard]] std::shared_ptr<Formats::NodeData3> ClosestNode() const noexcept;
        void ClosestNode(std::shared_ptr<Formats::NodeData3> value) noexcept;

        [[nodiscard]] CollisionVolume ModVolume() const noexcept;

        void Initialize() override;
        [[nodiscard]] bool GetTargetable() override;
        [[nodiscard]] bool Process() override;
        void HandleMessage(MessageInfo info) override;
        void GetDrawInfo() override;
        void GetDisplayVolumes() override;
        void SetActive(bool active) override;

    protected:
        [[nodiscard]] ::OpenTK::Mathematics::Matrix4 GetModelTransform(
            ModelInstance& inst, std::int32_t index) override;

    private:
        const JumpPadEntityData _data;
        ::OpenTK::Mathematics::Matrix4 _beamTransform{};
        ::OpenTK::Mathematics::Vector3 _beamVector{};
        CollisionVolume _volume{};
        ::OpenTK::Mathematics::Vector3 _prevPos{};

        bool _invSetUp = false;
        std::shared_ptr<EntityBase> _parent{};
        ::OpenTK::Mathematics::Vector3 _invPos{};

        std::uint16_t _cooldownTimer = 0;
        std::shared_ptr<Formats::NodeData3> _closestNode{};
        bool _hidden = false;
    };

    class FhJumpPadEntity : public EntityBase
    {
    public:
        FhJumpPadEntity(FhJumpPadEntityData data, Scene* scene);

        FhJumpPadEntity(const FhJumpPadEntity&) = delete;
        FhJumpPadEntity& operator=(const FhJumpPadEntity&) = delete;
        FhJumpPadEntity(FhJumpPadEntity&&) = delete;
        FhJumpPadEntity& operator=(FhJumpPadEntity&&) = delete;

        void GetDisplayVolumes() override;

    protected:
        [[nodiscard]] ::OpenTK::Mathematics::Matrix4 GetModelTransform(
            ModelInstance& inst, std::int32_t index) override;

    private:
        const FhJumpPadEntityData _data;
        ::OpenTK::Mathematics::Matrix4 _beamTransform{};
        CollisionVolume _volume{};
    };
}
