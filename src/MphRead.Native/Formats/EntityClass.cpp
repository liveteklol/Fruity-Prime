#include "EntityClass.hpp"

#include "../Entities/TriggerVolumeEntity.hpp"
#include "../NativeRuntime/System/Enum.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>
namespace
{
    std::shared_ptr<std::string> ES()
    {
        static const auto value = std::make_shared<std::string>();
        return value;
    }
    template <std::size_t N>
    std::shared_ptr<std::string> MS(const char (&value)[N])
    {
        std::size_t length = 0;
        while (length < N && value[length] != '\0')
        {
            ++length;
        }
        return length == 0 ? ES() : std::make_shared<std::string>(value, length);
    }
    using MphRead::NativeRuntime::EnumNameEntry;
    template <typename T>
    struct ManagedEnumInfo;
    // Entities\PlatformEntity.cs [Flags] PlatformFlags : uint
    constexpr EnumNameEntry PlatformFlagsNames[] = {
        {0x0ULL, "None"},
        {0x1ULL, "Hazard"},
        {0x2ULL, "ContactDamage"},
        {0x4ULL, "BeamSpawner"},
        {0x8ULL, "BeamColEffect"},
        {0x10ULL, "DamageReflect1"},
        {0x20ULL, "DamageReflect2"},
        {0x40ULL, "StandingColOnly"},
        {0x80ULL, "StartSleep"},
        {0x100ULL, "SleepAtEnd"},
        {0x200ULL, "DripMoat"},
        {0x400ULL, "SkipNodeRef"},
        {0x800ULL, "DrawIfNodeRef"},
        {0x1000ULL, "DrawAlways"},
        {0x2000ULL, "HideOnSleep"},
        {0x4000ULL, "SyluxShip"},
        {0x8000ULL, "Bit15"},
        {0x10000ULL, "BeamReflection"},
        {0x20000ULL, "UseRoomState"},
        {0x40000ULL, "BeamTarget"},
        {0x80000ULL, "SamusShip"},
        {0x100000ULL, "Breakable"},
        {0x200000ULL, "PersistRoomState"},
        {0x400000ULL, "NoBeamIfCull"},
        {0x800000ULL, "NoRecoil"},
        {0x1000000ULL, "Bit24"},
        {0x2000000ULL, "Bit25"},
        {0x4000000ULL, "Bit26"},
        {0x8000000ULL, "Bit27"},
        {0x10000000ULL, "Bit28"},
        {0x20000000ULL, "Bit29"},
        {0x40000000ULL, "Bit30"},
        {0x80000000ULL, "Bit31"},
    };
    template <> struct ManagedEnumInfo<MphRead::Entities::PlatformFlags>
    {
        static constexpr bool IsFlags = true;
        static constexpr bool IsSigned = false;
        static constexpr const EnumNameEntry* Names = PlatformFlagsNames;
        static constexpr std::size_t Count = std::size(PlatformFlagsNames);
    };
    // Entities\ObjectEntity.cs [Flags] ObjEffFlags : uint
    constexpr EnumNameEntry ObjEffFlagsNames[] = {
        {0x0ULL, "None"},
        {0x1ULL, "UseEffectVolume"},
        {0x2ULL, "UseEffectOffset"},
        {0x4ULL, "RepeatScanMessage"},
        {0x8ULL, "WeaponZoom"},
        {0x10ULL, "AttachEffect"},
        {0x20ULL, "DestroyEffect"},
        {0x40ULL, "AlwaysUpdateEffect"},
        {0x8000ULL, "Unknown"},
    };
    template <> struct ManagedEnumInfo<MphRead::Entities::ObjEffFlags>
    {
        static constexpr bool IsFlags = true;
        static constexpr bool IsSigned = false;
        static constexpr const EnumNameEntry* Names = ObjEffFlagsNames;
        static constexpr std::size_t Count = std::size(ObjEffFlagsNames);
    };
    // Entities\ObjectEntity.cs [Flags] ObjectFlags : byte
    constexpr EnumNameEntry ObjectFlagsNames[] = {
        {0x0ULL, "None"},
        {0x1ULL, "StateBit0"},
        {0x2ULL, "StateBit1"},
        {0x3ULL, "State"},
        {0x4ULL, "NoAnimation"},
        {0x8ULL, "EntityLinked"},
        {0x10ULL, "IsVisible"},
    };
    template <> struct ManagedEnumInfo<MphRead::Entities::ObjectFlags>
    {
        static constexpr bool IsFlags = true;
        static constexpr bool IsSigned = false;
        static constexpr const EnumNameEntry* Names = ObjectFlagsNames;
        static constexpr std::size_t Count = std::size(ObjectFlagsNames);
    };

