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

        // A Quake 3 level's trigger, read straight from its .pk3 (MapGen's
        // Q3Import), kept in bytes the cartridge leaves zero: Padding81 says
        // which, Unused28 holds a float.
        //  Push: trigger_push. The player is thrown with Speed along
        //    BeamVector -- the whole velocity, whatever it was moving at --
        //    and falls under Quake's own gravity (Unused28, units per frame
        //    per frame) until it lands, so it flies the arc and the time it
        //    flew in Quake. Touching it again in the next frame throws again,
        //    as in Quake, where the trigger sets the velocity every frame.
        //  Teleport: trigger_teleport. BeamVector is where it sends you, Speed
        //    how fast you leave it and Unused28 the way you face (radians).
        enum class Q3Trigger : std::uint8_t
        {
            None = 0,
            Push = 1,
            Teleport = 2
        };

        [[nodiscard]] Q3Trigger Q3Kind() const noexcept;
        [[nodiscard]] float Q3Value() const noexcept;
        // Quake units to one of this game's, in Padding82: what a Push
        // flight's air control (Quake's 320 a second) is measured in.
        [[nodiscard]] float Q3Unit() const noexcept { return static_cast<float>(_data.Padding82); }
        // The velocity it gives, in units per frame.
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 LaunchVelocity() const noexcept { return _beamVector; }
        // Where a Teleport sends you.
        [[nodiscard]] ::OpenTK::Mathematics::Vector3 Q3Target() const noexcept { return _q3Target; }

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
        Q3Trigger _q3 = Q3Trigger::None;
        ::OpenTK::Mathematics::Vector3 _q3Target{};
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
