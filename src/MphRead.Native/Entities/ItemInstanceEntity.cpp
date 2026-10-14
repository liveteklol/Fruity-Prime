#include "ItemInstanceEntity.hpp"

#include "../Formats/Effects.hpp"
#include "../GameState.hpp"
#include "../MemoryArrays.hpp"
#include "../Metadata/Metadata.hpp"
#include "../Scene.hpp"
#include "ItemSpawnEntity.hpp"
#include "Players/PlayerEntity.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../Formats/Types.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <stdexcept>
#include <utility>

using ::MphRead::NativeRuntime::RequireReference;
using ::OpenTK::Mathematics::CreateScale;
using ::OpenTK::Mathematics::Determinant;
using ::OpenTK::Mathematics::Inverted;
using ::OpenTK::Mathematics::LengthSquared;
using ::OpenTK::Mathematics::MathHelper::DegreesToRadians;
using ::OpenTK::Mathematics::Multiply;
using ::OpenTK::Mathematics::ScaleVector;

namespace
{
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;

    constexpr Vector3 UnitX(1.0F, 0.0F, 0.0F);
    constexpr Vector3 UnitY(0.0F, 1.0F, 0.0F);
    constexpr Vector3 UnitZ(0.0F, 0.0F, 1.0F);
    constexpr Vector3 One(1.0F, 1.0F, 1.0F);

    template <typename T, std::size_t Size>
    [[nodiscard]] const T& GetListItem(
        const std::array<T, Size>& values,
        std::int32_t index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= Size)
        {
            throw System::ArgumentOutOfRangeException();
        }
        return values[static_cast<std::size_t>(index)];
    }

}

namespace MphRead::Entities
{
    const std::array<std::int32_t, 22> ItemInstanceEntity::_scanIds{
        9, 11, 12, 0, 10, 21, 13, 23, 22, 18, 20, 19, 28, 14, 15, 16, 17, 0, 24, 463, 0, 0
    };

    const std::array<std::int32_t, 22> ItemInstanceEntity::_sfxIds{
        33, 33, 33, -1, 34, -1, 34, -1, -1, -1, -1, -1, -1, 33, 33, 33, 33, -1, -1, -1, -1, -1
    };

    std::uint16_t SpinningEntityBase::_nextItemRotation = 0;

    ItemInstanceEntityData& ItemInstanceEntityData::operator=(
        const ItemInstanceEntityData& other) noexcept
    {
        if (this != &other)
        {
            this->~ItemInstanceEntityData();
            ::new (this) ItemInstanceEntityData(other);
        }
        return *this;
    }

    SpinningEntityBase::SpinningEntityBase(
        float spinSpeed,
        Vector3 spinAxis,
        EntityType type,
        Scene* scene)
        : EntityBase(type, scene),
          _spin(GetItemRotation()),
          _spinSpeed(spinSpeed),
          _spinAxis(spinAxis),
          _spinModelIndex(-1),
          _floatModelIndex(-1)
    {
    }

    SpinningEntityBase::SpinningEntityBase(
        float spinSpeed,
        Vector3 spinAxis,
        std::int32_t spinModelIndex,
        EntityType type,
        Scene* scene)
        : EntityBase(type, scene),
          _spin(GetItemRotation()),
          _spinSpeed(spinSpeed),
          _spinAxis(spinAxis),
          _spinModelIndex(spinModelIndex),
          _floatModelIndex(-1)
    {
    }

    SpinningEntityBase::SpinningEntityBase(
        float spinSpeed,
        Vector3 spinAxis,
        std::int32_t spinModelIndex,
        std::int32_t floatModelIndex,
        EntityType type,
        Scene* scene)
        : EntityBase(type, scene),
          _spin(GetItemRotation()),
          _spinSpeed(spinSpeed),
          _spinAxis(spinAxis),
          _spinModelIndex(spinModelIndex),
          _floatModelIndex(floatModelIndex)
    {
    }

    SpinningEntityBase::SpinningEntityBase(
        float spinSpeed,
        Vector3 spinAxis,
        std::int32_t spinModelIndex,
        std::int32_t floatModelIndex,
        EntityType type,
        Formats::Culling::NodeRef nodeRef,
        Scene* scene)
        : EntityBase(type, nodeRef, scene),
          _spin(GetItemRotation()),
          _spinSpeed(spinSpeed),
          _spinAxis(spinAxis),
          _spinModelIndex(spinModelIndex),
          _floatModelIndex(floatModelIndex)
    {
    }

    SpinningEntityBase::~SpinningEntityBase() = default;

    bool SpinningEntityBase::Process()
    {
        _spin = std::fmod(
            static_cast<float>(
                _spin
                + RequireReference(_scene).FrameTime() * 360.0F * _spinSpeed),
            360.0F);
        return EntityBase::Process();
    }

