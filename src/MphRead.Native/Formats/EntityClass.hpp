#pragma once

#include "Entity.hpp"
#include "Formats.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace MphRead::Editor
{
    class EntityEditorBase
    {
    public:
        EntityType Type{};
        std::int16_t Id = 0;
        std::uint16_t LayerMask = 0;
        OpenTK::Mathematics::Vector3 Position{};
        OpenTK::Mathematics::Vector3 Up{};
        OpenTK::Mathematics::Vector3 Facing{};
        std::shared_ptr<std::string> NodeName{};

        explicit EntityEditorBase(EntityType type);
        explicit EntityEditorBase(const std::shared_ptr<Entity>& header);
        EntityEditorBase(const EntityEditorBase&) = delete;
        EntityEditorBase& operator=(const EntityEditorBase&) = delete;
        EntityEditorBase(EntityEditorBase&&) = delete;
        EntityEditorBase& operator=(EntityEditorBase&&) = delete;
        virtual ~EntityEditorBase() = 0;

    protected:
        void PrintValue(const std::shared_ptr<std::string>& value1,
            const std::shared_ptr<std::string>& value2, const char* name) const;

        template <typename T>
        void PrintValue(const T& value1, const T& value2, const char* name) const;

        template <typename T>
        void PrintValues(const std::shared_ptr<std::vector<T>>& value1,
            const std::shared_ptr<std::vector<T>>& value2, const char* name) const;
    };

    class PlatformEntityEditor : public EntityEditorBase
    {
    public:
        std::uint32_t NoPort = 0;
        std::uint32_t ModelId = 0;
        std::int16_t ParentId = 0;
        bool Active = false;
        std::uint8_t Delay = 0;
        std::uint16_t ScanData1 = 0;
        std::int16_t ScanMsgTarget = 0;
        Message ScanMessage{};
        std::uint16_t ScanData2 = 0;
        std::uint16_t PositionCount = 0;
        std::shared_ptr<std::vector<OpenTK::Mathematics::Vector3>> Positions
            = std::make_shared<std::vector<OpenTK::Mathematics::Vector3>>();
        std::shared_ptr<std::vector<OpenTK::Mathematics::Vector4>> Rotations
            = std::make_shared<std::vector<OpenTK::Mathematics::Vector4>>();
        OpenTK::Mathematics::Vector3 PositionOffset{};
        float ForwardSpeed = 0.0F;
        float BackwardSpeed = 0.0F;
        std::shared_ptr<std::string> PortalName{};
        std::uint32_t MovementType = 0;
        bool ForCutscene = false;
        std::uint32_t ReverseType = 0;
        Entities::PlatformFlags Flags{};
        std::uint32_t ContactDamage = 0;
        OpenTK::Mathematics::Vector3 BeamSpawnDir{};
        OpenTK::Mathematics::Vector3 BeamSpawnPos{};
        std::int32_t BeamId = 0;
        std::uint32_t BeamInterval = 0;
        std::uint32_t BeamOnIntervals = 0;
        std::int32_t ResistEffectId = 0;
        std::uint32_t Health = 0;
        std::uint32_t Effectiveness = 0;
        std::int32_t DamageEffectId = 0;
        std::int32_t DeadEffectId = 0;
        std::uint8_t ItemChance = 0;
        ::MphRead::ItemType ItemType{};
        std::uint32_t Unused1D0 = 0;
        std::uint32_t Unused1D4 = 0;
        std::int32_t BeamHitMsgTarget = 0;
        Message BeamHitMessage{};
        std::int32_t BeamHitMsgParam1 = 0;
        std::int32_t BeamHitMsgParam2 = 0;
        std::int32_t PlayerColMsgTarget = 0;
        Message PlayerColMessage{};
        std::int32_t PlayerColMsgParam1 = 0;
        std::int32_t PlayerColMsgParam2 = 0;
        std::int32_t DeadMsgTarget = 0;
        Message DeadMessage{};
        std::int32_t DeadMsgParam1 = 0;
        std::int32_t DeadMsgParam2 = 0;
        std::uint16_t LifetimeMsg1Index = 0;
        std::int16_t LifetimeMsg1Target = 0;
        Message LifetimeMessage1{};
        std::int32_t LifetimeMsg1Param1 = 0;
        std::int32_t LifetimeMsg1Param2 = 0;
        std::uint16_t LifetimeMsg2Index = 0;
        std::int16_t LifetimeMsg2Target = 0;
        Message LifetimeMessage2{};
        std::int32_t LifetimeMsg2Param1 = 0;
        std::int32_t LifetimeMsg2Param2 = 0;
        std::uint16_t LifetimeMsg3Index = 0;
        std::int16_t LifetimeMsg3Target = 0;
        Message LifetimeMessage3{};
        std::int32_t LifetimeMsg3Param1 = 0;
        std::int32_t LifetimeMsg3Param2 = 0;
        std::uint16_t LifetimeMsg4Index = 0;
        std::int16_t LifetimeMsg4Target = 0;
        Message LifetimeMessage4{};
        std::int32_t LifetimeMsg4Param1 = 0;
        std::int32_t LifetimeMsg4Param2 = 0;

        PlatformEntityEditor();
        PlatformEntityEditor(const std::shared_ptr<Entity>& header, PlatformEntityData raw);
        void CompareTo(const std::shared_ptr<PlatformEntityEditor>& other) const;
    };

    class FhPlatformEntityEditor : public EntityEditorBase
    {
    public:
        std::uint32_t NoPortal = 0;
        std::uint32_t GroupId = 0;
        std::uint32_t Unused2C = 0;
        std::uint8_t Delay = 0;
        std::uint8_t PositionCount = 0;
        CollisionVolume Volume;
        std::shared_ptr<std::vector<OpenTK::Mathematics::Vector3>> Positions
            = std::make_shared<std::vector<OpenTK::Mathematics::Vector3>>();
        float Speed = 0.0F;
        std::shared_ptr<std::string> PortalName{};

        FhPlatformEntityEditor();
        FhPlatformEntityEditor(const std::shared_ptr<Entity>& header, FhPlatformEntityData raw);
        void CompareTo(const std::shared_ptr<FhPlatformEntityEditor>& other) const;
    };

    class ObjectEntityEditor : public EntityEditorBase
    {
    public:
        Entities::ObjectFlags Flags{};
        Entities::ObjEffFlags EffectFlags{};
        std::int32_t ModelId = 0;
        std::int16_t LinkedEntity = 0;
        std::uint16_t ScanId = 0;
        std::int16_t ScanMsgTarget = 0;
        Message ScanMessage{};
        std::int32_t EffectId = 0;
        std::uint32_t EffectInterval = 0;
        std::uint32_t EffectOnIntervals = 0;
        OpenTK::Mathematics::Vector3 EffectPositionOffset{};
        CollisionVolume Volume;

        ObjectEntityEditor();
        ObjectEntityEditor(const std::shared_ptr<Entity>& header, ObjectEntityData raw);
        void CompareTo(const std::shared_ptr<ObjectEntityEditor>& other) const;
    };

    class PlayerSpawnEntityEditor : public EntityEditorBase
    {
    public:
        std::uint8_t Availability = 0;
        bool Active = true;
        std::int8_t TeamIndex = -1;

        PlayerSpawnEntityEditor();
        PlayerSpawnEntityEditor(const std::shared_ptr<Entity>& header, PlayerSpawnEntityData raw);
        void CompareTo(const std::shared_ptr<PlayerSpawnEntityEditor>& other) const;
    };

    class DoorEntityEditor : public EntityEditorBase
    {
    public:
        std::shared_ptr<std::string> DoorNodeName{};
        std::uint32_t PaletteId = 0;
        ::MphRead::DoorType DoorType{};
        std::uint32_t ConnectorId = 0;
        std::uint8_t TargetLayerId = 0;
        bool Locked = false;
        std::uint8_t Field42 = 0;
        std::uint8_t Field43 = 0;
        std::shared_ptr<std::string> EntityFilename{};
        std::shared_ptr<std::string> RoomName{};

        DoorEntityEditor();
        DoorEntityEditor(const std::shared_ptr<Entity>& header, DoorEntityData raw);
        void CompareTo(const std::shared_ptr<DoorEntityEditor>& other) const;
    };

    class FhDoorEntityEditor : public EntityEditorBase
    {
    public:
        std::shared_ptr<std::string> RoomName{};
        bool Locked = false;
        std::uint32_t ModelId = 0;

        FhDoorEntityEditor();
        FhDoorEntityEditor(const std::shared_ptr<Entity>& header, FhDoorEntityData raw);
        void CompareTo(const std::shared_ptr<FhDoorEntityEditor>& other) const;
    };

    class ItemSpawnEntityEditor : public EntityEditorBase
    {
    public:
        std::int32_t ParentId = 0;
        ::MphRead::ItemType ItemType{};
        bool Enabled = false;
        bool HasBase = false;
        bool AlwaysActive = false;
        std::uint16_t MaxSpawnCount = 0;
        std::uint16_t SpawnInterval = 0;
        std::uint16_t SpawnDelay = 0;
        std::int16_t NotifyEntityId = 0;
        Message CollectedMessage{};
        std::int32_t CollectedMsgParam1 = 0;
        std::int32_t CollectedMsgParam2 = 0;

        ItemSpawnEntityEditor();
        ItemSpawnEntityEditor(const std::shared_ptr<Entity>& header, ItemSpawnEntityData raw);
        void CompareTo(const std::shared_ptr<ItemSpawnEntityEditor>& other) const;
    };

    class FhItemSpawnEntityEditor : public EntityEditorBase
    {
    public:
        FhItemType ItemType{};
        std::uint16_t SpawnLimit = 0;
        std::uint16_t CooldownTime = 0;
        std::uint16_t Unused2C = 0;

        FhItemSpawnEntityEditor();
        FhItemSpawnEntityEditor(const std::shared_ptr<Entity>& header, FhItemSpawnEntityData raw);
        void CompareTo(const std::shared_ptr<FhItemSpawnEntityEditor>& other) const;
    };

    class TriggerVolumeEntityEditor : public EntityEditorBase
    {
    public:
        TriggerType Subtype{};
        CollisionVolume Volume;
        bool Active = false;
        bool AlwaysActive = false;
        bool DeactivateAfterUse = false;
        std::uint16_t RepeatDelay = 0;
        std::uint16_t CheckDelay = 0;
        std::uint16_t RequiredStateBit = 0;
        Entities::TriggerFlags TriggerFlags{};
        std::uint32_t TriggerThreshold = 0;
        std::int16_t ParentId = 0;
        Message ParentMessage{};
        std::int32_t ParentMsgParam1 = 0;
        std::int32_t ParentMsgParam2 = 0;
        std::int16_t ChildId = 0;
        Message ChildMessage{};
        std::int32_t ChildMsgParam1 = 0;
        std::int32_t ChildMsgParam2 = 0;

        TriggerVolumeEntityEditor();
        TriggerVolumeEntityEditor(const std::shared_ptr<Entity>& header, TriggerVolumeEntityData raw);
        void CompareTo(const std::shared_ptr<TriggerVolumeEntityEditor>& other) const;
    };

    class FhTriggerVolumeEntityEditor : public EntityEditorBase
    {
    public:
        FhTriggerType Subtype{};
        CollisionVolume Box;
        CollisionVolume Sphere;
        CollisionVolume Cylinder;
        std::uint16_t OneUse = 0;
        std::uint16_t Cooldown = 0;
        Entities::FhTriggerFlags TriggerFlags{};
        std::uint32_t Threshold = 0;
        std::int16_t ParentId = 0;
        FhMessage ParentMessage{};
        std::int32_t ParentMsgParam1 = 0;
        std::int16_t ChildId = 0;
        FhMessage ChildMessage{};
        std::int32_t ChildMsgParam1 = 0;

        FhTriggerVolumeEntityEditor();
        FhTriggerVolumeEntityEditor(const std::shared_ptr<Entity>& header, FhTriggerVolumeEntityData raw);
        void CompareTo(const std::shared_ptr<FhTriggerVolumeEntityEditor>& other) const;
    };

    class AreaVolumeEntityEditor : public EntityEditorBase
    {
    public:
        CollisionVolume Volume;
        bool Active = false;
        bool AlwaysActive = false;
        bool AllowMultiple = false;
        std::uint8_t MessageDelay = 0;
        std::uint16_t Unused6A = 0;
        Message InsideMessage{};
        std::int32_t InsideMsgParam1 = 0;
        std::int32_t InsideMsgParam2 = 0;
        std::int16_t ParentId = 0;
        Message ExitMessage{};
        std::int32_t ExitMsgParam1 = 0;
        std::int32_t ExitMsgParam2 = 0;
        std::int16_t ChildId = 0;
        std::uint16_t Cooldown = 0;
        std::uint32_t Priority = 0;
        Entities::TriggerFlags TriggerFlags{};

        AreaVolumeEntityEditor();
        AreaVolumeEntityEditor(const std::shared_ptr<Entity>& header, AreaVolumeEntityData raw);
        void CompareTo(const std::shared_ptr<AreaVolumeEntityEditor>& other) const;
    };

    class FhAreaVolumeEntityEditor : public EntityEditorBase
    {
    public:
        FhTriggerType Subtype{};
        CollisionVolume Box;
        CollisionVolume Sphere;
        CollisionVolume Cylinder;
        FhMessage InsideMessage{};
        std::int32_t InsideMsgParam1 = 0;
        FhMessage ExitMessage{};
        std::int32_t ExitMsgParam1 = 0;
        std::uint16_t Cooldown = 0;
        Entities::FhTriggerFlags TriggerFlags{};

        FhAreaVolumeEntityEditor();
        FhAreaVolumeEntityEditor(const std::shared_ptr<Entity>& header, FhAreaVolumeEntityData raw);
        void CompareTo(const std::shared_ptr<FhAreaVolumeEntityEditor>& other) const;
    };

    class JumpPadEntityEditor : public EntityEditorBase
    {
    public:
        std::int32_t ParentId = 0;
        std::uint32_t Unused28 = 0;
        CollisionVolume Volume;
        OpenTK::Mathematics::Vector3 BeamVector{};
        float Speed = 0.0F;
        std::uint16_t ControlLockTime = 0;
        std::uint16_t CooldownTime = 0;
        bool Active = false;
        // Zero on the cartridge; a custom map's Quake 3 trigger kind
        // (JumpPadEntity::Q3Trigger), with its float in Unused28.
        std::uint8_t Padding81 = 0;
        // And the level's Quake units per unit.
        std::uint16_t Padding82 = 0;
        std::uint32_t ModelId = 0;
        std::uint32_t BeamType = 0;
        Entities::TriggerFlags TriggerFlags{};

        JumpPadEntityEditor();
        JumpPadEntityEditor(const std::shared_ptr<Entity>& header, JumpPadEntityData raw);
        void CompareTo(const std::shared_ptr<JumpPadEntityEditor>& other) const;
    };

    class FhJumpPadEntityEditor : public EntityEditorBase
    {
    public:
        FhTriggerType VolumeType{};
        CollisionVolume Box;
        CollisionVolume Sphere;
        CollisionVolume Cylinder;
        std::uint32_t CooldownTime = 0;
        OpenTK::Mathematics::Vector3 BeamVector{};
        float Speed = 0.0F;
        std::uint32_t ControlLockTime = 0;
        std::uint32_t ModelId = 0;
        std::uint32_t BeamType = 0;
        Entities::FhTriggerFlags TriggerFlags{};

        FhJumpPadEntityEditor();
        FhJumpPadEntityEditor(const std::shared_ptr<Entity>& header, FhJumpPadEntityData raw);
        void CompareTo(const std::shared_ptr<FhJumpPadEntityEditor>& other) const;
    };

    class PointModuleEntityEditor : public EntityEditorBase
    {
    public:
        std::int16_t NextId = 0;
        std::int16_t PrevId = 0;
        bool Active = false;

        PointModuleEntityEditor();
        PointModuleEntityEditor(const std::shared_ptr<Entity>& header, PointModuleEntityData raw);
        void CompareTo(const std::shared_ptr<PointModuleEntityEditor>& other) const;
    };

    class MorphCameraEntityEditor : public EntityEditorBase
    {
    public:
        CollisionVolume Volume;

        MorphCameraEntityEditor();
        MorphCameraEntityEditor(const std::shared_ptr<Entity>& header, MorphCameraEntityData raw);
        MorphCameraEntityEditor(const std::shared_ptr<Entity>& header, FhMorphCameraEntityData raw);
        void CompareTo(const std::shared_ptr<MorphCameraEntityEditor>& other) const;
    };

    class OctolithFlagEntityEditor : public EntityEditorBase
    {
    public:
        std::uint8_t TeamId = 0;

        OctolithFlagEntityEditor();
        OctolithFlagEntityEditor(const std::shared_ptr<Entity>& header, OctolithFlagEntityData raw);
        void CompareTo(const std::shared_ptr<OctolithFlagEntityEditor>& other) const;
    };

    class FlagBaseEntityEditor : public EntityEditorBase
    {
    public:
        std::uint32_t TeamId = 0;
        CollisionVolume Volume;

        FlagBaseEntityEditor();
        FlagBaseEntityEditor(const std::shared_ptr<Entity>& header, FlagBaseEntityData raw);
        void CompareTo(const std::shared_ptr<FlagBaseEntityEditor>& other) const;
    };

    class TeleporterEntityEditor : public EntityEditorBase
    {
    public:
        std::uint8_t LoadIndex = 0;
        std::uint8_t TargetIndex = 0;
        std::uint8_t ArtifactId = 0;
        bool Active = false;
        bool Invisible = false;
        std::shared_ptr<std::string> TargetRoom{};
        OpenTK::Mathematics::Vector3 TargetPosition{};
        std::shared_ptr<std::string> TeleporterNodeName{};

        TeleporterEntityEditor();
        TeleporterEntityEditor(const std::shared_ptr<Entity>& header, TeleporterEntityData raw);
        void CompareTo(const std::shared_ptr<TeleporterEntityEditor>& other) const;
    };

    class NodeDefenseEntityEditor : public EntityEditorBase
    {
    public:
        CollisionVolume Volume;

        NodeDefenseEntityEditor();
        NodeDefenseEntityEditor(const std::shared_ptr<Entity>& header, NodeDefenseEntityData raw);
        void CompareTo(const std::shared_ptr<NodeDefenseEntityEditor>& other) const;
    };

    class LightSourceEntityEditor : public EntityEditorBase
    {
    public:
        CollisionVolume Volume;
        bool Light1Enabled = false;
        ColorRgb Light1Color{};
        OpenTK::Mathematics::Vector3 Light1Vector{};
        bool Light2Enabled = false;
        ColorRgb Light2Color{};
        OpenTK::Mathematics::Vector3 Light2Vector{};

        LightSourceEntityEditor();
        LightSourceEntityEditor(const std::shared_ptr<Entity>& header, LightSourceEntityData raw);
        void CompareTo(const std::shared_ptr<LightSourceEntityEditor>& other) const;
    };

    class ArtifactEntityEditor : public EntityEditorBase
    {
    public:
        std::uint8_t ModelId = 0;
        std::uint8_t ArtifactId = 0;
        bool Active = false;
        bool HasBase = false;
        std::int16_t Message1Target = 0;
        Message Message1{};
        std::int16_t Message2Target = 0;
        Message Message2{};
        std::int16_t Message3Target = 0;
        Message Message3{};
        std::int16_t LinkedEntityId = 0;

        ArtifactEntityEditor();
        ArtifactEntityEditor(const std::shared_ptr<Entity>& header, ArtifactEntityData raw);
        void CompareTo(const std::shared_ptr<ArtifactEntityEditor>& other) const;
    };

    class CameraSequenceEntityEditor : public EntityEditorBase
    {
    public:
        std::uint8_t SequenceId = 0;
        bool Handoff = false;
        bool Loop = false;
        bool BlockInput = false;
        bool ForceAltForm = false;
        bool ForceBipedForm = false;
        std::uint16_t DelayFrames = 0;
        std::uint8_t PlayerId1 = 0;
        std::uint8_t PlayerId2 = 0;
        std::int16_t Entity1 = -1;
        std::int16_t Entity2 = -1;
        std::int16_t EndMessageTargetId = 0;
        Message EndMessage{};
        std::int32_t EndMessageParam = 0;

        CameraSequenceEntityEditor();
        CameraSequenceEntityEditor(const std::shared_ptr<Entity>& header, CameraSequenceEntityData raw);
        void CompareTo(const std::shared_ptr<CameraSequenceEntityEditor>& other) const;
    };

    class ForceFieldEntityEditor : public EntityEditorBase
    {
    public:
        std::uint32_t ForceFieldType = 0;
        float Width = 0.0F;
        float Height = 0.0F;
        bool Active = false;

        ForceFieldEntityEditor();
        ForceFieldEntityEditor(const std::shared_ptr<Entity>& header, ForceFieldEntityData raw);
        void CompareTo(const std::shared_ptr<ForceFieldEntityEditor>& other) const;
    };
}