    template <typename T>
    std::string ManagedEnumToString(T value)
    {
        using Info = ManagedEnumInfo<T>;
        return MphRead::NativeRuntime::ManagedEnumToString(
            value, Info::Names, Info::Count, Info::IsFlags);
    }
    // ValueType.Equals for CollisionVolume: it holds float fields, so the runtime
    // compares every declared field (overlapping ones included) with its Equals.
    bool FloatEquals(float left, float right) noexcept { return left == right || (std::isnan(left) && std::isnan(right)); }
    bool Vector3Equals(OpenTK::Mathematics::Vector3 left, OpenTK::Mathematics::Vector3 right) noexcept
    {
        return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
    }
    bool CollisionVolumeEquals(const MphRead::CollisionVolume &left, const MphRead::CollisionVolume &right) noexcept
    {
        return left.Type == right.Type
            && Vector3Equals(left.BoxVector1, right.BoxVector1)
            && Vector3Equals(left.BoxVector2, right.BoxVector2)
            && Vector3Equals(left.BoxVector3, right.BoxVector3)
            && Vector3Equals(left.BoxPosition, right.BoxPosition)
            && FloatEquals(left.BoxDot1, right.BoxDot1)
            && FloatEquals(left.BoxDot2, right.BoxDot2)
            && FloatEquals(left.BoxDot3, right.BoxDot3)
            && Vector3Equals(left.CylinderVector, right.CylinderVector)
            && Vector3Equals(left.CylinderPosition, right.CylinderPosition)
            && FloatEquals(left.CylinderRadius, right.CylinderRadius)
            && FloatEquals(left.CylinderDot, right.CylinderDot)
            && Vector3Equals(left.SpherePosition, right.SpherePosition)
            && FloatEquals(left.SphereRadius, right.SphereRadius);
    }
    bool EQ(float left, float right) noexcept { return left == right || (std::isnan(left) && std::isnan(right)); }
    bool EQ(OpenTK::Mathematics::Vector3 left, OpenTK::Mathematics::Vector3 right) noexcept { return EQ(left.X, right.X) && EQ(left.Y, right.Y) && EQ(left.Z, right.Z); }
    bool EQ(OpenTK::Mathematics::Vector4 left, OpenTK::Mathematics::Vector4 right) noexcept { return EQ(left.X, right.X) && EQ(left.Y, right.Y) && EQ(left.Z, right.Z) && EQ(left.W, right.W); }
    bool EQ(const MphRead::CollisionVolume &left, const MphRead::CollisionVolume &right) noexcept { return CollisionVolumeEquals(left, right); }
    template <typename T>
    bool EQ(const T &left, const T &right)
    {
        if constexpr (std::is_floating_point_v<T>)
        {
            return EQ(static_cast<float>(left), static_cast<float>(right));
        }
        else if constexpr (requires { left.Equals(right); })
        {
            return left.Equals(right);
        }
        else
        {
            return left == right;
        }
    }
    template <typename T>
    std::string ST(const T &value)
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            return value ? "True" : "False";
        }
        else if constexpr (std::is_same_v<T, std::int8_t> || std::is_same_v<T, std::uint8_t>)
        {
            return std::to_string(static_cast<std::int32_t>(value));
        }
        else if constexpr (std::is_integral_v<T>)
        {
            return std::to_string(value);
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            std::ostringstream stream;
            stream << value;
            return stream.str();
        }
        else if constexpr (requires { ManagedEnumInfo<T>::Count; })
        {
            return ManagedEnumToString(value);
        }
        else if constexpr (std::is_same_v<T, MphRead::CollisionVolume>)
        {
            return "MphRead.CollisionVolume";
        }
        else if constexpr (requires { ::MphRead::ToString(value); })
        {
            // The enum's own ToString, declared beside its declaration.
            return ::MphRead::ToString(value);
        }
        else if constexpr (requires { ::MphRead::Entities::ToString(value); })
        {
            return ::MphRead::Entities::ToString(value);
        }
        else if constexpr (std::is_enum_v<T>)
        {
            static_assert(!std::is_same_v<T, T>, "Exact managed enum formatting requires a shared Enum.ToString owner.");
        }
        else if constexpr (requires { value.ToString(); })
        {
            return value.ToString();
        }
        else
        {
            std::ostringstream stream;
            stream << value;
            return stream.str();
        }
    }
    std::string ST(OpenTK::Mathematics::Vector3 value) { return "(" + ST(value.X) + ", " + ST(value.Y) + ", " + ST(value.Z) + ")"; }
    std::string ST(OpenTK::Mathematics::Vector4 value) { return "(" + ST(value.X) + ", " + ST(value.Y) + ", " + ST(value.Z) + ", " + ST(value.W) + ")"; }
    std::string ST(MphRead::ColorRgb) { return "MphRead.ColorRgb"; }
    void RR(bool present)
    {
        if (!present)
        {
            throw System::NullReferenceException();
        }
    }
}
namespace MphRead::Editor
{
    using ERef = std::shared_ptr<Entity>;
    template <class T>
    using R = std::shared_ptr<T>;
    EntityEditorBase::EntityEditorBase(EntityType type) : Type(type), NodeName(ES()) {}
    EntityEditorBase::EntityEditorBase(const ERef &header)
    {
        RR(static_cast<bool>(header));
        Type = header->Type;
        Id = header->EntityId;
        LayerMask = header->LayerMask;
        Position = header->Position;
        Up = header->UpVector;
        Facing = header->FacingVector;
        NodeName = header->NodeName ? std::make_shared<std::string>(*header->NodeName) : nullptr;
    }
    EntityEditorBase::~EntityEditorBase() = default;
    void EntityEditorBase::PrintValue(const std::shared_ptr<std::string> &value1, const std::shared_ptr<std::string> &value2, const char *name) const
    {
        const bool equal = (!value1 && !value2) || (value1 && value2 && *value1 == *value2);
        if (!equal)
        {
            std::cout << name << ": " << (value1 ? *value1 : std::string()) << " / " << (value2 ? *value2 : std::string()) << '\n';
        }
    }
    template <typename T>
    void EntityEditorBase::PrintValue(const T &value1, const T &value2, const char *name) const
    {
        if (!EQ(value1, value2))
        {
            std::cout << name << ": " << ST(value1) << " / " << ST(value2) << '\n';
        }
    }
    template <typename T>
    void EntityEditorBase::PrintValues(const std::shared_ptr<std::vector<T>> &value1, const std::shared_ptr<std::vector<T>> &value2, const char *name) const
    {
        RR(static_cast<bool>(value1));
        const std::size_t count1 = value1->size();
        RR(static_cast<bool>(value2));
        const std::size_t count2 = value2->size();
        if (count1 != count2)
        {
            std::cout << name << ": " << count1 << " / " << count2 << '\n';
            for (std::size_t i = 0; i < std::max(count1, count2); ++i)
            {
                std::cout << (i < count1 ? ST((*value1)[i]) : "N/A") << " / " << (i < count2 ? ST((*value2)[i]) : "N/A") << '\n';
            }
        }
        else if (!std::equal(value1->begin(), value1->end(), value2->begin(), [](const T &left, const T &right)
                             { return EQ(left, right); }))
        {
            for (std::size_t i = 0; i < count1; ++i)
            {
                std::cout << ST((*value1)[i]) << " / " << ST((*value2)[i]) << '\n';
            }
        }
    }
#define C(field) field = raw.field
#define B(field) field = raw.field != 0
#define RO() RR(static_cast<bool>(other))
#define P(field) PrintValue(field, other->field, #field)
#define L(field) PrintValues(field, other->field, #field)
    PlatformEntityEditor::PlatformEntityEditor() : EntityEditorBase(EntityType::Platform), PortalName(ES()) {}
    PlatformEntityEditor::PlatformEntityEditor(const ERef &header, PlatformEntityData raw) : EntityEditorBase(header)
    {
        C(NoPort);
        C(ModelId);
        C(ParentId);
        B(Active);
        C(Delay);
        C(ScanData1);
        C(ScanMsgTarget);
        C(ScanMessage);
        C(ScanData2);
        C(PositionCount);
        for (std::int32_t i = 0; i < 10; ++i)
        {
            Positions->push_back(raw.Positions[i].ToFloatVector());
        }
        for (std::int32_t i = 0; i < 10; ++i)
        {
            Rotations->push_back(raw.Rotations[i].ToFloatVector());
        }
        PositionOffset = raw.PositionOffset.ToFloatVector();
        ForwardSpeed = raw.ForwardSpeed.FloatValue();
        BackwardSpeed = raw.BackwardSpeed.FloatValue();
        PortalName = MS(raw.PortalName);
        C(MovementType);
        B(ForCutscene);
        C(ReverseType);
        C(Flags);
        C(ContactDamage);
        BeamSpawnDir = raw.BeamSpawnDir.ToFloatVector();
        BeamSpawnPos = raw.BeamSpawnPos.ToFloatVector();
        C(BeamId);
        C(BeamInterval);
        C(BeamOnIntervals);
        C(ResistEffectId);
        C(Health);
        C(Effectiveness);
        C(DamageEffectId);
        C(DeadEffectId);
        C(ItemChance);
        C(ItemType);
        C(Unused1D0);
        C(Unused1D4);
        C(BeamHitMsgTarget);
        C(BeamHitMessage);
        C(BeamHitMsgParam1);
        C(BeamHitMsgParam2);
        C(PlayerColMsgTarget);
        C(PlayerColMessage);
        C(PlayerColMsgParam1);
        C(PlayerColMsgParam2);
        C(DeadMsgTarget);
        C(DeadMessage);
        C(DeadMsgParam1);
        C(DeadMsgParam2);
        C(LifetimeMsg1Index);
        C(LifetimeMsg1Target);
        C(LifetimeMessage1);
        C(LifetimeMsg1Param1);
        C(LifetimeMsg1Param2);
        C(LifetimeMsg2Index);
        C(LifetimeMsg2Target);
        C(LifetimeMessage2);
        C(LifetimeMsg2Param1);
        C(LifetimeMsg2Param2);
        C(LifetimeMsg3Index);
        C(LifetimeMsg3Target);
        C(LifetimeMessage3);
        C(LifetimeMsg3Param1);
        C(LifetimeMsg3Param2);
        C(LifetimeMsg4Index);
        C(LifetimeMsg4Target);
        C(LifetimeMessage4);
        C(LifetimeMsg4Param1);
        C(LifetimeMsg4Param2);
    }
    void PlatformEntityEditor::CompareTo(const R<PlatformEntityEditor> &other) const
    {
        RO();
        P(NoPort);
        P(ModelId);
        P(ParentId);
        P(Active);
        P(Delay);
        P(ScanData1);
        P(ScanMsgTarget);
        P(ScanMessage);
        P(ScanData2);
        P(PositionCount);
        L(Positions);
        L(Rotations);
        P(PositionOffset);
        P(ForwardSpeed);
        P(BackwardSpeed);
        P(PortalName);
        P(MovementType);
        P(ForCutscene);
        P(ReverseType);
        P(Flags);
        P(ContactDamage);
        P(BeamSpawnDir);
        P(BeamSpawnPos);
        P(BeamId);
        P(BeamInterval);
        P(BeamOnIntervals);
        P(ResistEffectId);
        P(Health);
        P(Effectiveness);
        P(DamageEffectId);
        P(DeadEffectId);
        P(ItemChance);
        P(ItemType);
        P(Unused1D0);
        P(Unused1D4);
        P(BeamHitMsgTarget);
        P(BeamHitMessage);
        P(BeamHitMsgParam1);
        P(BeamHitMsgParam2);
        P(PlayerColMsgTarget);
        P(PlayerColMessage);
        P(PlayerColMsgParam1);
        P(PlayerColMsgParam2);
        P(DeadMsgTarget);
        P(DeadMessage);
        P(DeadMsgParam1);
        P(DeadMsgParam2);
        P(LifetimeMsg1Index);
        P(LifetimeMsg1Target);
        P(LifetimeMessage1);
        P(LifetimeMsg1Param1);
        P(LifetimeMsg1Param2);
        P(LifetimeMsg2Index);
        P(LifetimeMsg2Target);
        P(LifetimeMessage2);
        P(LifetimeMsg2Param1);
        P(LifetimeMsg2Param2);
        P(LifetimeMsg3Index);
        P(LifetimeMsg3Target);
        P(LifetimeMessage3);
        P(LifetimeMsg3Param1);
        P(LifetimeMsg3Param2);
        P(LifetimeMsg4Index);
        P(LifetimeMsg4Target);
        P(LifetimeMessage4);
        P(LifetimeMsg4Param1);
        P(LifetimeMsg4Param2);
    }
    FhPlatformEntityEditor::FhPlatformEntityEditor() : EntityEditorBase(EntityType::FhPlatform), PortalName(ES()) {}
    FhPlatformEntityEditor::FhPlatformEntityEditor(const ERef &header, FhPlatformEntityData raw) : EntityEditorBase(header)
    {
        C(NoPortal);
        C(GroupId);
        C(Unused2C);
        C(Delay);
        C(PositionCount);
        Volume = CollisionVolume(raw.Volume);
        for (std::int32_t i = 0; i < 8; ++i)
        {
            Positions->push_back(raw.Positions[i].ToFloatVector());
        }
        Speed = raw.Speed.FloatValue();
        PortalName = MS(raw.PortalName);
    }
    void FhPlatformEntityEditor::CompareTo(const R<FhPlatformEntityEditor> &other) const
    {
        RO();
        P(NoPortal);
        P(GroupId);
        P(Unused2C);
        P(Delay);
        P(PositionCount);
        P(Volume);
        L(Positions);
        P(Speed);
        P(PortalName);
    }
    ObjectEntityEditor::ObjectEntityEditor() : EntityEditorBase(EntityType::Object) {}
    ObjectEntityEditor::ObjectEntityEditor(const ERef &header, ObjectEntityData raw) : EntityEditorBase(header)
    {
        C(Flags);
        C(EffectFlags);
        C(ModelId);
        C(LinkedEntity);
        C(ScanId);
        C(ScanMsgTarget);
        C(ScanMessage);
        C(EffectId);
        C(EffectInterval);
        C(EffectOnIntervals);
        EffectPositionOffset = raw.EffectPositionOffset.ToFloatVector();
        Volume = CollisionVolume(raw.Volume);
    }
    void ObjectEntityEditor::CompareTo(const R<ObjectEntityEditor> &other) const
    {
        RO();
        P(Flags);
        P(EffectFlags);
        P(ModelId);
        P(LinkedEntity);
        P(ScanId);
        P(ScanMsgTarget);
        P(ScanMessage);
        P(EffectId);
        P(EffectInterval);
        P(EffectOnIntervals);
        P(EffectPositionOffset);
        P(Volume);
    }
    PlayerSpawnEntityEditor::PlayerSpawnEntityEditor() : EntityEditorBase(EntityType::PlayerSpawn) {}
    PlayerSpawnEntityEditor::PlayerSpawnEntityEditor(const ERef &header, PlayerSpawnEntityData raw) : EntityEditorBase(header)
    {
        C(Availability);
        B(Active);
        C(TeamIndex);
    }
    void PlayerSpawnEntityEditor::CompareTo(const R<PlayerSpawnEntityEditor> &other) const
    {
        RO();
        P(Availability);
        P(Active);
        P(TeamIndex);
    }
    DoorEntityEditor::DoorEntityEditor() : EntityEditorBase(EntityType::Door), DoorNodeName(ES()), EntityFilename(ES()), RoomName(ES()) {}
    DoorEntityEditor::DoorEntityEditor(const ERef &header, DoorEntityData raw) : EntityEditorBase(header)
    {
        DoorNodeName = MS(raw.NodeName);
        C(PaletteId);
        C(DoorType);
        C(ConnectorId);
        C(TargetLayerId);
        B(Locked);
        Field42 = raw.OutConnectorId;
        Field43 = raw.OutLoaderId;
        EntityFilename = MS(raw.EntityFilename);
        RoomName = MS(raw.RoomName);
    }
    void DoorEntityEditor::CompareTo(const R<DoorEntityEditor> &other) const
    {
        RO();
        P(DoorNodeName);
        P(PaletteId);
        P(DoorType);
        P(ConnectorId);
        P(TargetLayerId);
        P(Locked);
        P(Field42);
        P(Field43);
        P(EntityFilename);
        P(RoomName);
    }
    FhDoorEntityEditor::FhDoorEntityEditor() : EntityEditorBase(EntityType::FhDoor), RoomName(ES()) {}
    FhDoorEntityEditor::FhDoorEntityEditor(const ERef &header, FhDoorEntityData raw) : EntityEditorBase(header)
    {
        RoomName = MS(raw.RoomName);
        B(Locked);
        C(ModelId);
    }
    void FhDoorEntityEditor::CompareTo(const R<FhDoorEntityEditor> &other) const
    {
        RO();
        P(RoomName);
        P(Locked);
        P(ModelId);
    }
    ItemSpawnEntityEditor::ItemSpawnEntityEditor() : EntityEditorBase(EntityType::ItemSpawn) {}
    ItemSpawnEntityEditor::ItemSpawnEntityEditor(const ERef &header, ItemSpawnEntityData raw) : EntityEditorBase(header)
    {
        C(ParentId);
        C(ItemType);
        B(Enabled);
        B(HasBase);
        B(AlwaysActive);
        C(MaxSpawnCount);
        C(SpawnInterval);
        C(SpawnDelay);
        C(NotifyEntityId);
        C(CollectedMessage);
        C(CollectedMsgParam1);
        C(CollectedMsgParam2);
    }
    void ItemSpawnEntityEditor::CompareTo(const R<ItemSpawnEntityEditor> &other) const
    {
        RO();
        P(ParentId);
        P(ItemType);
        P(Enabled);
        P(HasBase);
        P(AlwaysActive);
        P(MaxSpawnCount);
        P(SpawnInterval);
        P(SpawnDelay);
        P(NotifyEntityId);
        P(CollectedMessage);
        P(CollectedMsgParam1);
        P(CollectedMsgParam2);
    }
    FhItemSpawnEntityEditor::FhItemSpawnEntityEditor() : EntityEditorBase(EntityType::FhItemSpawn) {}
    FhItemSpawnEntityEditor::FhItemSpawnEntityEditor(const ERef &header, FhItemSpawnEntityData raw) : EntityEditorBase(header)
    {
        C(ItemType);
        C(SpawnLimit);
        C(CooldownTime);
        C(Unused2C);
    }
    void FhItemSpawnEntityEditor::CompareTo(const R<FhItemSpawnEntityEditor> &other) const
    {
        RO();
        P(ItemType);
        P(SpawnLimit);
        P(CooldownTime);
        P(Unused2C);
    }
    TriggerVolumeEntityEditor::TriggerVolumeEntityEditor() : EntityEditorBase(EntityType::TriggerVolume) {}
    TriggerVolumeEntityEditor::TriggerVolumeEntityEditor(const ERef &header, TriggerVolumeEntityData raw) : EntityEditorBase(header)
    {
        C(Subtype);
        Volume = CollisionVolume(raw.Volume);
        B(Active);
        B(AlwaysActive);
        B(DeactivateAfterUse);
        C(RepeatDelay);
        C(CheckDelay);
        C(RequiredStateBit);
        C(TriggerFlags);
        C(TriggerThreshold);
        C(ParentId);
        C(ParentMessage);
        C(ParentMsgParam1);
        C(ParentMsgParam2);
        C(ChildId);
        C(ChildMessage);
        C(ChildMsgParam1);
        C(ChildMsgParam2);
    }
    void TriggerVolumeEntityEditor::CompareTo(const R<TriggerVolumeEntityEditor> &other) const
    {
        RO();
        P(Subtype);
        P(Volume);
        P(Active);
        P(AlwaysActive);
        P(DeactivateAfterUse);
        P(RepeatDelay);
        P(CheckDelay);
        P(RequiredStateBit);
        P(TriggerFlags);
        P(TriggerThreshold);
        P(ParentId);
        P(ParentMessage);
        P(ParentMsgParam1);
        P(ParentMsgParam2);
        P(ChildId);
        P(ChildMessage);
        P(ChildMsgParam1);
        P(ChildMsgParam2);
    }
    FhTriggerVolumeEntityEditor::FhTriggerVolumeEntityEditor() : EntityEditorBase(EntityType::FhTriggerVolume) {}
    FhTriggerVolumeEntityEditor::FhTriggerVolumeEntityEditor(const ERef &header, FhTriggerVolumeEntityData raw) : EntityEditorBase(header)
    {
        C(Subtype);
        Box = CollisionVolume(raw.Box);
        Sphere = CollisionVolume(raw.Sphere);
        Cylinder = CollisionVolume(raw.Cylinder);
        C(OneUse);
        C(Cooldown);
        C(TriggerFlags);
        C(Threshold);
        C(ParentId);
        C(ParentMessage);
        C(ParentMsgParam1);
        C(ChildId);
        C(ChildMessage);
        C(ChildMsgParam1);
    }
    void FhTriggerVolumeEntityEditor::CompareTo(const R<FhTriggerVolumeEntityEditor> &other) const
    {
        RO();
        P(Subtype);
        P(Box);
        P(Sphere);
        P(Cylinder);
        P(OneUse);
        P(Cooldown);
        P(TriggerFlags);
        P(Threshold);
        P(ParentId);
        P(ParentMessage);
        P(ParentMsgParam1);
        P(ChildId);
        P(ChildMessage);
        P(ChildMsgParam1);
    }
    AreaVolumeEntityEditor::AreaVolumeEntityEditor() : EntityEditorBase(EntityType::AreaVolume) {}
    AreaVolumeEntityEditor::AreaVolumeEntityEditor(const ERef &header, AreaVolumeEntityData raw) : EntityEditorBase(header)
    {
        Volume = CollisionVolume(raw.Volume);
        B(Active);
        B(AlwaysActive);
        B(AllowMultiple);
        C(MessageDelay);
        C(Unused6A);
        C(InsideMessage);
        C(InsideMsgParam1);
        C(InsideMsgParam2);
        C(ParentId);
        C(ExitMessage);
        C(ExitMsgParam1);
        C(ExitMsgParam2);
        C(ChildId);
        C(Cooldown);
        C(Priority);
        C(TriggerFlags);
    }
    void AreaVolumeEntityEditor::CompareTo(const R<AreaVolumeEntityEditor> &other) const
    {
        RO();
        P(Volume);
        P(Active);
        P(AlwaysActive);
        P(AllowMultiple);
        P(MessageDelay);
        P(Unused6A);
        P(InsideMessage);
        P(InsideMsgParam1);
        P(InsideMsgParam2);
        P(ParentId);
        P(ExitMessage);
        P(ExitMsgParam1);
        P(ExitMsgParam2);
        P(ChildId);
        P(Cooldown);
        P(Priority);
        P(TriggerFlags);
    }
    FhAreaVolumeEntityEditor::FhAreaVolumeEntityEditor() : EntityEditorBase(EntityType::FhAreaVolume) {}
    FhAreaVolumeEntityEditor::FhAreaVolumeEntityEditor(const ERef &header, FhAreaVolumeEntityData raw) : EntityEditorBase(header)
    {
        C(Subtype);
        Box = CollisionVolume(raw.Box);
        Sphere = CollisionVolume(raw.Sphere);
        Cylinder = CollisionVolume(raw.Cylinder);
        C(InsideMessage);
        C(InsideMsgParam1);
        C(ExitMessage);
        C(ExitMsgParam1);
        C(Cooldown);
        C(TriggerFlags);
    }
    void FhAreaVolumeEntityEditor::CompareTo(const R<FhAreaVolumeEntityEditor> &other) const
    {
        RO();
        P(Subtype);
        P(Box);
        P(Sphere);
        P(Cylinder);
        P(InsideMessage);
        P(InsideMsgParam1);
        P(ExitMessage);
        P(ExitMsgParam1);
        P(Cooldown);
        P(TriggerFlags);
    }
    JumpPadEntityEditor::JumpPadEntityEditor() : EntityEditorBase(EntityType::JumpPad) {}
    JumpPadEntityEditor::JumpPadEntityEditor(const ERef &header, JumpPadEntityData raw) : EntityEditorBase(header)
    {
        C(ParentId);
        C(Unused28);
        Volume = CollisionVolume(raw.Volume);
        BeamVector = raw.BeamVector.ToFloatVector();
        Speed = raw.Speed.FloatValue();
        C(ControlLockTime);
        C(CooldownTime);
        B(Active);
        C(Padding81);
        C(Padding82);
        C(ModelId);
        C(BeamType);
        C(TriggerFlags);
    }
    void JumpPadEntityEditor::CompareTo(const R<JumpPadEntityEditor> &other) const
    {
        RO();
        P(ParentId);
        P(Unused28);
        P(Volume);
        P(BeamVector);
        P(Speed);
        P(ControlLockTime);
        P(CooldownTime);
        P(Active);
        P(Padding81);
        P(Padding82);
        P(ModelId);
        P(BeamType);
        P(TriggerFlags);
    }
    FhJumpPadEntityEditor::FhJumpPadEntityEditor() : EntityEditorBase(EntityType::FhJumpPad) {}
    FhJumpPadEntityEditor::FhJumpPadEntityEditor(const ERef &header, FhJumpPadEntityData raw) : EntityEditorBase(header)
    {
        C(VolumeType);
        Box = CollisionVolume(raw.Box);
        Cylinder = CollisionVolume(raw.Cylinder);
        Sphere = CollisionVolume(raw.Sphere);
        C(CooldownTime);
        BeamVector = raw.BeamVector.ToFloatVector();
        Speed = raw.Speed.FloatValue();
        C(ControlLockTime);
        C(ModelId);
        C(BeamType);
        C(TriggerFlags);
    }
    void FhJumpPadEntityEditor::CompareTo(const R<FhJumpPadEntityEditor> &other) const
    {
        RO();
        P(VolumeType);
        P(Box);
        P(Sphere);
        P(Cylinder);
        P(CooldownTime);
        P(BeamVector);
        P(Speed);
        P(ControlLockTime);
        P(ModelId);
        P(BeamType);
        P(TriggerFlags);
    }
    PointModuleEntityEditor::PointModuleEntityEditor() : EntityEditorBase(EntityType::PointModule) {}
    PointModuleEntityEditor::PointModuleEntityEditor(const ERef &header, PointModuleEntityData raw) : EntityEditorBase(header)
    {
        C(NextId);
        C(PrevId);
        B(Active);
    }
    void PointModuleEntityEditor::CompareTo(const R<PointModuleEntityEditor> &other) const
    {
        RO();
        P(NextId);
        P(PrevId);
        P(Active);
    }
    MorphCameraEntityEditor::MorphCameraEntityEditor() : EntityEditorBase(EntityType::MorphCamera) {}
    MorphCameraEntityEditor::MorphCameraEntityEditor(const ERef &header, MorphCameraEntityData raw) : EntityEditorBase(header) { Volume = CollisionVolume(raw.Volume); }
    MorphCameraEntityEditor::MorphCameraEntityEditor(const ERef &header, FhMorphCameraEntityData raw) : EntityEditorBase(header) { Volume = CollisionVolume(raw.Volume); }
    void MorphCameraEntityEditor::CompareTo(const R<MorphCameraEntityEditor> &other) const
    {
        RO();
        P(Volume);
    }
    OctolithFlagEntityEditor::OctolithFlagEntityEditor() : EntityEditorBase(EntityType::OctolithFlag) {}
    OctolithFlagEntityEditor::OctolithFlagEntityEditor(const ERef &header, OctolithFlagEntityData raw) : EntityEditorBase(header) { C(TeamId); }
    void OctolithFlagEntityEditor::CompareTo(const R<OctolithFlagEntityEditor> &other) const
    {
        RO();
        P(TeamId);
    }
    FlagBaseEntityEditor::FlagBaseEntityEditor() : EntityEditorBase(EntityType::FlagBase) {}
    FlagBaseEntityEditor::FlagBaseEntityEditor(const ERef &header, FlagBaseEntityData raw) : EntityEditorBase(header)
    {
        C(TeamId);
        Volume = CollisionVolume(raw.Volume);
    }
    void FlagBaseEntityEditor::CompareTo(const R<FlagBaseEntityEditor> &other) const
    {
        RO();
        P(TeamId);
        P(Volume);
    }
    TeleporterEntityEditor::TeleporterEntityEditor() : EntityEditorBase(EntityType::Teleporter), TargetRoom(ES()), TeleporterNodeName(ES()) {}
    TeleporterEntityEditor::TeleporterEntityEditor(const ERef &header, TeleporterEntityData raw) : EntityEditorBase(header)
    {
        C(LoadIndex);
        C(TargetIndex);
        C(ArtifactId);
        B(Active);
        B(Invisible);
        TargetRoom = MS(raw.EntityFilename);
        TargetPosition = raw.TargetPosition.ToFloatVector();
        TeleporterNodeName = MS(raw.NodeName);
    }
    void TeleporterEntityEditor::CompareTo(const R<TeleporterEntityEditor> &other) const
    {
        RO();
        P(LoadIndex);
        P(TargetIndex);
        P(ArtifactId);
        P(Active);
        P(Invisible);
        P(TargetRoom);
        P(TargetPosition);
        P(TeleporterNodeName);
    }
    NodeDefenseEntityEditor::NodeDefenseEntityEditor() : EntityEditorBase(EntityType::NodeDefense) {}
    NodeDefenseEntityEditor::NodeDefenseEntityEditor(const ERef &header, NodeDefenseEntityData raw) : EntityEditorBase(header) { Volume = CollisionVolume(raw.Volume); }
    void NodeDefenseEntityEditor::CompareTo(const R<NodeDefenseEntityEditor> &other) const
    {
        RO();
        P(Volume);
    }
    LightSourceEntityEditor::LightSourceEntityEditor() : EntityEditorBase(EntityType::LightSource) {}
    LightSourceEntityEditor::LightSourceEntityEditor(const ERef &header, LightSourceEntityData raw) : EntityEditorBase(header)
    {
        Volume = CollisionVolume(raw.Volume);
        B(Light1Enabled);
        C(Light1Color);
        Light1Vector = raw.Light1Vector.ToFloatVector();
        B(Light2Enabled);
        C(Light2Color);
        Light2Vector = raw.Light2Vector.ToFloatVector();
    }
    void LightSourceEntityEditor::CompareTo(const R<LightSourceEntityEditor> &other) const
    {
        RO();
        P(Volume);
        P(Light1Enabled);
        P(Light1Color);
        P(Light1Vector);
        P(Light2Enabled);
        P(Light2Color);
        P(Light2Vector);
    }
    ArtifactEntityEditor::ArtifactEntityEditor() : EntityEditorBase(EntityType::Artifact) {}
    ArtifactEntityEditor::ArtifactEntityEditor(const ERef &header, ArtifactEntityData raw) : EntityEditorBase(header)
    {
        C(ModelId);
        C(ArtifactId);
        B(Active);
        B(HasBase);
        C(Message1Target);
        C(Message1);
        C(Message2Target);
        C(Message2);
        C(Message3Target);
        C(Message3);
        C(LinkedEntityId);
    }
    void ArtifactEntityEditor::CompareTo(const R<ArtifactEntityEditor> &other) const
    {
        RO();
        P(ModelId);
        P(ArtifactId);
        P(Active);
        P(HasBase);
        P(Message1Target);
        P(Message1);
        P(Message2Target);
        P(Message2);
        P(Message3Target);
        P(Message3);
        P(LinkedEntityId);
    }
    CameraSequenceEntityEditor::CameraSequenceEntityEditor() : EntityEditorBase(EntityType::CameraSequence) {}
    CameraSequenceEntityEditor::CameraSequenceEntityEditor(const ERef &header, CameraSequenceEntityData raw) : EntityEditorBase(header)
    {
        C(SequenceId);
        B(Handoff);
        B(Loop);
        B(BlockInput);
        B(ForceAltForm);
        B(ForceBipedForm);
        C(DelayFrames);
        C(PlayerId1);
        C(PlayerId2);
        C(Entity1);
        C(Entity2);
        C(EndMessageTargetId);
        C(EndMessage);
        C(EndMessageParam);
    }
    void CameraSequenceEntityEditor::CompareTo(const R<CameraSequenceEntityEditor> &other) const
    {
        RO();
        P(SequenceId);
        P(Handoff);
        P(Loop);
        P(BlockInput);
        P(ForceAltForm);
        P(ForceBipedForm);
        P(DelayFrames);
        P(PlayerId1);
        P(PlayerId2);
        P(Entity1);
        P(Entity2);
        P(EndMessageTargetId);
        P(EndMessage);
        P(EndMessageParam);
    }
    ForceFieldEntityEditor::ForceFieldEntityEditor() : EntityEditorBase(EntityType::ForceField) {}
    ForceFieldEntityEditor::ForceFieldEntityEditor(const ERef &header, ForceFieldEntityData raw) : EntityEditorBase(header)
    {
        ForceFieldType = raw.Type;
        Width = raw.Width.FloatValue();
        Height = raw.Height.FloatValue();
        B(Active);
    }
    void ForceFieldEntityEditor::CompareTo(const R<ForceFieldEntityEditor> &other) const
    {
        RO();
        P(ForceFieldType);
        P(Width);
        P(Height);
        P(Active);
    }
#undef C
#undef B
#undef RO
#undef P
#undef L
}