    Matrix4 SpinningEntityBase::GetModelTransform(
        ModelInstance& inst,
        std::int32_t index)
    {
        const std::shared_ptr<Model> model = inst.Model();
        Matrix4 transform = CreateScale(RequireReference(model).Scale);
        if (index == _spinModelIndex)
        {
            const Vector3 rotation(
                DegreesToRadians(_spinAxis.X * _spin),
                DegreesToRadians(_spinAxis.Y * _spin),
                DegreesToRadians(_spinAxis.Z * _spin));
            transform = Multiply(
                transform,
                Matrix::GetTransformSRT(One, rotation, Vector3::Zero));
        }
        transform = Multiply(transform, _transform);
        if (index == _floatModelIndex)
        {
            transform.M42 += (
                std::sin(_spin / 180.0F * std::numbers::pi_v<float>) + 1.0F)
                / 8.0F;
        }
        return transform;
    }

    float SpinningEntityBase::GetItemRotation() noexcept
    {
        const float rotation
            = static_cast<float>(_nextItemRotation)
            / static_cast<float>(0x10000)
            * 360.0F;
        _nextItemRotation = static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(_nextItemRotation) + 0x2000U);
        return rotation;
    }

    ItemInstanceEntity::ItemInstanceEntity(
        ItemInstanceEntityData data,
        Formats::Culling::NodeRef nodeRef,
        Scene* scene)
        : SpinningEntityBase(
              0.35F,
              UnitY,
              0,
              0,
              EntityType::ItemInstance,
              nodeRef,
              scene)
    {
        Position = data.Position;
        _itemType = data.ItemType;
        _scanId = GetListItem(
            _scanIds,
            static_cast<std::int32_t>(data.ItemType));
        if (GameState::Multiplayer()
            && GameState::AffinityWeapons()
            && (_itemType == MphRead::ItemType::VoltDriver
                || _itemType == MphRead::ItemType::Battlehammer
                || _itemType == MphRead::ItemType::Imperialist
                || _itemType == MphRead::ItemType::Judicator
                || _itemType == MphRead::ItemType::Magmaul
                || _itemType == MphRead::ItemType::ShockCoil))
        {
            _itemType = MphRead::ItemType::AffinityWeapon;
        }
        SetUpModel(GetListItem(
            Metadata::Items,
            static_cast<std::int32_t>(_itemType)));
        if (data.DespawnTimer > 0)
        {
            _despawnTimer = data.DespawnTimer;
        }
    }

    MphRead::ItemType ItemInstanceEntity::ItemType() const noexcept
    {
        return _itemType;
    }

    std::int32_t ItemInstanceEntity::ParentId() const noexcept
    {
        return _parentId;
    }

    void ItemInstanceEntity::SetParentId(std::int32_t value) noexcept
    {
        _parentId = value;
    }

    std::int32_t ItemInstanceEntity::DespawnTimer() const noexcept
    {
        return _despawnTimer;
    }

    void ItemInstanceEntity::SetDespawnTimer(std::int32_t value) noexcept
    {
        _despawnTimer = value;
    }

    ItemSpawnEntity* ItemInstanceEntity::Owner() const noexcept
    {
        return _owner;
    }

    void ItemInstanceEntity::SetOwner(ItemSpawnEntity* value) noexcept
    {
        _owner = value;
    }

    std::shared_ptr<Formats::NodeData3> ItemInstanceEntity::ClosestNode() const noexcept
    {
        return _closestNode;
    }

    void ItemInstanceEntity::SetClosestNode(
        std::shared_ptr<Formats::NodeData3> value) noexcept
    {
        _closestNode = std::move(value);
    }

    void ItemInstanceEntity::Initialize()
    {
        SpinningEntityBase::Initialize();
        if (_itemType == MphRead::ItemType::ArtifactKey)
        {
            const Matrix4 transform = Matrix::GetTransform4(
                UnitX,
                UnitY,
                Position);
            _effectEntry = RequireReference(_scene).SpawnEffectGetEntry(
                144,
                transform);
            if (_effectEntry != nullptr)
            {
                _effectEntry->SetElementExtension(true);
            }
        }
    }

    void ItemInstanceEntity::GetVectors(
        Vector3& position,
        Vector3& up,
        Vector3& facing)
    {
        position = Position;
        up = UnitY;
        facing = UnitZ;
    }

    bool ItemInstanceEntity::Process()
    {
        if (!_linkDone && _parentId != -1)
        {
            std::shared_ptr<EntityBase> parent{};
            if (RequireReference(_scene).TryGetEntity(_parentId, parent))
            {
                _parent = std::move(parent);
            }
            if (_parent != nullptr)
            {
                _invPos = Matrix::Vec3MultMtx4(
                    Position,
                    Inverted(_parent->CollisionTransform()));
            }
            _linkDone = true;
        }
        if (_linkDone && _parent != nullptr)
        {
            Position = Matrix::Vec3MultMtx4(
                _invPos,
                _parent->CollisionTransform());
        }

        _soundSource.Update(Position, 7);
        UpdateNodeRefVolume();

        if (_effectEntry != nullptr)
        {
            const Matrix4 transform = GetTransformMatrix(
                UnitX,
                UnitY,
                Position);
            _effectEntry->Transform(Position, transform);
        }

        if (_despawnTimer > 0)
        {
            --_despawnTimer;
        }
        if (_despawnTimer == 0)
        {
            if (_owner != nullptr)
            {
                _owner->SetItem(nullptr);
                if (GameState::SinglePlayer())
                {
                    auto storySave = GameState::StorySave;
                    const std::int32_t roomId = RequireReference(_scene).RoomId();
                    const std::int32_t ownerId = _owner->Id;
                    RequireReference(storySave).SetRoomState(
                        roomId,
                        ownerId,
                        1);
                    if (!_owner->AlwaysActive())
                    {
                        _owner->Active = false;
                    }
                }
            }
            if (_effectEntry != nullptr)
            {
                RequireReference(_scene).DetachEffectEntry(
                    _effectEntry,
                    false);
                _effectEntry.reset();
            }
            return false;
        }

        const std::int32_t sfx = GetListItem(
            _sfxIds,
            static_cast<std::int32_t>(_itemType));
        if (sfx != -1)
        {
            _soundSource.PlaySfx(sfx, true);
        }

        if (_owner == nullptr
            && GameState::SinglePlayer())
        {
            auto&& conditionMainValue = PlayerEntity::Main();
            PlayerEntity& conditionMain = RequireReference(conditionMainValue);
            auto&& conditionEquipValue = conditionMain.EquipInfo();
            EquipInfo& conditionEquip = RequireReference(conditionEquipValue);
            auto&& conditionWeaponValue = conditionEquip.Weapon;
            if (conditionWeaponValue != nullptr)
            {
                auto&& mainValue = PlayerEntity::Main();
                PlayerEntity& main = RequireReference(mainValue);
                auto&& equipValue = main.EquipInfo();
                EquipInfo& equip = RequireReference(equipValue);

                const auto chargeLevel = equip.ChargeLevel;
                auto&& weaponValue = equip.Weapon;
                WeaponInfo& weapon = RequireReference(weaponValue);
                if (chargeLevel >= weapon.MinCharge * 2)
                {
                    auto&& positionMainValue = PlayerEntity::Main();
                    PlayerEntity& positionMain = RequireReference(positionMainValue);
                    const Vector3 between
                        = static_cast<Vector3>(positionMain.Position)
                        - static_cast<Vector3>(Position);
                    const float distSqr = LengthSquared(between);
                    if (distSqr > 0.0F && distSqr < 20.0F * 20.0F)
                    {
                        const float distance = std::sqrt(distSqr);
                        const float div = distance / 20.0F;
                        const float pct = (1.0F - div) / distance;
                        Position = static_cast<Vector3>(Position)
                            + ScaleVector(
                                between,
                                pct / static_cast<float>(4 * 2));
                    }
                }
            }
        }

        return SpinningEntityBase::Process();
    }

    void ItemInstanceEntity::OnPickedUp(PlayerEntity* picker)
    {
        _despawnTimer = 0;
        if (_owner != nullptr)
        {
            _owner->OnItemPickedUp(picker);
        }
        if (GameState::SinglePlayer())
        {
            const std::int32_t scanId = GetScanId();
            auto storySave = GameState::StorySave;
            RequireReference(storySave).UpdateLogbook(scanId);
        }
    }

    void ItemInstanceEntity::GetDrawInfo()
    {
        // Instagib: the map has nothing to pick up, so it shows nothing.
        if (IsVisible(NodeRef) && !(GameState::Instagib() && GameState::Multiplayer()))
        {
            SpinningEntityBase::GetDrawInfo();
        }
    }

    void ItemInstanceEntity::Destroy()
    {
        if (_effectEntry != nullptr)
        {
            RequireReference(_scene).UnlinkEffectEntry(_effectEntry);
        }
        _soundSource.StopAllSfx(true);
        SpinningEntityBase::Destroy();
    }

    FhItemInstanceEntityData& FhItemInstanceEntityData::operator=(
        const FhItemInstanceEntityData& other) noexcept
    {
        if (this != &other)
        {
            this->~FhItemInstanceEntityData();
            ::new (this) FhItemInstanceEntityData(other);
        }
        return *this;
    }

    FhItemEntity::FhItemEntity(
        FhItemInstanceEntityData data,
        Scene* scene)
        : SpinningEntityBase(
              0.35F,
              UnitY,
              0,
              0,
              EntityType::FhItemInstance,
              scene)
    {
        Vector3 position = data.Position;
        position.Y += 0.5F;
        Position = position;
        SetUpModel(
            GetListItem(
                Metadata::FhItems,
                static_cast<std::int32_t>(data.ItemType)),
            0,
            AnimFlags::None,
            true);
    }
}
