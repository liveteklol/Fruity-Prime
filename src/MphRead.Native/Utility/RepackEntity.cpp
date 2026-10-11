#include "RepackEntity.hpp"

#include "../Entities/EntityBase.hpp"
#include "../Entities/PlatformEntity.hpp"
#include "../Entities/TriggerVolumeEntity.hpp"
#include "../Formats/EntityClass.hpp"
#include "../Formats/EntityEnemy.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Metadata/Rooms.hpp"
#include "../Program.hpp"
#include "../Read.hpp"
#include "../NativeRuntime/System/Globalization.hpp"
#include "../NativeRuntime/System/IO.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if defined(DEBUG)
#define REPACK_DEBUG_ASSERT(condition) do { if (!(condition)) { std::abort(); } } while (false)
#else
#define REPACK_DEBUG_ASSERT(condition) do { } while (false)
#endif

using ::MphRead::NativeRuntime::FileReadAllBytes;
using ::MphRead::NativeRuntime::FileWriteAllBytes;
using ::MphRead::NativeRuntime::StringTrim;

namespace MphRead::Utility
{
    std::size_t BinaryWriter::Position() const noexcept
    {
        return _position;
    }

    void BinaryWriter::Position(std::size_t value)
    {
        _position = value;
        if (_position > _bytes.size())
        {
            _bytes.resize(_position, 0);
        }
    }

    const std::vector<std::uint8_t>& BinaryWriter::Bytes() const noexcept
    {
        return _bytes;
    }

    std::vector<std::uint8_t> BinaryWriter::ToArray() const
    {
        return _bytes;
    }