namespace MphRead::Editor
{
    // The instantiations the editor's own comparisons need. C# reifies a
    // generic on demand; C++ needs each one named.
    template void EntityEditorBase::PrintValue<bool>(
        const bool&, const bool&, const char*) const;
    template void EntityEditorBase::PrintValue<float>(
        const float&, const float&, const char*) const;
    template void EntityEditorBase::PrintValue<std::int32_t>(
        const std::int32_t&, const std::int32_t&, const char*) const;
    template void EntityEditorBase::PrintValue<std::int16_t>(
        const std::int16_t&, const std::int16_t&, const char*) const;
    template void EntityEditorBase::PrintValue<std::uint8_t>(
        const std::uint8_t&, const std::uint8_t&, const char*) const;
    template void EntityEditorBase::PrintValue<std::uint32_t>(
        const std::uint32_t&, const std::uint32_t&, const char*) const;
    template void EntityEditorBase::PrintValue<std::uint16_t>(
        const std::uint16_t&, const std::uint16_t&, const char*) const;
    template void EntityEditorBase::PrintValue<::MphRead::CollisionVolume>(
        const ::MphRead::CollisionVolume&, const ::MphRead::CollisionVolume&, const char*) const;
    template void EntityEditorBase::PrintValue<::MphRead::EnemyType>(
        const ::MphRead::EnemyType&, const ::MphRead::EnemyType&, const char*) const;
    template void EntityEditorBase::PrintValue<::MphRead::FhEnemyType>(
        const ::MphRead::FhEnemyType&, const ::MphRead::FhEnemyType&, const char*) const;
    template void EntityEditorBase::PrintValue<::MphRead::FhMessage>(
        const ::MphRead::FhMessage&, const ::MphRead::FhMessage&, const char*) const;
    template void EntityEditorBase::PrintValue<::MphRead::Hunter>(
        const ::MphRead::Hunter&, const ::MphRead::Hunter&, const char*) const;
    template void EntityEditorBase::PrintValue<::MphRead::ItemType>(
        const ::MphRead::ItemType&, const ::MphRead::ItemType&, const char*) const;
    template void EntityEditorBase::PrintValue<::MphRead::Message>(
        const ::MphRead::Message&, const ::MphRead::Message&, const char*) const;
    template void EntityEditorBase::PrintValue<::OpenTK::Mathematics::Vector3>(
        const ::OpenTK::Mathematics::Vector3&, const ::OpenTK::Mathematics::Vector3&, const char*) const;
    template void EntityEditorBase::PrintValues<::OpenTK::Mathematics::Vector3>(
        const std::shared_ptr<std::vector<::OpenTK::Mathematics::Vector3>>&,
        const std::shared_ptr<std::vector<::OpenTK::Mathematics::Vector3>>&,
        const char*) const;
}