    void BinaryWriter::WriteRaw(std::uint32_t value, std::size_t count)
    {
        if (_position + count > _bytes.size())
        {
            _bytes.resize(_position + count, 0);
        }
        for (std::size_t i = 0; i < count; ++i)
        {
            _bytes[_position++] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFU);
        }
    }

    void BinaryWriter::Write(std::uint8_t value) { WriteRaw(value, 1); }
    void BinaryWriter::Write(std::int8_t value) { WriteRaw(static_cast<std::uint8_t>(value), 1); }
    void BinaryWriter::Write(std::uint16_t value) { WriteRaw(value, 2); }
    void BinaryWriter::Write(std::int16_t value) { WriteRaw(static_cast<std::uint16_t>(value), 2); }
    void BinaryWriter::Write(std::uint32_t value) { WriteRaw(value, 4); }
    void BinaryWriter::Write(std::int32_t value) { WriteRaw(static_cast<std::uint32_t>(value), 4); }

    void BinaryWriter::WriteString(const std::string& value, std::size_t length)
    {
        REPACK_DEBUG_ASSERT(value.size() <= length);
        std::size_t i = 0;
        for (; i < value.size(); ++i)
        {
            Write(static_cast<std::uint8_t>(value[i]));
        }
        for (; i < length; ++i)
        {
            Write(static_cast<std::uint8_t>(0));
        }
    }

    void BinaryWriter::WriteFloat(float value)
    {
        Write(Fixed::ToInt(value));
    }

    void BinaryWriter::WriteVector3(OpenTK::Mathematics::Vector3 value)
    {
        WriteFloat(value.X);
        WriteFloat(value.Y);
        WriteFloat(value.Z);
    }

    void BinaryWriter::WriteVector4(OpenTK::Mathematics::Vector4 value)
    {
        WriteFloat(value.X);
        WriteFloat(value.Y);
        WriteFloat(value.Z);
        WriteFloat(value.W);
    }

    void BinaryWriter::WriteColorRgb(ColorRgb value)
    {
        Write(value.Red);
        Write(value.Green);
        Write(value.Blue);
    }

    void BinaryWriter::WriteByte(bool value)
    {
        Write(static_cast<std::uint8_t>(value ? 1 : 0));
    }

    void BinaryWriter::WriteInt(bool value)
    {
        Write(static_cast<std::uint32_t>(value ? 1 : 0));
    }

    namespace
    {
        using Editor::AreaVolumeEntityEditor;
        using Editor::ArtifactEntityEditor;
        using Editor::CameraSequenceEntityEditor;
        using Editor::DoorEntityEditor;
        using Editor::EnemySpawnEntityEditor;
        using Editor::EntityEditorBase;
        using Editor::FhAreaVolumeEntityEditor;
        using Editor::FhDoorEntityEditor;
        using Editor::FhEnemySpawnEntityEditor;
        using Editor::FhItemSpawnEntityEditor;
        using Editor::FhJumpPadEntityEditor;
        using Editor::FhPlatformEntityEditor;
        using Editor::FhTriggerVolumeEntityEditor;
        using Editor::FlagBaseEntityEditor;
        using Editor::ForceFieldEntityEditor;
        using Editor::ItemSpawnEntityEditor;
        using Editor::JumpPadEntityEditor;
        using Editor::LightSourceEntityEditor;
        using Editor::MorphCameraEntityEditor;
        using Editor::NodeDefenseEntityEditor;
        using Editor::ObjectEntityEditor;
        using Editor::OctolithFlagEntityEditor;
        using Editor::PlatformEntityEditor;
        using Editor::PlayerSpawnEntityEditor;
        using Editor::PointModuleEntityEditor;
        using Editor::TeleporterEntityEditor;
        using Editor::TriggerVolumeEntityEditor;
        using EditorPtr = std::shared_ptr<EntityEditorBase>;
        using EditorList = std::vector<EditorPtr>;
        using Vec3 = OpenTK::Mathematics::Vector3;
        using Vec4 = OpenTK::Mathematics::Vector4;

        [[nodiscard]] std::span<const std::uint8_t> Span(const std::vector<std::uint8_t>& bytes) noexcept
        {
            return std::span<const std::uint8_t>(bytes.data(), bytes.size());
        }

        [[nodiscard]] std::shared_ptr<std::string> S(std::string value)
        {
            return std::make_shared<std::string>(std::move(value));
        }

        [[nodiscard]] const std::string& R(const std::shared_ptr<std::string>& value)
        {
            if (!value)
            {
                throw System::NullReferenceException();
            }
            return *value;
        }

        [[nodiscard]] Vec3 Zero3() noexcept { return Vec3(0.0F, 0.0F, 0.0F); }
        [[nodiscard]] Vec3 One3() noexcept { return Vec3(1.0F, 1.0F, 1.0F); }
        [[nodiscard]] Vec3 UnitZ() noexcept { return Vec3(0.0F, 0.0F, 1.0F); }
        [[nodiscard]] Vec4 UnitW() noexcept { return Vec4(0.0F, 0.0F, 0.0F, 1.0F); }

        [[nodiscard]] bool Vec3Equals(Vec3 left, Vec3 right) noexcept
        {
            return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
        }

        [[nodiscard]] bool Vec4Equals(Vec4 left, Vec4 right) noexcept
        {
            return left.X == right.X && left.Y == right.Y && left.Z == right.Z && left.W == right.W;
        }

        [[nodiscard]] std::string FloatText(float value)
        {
            std::ostringstream stream;
            stream << value;
            return stream.str();
        }

        [[nodiscard]] std::string Vec3Text(Vec3 value)
        {
            return "(" + FloatText(value.X) + ", " + FloatText(value.Y) + ", " + FloatText(value.Z) + ")";
        }

        [[nodiscard]] bool StringEquals(
            const std::shared_ptr<std::string>& left,
            const std::shared_ptr<std::string>& right) noexcept
        {
            return (!left && !right) || (left && right && *left == *right);
        }

        template <typename T>
        [[nodiscard]] constexpr auto U(T value) noexcept
        {
            return static_cast<std::underlying_type_t<T>>(value);
        }

        template <typename T>
        [[nodiscard]] constexpr bool Has(T value, T flag) noexcept
        {
            using V = std::make_unsigned_t<std::underlying_type_t<T>>;
            return (static_cast<V>(U(value)) & static_cast<V>(U(flag))) != 0;
        }

        [[nodiscard]] std::string EntityTypeText(EntityType type)
        {
            return ::MphRead::ToString(type);
        }

        [[nodiscard]] std::string FhItemTypeText(FhItemType type)
        {
            return ::MphRead::ToString(type);
        }

        [[nodiscard]] std::string EnemyTypeText(EnemyType type)
        {
            return ::MphRead::ToString(type);
        }

        [[nodiscard]] std::string FhEnemyTypeText(FhEnemyType type)
        {
            return ::MphRead::ToString(type);
        }

        [[nodiscard]] std::string TriggerTypeText(TriggerType type)
        {
            return ::MphRead::ToString(type);
        }

        [[nodiscard]] std::string FhTriggerTypeText(FhTriggerType type)
        {
            return ::MphRead::ToString(type);
        }

        [[nodiscard]] std::string MessageText(Message message)
        {
            return ::MphRead::ToString(message);
        }

        [[nodiscard]] std::string FhMessageText(FhMessage message)
        {
            return ::MphRead::ToString(message);
        }

        [[nodiscard]] bool IsDefinedEntityType(EntityType type) noexcept
        {
            switch (type)
            {
            case EntityType::Platform: case EntityType::Object: case EntityType::PlayerSpawn:
            case EntityType::Door: case EntityType::ItemSpawn: case EntityType::ItemInstance:
            case EntityType::EnemySpawn: case EntityType::TriggerVolume: case EntityType::AreaVolume:
            case EntityType::JumpPad: case EntityType::PointModule: case EntityType::MorphCamera:
            case EntityType::OctolithFlag: case EntityType::FlagBase: case EntityType::Teleporter:
            case EntityType::NodeDefense: case EntityType::LightSource: case EntityType::Artifact:
            case EntityType::CameraSequence: case EntityType::ForceField: case EntityType::BeamEffect:
            case EntityType::Bomb: case EntityType::EnemyInstance: case EntityType::Halfturret:
            case EntityType::Player: case EntityType::BeamProjectile: case EntityType::ListHead:
            case EntityType::FhUnknown0: case EntityType::FhPlayerSpawn: case EntityType::FhUnknown2:
            case EntityType::FhDoor: case EntityType::FhItemSpawn: case EntityType::FhItemInstance:
            case EntityType::FhEnemySpawn: case EntityType::FhEffectInstance: case EntityType::FhBomb:
            case EntityType::FhTriggerVolume: case EntityType::FhAreaVolume: case EntityType::FhPlatform:
            case EntityType::FhJumpPad: case EntityType::FhPointModule: case EntityType::FhMorphCamera:
            case EntityType::FhEnemyInstance: case EntityType::FhPlayer: case EntityType::FhBeamProjectile:
            case EntityType::Room: case EntityType::Model: case EntityType::All:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool ValidMph(EntityType type) noexcept
        {
            switch (type)
            {
            case EntityType::Platform: case EntityType::Object: case EntityType::PlayerSpawn:
            case EntityType::Door: case EntityType::ItemSpawn: case EntityType::EnemySpawn:
            case EntityType::TriggerVolume: case EntityType::AreaVolume: case EntityType::JumpPad:
            case EntityType::PointModule: case EntityType::MorphCamera: case EntityType::OctolithFlag:
            case EntityType::FlagBase: case EntityType::Teleporter: case EntityType::NodeDefense:
            case EntityType::LightSource: case EntityType::Artifact: case EntityType::CameraSequence:
            case EntityType::ForceField:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] bool ValidFh(EntityType type) noexcept
        {
            switch (type)
            {
            case EntityType::FhUnknown0: case EntityType::FhPlayerSpawn: case EntityType::FhUnknown2:
            case EntityType::FhDoor: case EntityType::FhItemSpawn: case EntityType::FhEnemySpawn:
            case EntityType::FhTriggerVolume: case EntityType::FhAreaVolume: case EntityType::FhPlatform:
            case EntityType::FhJumpPad: case EntityType::FhPointModule: case EntityType::FhMorphCamera:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] bool ValidFhEnemy(EnemyType type) noexcept
        {
            switch (type)
            {
            case EnemyType::WarWasp: case EnemyType::BarbedWarWasp: case EnemyType::Zoomer:
            case EnemyType::Geemer: case EnemyType::Temroid: case EnemyType::Petrasyl1:
            case EnemyType::Petrasyl2: case EnemyType::Petrasyl3: case EnemyType::Petrasyl4:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] const RoomMetadata& Room(const std::string& name)
        {
            return *Metadata::RoomMetadata.at(name);
        }

        [[nodiscard]] std::string EntityPath(const RoomMetadata& meta)
        {
            REPACK_DEBUG_ASSERT(meta.EntityPath.has_value());
            if (!meta.EntityPath)
            {
                throw System::NullReferenceException();
            }
            return *meta.EntityPath;
        }

        void Nop() noexcept
        {
        }

        void DebugBreak()
        {
#if defined(_WIN32)
            __debugbreak();
#else
            std::raise(SIGTRAP);
#endif
        }

        [[noreturn]] void ThrowInvalidOperation()
        {
            throw std::logic_error("Operation is not valid due to the current state of the object.");
        }

        [[noreturn]] void ThrowMoreThanOneMatching()
        {
            throw std::logic_error("Sequence contains more than one matching element");
        }

        [[noreturn]] void ThrowNoMatching()
        {
            throw std::logic_error("Sequence contains no matching element");
        }

        [[noreturn]] void ThrowNoElements()
        {
            throw std::logic_error("Sequence contains no elements");
        }

        [[nodiscard]] std::int16_t IncrementInt16Unchecked(std::int16_t value) noexcept
        {
            std::uint16_t bits = std::bit_cast<std::uint16_t>(value);
            bits = static_cast<std::uint16_t>(bits + 1U);
            return std::bit_cast<std::int16_t>(bits);
        }

        [[nodiscard]] Message ToMphMessage(FhMessage message) noexcept
        {
            switch (message)
            {
            case FhMessage::None: return Message::None;
            case FhMessage::Activate: return Message::Activate;
            case FhMessage::Destroyed: return Message::Destroyed;
            case FhMessage::Damage: return Message::Damage;
            case FhMessage::Trigger: return Message::Trigger;
            case FhMessage::Gravity: return Message::Gravity;
            case FhMessage::Unlock: return Message::Unlock;
            case FhMessage::SetActive: return Message::SetActive;
            case FhMessage::Complete: return Message::Complete;
            case FhMessage::Impact: return Message::Impact;
            case FhMessage::Death: return Message::Death;
            case FhMessage::Unknown21: return Message::Unused22;
            }
            return Message::UnlockOubliette;
        }

        [[nodiscard]] FhMessage ToFhMessage(Message message) noexcept
        {
            switch (message)
            {
            case Message::None: return FhMessage::None;
            case Message::Activate: return FhMessage::Activate;
            case Message::Destroyed: return FhMessage::Destroyed;
            case Message::Damage: return FhMessage::Damage;
            case Message::Trigger: return FhMessage::Trigger;
            case Message::Gravity: return FhMessage::Gravity;
            case Message::Unlock: return FhMessage::Unlock;
            case Message::SetActive: return FhMessage::SetActive;
            case Message::Complete: return FhMessage::Complete;
            case Message::Impact: return FhMessage::Impact;
            case Message::Death: return FhMessage::Death;
            case Message::Unused22: return FhMessage::Unknown21;
            default: return static_cast<FhMessage>(255);
            }
        }

        [[nodiscard]] Entities::TriggerFlags ToMphFlags(
            Entities::FhTriggerFlags fhFlags, FhTriggerType subtype) noexcept
        {
            Entities::TriggerFlags flags = Entities::TriggerFlags::None;
            if (subtype != FhTriggerType::Threshold)
            {
                if (Has(fhFlags, Entities::FhTriggerFlags::Beam))
                {
                    flags |= Entities::TriggerFlags::PowerBeam;
                    flags |= Entities::TriggerFlags::VoltDriver;
                    flags |= Entities::TriggerFlags::Missile;
                    flags |= Entities::TriggerFlags::Battlehammer;
                    flags |= Entities::TriggerFlags::Imperialist;
                    flags |= Entities::TriggerFlags::Judicator;
                    flags |= Entities::TriggerFlags::Magmaul;
                    flags |= Entities::TriggerFlags::ShockCoil;
                }
                if (Has(fhFlags, Entities::FhTriggerFlags::PlayerBiped))
                {
                    flags |= Entities::TriggerFlags::PlayerBiped;
                }
                if (Has(fhFlags, Entities::FhTriggerFlags::PlayerAlt))
                {
                    flags |= Entities::TriggerFlags::PlayerAlt;
                }
            }
            return flags;
        }

        [[nodiscard]] Entities::FhTriggerFlags ToFhFlags(
            Entities::TriggerFlags mphFlags, TriggerType subtype) noexcept
        {
            Entities::FhTriggerFlags flags = Entities::FhTriggerFlags::None;
            if (subtype == TriggerType::Volume)
            {
                if (Has(mphFlags, Entities::TriggerFlags::PowerBeam)
                    || Has(mphFlags, Entities::TriggerFlags::VoltDriver)
                    || Has(mphFlags, Entities::TriggerFlags::Missile)
                    || Has(mphFlags, Entities::TriggerFlags::Battlehammer)
                    || Has(mphFlags, Entities::TriggerFlags::Imperialist)
                    || Has(mphFlags, Entities::TriggerFlags::Judicator)
                    || Has(mphFlags, Entities::TriggerFlags::ShockCoil)
                    || Has(mphFlags, Entities::TriggerFlags::ShockCoil))
                {
                    flags = static_cast<Entities::FhTriggerFlags>(
                        U(flags) | U(Entities::FhTriggerFlags::Beam));
                }
                if (Has(mphFlags, Entities::TriggerFlags::PlayerBiped))
                {
                    flags = static_cast<Entities::FhTriggerFlags>(
                        U(flags) | U(Entities::FhTriggerFlags::PlayerBiped));
                }
                if (Has(mphFlags, Entities::TriggerFlags::PlayerAlt))
                {
                    flags = static_cast<Entities::FhTriggerFlags>(
                        U(flags) | U(Entities::FhTriggerFlags::PlayerAlt));
                }
            }
            return flags;
        }

        struct Matrix3Value
        {
            float M11 = 0.0F;
            float M12 = 0.0F;
            float M13 = 0.0F;
            float M21 = 0.0F;
            float M22 = 0.0F;
            float M23 = 0.0F;
            float M31 = 0.0F;
            float M32 = 0.0F;
            float M33 = 0.0F;
        };

        [[nodiscard]] Matrix3Value Matrix3From(const OpenTK::Mathematics::Matrix4& value) noexcept
        {
            return {
                value.M11, value.M12, value.M13,
                value.M21, value.M22, value.M23,
                value.M31, value.M32, value.M33
            };
        }

        [[nodiscard]] bool IsIdentity(const Matrix3Value& value) noexcept
        {
            return value.M11 == 1.0F && value.M12 == 0.0F && value.M13 == 0.0F
                && value.M21 == 0.0F && value.M22 == 1.0F && value.M23 == 0.0F
                && value.M31 == 0.0F && value.M32 == 0.0F && value.M33 == 1.0F;
        }

        [[nodiscard]] Vec3 Multiply(Vec3 vector, const Matrix3Value& matrix) noexcept
        {
            return Vec3(
                vector.X * matrix.M11 + vector.Y * matrix.M21 + vector.Z * matrix.M31,
                vector.X * matrix.M12 + vector.Y * matrix.M22 + vector.Z * matrix.M32,
                vector.X * matrix.M13 + vector.Y * matrix.M23 + vector.Z * matrix.M33);
        }

        [[nodiscard]] Matrix3Value Invert(const Matrix3Value& matrix)
        {
            const float determinant = matrix.M11 * (matrix.M22 * matrix.M33 - matrix.M23 * matrix.M32)
                - matrix.M12 * (matrix.M21 * matrix.M33 - matrix.M23 * matrix.M31)
                + matrix.M13 * (matrix.M21 * matrix.M32 - matrix.M22 * matrix.M31);
            const float inverse = 1.0F / determinant;
            return {
                (matrix.M22 * matrix.M33 - matrix.M23 * matrix.M32) * inverse,
                (matrix.M13 * matrix.M32 - matrix.M12 * matrix.M33) * inverse,
                (matrix.M12 * matrix.M23 - matrix.M13 * matrix.M22) * inverse,
                (matrix.M23 * matrix.M31 - matrix.M21 * matrix.M33) * inverse,
                (matrix.M11 * matrix.M33 - matrix.M13 * matrix.M31) * inverse,
                (matrix.M13 * matrix.M21 - matrix.M11 * matrix.M23) * inverse,
                (matrix.M21 * matrix.M32 - matrix.M22 * matrix.M31) * inverse,
                (matrix.M12 * matrix.M31 - matrix.M11 * matrix.M32) * inverse,
                (matrix.M11 * matrix.M22 - matrix.M12 * matrix.M21) * inverse
            };
        }

        template <typename TData, typename TEditor>
        void AddEditor(EditorList& result, const std::shared_ptr<Entity>& entity)
        {
            const auto typed = std::dynamic_pointer_cast<EntityOf<TData>>(entity);
            if (!typed)
            {
                throw std::bad_cast();
            }
            result.push_back(std::make_shared<TEditor>(entity, typed->Data));
        }

        [[nodiscard]] EditorList GetFhEntities(const std::string& path, bool fullPath = false)
        {
            EditorList entities;
            const auto source = fullPath
                ? Read::GetEntitiesFromPath(path, -1, true)
                : Read::GetEntities(path, -1, true);
            for (const auto& entity : *source)
            {
                switch (entity->Type)
                {
                case EntityType::FhPlatform:
                    AddEditor<FhPlatformEntityData, FhPlatformEntityEditor>(entities, entity);
                    break;
                case EntityType::FhPlayerSpawn:
                    AddEditor<PlayerSpawnEntityData, PlayerSpawnEntityEditor>(entities, entity);
                    break;
                case EntityType::FhDoor:
                    AddEditor<FhDoorEntityData, FhDoorEntityEditor>(entities, entity);
                    break;
                case EntityType::FhItemSpawn:
                    AddEditor<FhItemSpawnEntityData, FhItemSpawnEntityEditor>(entities, entity);
                    break;
                case EntityType::FhEnemySpawn:
                    AddEditor<FhEnemySpawnEntityData, FhEnemySpawnEntityEditor>(entities, entity);
                    break;
                case EntityType::FhTriggerVolume:
                    AddEditor<FhTriggerVolumeEntityData, FhTriggerVolumeEntityEditor>(entities, entity);
                    break;
                case EntityType::FhAreaVolume:
                    AddEditor<FhAreaVolumeEntityData, FhAreaVolumeEntityEditor>(entities, entity);
                    break;
                case EntityType::FhJumpPad:
                    AddEditor<FhJumpPadEntityData, FhJumpPadEntityEditor>(entities, entity);
                    break;
                case EntityType::FhPointModule:
                    AddEditor<PointModuleEntityData, PointModuleEntityEditor>(entities, entity);
                    break;
                case EntityType::FhMorphCamera:
                    AddEditor<FhMorphCameraEntityData, MorphCameraEntityEditor>(entities, entity);
                    break;
                default:
                    break;
                }
            }
            return entities;
        }

        [[nodiscard]] EditorList GetEntities(
            const std::string& path, RepackFilter filter = RepackFilter::All, bool fullPath = false)
        {
            EditorList entities;
            std::int32_t layerId = -1;
            if (filter != RepackFilter::All)
            {
                layerId = filter == RepackFilter::Multiplayer
                    ? Metadata::GetMultiplayerEntityLayer(GameMode::Battle, 2)
                    : 0;
            }
            const auto source = fullPath
                ? Read::GetEntitiesFromPath(path, layerId, false)
                : Read::GetEntities(path, layerId, false);
            for (const auto& entity : *source)
            {
                switch (entity->Type)
                {
                case EntityType::Platform: AddEditor<PlatformEntityData, PlatformEntityEditor>(entities, entity); break;
                case EntityType::Object: AddEditor<ObjectEntityData, ObjectEntityEditor>(entities, entity); break;
                case EntityType::PlayerSpawn: AddEditor<PlayerSpawnEntityData, PlayerSpawnEntityEditor>(entities, entity); break;
                case EntityType::Door: AddEditor<DoorEntityData, DoorEntityEditor>(entities, entity); break;
                case EntityType::ItemSpawn: AddEditor<ItemSpawnEntityData, ItemSpawnEntityEditor>(entities, entity); break;
                case EntityType::EnemySpawn: AddEditor<EnemySpawnEntityData, EnemySpawnEntityEditor>(entities, entity); break;
                case EntityType::TriggerVolume: AddEditor<TriggerVolumeEntityData, TriggerVolumeEntityEditor>(entities, entity); break;
                case EntityType::AreaVolume: AddEditor<AreaVolumeEntityData, AreaVolumeEntityEditor>(entities, entity); break;
                case EntityType::JumpPad: AddEditor<JumpPadEntityData, JumpPadEntityEditor>(entities, entity); break;
                case EntityType::PointModule: AddEditor<PointModuleEntityData, PointModuleEntityEditor>(entities, entity); break;
                case EntityType::MorphCamera: AddEditor<MorphCameraEntityData, MorphCameraEntityEditor>(entities, entity); break;
                case EntityType::OctolithFlag: AddEditor<OctolithFlagEntityData, OctolithFlagEntityEditor>(entities, entity); break;
                case EntityType::FlagBase: AddEditor<FlagBaseEntityData, FlagBaseEntityEditor>(entities, entity); break;
                case EntityType::Teleporter: AddEditor<TeleporterEntityData, TeleporterEntityEditor>(entities, entity); break;
                case EntityType::NodeDefense: AddEditor<NodeDefenseEntityData, NodeDefenseEntityEditor>(entities, entity); break;
                case EntityType::LightSource: AddEditor<LightSourceEntityData, LightSourceEntityEditor>(entities, entity); break;
                case EntityType::Artifact: AddEditor<ArtifactEntityData, ArtifactEntityEditor>(entities, entity); break;
                case EntityType::CameraSequence: AddEditor<CameraSequenceEntityData, CameraSequenceEntityEditor>(entities, entity); break;
                case EntityType::ForceField: AddEditor<ForceFieldEntityData, ForceFieldEntityEditor>(entities, entity); break;
                default: break;
                }
            }
            return entities;
        }

        void EnsureActiveSpawn(EditorList& converted, EntityType type, const char* warning)
        {
            std::vector<std::shared_ptr<PlayerSpawnEntityEditor>> spawns;
            for (const auto& entity : converted)
            {
                if (entity->Type == type)
                {
                    const auto spawn = std::dynamic_pointer_cast<PlayerSpawnEntityEditor>(entity);
                    if (!spawn)
                    {
                        throw std::bad_cast();
                    }
                    spawns.push_back(spawn);
                }
            }
            if (spawns.empty())
            {
                std::cout << warning << '\n';
            }
            else if (std::none_of(spawns.begin(), spawns.end(), [](const auto& spawn)
                {
                    return spawn->Active;
                }))
            {
                spawns.front()->Active = true;
            }
        }

        [[nodiscard]] EditorList ConvertFhToMph(const EditorList& entities)
        {
            EditorList converted;
            for (const auto& entity : entities)
            {
                if (const auto platform = std::dynamic_pointer_cast<FhPlatformEntityEditor>(entity))
                {
                    auto mph = std::make_shared<PlatformEntityEditor>();
                    mph->Id = platform->Id;
                    mph->Active = true;
                    mph->BackwardSpeed = platform->Speed / 2.0F;
                    mph->BeamHitMessage = Message::None;
                    mph->BeamHitMsgParam1 = 0;
                    mph->BeamHitMsgParam2 = 0;
                    mph->BeamHitMsgTarget = 0xFFFF;
                    mph->BeamId = 0;
                    mph->BeamInterval = 10;
                    mph->BeamOnIntervals = 1;
                    mph->BeamSpawnDir = Vec3(6.0F, 0.0F, 0.0F);
                    mph->BeamSpawnPos = Zero3();
                    mph->ContactDamage = 1;
                    mph->DamageEffectId = 0;
                    mph->DeadEffectId = 0;
                    mph->DeadMessage = Message::None;
                    mph->DeadMsgParam1 = 0;
                    mph->DeadMsgParam2 = 0;
                    mph->DeadMsgTarget = 0xFFFF;
                    mph->Delay = platform->Delay;
                    mph->Effectiveness = 1;
                    mph->Facing = platform->Facing;
                    mph->Flags = Entities::PlatformFlags::Bit15;
                    mph->ForCutscene = false;
                    mph->ForwardSpeed = platform->Speed;
                    mph->Health = 100;
                    mph->ItemChance = 100;
                    mph->ItemType = ItemType::None;
                    mph->LayerMask = 0xFFFF;
                    mph->LifetimeMessage1 = Message::None;
                    mph->LifetimeMessage2 = Message::None;
                    mph->LifetimeMessage3 = Message::None;
                    mph->LifetimeMessage4 = Message::None;
                    mph->LifetimeMsg1Index = 255;
                    mph->LifetimeMsg1Param1 = 0;
                    mph->LifetimeMsg1Param2 = 0;
                    mph->LifetimeMsg1Target = -1;
                    mph->LifetimeMsg2Index = 255;
                    mph->LifetimeMsg2Param1 = 0;
                    mph->LifetimeMsg2Param2 = 0;
                    mph->LifetimeMsg2Target = -1;
                    mph->LifetimeMsg3Index = 255;
                    mph->LifetimeMsg3Param1 = 0;
                    mph->LifetimeMsg3Param2 = 0;
                    mph->LifetimeMsg3Target = -1;
                    mph->LifetimeMsg4Index = 255;
                    mph->LifetimeMsg4Param1 = 0;
                    mph->LifetimeMsg4Param2 = 0;
                    mph->LifetimeMsg4Target = -1;
                    mph->ModelId = 1;
                    mph->MovementType = 0;
                    mph->NoPort = platform->NoPortal == 0 ? 0U : 1U;
                    mph->NodeName = platform->NodeName;
                    mph->ParentId = -1;
                    mph->PlayerColMessage = Message::None;
                    mph->PlayerColMsgParam1 = 0;
                    mph->PlayerColMsgParam2 = 0;
                    mph->PlayerColMsgTarget = 0xFFFF;
                    mph->PortalName = S(StringTrim(R(platform->PortalName)));
                    mph->Position = platform->Position;
                    mph->PositionCount = platform->PositionCount;
                    mph->PositionOffset = Zero3();
                    mph->ResistEffectId = 0;
                    mph->ReverseType = 0;
                    mph->ScanData1 = 0;
                    mph->ScanData2 = 0;
                    mph->ScanMessage = Message::None;
                    mph->ScanMsgTarget = -1;
                    mph->Unused1D0 = 0;
                    mph->Unused1D4 = std::numeric_limits<std::uint32_t>::max();
                    mph->Up = platform->Up;
                    for (std::int32_t i = 0; i < platform->PositionCount; ++i)
                    {
                        mph->Positions->push_back(platform->Positions->at(static_cast<std::size_t>(i)));
                    }
                    for (std::int32_t i = 0; i < 10 - platform->PositionCount; ++i)
                    {
                        mph->Positions->push_back(Zero3());
                    }
                    REPACK_DEBUG_ASSERT(mph->Positions->size() == 10);
                    for (std::int32_t i = 0; i < 10; ++i)
                    {
                        mph->Rotations->push_back(UnitW());
                    }
                    REPACK_DEBUG_ASSERT(mph->Rotations->size() == 10);
                    converted.push_back(mph);
                }
                else if (const auto door = std::dynamic_pointer_cast<FhDoorEntityEditor>(entity))
                {
                    auto mph = std::make_shared<DoorEntityEditor>();
                    mph->Id = door->Id;
                    mph->ConnectorId = 255;
                    mph->DoorNodeName = S("");
                    mph->EntityFilename = S(" ");
                    mph->Facing = door->Facing;
                    mph->Field42 = 255;
                    mph->Field43 = 255;
                    mph->Locked = door->Locked;
                    mph->LayerMask = 0xFFFF;
                    mph->DoorType = DoorType::Standard;
                    mph->NodeName = door->NodeName;
                    mph->PaletteId = 9;
                    mph->Position = door->Position;
                    mph->RoomName = door->RoomName;
                    mph->TargetLayerId = 255;
                    mph->Up = door->Up;
                    converted.push_back(mph);
                }
                else if (const auto item = std::dynamic_pointer_cast<FhItemSpawnEntityEditor>(entity))
                {
                    ItemType type = ItemType::None;
                    switch (item->ItemType)
                    {
                    case FhItemType::AmmoSmall: type = ItemType::UASmall; break;
                    case FhItemType::AmmoBig: type = ItemType::UABig; break;
                    case FhItemType::HealthSmall: type = ItemType::HealthSmall; break;
                    case FhItemType::HealthBig: type = ItemType::HealthBig; break;
                    case FhItemType::DoubleDamage: type = ItemType::DoubleDamage; break;
                    case FhItemType::ElectroLob: type = ItemType::VoltDriver; break;
                    case FhItemType::Missile: type = ItemType::MissileBig; break;
                    default: type = ItemType::None; break;
                    }
                    if (type == ItemType::None)
                    {
                        std::cout << "FH to MPH: Skipping item spawn entity ID " << entity->Id
                            << " with item type " << FhItemTypeText(item->ItemType) << ".\n";
                        continue;
                    }
                    auto mph = std::make_shared<ItemSpawnEntityEditor>();
                    mph->Id = item->Id;
                    mph->AlwaysActive = false;
                    mph->CollectedMessage = Message::None;
                    mph->CollectedMsgParam1 = 0;
                    mph->CollectedMsgParam2 = 0;
                    mph->Enabled = true;
                    mph->Facing = item->Facing;
                    mph->HasBase = false;
                    mph->ItemType = type;
                    mph->LayerMask = 0xFFFF;
                    mph->MaxSpawnCount = item->SpawnLimit;
                    mph->NodeName = item->NodeName;
                    mph->ParentId = 0xFFFF;
                    mph->Position = item->Position;
                    mph->NotifyEntityId = -1;
                    mph->SpawnDelay = 0;
                    mph->SpawnInterval = item->CooldownTime;
                    mph->Up = item->Up;
                    converted.push_back(mph);
                }
                else if (const auto enemy = std::dynamic_pointer_cast<FhEnemySpawnEntityEditor>(entity))
                {
                    auto mph = std::make_shared<EnemySpawnEntityEditor>();
                    mph->Id = enemy->Id;
                    mph->Active = true;
                    mph->ActiveDistance = 30.0F;
                    mph->AlwaysActive = true;
                    mph->CooldownTime = enemy->Cooldown;
                    mph->EnemyType = static_cast<EnemyType>(static_cast<std::uint8_t>(enemy->EnemyType));
                    mph->EntityId1 = enemy->ParentId;
                    mph->EntityId2 = -1;
                    mph->EntityId3 = -1;
                    mph->Facing = enemy->Facing;
                    mph->EnemyActiveDistance = 35.0F;
                    mph->InitialCooldown = 0;
                    mph->ItemChance = 100;
                    mph->ItemType = ItemType::None;
                    mph->LayerMask = 0xFFFF;
                    mph->Message1 = ToMphMessage(enemy->EmptyMessage);
                    mph->Message2 = Message::None;
                    mph->Message3 = Message::None;
                    mph->NodeName = enemy->NodeName;
                    mph->Position = enemy->Position;
                    mph->LinkedEntityId = -1;
                    mph->SpawnCount = enemy->SpawnCount;
                    mph->SpawnTotal = enemy->SpawnTotal;
                    mph->SpawnLimit = enemy->SpawnLimit;
                    mph->SpawnNodeName = enemy->SpawnNodeName;
                    mph->SpawnerHealth = 0;
                    mph->Up = enemy->Up;
                    if (enemy->EnemyType == FhEnemyType::Metroid
                        || enemy->EnemyType == FhEnemyType::Mochtroid1)
                    {
                        mph->Volume0 = CollisionVolume(Zero3(), 1.0F);
                        mph->Volume1 = enemy->Box;
                        mph->Facing = UnitZ();
                        mph->Position = Zero3();
                        mph->IdleRange = One3();
                    }
                    else if (enemy->EnemyType == FhEnemyType::Mochtroid2
                        || enemy->EnemyType == FhEnemyType::Mochtroid3
                        || enemy->EnemyType == FhEnemyType::Mochtroid4)
                    {
                        mph->Volume0 = CollisionVolume(Zero3(), 1.0F);
                        mph->Volume1 = enemy->Cylinder;
                        mph->Position = Zero3();
                        mph->WeaveOffset = 0;
                        mph->Unknown01 = 0;
                    }
                    else if (enemy->EnemyType == FhEnemyType::Zoomer)
                    {
                        mph->Volume0 = CollisionVolume(Zero3(), 1.0F);
                        mph->Volume1 = enemy->Sphere;
                    }
                    else
                    {
                        throw ProgramException("Invalid FH enemy type " + FhEnemyTypeText(enemy->EnemyType));
                    }
                    converted.push_back(mph);
                }
                else if (const auto trigger = std::dynamic_pointer_cast<FhTriggerVolumeEntityEditor>(entity))
                {
                    CollisionVolume volume(Zero3(), 1.0F);
                    Entities::TriggerFlags flags = ToMphFlags(trigger->TriggerFlags, trigger->Subtype);
                    flags |= Entities::TriggerFlags::IncludeBots;
                    TriggerType subtype = TriggerType::Volume;
                    if (trigger->Subtype == FhTriggerType::Threshold)
                    {
                        subtype = TriggerType::Threshold;
                    }
                    else if (trigger->Subtype == FhTriggerType::Box)
                    {
                        volume = trigger->Box;
                    }
                    else if (trigger->Subtype == FhTriggerType::Cylinder)
                    {
                        volume = trigger->Cylinder;
                    }
                    else if (trigger->Subtype == FhTriggerType::Sphere)
                    {
                        volume = trigger->Sphere;
                    }
                    else
                    {
                        throw ProgramException("Invalid FH trigger type " + FhTriggerTypeText(trigger->Subtype));
                    }
                    const Message childMsg = ToMphMessage(trigger->ChildMessage);
                    const Message parentMsg = ToMphMessage(trigger->ParentMessage);
                    if (childMsg == Message::UnlockOubliette)
                    {
                        std::cout << "FH to MPH: Skipping trigger entity ID " << entity->Id
                            << " with child message " << FhMessageText(trigger->ChildMessage) << ".\n";
                        continue;
                    }
                    if (parentMsg == Message::UnlockOubliette)
                    {
                        std::cout << "FH to MPH: Skipping trigger entity ID " << entity->Id
                            << " with parent message " << FhMessageText(trigger->ParentMessage) << ".\n";
                    }
                    auto mph = std::make_shared<TriggerVolumeEntityEditor>();
                    mph->Id = trigger->Id;
                    mph->Active = true;
                    mph->AlwaysActive = false;
                    mph->CheckDelay = 0;
                    mph->ChildId = trigger->ChildId;
                    mph->ChildMessage = childMsg;
                    mph->ChildMsgParam1 = trigger->ChildMsgParam1;
                    mph->ChildMsgParam2 = 0;
                    mph->DeactivateAfterUse = trigger->OneUse != 0;
                    mph->Facing = trigger->Facing;
                    mph->LayerMask = 0xFFFF;
                    mph->NodeName = trigger->NodeName;
                    mph->ParentId = trigger->ParentId;
                    mph->ParentMessage = parentMsg;
                    mph->ParentMsgParam1 = trigger->ParentMsgParam1;
                    mph->ParentMsgParam2 = 0;
                    mph->Position = trigger->Position;
                    mph->RepeatDelay = trigger->Cooldown;
                    mph->RequiredStateBit = 0;
                    mph->Subtype = subtype;
                    mph->TriggerFlags = flags;
                    mph->TriggerThreshold = trigger->Threshold;
                    mph->Up = trigger->Up;
                    mph->Volume = volume;
                    converted.push_back(mph);
                }
                else if (const auto area = std::dynamic_pointer_cast<FhAreaVolumeEntityEditor>(entity))
                {
                    const Entities::TriggerFlags flags = ToMphFlags(area->TriggerFlags, area->Subtype);
                    CollisionVolume volume;
                    if (area->Subtype == FhTriggerType::Box)
                    {
                        volume = area->Box;
                    }
                    else if (area->Subtype == FhTriggerType::Cylinder)
                    {
                        volume = area->Cylinder;
                    }
                    else if (area->Subtype == FhTriggerType::Sphere)
                    {
                        volume = area->Sphere;
                    }
                    else
                    {
                        throw ProgramException("Invalid FH area volume type " + FhTriggerTypeText(area->Subtype));
                    }
                    const Message insideMsg = ToMphMessage(area->InsideMessage);
                    const Message exitMsg = ToMphMessage(area->ExitMessage);
                    if (insideMsg == Message::UnlockOubliette)
                    {
                        std::cout << "FH to MPH: Skipping area volume entity ID " << entity->Id
                            << " with inside message " << FhMessageText(area->InsideMessage) << ".\n";
                        continue;
                    }
                    if (exitMsg == Message::UnlockOubliette)
                    {
                        std::cout << "FH to MPH: Skipping area volume entity ID " << entity->Id
                            << " with exit message " << FhMessageText(area->ExitMessage) << ".\n";
                    }
                    auto mph = std::make_shared<AreaVolumeEntityEditor>();
                    mph->Id = area->Id;
                    mph->Active = true;
                    mph->AllowMultiple = false;
                    mph->AlwaysActive = false;
                    mph->ChildId = -1;
                    mph->Cooldown = area->Cooldown;
                    mph->ExitMessage = exitMsg;
                    mph->ExitMsgParam1 = area->ExitMsgParam1;
                    mph->ExitMsgParam2 = 0;
                    mph->Facing = area->Facing;
                    mph->InsideMessage = insideMsg;
                    mph->InsideMsgParam1 = area->InsideMsgParam1;
                    mph->InsideMsgParam2 = 0;
                    mph->LayerMask = 0xFFFF;
                    mph->MessageDelay = 1;
                    mph->NodeName = area->NodeName;
                    mph->ParentId = -1;
                    mph->Position = area->Position;
                    mph->Priority = 0;
                    mph->TriggerFlags = flags;
                    mph->Unused6A = 0;
                    mph->Up = area->Up;
                    mph->Volume = volume;
                    converted.push_back(mph);
                }
                else if (const auto jump = std::dynamic_pointer_cast<FhJumpPadEntityEditor>(entity))
                {
                    const Entities::TriggerFlags flags = ToMphFlags(jump->TriggerFlags, jump->VolumeType);
                    CollisionVolume volume;
                    if (jump->VolumeType == FhTriggerType::Box)
                    {
                        volume = jump->Box;
                    }
                    else if (jump->VolumeType == FhTriggerType::Cylinder)
                    {
                        volume = jump->Cylinder;
                    }
                    else if (jump->VolumeType == FhTriggerType::Sphere)
                    {
                        volume = jump->Sphere;
                    }
                    else
                    {
                        throw ProgramException("Invalid FH jump pad volume type " + FhTriggerTypeText(jump->VolumeType));
                    }
                    Matrix3Value transform = Matrix3From(Entities::EntityBase::GetTransformMatrix(jump->Facing, jump->Up));
                    Vec3 beam = jump->BeamVector;
                    if (!IsIdentity(transform))
                    {
                        beam = Multiply(beam, Invert(transform));
                    }
                    auto mph = std::make_shared<JumpPadEntityEditor>();
                    mph->Id = jump->Id;
                    mph->Active = true;
                    mph->BeamVector = beam;
                    mph->ControlLockTime = static_cast<std::uint16_t>(jump->ControlLockTime);
                    mph->CooldownTime = static_cast<std::uint16_t>(jump->CooldownTime);
                    mph->Facing = jump->Facing;
                    mph->TriggerFlags = flags;
                    mph->LayerMask = 0xFFFF;
                    mph->ModelId = 0;
                    mph->NodeName = jump->NodeName;
                    mph->ParentId = 0xFFFF;
                    mph->Position = jump->Position;
                    mph->Speed = jump->Speed;
                    mph->Unused28 = 0;
                    mph->Up = jump->Up;
                    mph->Volume = volume;
                    converted.push_back(mph);
                }
                else if (std::dynamic_pointer_cast<PlayerSpawnEntityEditor>(entity)
                    || std::dynamic_pointer_cast<MorphCameraEntityEditor>(entity))
                {
                    if (entity->Type == EntityType::FhPlayerSpawn)
                    {
                        entity->Type = EntityType::PlayerSpawn;
                    }
                    else if (entity->Type == EntityType::FhMorphCamera)
                    {
                        entity->Type = EntityType::MorphCamera;
                    }
                    else
                    {
                        ThrowInvalidOperation();
                    }
                    entity->LayerMask = 0xFFFF;
                    converted.push_back(entity);
                }
                else
                {
                    std::cout << "FH to MPH: Skipping entity ID " << entity->Id
                        << " of type " << EntityTypeText(entity->Type) << ".\n";
                }
            }
            EnsureActiveSpawn(
                converted, EntityType::PlayerSpawn,
                "FH to MPH: Warning: No player spawn entities are present.");
            return converted;
        }

        [[nodiscard]] EditorList ConvertMphToFh(const EditorList& entities)
        {
            EditorList converted;
            for (const auto& entity : entities)
            {
                if (const auto platform = std::dynamic_pointer_cast<PlatformEntityEditor>(entity))
                {
                    if (platform->PositionCount > 8)
                    {
                        std::cout << "MPH to FH: Skipping platform entity ID " << entity->Id
                            << " with more than 8 positions.\n";
                        continue;
                    }
                    while (platform->Positions->size() > 8)
                    {
                        platform->Positions->erase(platform->Positions->end() - 1);
                    }
                    while (platform->Rotations->size() > 8)
                    {
                        platform->Rotations->erase(platform->Rotations->end() - 1);
                    }
                    const std::size_t take = std::min<std::size_t>(
                        platform->Rotations->size(), platform->PositionCount);
                    bool multiple = false;
                    if (take > 1)
                    {
                        const Vec4 first = platform->Rotations->at(0);
                        for (std::size_t i = 1; i < take; ++i)
                        {
                            if (!Vec4Equals(first, platform->Rotations->at(i)))
                            {
                                multiple = true;
                                break;
                            }
                        }
                    }
                    if (multiple)
                    {
                        std::cout << "MPH to FH: Warning: Platform entity ID " << entity->Id
                            << " has multiple rotation values.\n";
                    }
                    auto fh = std::make_shared<FhPlatformEntityEditor>();
                    fh->Id = platform->Id;
                    fh->Delay = platform->Delay;
                    fh->Facing = platform->Facing;
                    fh->GroupId = 0;
                    fh->NodeName = platform->NodeName;
                    fh->NoPortal = platform->NoPort;
                    fh->PortalName = platform->PortalName;
                    fh->Position = platform->Position;
                    fh->PositionCount = static_cast<std::uint8_t>(platform->PositionCount);
                    fh->Positions = platform->Positions;
                    fh->Speed = platform->ForwardSpeed;
                    fh->Up = platform->Up;
                    fh->Volume = CollisionVolume(Zero3(), 1.0F);
                    converted.push_back(fh);
                }
                else if (const auto door = std::dynamic_pointer_cast<DoorEntityEditor>(entity))
                {
                    auto fh = std::make_shared<FhDoorEntityEditor>();
                    fh->Id = door->Id;
                    fh->Facing = door->Facing;
                    fh->Locked = door->Locked;
                    fh->ModelId = 0;
                    fh->NodeName = door->NodeName;
                    fh->Position = door->Position;
                    fh->RoomName = door->RoomName;
                    fh->Up = door->Up;
                    converted.push_back(fh);
                }
                else if (const auto item = std::dynamic_pointer_cast<ItemSpawnEntityEditor>(entity))
                {
                    FhItemType type = FhItemType::None;
                    switch (item->ItemType)
                    {
                    case ItemType::AffinityWeapon:
                    case ItemType::Battlehammer:
                    case ItemType::Imperialist:
                    case ItemType::Judicator:
                    case ItemType::Magmaul:
                    case ItemType::ShockCoil:
                    case ItemType::VoltDriver:
                        type = FhItemType::ElectroLob;
                        break;
                    case ItemType::DoubleDamage:
                    case ItemType::Cloak:
                    case ItemType::Deathalt:
                        type = FhItemType::DoubleDamage;
                        break;
                    case ItemType::HealthBig:
                    case ItemType::EnergyTank:
                        type = FhItemType::HealthBig;
                        break;
                    case ItemType::HealthMedium:
                    case ItemType::HealthSmall:
                        type = FhItemType::HealthSmall;
                        break;
                    case ItemType::MissileBig:
                    case ItemType::MissileSmall:
                    case ItemType::MissileExpansion:
                        type = FhItemType::Missile;
                        break;
                    case ItemType::UABig:
                    case ItemType::OmegaCannon:
                    case ItemType::UAExpansion:
                        type = FhItemType::AmmoBig;
                        break;
                    case ItemType::UASmall:
                        type = FhItemType::AmmoSmall;
                        break;
                    default:
                        type = FhItemType::None;
                        break;
                    }
                    auto fh = std::make_shared<FhItemSpawnEntityEditor>();
                    fh->Id = item->Id;
                    fh->CooldownTime = item->SpawnInterval;
                    fh->Facing = item->Facing;
                    fh->Unused2C = 0;
                    fh->ItemType = type;
                    fh->NodeName = item->NodeName;
                    fh->Position = item->Position;
                    fh->SpawnLimit = item->MaxSpawnCount;
                    fh->Up = item->Up;
                    converted.push_back(fh);
                }
                else if (const auto enemy = std::dynamic_pointer_cast<EnemySpawnEntityEditor>(entity))
                {
                    if (!ValidFhEnemy(enemy->EnemyType))
                    {
                        std::cout << "MPH to FH: Skipping enemy spawn entity ID " << entity->Id
                            << " with enemy type " << EnemyTypeText(enemy->EnemyType) << ".\n";
                        continue;
                    }
                    FhEnemyType type;
                    switch (enemy->EnemyType)
                    {
                    case EnemyType::WarWasp:
                    case EnemyType::BarbedWarWasp:
                        type = FhEnemyType::WarWasp;
                        break;
                    case EnemyType::Zoomer:
                    case EnemyType::Geemer:
                        type = FhEnemyType::Zoomer;
                        break;
                    case EnemyType::Temroid:
                        type = FhEnemyType::Metroid;
                        break;
                    case EnemyType::Petrasyl1:
                        type = FhEnemyType::Mochtroid1;
                        break;
                    case EnemyType::Petrasyl2:
                        type = FhEnemyType::Mochtroid2;
                        break;
                    case EnemyType::Petrasyl3:
                        type = FhEnemyType::Mochtroid3;
                        break;
                    case EnemyType::Petrasyl4:
                        type = FhEnemyType::Mochtroid4;
                        break;
                    default:
                        ThrowInvalidOperation();
                    }
                    auto fh = std::make_shared<FhEnemySpawnEntityEditor>();
                    fh->Id = enemy->Id;
                    fh->Box = enemy->Volume1;
                    fh->Cooldown = enemy->CooldownTime;
                    fh->Cylinder = enemy->Volume1;
                    fh->EmptyMessage = ToFhMessage(enemy->Message1);
                    fh->StartFrame = 0;
                    fh->EnemyType = type;
                    fh->Facing = enemy->Facing;
                    fh->NodeName = enemy->NodeName;
                    fh->ParentId = enemy->EntityId1;
                    fh->Position = enemy->Position;
                    fh->SpawnCount = enemy->SpawnCount;
                    fh->SpawnLimit = enemy->SpawnLimit;
                    fh->SpawnNodeName = enemy->SpawnNodeName;
                    fh->SpawnTotal = enemy->SpawnTotal;
                    fh->Sphere = enemy->Volume1;
                    fh->Up = enemy->Up;
                    converted.push_back(fh);
                }
                else if (const auto trigger = std::dynamic_pointer_cast<TriggerVolumeEntityEditor>(entity))
                {
                    if (trigger->Subtype == TriggerType::Relay
                        || trigger->Subtype == TriggerType::Automatic
                        || trigger->Subtype == TriggerType::StateBits)
                    {
                        std::cout << "MPH to FH: Skipping trigger entity ID " << entity->Id
                            << " with subtype " << TriggerTypeText(trigger->Subtype) << ".\n";
                        continue;
                    }
                    CollisionVolume volume(Zero3(), 1.0F);
                    FhTriggerType subtype = FhTriggerType::Threshold;
                    if (trigger->Subtype != TriggerType::Threshold)
                    {
                        if (trigger->Volume.Type == VolumeType::Box)
                        {
                            subtype = FhTriggerType::Box;
                        }
                        else if (trigger->Volume.Type == VolumeType::Cylinder)
                        {
                            subtype = FhTriggerType::Cylinder;
                        }
                        else if (trigger->Volume.Type == VolumeType::Sphere)
                        {
                            subtype = FhTriggerType::Sphere;
                        }
                        else
                        {
                            ThrowInvalidOperation();
                        }
                        volume = trigger->Volume;
                    }
                    const FhMessage childMsg = ToFhMessage(trigger->ChildMessage);
                    const FhMessage parentMsg = ToFhMessage(trigger->ParentMessage);
                    if (static_cast<std::uint32_t>(childMsg) == 255)
                    {
                        std::cout << "MPH to FH: Skipping trigger entity ID " << entity->Id
                            << " with child message " << MessageText(trigger->ChildMessage) << ".\n";
                        continue;
                    }
                    if (static_cast<std::uint32_t>(parentMsg) == 255)
                    {
                        std::cout << "MPH to FH: Skipping trigger entity ID " << entity->Id
                            << " with parent message " << MessageText(trigger->ParentMessage) << ".\n";
                    }
                    auto fh = std::make_shared<FhTriggerVolumeEntityEditor>();
                    fh->Id = trigger->Id;
                    fh->Box = trigger->Volume;
                    fh->ChildId = trigger->ChildId;
                    fh->ChildMessage = childMsg;
                    fh->ChildMsgParam1 = trigger->ChildMsgParam1;
                    fh->Cooldown = trigger->RepeatDelay;
                    fh->Cylinder = trigger->Volume;
                    fh->Facing = trigger->Facing;
                    fh->NodeName = trigger->NodeName;
                    fh->OneUse = trigger->DeactivateAfterUse ? static_cast<std::uint16_t>(1) : static_cast<std::uint16_t>(0);
                    fh->ParentId = trigger->ParentId;
                    fh->ParentMessage = parentMsg;
                    fh->ParentMsgParam1 = trigger->ParentMsgParam1;
                    fh->Position = trigger->Position;
                    fh->Sphere = trigger->Volume;
                    fh->Subtype = subtype;
                    fh->Threshold = trigger->TriggerThreshold;
                    fh->TriggerFlags = ToFhFlags(trigger->TriggerFlags, trigger->Subtype);
                    fh->Up = trigger->Up;
                    converted.push_back(fh);
                    static_cast<void>(volume);
                }
                else if (const auto area = std::dynamic_pointer_cast<AreaVolumeEntityEditor>(entity))
                {
                    FhTriggerType subtype;
                    if (area->Volume.Type == VolumeType::Box)
                    {
                        subtype = FhTriggerType::Box;
                    }
                    else if (area->Volume.Type == VolumeType::Cylinder)
                    {
                        subtype = FhTriggerType::Cylinder;
                    }
                    else if (area->Volume.Type == VolumeType::Sphere)
                    {
                        subtype = FhTriggerType::Sphere;
                    }
                    else
                    {
                        ThrowInvalidOperation();
                    }
                    CollisionVolume volume = area->Volume;
                    const FhMessage insideMsg = ToFhMessage(area->InsideMessage);
                    const FhMessage exitMsg = ToFhMessage(area->ExitMessage);
                    if (static_cast<std::uint32_t>(insideMsg) == 255)
                    {
                        std::cout << "MPH to FH: Skipping area volume entity ID " << entity->Id
                            << " with inside message " << MessageText(area->InsideMessage) << ".\n";
                        continue;
                    }
                    if (static_cast<std::uint32_t>(exitMsg) == 255)
                    {
                        std::cout << "MPH to FH: Skipping area volume entity ID " << entity->Id
                            << " with exit message " << MessageText(area->ExitMessage) << ".\n";
                    }
                    auto fh = std::make_shared<FhAreaVolumeEntityEditor>();
                    fh->Id = area->Id;
                    fh->Box = volume;
                    fh->Cooldown = area->Cooldown;
                    fh->Cylinder = volume;
                    fh->ExitMessage = exitMsg;
                    fh->ExitMsgParam1 = area->ExitMsgParam1;
                    fh->Facing = area->Facing;
                    fh->InsideMessage = insideMsg;
                    fh->InsideMsgParam1 = area->InsideMsgParam1;
                    fh->NodeName = area->NodeName;
                    fh->Position = area->Position;
                    fh->Sphere = volume;
                    fh->Subtype = subtype;
                    fh->TriggerFlags = ToFhFlags(area->TriggerFlags, TriggerType::Volume);
                    fh->Up = area->Up;
                    converted.push_back(fh);
                }
                else if (const auto jump = std::dynamic_pointer_cast<JumpPadEntityEditor>(entity))
                {
                    FhTriggerType volumeType;
                    if (jump->Volume.Type == VolumeType::Box)
                    {
                        volumeType = FhTriggerType::Box;
                    }
                    else if (jump->Volume.Type == VolumeType::Cylinder)
                    {
                        volumeType = FhTriggerType::Cylinder;
                    }
                    else if (jump->Volume.Type == VolumeType::Sphere)
                    {
                        volumeType = FhTriggerType::Sphere;
                    }
                    else
                    {
                        ThrowInvalidOperation();
                    }
                    Matrix3Value transform = Matrix3From(Entities::EntityBase::GetTransformMatrix(jump->Facing, jump->Up));
                    Vec3 beam = jump->BeamVector;
                    if (!IsIdentity(transform))
                    {
                        beam = Multiply(beam, transform);
                    }
                    auto fh = std::make_shared<FhJumpPadEntityEditor>();
                    fh->Id = jump->Id;
                    fh->BeamType = 0;
                    fh->BeamVector = beam;
                    fh->Box = jump->Volume;
                    fh->ControlLockTime = jump->ControlLockTime;
                    fh->CooldownTime = jump->CooldownTime;
                    fh->Cylinder = jump->Volume;
                    fh->Facing = jump->Facing;
                    fh->ModelId = 0;
                    fh->NodeName = jump->NodeName;
                    fh->Position = jump->Position;
                    fh->Speed = jump->Speed;
                    fh->Sphere = jump->Volume;
                    fh->TriggerFlags = ToFhFlags(jump->TriggerFlags, TriggerType::Volume);
                    fh->Up = jump->Up;
                    fh->VolumeType = volumeType;
                    converted.push_back(fh);
                }
                else if (std::dynamic_pointer_cast<PlayerSpawnEntityEditor>(entity)
                    || std::dynamic_pointer_cast<MorphCameraEntityEditor>(entity))
                {
                    if (entity->Type == EntityType::PlayerSpawn)
                    {
                        entity->Type = EntityType::FhPlayerSpawn;
                    }
                    else if (entity->Type == EntityType::MorphCamera)
                    {
                        entity->Type = EntityType::FhMorphCamera;
                    }
                    else
                    {
                        ThrowInvalidOperation();
                    }
                    converted.push_back(entity);
                }
                else
                {
                    std::cout << "MPH to FH: Skipping entity ID " << entity->Id
                        << " of type " << EntityTypeText(entity->Type) << ".\n";
                }
            }
            EnsureActiveSpawn(
                converted, EntityType::FhPlayerSpawn,
                "MPH to FH: Warning: No player spawn entities are present.");
            return converted;
        }

        void ThrowIfInvalid(const EditorPtr& entity, bool firstHunt)
        {
            if (!entity)
            {
                throw System::NullReferenceException();
            }
            if (entity->Id < 0)
            {
                throw ProgramException("File entities must have a positive entity ID.");
            }
            if (!IsDefinedEntityType(entity->Type))
            {
                throw ProgramException(
                    "Unknown entity type "
                    + std::to_string(static_cast<std::uint16_t>(entity->Type)) + ".");
            }
            if (!firstHunt && ValidFh(entity->Type))
            {
                throw ProgramException(
                    "Cannot add FH entity type " + EntityTypeText(entity->Type)
                    + " to MPH entity file.");
            }
            if (firstHunt && ValidMph(entity->Type))
            {
                throw ProgramException(
                    "Cannot add MPH entity type " + EntityTypeText(entity->Type)
                    + " to FH entity file.");
            }
            if ((!firstHunt && !ValidMph(entity->Type))
                || (firstHunt && !ValidFh(entity->Type)))
            {
                throw ProgramException(
                    "Cannot add entity type " + EntityTypeText(entity->Type)
                    + " to entity file.");
            }
        }

        template <typename TEditor>
        [[nodiscard]] std::shared_ptr<TEditor> CastEditor(const EditorPtr& entity)
        {
            const auto typed = std::dynamic_pointer_cast<TEditor>(entity);
            if (!typed)
            {
                throw std::bad_cast();
            }
            return typed;
        }

        void WriteMphPlatform(const std::shared_ptr<PlatformEntityEditor>& entity, BinaryWriter& writer)
        {
            REPACK_DEBUG_ASSERT(entity->Positions && entity->Positions->size() == 10);
            REPACK_DEBUG_ASSERT(entity->Rotations && entity->Rotations->size() == 10);
            writer.Write(entity->NoPort);
            writer.Write(entity->ModelId);
            writer.Write(entity->ParentId);
            writer.WriteByte(entity->Active);
            writer.Write(entity->Delay);
            writer.Write(entity->ScanData1);
            writer.Write(entity->ScanMsgTarget);
            writer.Write(static_cast<std::uint32_t>(entity->ScanMessage));
            writer.Write(entity->ScanData2);
            writer.Write(entity->PositionCount);
            for (const Vec3& position : *entity->Positions)
            {
                writer.WriteVector3(position);
            }
            for (const Vec4& rotation : *entity->Rotations)
            {
                writer.WriteVector4(rotation);
            }
            writer.WriteVector3(entity->PositionOffset);
            writer.WriteFloat(entity->ForwardSpeed);
            writer.WriteFloat(entity->BackwardSpeed);
            writer.WriteString(R(entity->PortalName), 16);
            writer.Write(entity->MovementType);
            writer.WriteInt(entity->ForCutscene);
            writer.Write(entity->ReverseType);
            writer.Write(static_cast<std::uint32_t>(entity->Flags));
            writer.Write(entity->ContactDamage);
            writer.WriteVector3(entity->BeamSpawnDir);
            writer.WriteVector3(entity->BeamSpawnPos);
            writer.Write(entity->BeamId);
            writer.Write(entity->BeamInterval);
            writer.Write(entity->BeamOnIntervals);
            writer.Write(std::numeric_limits<std::uint16_t>::max());
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(entity->ResistEffectId);
            writer.Write(entity->Health);
            writer.Write(entity->Effectiveness);
            writer.Write(entity->DamageEffectId);
            writer.Write(entity->DeadEffectId);
            writer.Write(entity->ItemChance);
            writer.Write(static_cast<std::uint8_t>(0));
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::int32_t>(entity->ItemType));
            writer.Write(entity->Unused1D0);
            writer.Write(entity->Unused1D4);
            writer.Write(entity->BeamHitMsgTarget);
            writer.Write(static_cast<std::uint32_t>(entity->BeamHitMessage));
            writer.Write(entity->BeamHitMsgParam1);
            writer.Write(entity->BeamHitMsgParam2);
            writer.Write(entity->PlayerColMsgTarget);
            writer.Write(static_cast<std::uint32_t>(entity->PlayerColMessage));
            writer.Write(entity->PlayerColMsgParam1);
            writer.Write(entity->PlayerColMsgParam2);
            writer.Write(entity->DeadMsgTarget);
            writer.Write(static_cast<std::uint32_t>(entity->DeadMessage));
            writer.Write(entity->DeadMsgParam1);
            writer.Write(entity->DeadMsgParam2);
            writer.Write(entity->LifetimeMsg1Index);
            writer.Write(entity->LifetimeMsg1Target);
            writer.Write(static_cast<std::uint32_t>(entity->LifetimeMessage1));
            writer.Write(entity->LifetimeMsg1Param1);
            writer.Write(entity->LifetimeMsg1Param2);
            writer.Write(entity->LifetimeMsg2Index);
            writer.Write(entity->LifetimeMsg2Target);
            writer.Write(static_cast<std::uint32_t>(entity->LifetimeMessage2));
            writer.Write(entity->LifetimeMsg2Param1);
            writer.Write(entity->LifetimeMsg2Param2);
            writer.Write(entity->LifetimeMsg3Index);
            writer.Write(entity->LifetimeMsg3Target);
            writer.Write(static_cast<std::uint32_t>(entity->LifetimeMessage3));
            writer.Write(entity->LifetimeMsg3Param1);
            writer.Write(entity->LifetimeMsg3Param2);
            writer.Write(entity->LifetimeMsg4Index);
            writer.Write(entity->LifetimeMsg4Target);
            writer.Write(static_cast<std::uint32_t>(entity->LifetimeMessage4));
            writer.Write(entity->LifetimeMsg4Param1);
            writer.Write(entity->LifetimeMsg4Param2);
        }

        void WriteMphObject(const std::shared_ptr<ObjectEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(static_cast<std::uint8_t>(entity->Flags));
            writer.Write(static_cast<std::uint8_t>(0));
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->EffectFlags));
            writer.Write(entity->ModelId);
            writer.Write(entity->LinkedEntity);
            writer.Write(entity->ScanId);
            writer.Write(entity->ScanMsgTarget);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->ScanMessage));
            writer.Write(entity->EffectId);
            writer.Write(entity->EffectInterval);
            writer.Write(entity->EffectOnIntervals);
            writer.WriteVector3(entity->EffectPositionOffset);
            Repack::WriteVolume(writer, entity->Volume);
        }

        void WritePlayerSpawn(const std::shared_ptr<PlayerSpawnEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(entity->Availability);
            writer.WriteByte(entity->Active);
            writer.Write(entity->TeamIndex);
        }

        void WriteMphDoor(const std::shared_ptr<DoorEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.WriteString(R(entity->DoorNodeName), 16);
            writer.Write(entity->PaletteId);
            writer.Write(static_cast<std::uint32_t>(entity->DoorType));
            writer.Write(entity->ConnectorId);
            writer.Write(entity->TargetLayerId);
            writer.WriteByte(entity->Locked);
            writer.Write(entity->Field42);
            writer.Write(entity->Field43);
            writer.WriteString(R(entity->EntityFilename), 16);
            writer.WriteString(R(entity->RoomName), 16);
        }

        void WriteMphItemSpawn(const std::shared_ptr<ItemSpawnEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(entity->ParentId);
            writer.Write(static_cast<std::uint32_t>(static_cast<std::int32_t>(entity->ItemType)));
            writer.WriteByte(entity->Enabled);
            writer.WriteByte(entity->HasBase);
            writer.WriteByte(entity->AlwaysActive);
            writer.Write(static_cast<std::uint8_t>(0));
            writer.Write(entity->MaxSpawnCount);
            writer.Write(entity->SpawnInterval);
            writer.Write(entity->SpawnDelay);
            writer.Write(entity->NotifyEntityId);
            writer.Write(static_cast<std::uint32_t>(entity->CollectedMessage));
            writer.Write(entity->CollectedMsgParam1);
            writer.Write(entity->CollectedMsgParam2);
        }

        void WriteMphEnemySpawn(const std::shared_ptr<EnemySpawnEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(static_cast<std::uint32_t>(entity->EnemyType));
            const std::int32_t spawnerType = entity->SpawnerType();
            auto writeZeros = [&](std::int32_t count)
            {
                for (std::int32_t i = 0; i < count; ++i)
                {
                    writer.Write(static_cast<std::uint32_t>(0));
                }
            };
            auto writeWarWaspFields = [&]()
            {
                Repack::WriteVolume(writer, entity->Volume0);
                Repack::WriteVolume(writer, entity->Volume1);
                Repack::WriteVolume(writer, entity->Volume2);
                REPACK_DEBUG_ASSERT(entity->MovementVectors && entity->MovementVectors->size() == 16);
                for (const Vec3& vector : *entity->MovementVectors)
                {
                    writer.WriteVector3(vector);
                }
                writer.Write(entity->PositionCount);
                writer.Write(static_cast<std::uint8_t>(0));
                writer.Write(static_cast<std::uint16_t>(0));
                writer.Write(entity->MovementType);
            };
            if (spawnerType == 0)
            {
                Repack::WriteVolume(writer, entity->Volume0);
                Repack::WriteVolume(writer, entity->Volume1);
                Repack::WriteVolume(writer, entity->Volume2);
                Repack::WriteVolume(writer, entity->Volume3);
                writeZeros(36);
            }
            else if (spawnerType == 2)
            {
                Repack::WriteVolume(writer, entity->Volume0);
                writer.WriteVector3(entity->PathVector);
                Repack::WriteVolume(writer, entity->Volume1);
                Repack::WriteVolume(writer, entity->Volume2);
                writeZeros(49);
            }
            else if (spawnerType == 3)
            {
                Repack::WriteVolume(writer, entity->Volume0);
                writer.Write(entity->Unused68);
                writer.Write(entity->Unused6C);
                writer.Write(entity->Unused70);
                writer.Write(entity->Unused74);
                writer.Write(entity->Unused78);
                writer.Write(entity->Unused7C);
                writer.Write(entity->Unused80);
                writer.WriteVector3(entity->EnemyFacing);
                writer.WriteVector3(entity->EnemyPosition);
                writer.WriteVector3(entity->IdleRange);
                writeZeros(68);
            }
            else if (spawnerType == 4)
            {
                Repack::WriteVolume(writer, entity->Volume0);
                writer.Write(entity->Unused68);
                writer.Write(entity->Unused6C);
                writer.Write(entity->Unused70);
                writer.Write(entity->Unused74);
                writer.WriteVector3(entity->EnemyPosition);
                writer.Write(entity->WeaveOffset);
                writer.Write(entity->Unknown01);
                writeZeros(75);
            }
            else if (spawnerType == 1 || spawnerType == 8)
            {
                if (spawnerType == 1)
                {
                    writeWarWaspFields();
                    writer.Write(static_cast<std::uint32_t>(0));
                    writer.Write(static_cast<std::uint32_t>(0));
                }
                else
                {
                    writer.Write(entity->EnemySubtype);
                    writer.Write(entity->EnemyVersion);
                    writeWarWaspFields();
                }
            }
            else if (spawnerType == 5)
            {
                writer.Write(entity->EnemySubtype);
                Repack::WriteVolume(writer, entity->Volume0);
                Repack::WriteVolume(writer, entity->Volume1);
                Repack::WriteVolume(writer, entity->Volume2);
                Repack::WriteVolume(writer, entity->Volume3);
                writeZeros(35);
            }
            else if (spawnerType == 6)
            {
                writer.Write(entity->EnemySubtype);
                writer.Write(entity->EnemyVersion);
                Repack::WriteVolume(writer, entity->Volume0);
                Repack::WriteVolume(writer, entity->Volume1);
                Repack::WriteVolume(writer, entity->Volume2);
                Repack::WriteVolume(writer, entity->Volume3);
                writeZeros(34);
            }
            else if (spawnerType == 7)
            {
                writer.Write(entity->EnemyHealth);
                writer.Write(entity->EnemyDamage);
                writer.Write(entity->EnemySubtype);
                Repack::WriteVolume(writer, entity->Volume0);
                writeZeros(82);
            }
            else if (spawnerType == 9)
            {
                writer.Write(static_cast<std::uint32_t>(entity->Hunter));
                writer.Write(entity->EncounterType);
                writer.Write(entity->HunterWeapon);
                writer.Write(entity->HunterHealth);
                writer.Write(entity->HunterHealthMax);
                writer.Write(entity->HunterHealthThreshold);
                writer.Write(entity->HunterColor);
                writer.Write(entity->HunterChance);
                writeZeros(95);
            }
            else if (spawnerType == 10)
            {
                writer.Write(entity->EnemySubtype);
                writer.Write(entity->EnemyVersion);
                Repack::WriteVolume(writer, entity->Volume0);
                Repack::WriteVolume(writer, entity->Volume1);
                writer.Write(entity->Index);
                writeZeros(65);
            }
            else if (spawnerType == 11)
            {
                REPACK_DEBUG_ASSERT(entity->Volume0.Type == VolumeType::Sphere);
                REPACK_DEBUG_ASSERT(entity->Volume1.Type == VolumeType::Sphere);
                writer.WriteVector3(entity->Volume0.SpherePosition);
                writer.WriteFloat(entity->Volume0.SphereRadius);
                writer.WriteVector3(entity->Volume1.SpherePosition);
                writer.WriteFloat(entity->Volume1.SphereRadius);
                writeZeros(92);
            }
            else if (spawnerType == 12)
            {
                writer.WriteVector3(entity->Unknown05);
                writer.WriteFloat(entity->Unknown06);
                writer.WriteFloat(entity->Unknown07);
                writeZeros(95);
            }
            else
            {
                ThrowInvalidOperation();
            }
            writer.Write(entity->LinkedEntityId);
            writer.Write(entity->SpawnTotal);
            writer.Write(entity->SpawnLimit);
            writer.Write(entity->SpawnCount);
            writer.WriteByte(entity->Active);
            writer.WriteByte(entity->AlwaysActive);
            writer.Write(entity->ItemChance);
            writer.Write(entity->SpawnerHealth);
            writer.Write(entity->CooldownTime);
            writer.Write(entity->InitialCooldown);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.WriteFloat(entity->ActiveDistance);
            writer.WriteFloat(entity->EnemyActiveDistance);
            writer.WriteString(R(entity->SpawnNodeName), 16);
            writer.Write(entity->EntityId1);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->Message1));
            writer.Write(entity->EntityId2);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->Message2));
            writer.Write(entity->EntityId3);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->Message3));
            writer.Write(static_cast<std::int32_t>(entity->ItemType));
        }

        void WriteMphTriggerVolume(const std::shared_ptr<TriggerVolumeEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(static_cast<std::uint32_t>(entity->Subtype));
            Repack::WriteVolume(writer, entity->Volume);
            writer.Write(std::numeric_limits<std::uint16_t>::max());
            writer.WriteByte(entity->Active);
            writer.WriteByte(entity->AlwaysActive);
            writer.WriteByte(entity->DeactivateAfterUse);
            writer.Write(static_cast<std::uint8_t>(0));
            writer.Write(entity->RepeatDelay);
            writer.Write(entity->CheckDelay);
            writer.Write(entity->RequiredStateBit);
            writer.Write(static_cast<std::uint32_t>(entity->TriggerFlags));
            writer.Write(entity->TriggerThreshold);
            writer.Write(entity->ParentId);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->ParentMessage));
            writer.Write(entity->ParentMsgParam1);
            writer.Write(entity->ParentMsgParam2);
            writer.Write(entity->ChildId);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->ChildMessage));
            writer.Write(entity->ChildMsgParam1);
            writer.Write(entity->ChildMsgParam2);
        }

        void WriteMphAreaVolume(const std::shared_ptr<AreaVolumeEntityEditor>& entity, BinaryWriter& writer)
        {
            Repack::WriteVolume(writer, entity->Volume);
            writer.Write(std::numeric_limits<std::uint16_t>::max());
            writer.WriteByte(entity->Active);
            writer.WriteByte(entity->AlwaysActive);
            writer.WriteByte(entity->AllowMultiple);
            writer.Write(entity->MessageDelay);
            writer.Write(entity->Unused6A);
            writer.Write(static_cast<std::uint32_t>(entity->InsideMessage));
            writer.Write(entity->InsideMsgParam1);
            writer.Write(entity->InsideMsgParam2);
            writer.Write(entity->ParentId);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->ExitMessage));
            writer.Write(entity->ExitMsgParam1);
            writer.Write(entity->ExitMsgParam2);
            writer.Write(entity->ChildId);
            writer.Write(entity->Cooldown);
            writer.Write(entity->Priority);
            writer.Write(static_cast<std::uint32_t>(entity->TriggerFlags));
        }

        void WriteMphJumpPad(const std::shared_ptr<JumpPadEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(entity->ParentId);
            writer.Write(entity->Unused28);
            Repack::WriteVolume(writer, entity->Volume);
            writer.WriteVector3(entity->BeamVector);
            writer.WriteFloat(entity->Speed);
            writer.Write(entity->ControlLockTime);
            writer.Write(entity->CooldownTime);
            writer.WriteByte(entity->Active);
            writer.Write(entity->Padding81);
            writer.Write(entity->Padding82);
            writer.Write(entity->ModelId);
            writer.Write(entity->BeamType);
            writer.Write(static_cast<std::uint32_t>(entity->TriggerFlags));
        }

        void WritePointModule(const std::shared_ptr<PointModuleEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(entity->NextId);
            writer.Write(entity->PrevId);
            writer.WriteByte(entity->Active);
        }

        void WriteMphMorphCamera(const std::shared_ptr<MorphCameraEntityEditor>& entity, BinaryWriter& writer)
        {
            Repack::WriteVolume(writer, entity->Volume);
        }

        void WriteFhMorphCamera(const std::shared_ptr<MorphCameraEntityEditor>& entity, BinaryWriter& writer)
        {
            Repack::WriteFhVolume(writer, entity->Volume);
        }

        void WriteMphOctolithFlag(const std::shared_ptr<OctolithFlagEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(entity->TeamId);
        }

        void WriteMphFlagBase(const std::shared_ptr<FlagBaseEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(entity->TeamId);
            Repack::WriteVolume(writer, entity->Volume);
        }

        void WriteMphTeleporter(const std::shared_ptr<TeleporterEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(entity->LoadIndex);
            writer.Write(entity->TargetIndex);
            writer.Write(entity->ArtifactId);
            writer.WriteByte(entity->Active);
            writer.WriteByte(entity->Invisible);
            writer.WriteString(R(entity->TargetRoom), 15);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(std::numeric_limits<std::uint16_t>::max());
            writer.WriteVector3(entity->TargetPosition);
            writer.WriteString(R(entity->TeleporterNodeName), 16);
        }

        void WriteMphNodeDefense(const std::shared_ptr<NodeDefenseEntityEditor>& entity, BinaryWriter& writer)
        {
            Repack::WriteVolume(writer, entity->Volume);
        }

        void WriteMphLightSource(const std::shared_ptr<LightSourceEntityEditor>& entity, BinaryWriter& writer)
        {
            Repack::WriteVolume(writer, entity->Volume);
            writer.WriteByte(entity->Light1Enabled);
            writer.WriteColorRgb(entity->Light1Color);
            writer.WriteVector3(entity->Light1Vector);
            writer.WriteByte(entity->Light2Enabled);
            writer.WriteColorRgb(entity->Light2Color);
            writer.WriteVector3(entity->Light2Vector);
        }

        void WriteMphArtifact(const std::shared_ptr<ArtifactEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(entity->ModelId);
            writer.Write(entity->ArtifactId);
            writer.WriteByte(entity->Active);
            writer.WriteByte(entity->HasBase);
            writer.Write(entity->Message1Target);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->Message1));
            writer.Write(entity->Message2Target);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->Message2));
            writer.Write(entity->Message3Target);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->Message3));
            writer.Write(entity->LinkedEntityId);
        }

        void WriteMphCameraSequence(const std::shared_ptr<CameraSequenceEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(entity->SequenceId);
            writer.WriteByte(entity->Handoff);
            writer.WriteByte(entity->Loop);
            writer.WriteByte(entity->BlockInput);
            writer.WriteByte(entity->ForceAltForm);
            writer.WriteByte(entity->ForceBipedForm);
            writer.Write(entity->DelayFrames);
            writer.Write(entity->PlayerId1);
            writer.Write(entity->PlayerId2);
            writer.Write(entity->Entity1);
            writer.Write(entity->Entity2);
            writer.Write(entity->EndMessageTargetId);
            writer.Write(static_cast<std::uint32_t>(entity->EndMessage));
            writer.Write(entity->EndMessageParam);
        }

        void WriteMphForceField(const std::shared_ptr<ForceFieldEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(entity->ForceFieldType);
            writer.WriteFloat(entity->Width);
            writer.WriteFloat(entity->Height);
            writer.WriteByte(entity->Active);
        }

        [[nodiscard]] std::int32_t WriteEntity(const EditorPtr& entity, BinaryWriter& writer)
        {
            const std::size_t position = writer.Position();
            writer.Write(static_cast<std::uint16_t>(entity->Type));
            writer.Write(entity->Id);
            writer.WriteVector3(entity->Position);
            writer.WriteVector3(entity->Up);
            writer.WriteVector3(entity->Facing);
            switch (entity->Type)
            {
            case EntityType::Platform: WriteMphPlatform(CastEditor<PlatformEntityEditor>(entity), writer); break;
            case EntityType::Object: WriteMphObject(CastEditor<ObjectEntityEditor>(entity), writer); break;
            case EntityType::PlayerSpawn: WritePlayerSpawn(CastEditor<PlayerSpawnEntityEditor>(entity), writer); break;
            case EntityType::Door: WriteMphDoor(CastEditor<DoorEntityEditor>(entity), writer); break;
            case EntityType::ItemSpawn: WriteMphItemSpawn(CastEditor<ItemSpawnEntityEditor>(entity), writer); break;
            case EntityType::EnemySpawn: WriteMphEnemySpawn(CastEditor<EnemySpawnEntityEditor>(entity), writer); break;
            case EntityType::TriggerVolume: WriteMphTriggerVolume(CastEditor<TriggerVolumeEntityEditor>(entity), writer); break;
            case EntityType::AreaVolume: WriteMphAreaVolume(CastEditor<AreaVolumeEntityEditor>(entity), writer); break;
            case EntityType::JumpPad: WriteMphJumpPad(CastEditor<JumpPadEntityEditor>(entity), writer); break;
            case EntityType::PointModule: WritePointModule(CastEditor<PointModuleEntityEditor>(entity), writer); break;
            case EntityType::MorphCamera: WriteMphMorphCamera(CastEditor<MorphCameraEntityEditor>(entity), writer); break;
            case EntityType::OctolithFlag: WriteMphOctolithFlag(CastEditor<OctolithFlagEntityEditor>(entity), writer); break;
            case EntityType::FlagBase: WriteMphFlagBase(CastEditor<FlagBaseEntityEditor>(entity), writer); break;
            case EntityType::Teleporter: WriteMphTeleporter(CastEditor<TeleporterEntityEditor>(entity), writer); break;
            case EntityType::NodeDefense: WriteMphNodeDefense(CastEditor<NodeDefenseEntityEditor>(entity), writer); break;
            case EntityType::LightSource: WriteMphLightSource(CastEditor<LightSourceEntityEditor>(entity), writer); break;
            case EntityType::Artifact: WriteMphArtifact(CastEditor<ArtifactEntityEditor>(entity), writer); break;
            case EntityType::CameraSequence: WriteMphCameraSequence(CastEditor<CameraSequenceEntityEditor>(entity), writer); break;
            case EntityType::ForceField: WriteMphForceField(CastEditor<ForceFieldEntityEditor>(entity), writer); break;
            default: break;
            }
            return std::bit_cast<std::int32_t>(
                static_cast<std::uint32_t>(writer.Position() - position));
        }

        [[nodiscard]] std::vector<std::uint8_t> RepackEntities(const EditorList& entities)
        {
            BinaryWriter writer;
            std::array<std::uint16_t, 16> lengths{};
            for (const auto& entity : entities)
            {
                ThrowIfInvalid(entity, false);
                for (std::int32_t i = 0; i < 16; ++i)
                {
                    if ((entity->LayerMask & (1U << i)) != 0)
                    {
                        ++lengths[static_cast<std::size_t>(i)];
                    }
                }
            }
            writer.Write(static_cast<std::uint32_t>(2));
            for (std::uint16_t length : lengths)
            {
                writer.Write(length);
            }
            REPACK_DEBUG_ASSERT(writer.Position() == sizeof(EntityHeader));
            writer.Position(writer.Position() + sizeof(EntityEntry) * (entities.size() + 1));
            std::vector<std::pair<std::int32_t, std::int32_t>> results;
            for (std::size_t i = 0; i < entities.size(); ++i)
            {
                const std::int32_t offset = std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(writer.Position()));
                const std::int32_t size = WriteEntity(entities[i], writer);
                results.emplace_back(offset, size);
                if (i + 1 < entities.size())
                {
                    while (writer.Position() % 4 != 0)
                    {
                        writer.Write(static_cast<std::uint8_t>(0));
                    }
                }
            }
            writer.Position(sizeof(EntityHeader));
            for (std::size_t i = 0; i < entities.size(); ++i)
            {
                writer.WriteString(R(entities[i]->NodeName), 16);
                writer.Write(entities[i]->LayerMask);
                writer.Write(static_cast<std::uint16_t>(results[i].second));
                writer.Write(results[i].first);
            }
            writer.WriteString("", 16);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(0));
            return writer.ToArray();
        }

        void WriteFhPlatform(const std::shared_ptr<FhPlatformEntityEditor>& entity, BinaryWriter& writer)
        {
            REPACK_DEBUG_ASSERT(entity->Positions && entity->Positions->size() == 8);
            writer.Write(entity->NoPortal);
            writer.Write(entity->GroupId);
            writer.Write(entity->Unused2C);
            writer.Write(entity->Delay);
            writer.Write(entity->PositionCount);
            writer.Write(static_cast<std::uint16_t>(0));
            Repack::WriteFhVolume(writer, entity->Volume);
            for (const Vec3& position : *entity->Positions)
            {
                writer.WriteVector3(position);
            }
            writer.WriteFloat(entity->Speed);
            writer.WriteString(R(entity->PortalName), 16);
        }

        void WriteFhDoor(const std::shared_ptr<FhDoorEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.WriteString(R(entity->RoomName), 16);
            writer.WriteInt(entity->Locked);
            writer.Write(entity->ModelId);
        }

        void WriteFhItemSpawn(const std::shared_ptr<FhItemSpawnEntityEditor>& entity, BinaryWriter& writer)
        {
            writer.Write(static_cast<std::uint32_t>(static_cast<std::int32_t>(entity->ItemType)));
            writer.Write(entity->SpawnLimit);
            writer.Write(entity->CooldownTime);
            writer.Write(entity->Unused2C);
        }

        void WriteFhEnemySpawn(const std::shared_ptr<FhEnemySpawnEntityEditor>& entity, BinaryWriter& writer)
        {
            Repack::WriteFhVolume(writer, entity->Box);
            Repack::WriteFhVolume(writer, entity->Cylinder);
            Repack::WriteFhVolume(writer, entity->Sphere);
            writer.Write(static_cast<std::uint32_t>(entity->EnemyType));
            writer.Write(entity->SpawnTotal);
            writer.Write(entity->SpawnLimit);
            writer.Write(entity->SpawnCount);
            writer.Write(static_cast<std::uint8_t>(0));
            writer.Write(entity->Cooldown);
            writer.Write(entity->StartFrame);
            writer.WriteString(R(entity->SpawnNodeName), 16);
            writer.Write(entity->ParentId);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->EmptyMessage));
        }

        [[nodiscard]] bool DefinedFhTrigger(FhTriggerType value) noexcept
        {
            return value == FhTriggerType::Sphere
                || value == FhTriggerType::Box
                || value == FhTriggerType::Cylinder
                || value == FhTriggerType::Threshold;
        }

        void WriteFhTriggerVolume(const std::shared_ptr<FhTriggerVolumeEntityEditor>& entity, BinaryWriter& writer)
        {
            REPACK_DEBUG_ASSERT(DefinedFhTrigger(entity->Subtype));
            writer.Write(static_cast<std::uint32_t>(entity->Subtype));
            Repack::WriteFhVolume(writer, entity->Box);
            Repack::WriteFhVolume(writer, entity->Sphere);
            Repack::WriteFhVolume(writer, entity->Cylinder);
            writer.Write(entity->OneUse);
            writer.Write(entity->Cooldown);
            writer.Write(static_cast<std::uint32_t>(entity->TriggerFlags));
            writer.Write(entity->Threshold);
            writer.Write(entity->ParentId);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->ParentMessage));
            writer.Write(entity->ParentMsgParam1);
            writer.Write(entity->ChildId);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->ChildMessage));
            writer.Write(entity->ChildMsgParam1);
        }

        void WriteFhAreaVolume(const std::shared_ptr<FhAreaVolumeEntityEditor>& entity, BinaryWriter& writer)
        {
            REPACK_DEBUG_ASSERT(DefinedFhTrigger(entity->Subtype) && entity->Subtype != FhTriggerType::Threshold);
            writer.Write(static_cast<std::uint32_t>(entity->Subtype));
            Repack::WriteFhVolume(writer, entity->Box);
            Repack::WriteFhVolume(writer, entity->Sphere);
            Repack::WriteFhVolume(writer, entity->Cylinder);
            writer.Write(static_cast<std::uint32_t>(entity->InsideMessage));
            writer.Write(entity->InsideMsgParam1);
            writer.Write(static_cast<std::uint32_t>(entity->ExitMessage));
            writer.Write(entity->ExitMsgParam1);
            writer.Write(entity->Cooldown);
            writer.Write(static_cast<std::uint16_t>(0));
            writer.Write(static_cast<std::uint32_t>(entity->TriggerFlags));
        }

        void WriteFhJumpPad(const std::shared_ptr<FhJumpPadEntityEditor>& entity, BinaryWriter& writer)
        {
            REPACK_DEBUG_ASSERT(DefinedFhTrigger(entity->VolumeType) && entity->VolumeType != FhTriggerType::Threshold);
            writer.Write(static_cast<std::uint32_t>(entity->VolumeType));
            Repack::WriteFhVolume(writer, entity->Box);
            Repack::WriteFhVolume(writer, entity->Sphere);
            Repack::WriteFhVolume(writer, entity->Cylinder);
            writer.Write(entity->CooldownTime);
            writer.WriteVector3(entity->BeamVector);
            writer.WriteFloat(entity->Speed);
            writer.Write(entity->ControlLockTime);
            writer.Write(entity->ModelId);
            writer.Write(entity->BeamType);
            writer.Write(static_cast<std::uint32_t>(entity->TriggerFlags));
        }

        void WriteFhEntity(const EditorPtr& entity, BinaryWriter& writer)
        {
            if (static_cast<std::uint16_t>(entity->Type) < 100)
            {
                if (entity->Type == EntityType::PlayerSpawn)
                {
                    entity->Type = EntityType::FhPlayerSpawn;
                }
                else if (entity->Type == EntityType::PointModule)
                {
                    entity->Type = EntityType::FhPointModule;
                }
                else if (entity->Type == EntityType::MorphCamera)
                {
                    entity->Type = EntityType::FhMorphCamera;
                }
                else
                {
                    throw ProgramException("Invalid FH entity type " + EntityTypeText(entity->Type));
                }
            }
            writer.Write(static_cast<std::uint16_t>(static_cast<std::uint16_t>(entity->Type) - 100));
            writer.Write(entity->Id);
            writer.WriteVector3(entity->Position);
            writer.WriteVector3(entity->Up);
            writer.WriteVector3(entity->Facing);
            switch (entity->Type)
            {
            case EntityType::FhPlatform: WriteFhPlatform(CastEditor<FhPlatformEntityEditor>(entity), writer); break;
            case EntityType::FhPlayerSpawn: WritePlayerSpawn(CastEditor<PlayerSpawnEntityEditor>(entity), writer); break;
            case EntityType::FhDoor: WriteFhDoor(CastEditor<FhDoorEntityEditor>(entity), writer); break;
            case EntityType::FhItemSpawn: WriteFhItemSpawn(CastEditor<FhItemSpawnEntityEditor>(entity), writer); break;
            case EntityType::FhEnemySpawn: WriteFhEnemySpawn(CastEditor<FhEnemySpawnEntityEditor>(entity), writer); break;
            case EntityType::FhTriggerVolume: WriteFhTriggerVolume(CastEditor<FhTriggerVolumeEntityEditor>(entity), writer); break;
            case EntityType::FhAreaVolume: WriteFhAreaVolume(CastEditor<FhAreaVolumeEntityEditor>(entity), writer); break;
            case EntityType::FhJumpPad: WriteFhJumpPad(CastEditor<FhJumpPadEntityEditor>(entity), writer); break;
            case EntityType::FhPointModule: WritePointModule(CastEditor<PointModuleEntityEditor>(entity), writer); break;
            case EntityType::FhMorphCamera: WriteFhMorphCamera(CastEditor<MorphCameraEntityEditor>(entity), writer); break;
            default: break;
            }
        }

        [[nodiscard]] std::vector<std::uint8_t> RepackFhEntityList(const EditorList& entities)
        {
            BinaryWriter writer;
            writer.Write(static_cast<std::uint32_t>(1));
            writer.Position(writer.Position() + sizeof(FhEntityEntry) * (entities.size() + 1));
            std::vector<std::int32_t> offsets;
            for (std::size_t i = 0; i < entities.size(); ++i)
            {
                ThrowIfInvalid(entities[i], true);
                offsets.push_back(std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(writer.Position())));
                WriteFhEntity(entities[i], writer);
                if (i + 1 < entities.size())
                {
                    while (writer.Position() % 4 != 0)
                    {
                        writer.Write(static_cast<std::uint8_t>(0));
                    }
                }
            }
            writer.Position(sizeof(std::uint32_t));
            for (std::size_t i = 0; i < entities.size(); ++i)
            {
                writer.WriteString(R(entities[i]->NodeName), 16);
                writer.Write(offsets[i]);
            }
            writer.WriteString("", 16);
            writer.Write(static_cast<std::uint32_t>(0));
            return writer.ToArray();
        }

        [[nodiscard]] std::vector<EntityEntry> GetEntries(std::span<const std::uint8_t> bytes)
        {
            std::vector<EntityEntry> entries;
            std::int32_t position = static_cast<std::int32_t>(sizeof(EntityHeader));
            while (true)
            {
                const EntityEntry entry = Read::DoOffset<EntityEntry>(bytes, position);
                if (entry.DataOffset == 0)
                {
                    break;
                }
                entries.push_back(entry);
                position += static_cast<std::int32_t>(sizeof(EntityEntry));
                if (position > static_cast<std::int32_t>(bytes.size()))
                {
                    REPACK_DEBUG_ASSERT(false);
                    break;
                }
            }
            return entries;
        }

        [[nodiscard]] std::vector<FhEntityEntry> GetFhEntries(std::span<const std::uint8_t> bytes)
        {
            std::vector<FhEntityEntry> entries;
            std::int32_t position = static_cast<std::int32_t>(sizeof(std::uint32_t));
            while (true)
            {
                const FhEntityEntry entry = Read::DoOffset<FhEntityEntry>(bytes, position);
                if (entry.DataOffset == 0)
                {
                    break;
                }
                entries.push_back(entry);
                position += static_cast<std::int32_t>(sizeof(FhEntityEntry));
                if (position > static_cast<std::int32_t>(bytes.size()))
                {
                    REPACK_DEBUG_ASSERT(false);
                    break;
                }
            }
            return entries;
        }

        [[nodiscard]] bool EqualNodeName(const char* left, const char* right, std::size_t count) noexcept
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                if (left[i] != right[i])
                {
                    return false;
                }
            }
            return true;
        }

        void CompareEntitiesBytes(
            const std::vector<std::uint8_t>& pack,
            const std::vector<std::uint8_t>& file)
        {
            REPACK_DEBUG_ASSERT(pack.size() == file.size());
            const EntityHeader packHeader = Read::ReadStruct<EntityHeader>(Span(pack));
            const EntityHeader fileHeader = Read::ReadStruct<EntityHeader>(Span(file));
            REPACK_DEBUG_ASSERT(packHeader.Version == fileHeader.Version);
            for (std::int32_t i = 0; i < 16; ++i)
            {
                REPACK_DEBUG_ASSERT(packHeader.Lengths[i] == fileHeader.Lengths[i]);
            }
            const auto packEntries = GetEntries(Span(pack));
            const auto fileEntries = GetEntries(Span(file));
            REPACK_DEBUG_ASSERT(packEntries.size() == fileEntries.size());
            for (std::size_t i = 0; i < packEntries.size(); ++i)
            {
                REPACK_DEBUG_ASSERT(packEntries[i].DataOffset == fileEntries[i].DataOffset);
                REPACK_DEBUG_ASSERT(packEntries[i].LayerMask == fileEntries[i].LayerMask);
                REPACK_DEBUG_ASSERT(packEntries[i].Length == fileEntries[i].Length);
                REPACK_DEBUG_ASSERT(EqualNodeName(packEntries[i].NodeName, fileEntries[i].NodeName, 16));
            }
            REPACK_DEBUG_ASSERT(pack == file);
            Nop();
        }

        [[nodiscard]] std::int32_t AddSize(std::int32_t offset, std::size_t size) noexcept
        {
            return std::bit_cast<std::int32_t>(
                static_cast<std::uint32_t>(offset) + static_cast<std::uint32_t>(size));
        }

        [[nodiscard]] bool RangeEqual(
            const std::vector<std::uint8_t>& pack,
            const std::vector<std::uint8_t>& file,
            std::int32_t start,
            std::int32_t end)
        {
            if (start < 0 || end < start || static_cast<std::size_t>(end) > pack.size())
            {
                throw std::out_of_range("Specified argument was out of the range of valid values.");
            }
            if (static_cast<std::size_t>(end) > file.size())
            {
                throw std::out_of_range("Specified argument was out of the range of valid values.");
            }
            return std::equal(
                pack.begin() + start, pack.begin() + end,
                file.begin() + start, file.begin() + end);
        }

        void CompareData(
            std::int32_t offset,
            const std::vector<std::uint8_t>& pack,
            const std::vector<std::uint8_t>& file)
        {
            const EntityDataHeader packHeader = Read::DoOffset<EntityDataHeader>(Span(pack), offset);
            const EntityDataHeader fileHeader = Read::DoOffset<EntityDataHeader>(Span(file), offset);
            REPACK_DEBUG_ASSERT(packHeader.Type == fileHeader.Type);
            REPACK_DEBUG_ASSERT(packHeader.EntityId == fileHeader.EntityId);
            REPACK_DEBUG_ASSERT(packHeader.Position.X.Value == fileHeader.Position.X.Value);
            REPACK_DEBUG_ASSERT(packHeader.Position.Y.Value == fileHeader.Position.Y.Value);
            REPACK_DEBUG_ASSERT(packHeader.Position.Z.Value == fileHeader.Position.Z.Value);
            REPACK_DEBUG_ASSERT(packHeader.UpVector.X.Value == fileHeader.UpVector.X.Value);
            REPACK_DEBUG_ASSERT(packHeader.UpVector.Y.Value == fileHeader.UpVector.Y.Value);
            REPACK_DEBUG_ASSERT(packHeader.UpVector.Z.Value == fileHeader.UpVector.Z.Value);
            REPACK_DEBUG_ASSERT(packHeader.FacingVector.X.Value == fileHeader.FacingVector.X.Value);
            REPACK_DEBUG_ASSERT(packHeader.FacingVector.Y.Value == fileHeader.FacingVector.Y.Value);
            REPACK_DEBUG_ASSERT(packHeader.FacingVector.Z.Value == fileHeader.FacingVector.Z.Value);

            const std::int32_t type = static_cast<std::int32_t>(packHeader.Type) + 100;
            std::int32_t end = 0;
            if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhPlatform)))
            {
                end = AddSize(offset, sizeof(FhPlatformEntityData));
            }
            else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhPlayerSpawn)))
            {
                end = AddSize(offset, sizeof(PlayerSpawnEntityData));
            }
            else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhDoor)))
            {
                end = AddSize(offset, sizeof(FhDoorEntityData));
            }
            else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhItemSpawn)))
            {
                end = AddSize(offset, sizeof(FhItemSpawnEntityData));
            }
            else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhEnemySpawn)))
            {
                end = AddSize(offset, sizeof(NativeInteropDetail::FhEnemySpawnEntityDataUnmanagedLayout));
            }
            else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhTriggerVolume)))
            {
                end = AddSize(offset, sizeof(FhTriggerVolumeEntityData));
            }
            else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhAreaVolume)))
            {
                end = AddSize(offset, sizeof(FhAreaVolumeEntityData));
            }
            else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhJumpPad)))
            {
                end = AddSize(offset, sizeof(FhJumpPadEntityData));
            }
            else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhPointModule)))
            {
                end = AddSize(offset, sizeof(PointModuleEntityData));
            }
            else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhMorphCamera)))
            {
                end = AddSize(offset, sizeof(FhMorphCameraEntityData));
            }

            if (!RangeEqual(pack, file, offset, end))
            {
                if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhPlatform)))
                {
                    [[maybe_unused]] const FhPlatformEntityData packData = Read::DoOffset<FhPlatformEntityData>(Span(pack), offset);
                    [[maybe_unused]] const FhPlatformEntityData fileData = Read::DoOffset<FhPlatformEntityData>(Span(file), offset);
                    DebugBreak();
                }
                else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhPlayerSpawn)))
                {
                    [[maybe_unused]] const PlayerSpawnEntityData packData = Read::DoOffset<PlayerSpawnEntityData>(Span(pack), offset);
                    [[maybe_unused]] const PlayerSpawnEntityData fileData = Read::DoOffset<PlayerSpawnEntityData>(Span(file), offset);
                    DebugBreak();
                }
                else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhDoor)))
                {
                    [[maybe_unused]] const FhDoorEntityData packData = Read::DoOffset<FhDoorEntityData>(Span(pack), offset);
                    [[maybe_unused]] const FhDoorEntityData fileData = Read::DoOffset<FhDoorEntityData>(Span(file), offset);
                    DebugBreak();
                }
                else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhItemSpawn)))
                {
                    [[maybe_unused]] const FhItemSpawnEntityData packData = Read::DoOffset<FhItemSpawnEntityData>(Span(pack), offset);
                    [[maybe_unused]] const FhItemSpawnEntityData fileData = Read::DoOffset<FhItemSpawnEntityData>(Span(file), offset);
                    DebugBreak();
                }
                else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhEnemySpawn)))
                {
                    using RawFhEnemy = NativeInteropDetail::FhEnemySpawnEntityDataUnmanagedLayout;
                    [[maybe_unused]] const RawFhEnemy packData = Read::DoOffset<RawFhEnemy>(Span(pack), offset);
                    [[maybe_unused]] const RawFhEnemy fileData = Read::DoOffset<RawFhEnemy>(Span(file), offset);
                    DebugBreak();
                }
                else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhTriggerVolume)))
                {
                    [[maybe_unused]] const FhTriggerVolumeEntityData packData = Read::DoOffset<FhTriggerVolumeEntityData>(Span(pack), offset);
                    [[maybe_unused]] const FhTriggerVolumeEntityData fileData = Read::DoOffset<FhTriggerVolumeEntityData>(Span(file), offset);
                    DebugBreak();
                }
                else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhAreaVolume)))
                {
                    [[maybe_unused]] const FhAreaVolumeEntityData packData = Read::DoOffset<FhAreaVolumeEntityData>(Span(pack), offset);
                    [[maybe_unused]] const FhAreaVolumeEntityData fileData = Read::DoOffset<FhAreaVolumeEntityData>(Span(file), offset);
                    DebugBreak();
                }
                else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhJumpPad)))
                {
                    const FhJumpPadEntityData packData = Read::DoOffset<FhJumpPadEntityData>(Span(pack), offset);
                    const FhJumpPadEntityData fileData = Read::DoOffset<FhJumpPadEntityData>(Span(file), offset);
                    REPACK_DEBUG_ASSERT(packData.VolumeType == fileData.VolumeType);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxVector1.X.Value == fileData.Box.BoxVector1.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxVector1.Y.Value == fileData.Box.BoxVector1.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxVector1.Z.Value == fileData.Box.BoxVector1.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxVector2.X.Value == fileData.Box.BoxVector2.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxVector2.Y.Value == fileData.Box.BoxVector2.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxVector2.Z.Value == fileData.Box.BoxVector2.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxVector3.X.Value == fileData.Box.BoxVector3.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxVector3.Y.Value == fileData.Box.BoxVector3.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxVector3.Z.Value == fileData.Box.BoxVector3.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxPosition.X.Value == fileData.Box.BoxPosition.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxPosition.Y.Value == fileData.Box.BoxPosition.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxPosition.Z.Value == fileData.Box.BoxPosition.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxDot1.Value == fileData.Box.BoxDot1.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxDot2.Value == fileData.Box.BoxDot2.Value);
                    REPACK_DEBUG_ASSERT(packData.Box.BoxDot3.Value == fileData.Box.BoxDot3.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxVector1.X.Value == fileData.Sphere.BoxVector1.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxVector1.Y.Value == fileData.Sphere.BoxVector1.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxVector1.Z.Value == fileData.Sphere.BoxVector1.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxVector2.X.Value == fileData.Sphere.BoxVector2.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxVector2.Y.Value == fileData.Sphere.BoxVector2.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxVector2.Z.Value == fileData.Sphere.BoxVector2.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxVector3.X.Value == fileData.Sphere.BoxVector3.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxVector3.Y.Value == fileData.Sphere.BoxVector3.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxVector3.Z.Value == fileData.Sphere.BoxVector3.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxPosition.X.Value == fileData.Sphere.BoxPosition.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxPosition.Y.Value == fileData.Sphere.BoxPosition.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxPosition.Z.Value == fileData.Sphere.BoxPosition.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxDot1.Value == fileData.Sphere.BoxDot1.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxDot2.Value == fileData.Sphere.BoxDot2.Value);
                    REPACK_DEBUG_ASSERT(packData.Sphere.BoxDot3.Value == fileData.Sphere.BoxDot3.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxVector1.X.Value == fileData.Cylinder.BoxVector1.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxVector1.Y.Value == fileData.Cylinder.BoxVector1.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxVector1.Z.Value == fileData.Cylinder.BoxVector1.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxVector2.X.Value == fileData.Cylinder.BoxVector2.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxVector2.Y.Value == fileData.Cylinder.BoxVector2.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxVector2.Z.Value == fileData.Cylinder.BoxVector2.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxVector3.X.Value == fileData.Cylinder.BoxVector3.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxVector3.Y.Value == fileData.Cylinder.BoxVector3.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxVector3.Z.Value == fileData.Cylinder.BoxVector3.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxPosition.X.Value == fileData.Cylinder.BoxPosition.X.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxPosition.Y.Value == fileData.Cylinder.BoxPosition.Y.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxPosition.Z.Value == fileData.Cylinder.BoxPosition.Z.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxDot1.Value == fileData.Cylinder.BoxDot1.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxDot2.Value == fileData.Cylinder.BoxDot2.Value);
                    REPACK_DEBUG_ASSERT(packData.Cylinder.BoxDot3.Value == fileData.Cylinder.BoxDot3.Value);
                    DebugBreak();
                }
                else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhPointModule)))
                {
                    [[maybe_unused]] const PointModuleEntityData packData = Read::DoOffset<PointModuleEntityData>(Span(pack), offset);
                    [[maybe_unused]] const PointModuleEntityData fileData = Read::DoOffset<PointModuleEntityData>(Span(file), offset);
                    DebugBreak();
                }
                else if (type == static_cast<std::int32_t>(static_cast<std::uint16_t>(EntityType::FhMorphCamera)))
                {
                    [[maybe_unused]] const FhMorphCameraEntityData packData = Read::DoOffset<FhMorphCameraEntityData>(Span(pack), offset);
                    [[maybe_unused]] const FhMorphCameraEntityData fileData = Read::DoOffset<FhMorphCameraEntityData>(Span(file), offset);
                    DebugBreak();
                }
            }
            Nop();
        }

        void CompareFhEntitiesBytes(
            const std::vector<std::uint8_t>& pack,
            const std::vector<std::uint8_t>& file)
        {
            REPACK_DEBUG_ASSERT(pack.size() == file.size());
            const std::uint32_t packVersion = Read::ReadStruct<std::uint32_t>(Span(pack));
            const std::uint32_t fileVersion = Read::ReadStruct<std::uint32_t>(Span(file));
            REPACK_DEBUG_ASSERT(packVersion == fileVersion);
            const auto packEntries = GetFhEntries(Span(pack));
            const auto fileEntries = GetFhEntries(Span(file));
            REPACK_DEBUG_ASSERT(packEntries.size() == fileEntries.size());
            for (std::size_t i = 0; i < packEntries.size(); ++i)
            {
                REPACK_DEBUG_ASSERT(packEntries[i].DataOffset == fileEntries[i].DataOffset);
                REPACK_DEBUG_ASSERT(EqualNodeName(packEntries[i].NodeName, fileEntries[i].NodeName, 16));
                CompareData(std::bit_cast<std::int32_t>(packEntries[i].DataOffset), pack, file);
            }
            REPACK_DEBUG_ASSERT(pack == file);
            Nop();
        }

        template <typename TEditor>
        void CompareTyped(const EditorPtr& left, const EditorPtr& right)
        {
            const auto typedLeft = std::dynamic_pointer_cast<TEditor>(left);
            const auto typedRight = std::dynamic_pointer_cast<TEditor>(right);
            if (!typedLeft || !typedRight)
            {
                throw std::bad_cast();
            }
            typedLeft->CompareTo(typedRight);
        }

        void CompareEditors(
            const EditorPtr& left,
            const EditorPtr& right,
            bool firstHunt,
            bool multiplayer)
        {
            REPACK_DEBUG_ASSERT(left->Type == right->Type);
            REPACK_DEBUG_ASSERT(StringEquals(left->NodeName, right->NodeName));
            if (left->LayerMask != right->LayerMask)
            {
                std::cout << "layer mask:\n";
                std::cout << Metadata::GetLayerNames(left->LayerMask, multiplayer) << '\n';
                std::cout << Metadata::GetLayerNames(right->LayerMask, multiplayer) << '\n';
            }
            if (!Vec3Equals(left->Position, right->Position))
            {
                std::cout << "position:\n";
                std::cout << Vec3Text(left->Position) << '\n';
                std::cout << Vec3Text(right->Position) << '\n';
            }
            if (!Vec3Equals(left->Facing, right->Facing))
            {
                std::cout << "facing:\n";
                std::cout << Vec3Text(left->Facing) << '\n';
                std::cout << Vec3Text(right->Facing) << '\n';
            }
            if (!Vec3Equals(left->Up, right->Up))
            {
                std::cout << "up:\n";
                std::cout << Vec3Text(left->Up) << '\n';
                std::cout << Vec3Text(right->Up) << '\n';
            }
            if (firstHunt)
            {
                switch (left->Type)
                {
                case EntityType::FhAreaVolume: CompareTyped<FhAreaVolumeEntityEditor>(left, right); break;
                case EntityType::FhDoor: CompareTyped<FhDoorEntityEditor>(left, right); break;
                case EntityType::FhEnemySpawn: CompareTyped<FhEnemySpawnEntityEditor>(left, right); break;
                case EntityType::FhItemSpawn: CompareTyped<FhItemSpawnEntityEditor>(left, right); break;
                case EntityType::FhJumpPad: CompareTyped<FhJumpPadEntityEditor>(left, right); break;
                case EntityType::FhMorphCamera: CompareTyped<MorphCameraEntityEditor>(left, right); break;
                case EntityType::FhPlatform: CompareTyped<FhPlatformEntityEditor>(left, right); break;
                case EntityType::FhPlayerSpawn: CompareTyped<PlayerSpawnEntityEditor>(left, right); break;
                case EntityType::FhPointModule: CompareTyped<PointModuleEntityEditor>(left, right); break;
                case EntityType::FhTriggerVolume: CompareTyped<FhTriggerVolumeEntityEditor>(left, right); break;
                default: REPACK_DEBUG_ASSERT(false); break;
                }
            }
            else
            {
                switch (left->Type)
                {
                case EntityType::AreaVolume: CompareTyped<AreaVolumeEntityEditor>(left, right); break;
                case EntityType::Artifact: CompareTyped<ArtifactEntityEditor>(left, right); break;
                case EntityType::CameraSequence: CompareTyped<CameraSequenceEntityEditor>(left, right); break;
                case EntityType::Door: CompareTyped<DoorEntityEditor>(left, right); break;
                case EntityType::EnemySpawn: CompareTyped<EnemySpawnEntityEditor>(left, right); break;
                case EntityType::FlagBase: CompareTyped<FlagBaseEntityEditor>(left, right); break;
                case EntityType::ForceField: CompareTyped<ForceFieldEntityEditor>(left, right); break;
                case EntityType::ItemSpawn: CompareTyped<ItemSpawnEntityEditor>(left, right); break;
                case EntityType::JumpPad: CompareTyped<JumpPadEntityEditor>(left, right); break;
                case EntityType::LightSource: CompareTyped<LightSourceEntityEditor>(left, right); break;
                case EntityType::MorphCamera: CompareTyped<MorphCameraEntityEditor>(left, right); break;
                case EntityType::NodeDefense: CompareTyped<NodeDefenseEntityEditor>(left, right); break;
                case EntityType::Object: CompareTyped<ObjectEntityEditor>(left, right); break;
                case EntityType::OctolithFlag: CompareTyped<OctolithFlagEntityEditor>(left, right); break;
                case EntityType::Platform: CompareTyped<PlatformEntityEditor>(left, right); break;
                case EntityType::PlayerSpawn: CompareTyped<PlayerSpawnEntityEditor>(left, right); break;
                case EntityType::PointModule: CompareTyped<PointModuleEntityEditor>(left, right); break;
                case EntityType::Teleporter: CompareTyped<TeleporterEntityEditor>(left, right); break;
                case EntityType::TriggerVolume: CompareTyped<TriggerVolumeEntityEditor>(left, right); break;
                default: REPACK_DEBUG_ASSERT(false); break;
                }
            }
        }

        [[nodiscard]] EditorPtr SingleMatching(
            const EditorList& entities,
            const std::function<bool(const EditorPtr&)>& predicate)
        {
            EditorPtr result;
            std::size_t count = 0;
            for (const auto& entity : entities)
            {
                if (predicate(entity))
                {
                    result = entity;
                    ++count;
                    if (count > 1)
                    {
                        ThrowMoreThanOneMatching();
                    }
                }
            }
            if (count == 0)
            {
                ThrowNoMatching();
            }
            return result;
        }

        [[nodiscard]] EditorPtr SingleOrDefaultMatching(
            const EditorList& entities,
            const std::function<bool(const EditorPtr&)>& predicate)
        {
            EditorPtr result;
            std::size_t count = 0;
            for (const auto& entity : entities)
            {
                if (predicate(entity))
                {
                    result = entity;
                    ++count;
                    if (count > 1)
                    {
                        ThrowMoreThanOneMatching();
                    }
                }
            }
            return result;
        }

        [[nodiscard]] std::int16_t MaxEntityId(const EditorList& entities)
        {
            if (entities.empty())
            {
                ThrowNoElements();
            }
            std::int16_t result = entities.front()->Id;
            for (std::size_t i = 1; i < entities.size(); ++i)
            {
                result = std::max(result, entities[i]->Id);
            }
            return result;
        }
    }

    std::vector<std::uint8_t> Repack::RepackMphEntities(const std::string& room)
    {
        const RoomMetadata& meta = Room(room);
        const std::string path = EntityPath(meta);
        EditorList entities = meta.FirstHunt ? GetFhEntities(path) : GetEntities(path);
        return RepackEntities(ConvertFhToMph(entities));
    }

    std::vector<std::uint8_t> Repack::RepackFhEntities(const std::string& room, RepackFilter filter)
    {
        const RoomMetadata& meta = Room(room);
        const std::string path = EntityPath(meta);
        EditorList entities = meta.FirstHunt ? GetFhEntities(path) : GetEntities(path, filter);
        return RepackFhEntityList(ConvertMphToFh(entities));
    }

    std::vector<std::uint8_t> Repack::RepackHook(const std::string& path, bool firstHunt)
    {
        const bool hookEnabled = false;
        std::vector<std::uint8_t> bytes = Read::ReadBytes(path, firstHunt);
        if (!hookEnabled)
        {
            return bytes;
        }
        const std::string marker = "levels\\entities\\";
        const std::size_t found = path.rfind(marker);
        if (found != std::string::npos)
        {
            const std::string filename = marker + path.substr(found + marker.size());
            auto matchesRoom = [&](const char* name)
            {
                const auto& entityPath = Room(name).EntityPath;
                return entityPath && filename == *entityPath;
            };
            if (matchesRoom("UNIT2_LAND"))
            {
                EditorList entities = GetEntities(filename);
                std::int16_t id = MaxEntityId(entities);
                id = IncrementInt16Unchecked(id);
                auto teleporter = std::make_shared<TeleporterEntityEditor>();
                teleporter->Id = id;
                teleporter->LayerMask = 7;
                teleporter->Up = ::OpenTK::Mathematics::Vector3::UnitY;
                teleporter->Facing = UnitZ();
                teleporter->Position = Vec3(-19.889404F, 0.0F, 0.0F);
                teleporter->NodeName = S("rmMain");
                teleporter->Active = true;
                teleporter->ArtifactId = 8;
                teleporter->LoadIndex = 88;
                teleporter->TargetIndex = 88;
                teleporter->TargetRoom = S("unit2_RM3_Ent.b");
                entities.push_back(teleporter);
                return RepackEntities(entities);
            }
            if (matchesRoom("UNIT2_RM3"))
            {
                EditorList entities = GetEntities(filename);
                std::int16_t id = MaxEntityId(entities);
                id = IncrementInt16Unchecked(id);
                auto teleporter = std::make_shared<TeleporterEntityEditor>();
                teleporter->Id = id;
                teleporter->LayerMask = 7;
                teleporter->Up = ::OpenTK::Mathematics::Vector3::UnitY;
                teleporter->Facing = UnitZ();
                teleporter->Position = Vec3(13.573242F, 2.576416F, -13.726074F);
                teleporter->NodeName = S("rmMain");
                teleporter->Active = false;
                teleporter->Invisible = true;
                teleporter->ArtifactId = 8;
                teleporter->LoadIndex = 88;
                teleporter->TargetIndex = 88;
                teleporter->TargetRoom = S("unit2_Land_Ent.");
                entities.push_back(teleporter);

                const auto triggerEntity = SingleMatching(entities, [](const EditorPtr& entity)
                    {
                        return entity->Id == 25 && entity->Type == EntityType::TriggerVolume;
                    });
                const auto trigger = std::dynamic_pointer_cast<TriggerVolumeEntityEditor>(triggerEntity);
                if (!trigger)
                {
                    throw std::bad_cast();
                }
                trigger->Volume = CollisionVolume(
                    trigger->Volume.BoxVector1,
                    trigger->Volume.BoxVector2,
                    trigger->Volume.BoxVector3,
                    Vec3(-4.866759F, -2.576416F, 3.890676F),
                    trigger->Volume.BoxDot1,
                    trigger->Volume.BoxDot2,
                    trigger->Volume.BoxDot3);

                const auto spawnEntity = SingleMatching(entities, [](const EditorPtr& entity)
                    {
                        return entity->Type == EntityType::PlayerSpawn;
                    });
                const auto spawn = std::dynamic_pointer_cast<PlayerSpawnEntityEditor>(spawnEntity);
                if (!spawn)
                {
                    throw std::bad_cast();
                }
                spawn->Position = Vec3(18.898872F, 3.5332031F, -19.64511F);
                spawn->NodeName = S("rmE");
                return RepackEntities(entities);
            }
            if (matchesRoom("UNIT3_RM1"))
            {
                EditorList entities = GetEntities(filename);
                const auto spawnEntity = SingleMatching(entities, [](const EditorPtr& entity)
                    {
                        return entity->Id == 16 && entity->Type == EntityType::PlayerSpawn;
                    });
                const auto spawn = std::dynamic_pointer_cast<PlayerSpawnEntityEditor>(spawnEntity);
                if (!spawn)
                {
                    throw std::bad_cast();
                }
                spawn->Position = Vec3(15.581741F, 11.746094F, -17.398003F);
                spawn->Facing = UnitZ();
                spawn->NodeName = S("rmHallB");
                return RepackEntities(entities);
            }
            if (matchesRoom("Gorea_b1"))
            {
                EditorList entities = GetEntities(filename);
                const auto spawnEntity = SingleMatching(entities, [](const EditorPtr& entity)
                    {
                        return entity->Type == EntityType::PlayerSpawn;
                    });
                const auto spawn = std::dynamic_pointer_cast<PlayerSpawnEntityEditor>(spawnEntity);
                if (!spawn)
                {
                    throw std::bad_cast();
                }
                spawn->Position = Vec3(0.0F, -0.27163085F, -30.45435F);
                return RepackEntities(entities);
            }
        }
        return bytes;
    }

    std::vector<std::uint8_t> Repack::TestEntityEdit()
    {
        const RoomMetadata& meta = Room("Level SP Regulator");
        std::optional<std::string> entityPath = meta.EntityPath;
        EditorList entities;
        if (entityPath)
        {
            entities = meta.FirstHunt ? GetFhEntities(*entityPath) : GetEntities(*entityPath);
        }
        else
        {
            entityPath = meta.Name + "_ent.bin";
        }
        for (const auto& entity : entities)
        {
            if (const auto editor = std::dynamic_pointer_cast<FhEnemySpawnEntityEditor>(entity))
            {
                editor->EnemyType = FhEnemyType::WarWasp;
                editor->SpawnLimit = 1;
            }
        }
        std::vector<std::uint8_t> bytes = meta.FirstHunt
            ? RepackFhEntityList(entities)
            : RepackEntities(entities);
        const std::string outputPath = Paths::Combine(
            Paths::Export(), "_pack", ::MphRead::NativeRuntime::PathGetFileName(*entityPath));
        FileWriteAllBytes(outputPath, bytes);
        Nop();
        return bytes;
    }

    void Repack::TestEntities()
    {
        for (const auto& pair : Metadata::RoomMetadata)
        {
            const RoomMetadata& meta = *pair.second;
            if (!meta.EntityPath)
            {
                continue;
            }
            if (meta.FirstHunt)
            {
                const EditorList entities = GetFhEntities(*meta.EntityPath);
                const auto bytes = RepackFhEntityList(entities);
                const auto fileBytes = FileReadAllBytes(Paths::Combine(Paths::FhFileSystem(), *meta.EntityPath));
                CompareFhEntitiesBytes(bytes, fileBytes);
                Nop();
            }
            else
            {
                const EditorList entities = GetEntities(*meta.EntityPath);
                const auto bytes = RepackEntities(entities);
                const auto fileBytes = FileReadAllBytes(Paths::Combine(Paths::FileSystem(), *meta.EntityPath));
                CompareEntitiesBytes(bytes, fileBytes);
                Nop();
            }
        }
        Nop();
    }

    void Repack::CompareRooms(
        const std::string& room1,
        const std::string& room2,
        const std::string& game1,
        const std::string& game2)
    {
        std::cout << "Comparing " << game1 << " \"" << room1 << "\" to "
            << game2 << " \"" << room2 << "\"\n";
        const RoomMetadata& meta1 = Room(room1);
        const RoomMetadata& meta2 = Room(room2);
        REPACK_DEBUG_ASSERT(meta1.FirstHunt == meta2.FirstHunt);
        REPACK_DEBUG_ASSERT(meta1.EntityPath && meta2.EntityPath);
        if (!meta1.EntityPath || !meta2.EntityPath)
        {
            throw System::NullReferenceException();
        }
        const std::string root = ::MphRead::NativeRuntime::PathToUtf8(::MphRead::NativeRuntime::PathFromUtf8(Paths::FileSystem()).parent_path());
        std::string path1;
        std::string path2;
        if (meta1.FirstHunt)
        {
            path1 = Paths::Combine(root, game1, "data", *meta1.EntityPath);
            path2 = Paths::Combine(root, game2, "data", *meta2.EntityPath);
        }
        else
        {
            path1 = Paths::Combine(root, game1, *meta1.EntityPath);
            path2 = Paths::Combine(root, game2, *meta2.EntityPath);
        }
        const auto bytes1 = FileReadAllBytes(path1);
        const auto bytes2 = FileReadAllBytes(path2);
        bool differences = false;
        if (bytes1.size() != bytes2.size())
        {
            std::cout << "game1 length = " << bytes1.size()
                << ", game2 length = " << bytes2.size() << '\n';
            differences = true;
        }
        else if (bytes1 != bytes2)
        {
            std::cout << "byte sequences differ\n";
            differences = true;
        }
        if (differences)
        {
            const EntityHeader header1 = Read::ReadStruct<EntityHeader>(Span(bytes1));
            const EntityHeader header2 = Read::ReadStruct<EntityHeader>(Span(bytes2));
            REPACK_DEBUG_ASSERT(header1.Version == header2.Version);
            for (std::int32_t i = 0; i < 16; ++i)
            {
                const char* prefix = header1.Lengths[i] == header2.Lengths[i] ? "" : "* ";
                std::cout << prefix << Metadata::GetLayerName(i, meta1.Multiplayer)
                    << ": game1 = " << header1.Lengths[i]
                    << ", game2 = " << header2.Lengths[i] << '\n';
            }
            const auto entries1 = GetEntries(Span(bytes1));
            const auto entries2 = GetEntries(Span(bytes2));
            if (entries1.size() != entries2.size())
            {
                std::cout << "game1 entries = " << entries1.size()
                    << ", game2 entries = " << entries2.size() << '\n';
            }
            std::cout << '\n';
            EditorList list1 = meta1.FirstHunt
                ? GetFhEntities(path1, true)
                : GetEntities(path1, RepackFilter::All, true);
            EditorList list2 = meta2.FirstHunt
                ? GetFhEntities(path2, true)
                : GetEntities(path2, RepackFilter::All, true);
            REPACK_DEBUG_ASSERT(list1.size() == entries1.size());
            REPACK_DEBUG_ASSERT(list2.size() == entries2.size());
            std::vector<std::int16_t> ids;
            ids.reserve(list1.size() + list2.size());
            for (const auto& entity : list1)
            {
                ids.push_back(entity->Id);
            }
            for (const auto& entity : list2)
            {
                ids.push_back(entity->Id);
            }
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
            for (std::int16_t entityId : ids)
            {
                const EditorPtr entity1 = SingleOrDefaultMatching(list1, [&](const EditorPtr& entity)
                    {
                        return entity->Id == entityId;
                    });
                const EditorPtr entity2 = SingleOrDefaultMatching(list2, [&](const EditorPtr& entity)
                    {
                        return entity->Id == entityId;
                    });
                if (entity1 && !entity2)
                {
                    std::cout << "game1 has " << EntityTypeText(entity1->Type)
                        << " ID = " << entity1->Id << '\n';
                }
                else if (!entity1 && entity2)
                {
                    std::cout << "game2 has " << EntityTypeText(entity2->Type)
                        << " ID = " << entity2->Id << '\n';
                }
                else if (entity1 && entity2)
                {
                    std::cout << EntityTypeText(entity1->Type) << ' ' << entity1->Id << '\n';
                    CompareEditors(entity1, entity2, meta1.FirstHunt, meta1.Multiplayer);
                }
                else
                {
                    REPACK_DEBUG_ASSERT(false);
                }
                std::cout << '\n';
            }
            std::cout << "done\n";
        }
        else
        {
            std::cout << "no differences\n";
        }
        Nop();
    }

    void Repack::PrintLayers(std::uint16_t mask)
    {
        std::vector<std::string> sp;
        std::vector<std::string> mp;
        if ((mask & 1) != 0)
        {
            sp.push_back("Initial");
            mp.push_back("Battle/Prime Hunter 2P");
        }
        if ((mask & 2) != 0)
        {
            sp.push_back("Cleared");
            mp.push_back("Battle/Prime Hunter 3P");
        }
        if ((mask & 4) != 0)
        {
            sp.push_back("Layer 2");
            mp.push_back("Battle/Prime Hunter 4P");
        }
        if ((mask & 8) != 0)
        {
            sp.push_back("Layer 3");
            mp.push_back("Battle Teams");
        }
        if ((mask & 0x10) != 0) mp.push_back("Nodes 2P");
        if ((mask & 0x20) != 0) mp.push_back("Nodes 3P");
        if ((mask & 0x40) != 0) mp.push_back("Nodes 4P");
        if ((mask & 0x80) != 0) mp.push_back("Nodes Teams");
        if ((mask & 0x100) != 0) mp.push_back("Bounty 2P");
        if ((mask & 0x200) != 0) mp.push_back("Bounty 3P");
        if ((mask & 0x400) != 0) mp.push_back("Bounty 4P");
        if ((mask & 0x800) != 0) mp.push_back("Bounty Teams");
        if ((mask & 0x1000) != 0) mp.push_back("Capture");
        if ((mask & 0x2000) != 0) mp.push_back("Mode 15");
        if ((mask & 0x4000) != 0) mp.push_back("Defender");
        if ((mask & 0x8000) != 0) mp.push_back("Survival");
        if (sp.empty()) sp.push_back("None");
        if (mp.empty()) mp.push_back("None");
        auto print = [](const char* label, const std::vector<std::string>& values)
        {
            std::cout << label;
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i != 0)
                {
                    std::cout << ", ";
                }
                std::cout << values[i];
            }
            std::cout << '\n';
        };
        print("1P: ", sp);
        print("MP: ", mp);
    }

    void Repack::WriteVolume(BinaryWriter& writer, const CollisionVolume& volume)
    {
        REPACK_DEBUG_ASSERT(
            volume.Type == VolumeType::Box
            || volume.Type == VolumeType::Cylinder
            || volume.Type == VolumeType::Sphere);
        writer.Write(static_cast<std::uint32_t>(volume.Type));
        if (volume.Type == VolumeType::Box)
        {
            writer.WriteVector3(volume.BoxVector1);
            writer.WriteVector3(volume.BoxVector2);
            writer.WriteVector3(volume.BoxVector3);
            writer.WriteVector3(volume.BoxPosition);
            writer.WriteFloat(volume.BoxDot1);
            writer.WriteFloat(volume.BoxDot2);
            writer.WriteFloat(volume.BoxDot3);
        }
        else if (volume.Type == VolumeType::Cylinder)
        {
            writer.WriteVector3(volume.CylinderVector);
            writer.WriteVector3(volume.CylinderPosition);
            writer.WriteFloat(volume.CylinderRadius);
            writer.WriteFloat(volume.CylinderDot);
            for (std::int32_t i = 0; i < 7; ++i)
            {
                writer.Write(static_cast<std::uint32_t>(0));
            }
        }
        else if (volume.Type == VolumeType::Sphere)
        {
            writer.WriteVector3(volume.SpherePosition);
            writer.WriteFloat(volume.SphereRadius);
            for (std::int32_t i = 0; i < 11; ++i)
            {
                writer.Write(static_cast<std::uint32_t>(0));
            }
        }
    }

    void Repack::WriteFhVolume(BinaryWriter& writer, const CollisionVolume& volume)
    {
        REPACK_DEBUG_ASSERT(
            volume.Type == VolumeType::Box
            || volume.Type == VolumeType::Cylinder
            || volume.Type == VolumeType::Sphere);
        if (volume.Type == VolumeType::Box)
        {
            writer.Write(static_cast<std::uint32_t>(FhVolumeType::Box));
            writer.WriteVector3(volume.BoxPosition);
            writer.WriteVector3(volume.BoxVector1);
            writer.WriteVector3(volume.BoxVector2);
            writer.WriteVector3(volume.BoxVector3);
            writer.WriteFloat(volume.BoxDot1);
            writer.WriteFloat(volume.BoxDot2);
            writer.WriteFloat(volume.BoxDot3);
        }
        else if (volume.Type == VolumeType::Cylinder)
        {
            writer.Write(static_cast<std::uint32_t>(FhVolumeType::Cylinder));
            writer.WriteVector3(volume.CylinderPosition);
            writer.WriteVector3(volume.CylinderVector);
            writer.WriteFloat(volume.CylinderDot);
            writer.WriteFloat(volume.CylinderRadius);
            for (std::int32_t i = 0; i < 7; ++i)
            {
                writer.Write(static_cast<std::uint32_t>(0));
            }
        }
        else if (volume.Type == VolumeType::Sphere)
        {
            writer.Write(static_cast<std::uint32_t>(FhVolumeType::Sphere));
            writer.WriteVector3(volume.SpherePosition);
            writer.WriteFloat(volume.SphereRadius);
            for (std::int32_t i = 0; i < 11; ++i)
            {
                writer.Write(static_cast<std::uint32_t>(0));
            }
        }
    }
}

#undef REPACK_DEBUG_ASSERT

namespace MphRead::Utility
{
    std::vector<std::uint8_t> Repack::RepackEntitiesFrom(
        std::span<Editor::EntityEditorBase* const> entities)
    {
        // The caller owns the editors; these references do not.
        EditorList list;
        list.reserve(entities.size());
        for (Editor::EntityEditorBase* const entity : entities)
        {
            list.push_back(EditorPtr(entity, [](Editor::EntityEditorBase*) {}));
        }
        return RepackEntities(list);
    }
}
