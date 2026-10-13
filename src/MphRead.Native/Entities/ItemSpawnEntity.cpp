#include "ItemSpawnEntity.hpp"
#include "Players/PlayerEntity.hpp"
#include "../Mods/Multiplayer/MapResourceRules.hpp"
#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"
#include "Mods/Network/Gameplay/Pickups/NetPowerUpPrediction.hpp"
#include "Mods/Network/Session/NetSession.hpp"

#include "../GameState.hpp"
#include "../MemoryArrays.hpp"
#include "../Messaging.hpp"
#include "../Scene.hpp"
#include "../Sound/Sfx.hpp"
#include "../Utility/Rng.hpp"
#include "EnemySpawnEntity.hpp"
#include "ItemInstanceEntity.hpp"
#include "Players/PlayerEntity.hpp"
#include "../NativeRuntime/System/Managed.hpp"
#include "../Formats/Types.hpp"

#include <any>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

using ::MphRead::NativeRuntime::RequireReference;
using ::OpenTK::Mathematics::Determinant;
using ::OpenTK::Mathematics::Inverted;

namespace
{
    using OpenTK::Mathematics::Matrix4;
    using OpenTK::Mathematics::Vector3;

}

namespace MphRead::Entities
{
    ItemSpawnEntity::ItemSpawnEntity(
        ItemSpawnEntityData data,
        std::string nodeName,
        Scene* scene)
        : EntityBase(EntityType::ItemSpawn, std::move(nodeName), scene),
          _data(data)
    {
        Id = data.Header.EntityId;
        Position = data.Header.Position.ToFloatVector();
        _alwaysActive = data.AlwaysActive != 0;
        if (GameState::Mode() == GameMode::SinglePlayer)
        {
            std::shared_ptr<StorySave> storySave = GameState::StorySave;
            const std::int32_t roomId = RequireReference(_scene).RoomId();
            if (storySave == nullptr)
            {
                throw System::NullReferenceException();
            }
            const std::int32_t state = storySave->InitRoomState(
                roomId,
                Id,
                data.Enabled != 0);
            if (_alwaysActive)
            {
                Active = data.Enabled != 0;
            }
            else
            {
                Active = state != 0;
            }
        }
        else
        {
            Active = data.Enabled != 0;
        }
        _spawnCooldown = static_cast<std::uint16_t>(
            static_cast<std::int32_t>(data.SpawnDelay) * 2);
        if (data.HasBase != 0)
        {
            SetUpModel("items_base");
        }
        else
        {
            AddPlaceholderModel();
        }
    }

    ItemSpawnEntityData ItemSpawnEntity::Data() const
    {
        return _data;
    }

    bool ItemSpawnEntity::AlwaysActive() const noexcept
    {
        return _alwaysActive;
    }

    void ItemSpawnEntity::SetAlwaysActive(bool value) noexcept
    {
        _alwaysActive = value;
    }

    std::shared_ptr<ItemInstanceEntity> ItemSpawnEntity::Item() const noexcept
    {
        return _item;
    }

    void ItemSpawnEntity::SetItem(std::shared_ptr<ItemInstanceEntity> value) noexcept
    {
        _item = std::move(value);
    }

    std::shared_ptr<Formats::NodeData3> ItemSpawnEntity::ClosestNode() const noexcept
    {
        return _closestNode;
    }

    void ItemSpawnEntity::SetClosestNode(
        std::shared_ptr<Formats::NodeData3> value) noexcept
    {
        _closestNode = std::move(value);
    }

    std::optional<OpenTK::Mathematics::Vector4> ItemSpawnEntity::OverrideColor() const
    {
        return _overrideColor;
    }

    void ItemSpawnEntity::Initialize()
    {
        EntityBase::Initialize();
        Mods::Network::NetItemState::Register(*this);
        (void)RequireReference(_scene).TryGetEntity(
            _data.NotifyEntityId,
            _pickupNotifyEntity);
    }

    Mods::Network::ItemSpawnState ItemSpawnEntity::ModItemState() const
    {
        return Mods::Network::ItemSpawnState{
            _item != nullptr && _item->DespawnTimer() != 0, Active, _spawnCooldown, _spawnCount, _lastPickerSlot};
    }

    bool ItemSpawnEntity::Process()
    {
        if (!_linkDone && _data.ParentId != -1)
        {
            std::shared_ptr<EntityBase> parent{};
            if (RequireReference(_scene).TryGetEntity(_data.ParentId, parent))
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
        if (Mods::Network::NetItemState::Mirrors(*this))
        {
            Mods::Network::ItemSpawnState state{};
            if (Mods::Network::NetItemState::TryGet(static_cast<std::int16_t>(Id), state))
            {
                Active = state.Active;
                _spawnCooldown = state.Cooldown;
                _spawnCount = state.SpawnCount;
                if (!state.Available && _item != nullptr)
                {
                    const std::int32_t localSlot = Mods::Network::NetSession::LocalSlot();
                    if (_item->DespawnTimer() != 0 && state.PickerSlot == localSlot
                        && localSlot >= 0 && localSlot < static_cast<std::int32_t>(PlayerEntity::Players().size()))
                    {
                        RequireReference(PlayerEntity::Players()[static_cast<std::size_t>(localSlot)])
                            .PlayHealthPickupSfx(_item->ItemType());
                    }
                    _item->SetDespawnTimer(0);
                }
                else if (state.Available && _item == nullptr
                    && !Mods::Network::NetPowerUpPrediction::Holds(static_cast<std::int16_t>(Id), state.SpawnCount))
                {
                    _item = SpawnItem(_data.ItemType, TypeExtensions::AddY(Position, 0.65F), NodeRef, _scene);
                    if (_item != nullptr)
                    {
                        _item->SetOwner(this);
                        _item->SetParentId(_data.ParentId);
                    }
                }
            }
            return EntityBase::Process();
        }
        if (!Active)
        {
            return true;
        }
        if (_item == nullptr && _spawnCooldown > 0)
        {
            --_spawnCooldown;
        }
        if (_item == nullptr
            && _spawnCooldown == 0
            && (_data.MaxSpawnCount == 0 || _spawnCount < _data.MaxSpawnCount))
        {
            _item = SpawnItem(
                _data.ItemType,
                TypeExtensions::AddY(Position, 0.65F),
                NodeRef,
                _scene);
            if (_item != nullptr)
            {
                _spawnCooldown = static_cast<std::uint16_t>(
                    static_cast<std::int32_t>(_data.SpawnInterval) * 2);
                ++_spawnCount;
                Mods::Network::NetItemState::NoteSpawned(*this);
                if (Mods::Network::NetItemState::ServerOwned(_data.ItemType))
                {
                    _lastPickerSlot = -1;
                }
                _item->SetOwner(this);
                _item->SetParentId(_data.ParentId);
                if (_data.ItemType != ItemType::ArtifactKey)
                {
                    _soundSource.Update(Position, 7);
                    UpdateNodeRefVolume();
                    _soundSource.PlaySfx(SfxId::ITEM_SPAWN1);
                }
                else if (_playKeySfx)
                {
                    (void)_soundSource.PlayFreeSfx(SfxId::KEY_APPEAR);
                }
                _playKeySfx = false;
            }
        }
        return EntityBase::Process();
    }

    void ItemSpawnEntity::OnItemPickedUp(PlayerEntity* picker)
    {
        if (Mods::Network::NetItemState::ServerOwned(_data.ItemType))
        {
            _lastPickerSlot = picker == nullptr ? static_cast<std::int8_t>(-1)
                : static_cast<std::int8_t>(picker->SlotIndex());
        }
        if (_data.CollectedMessage != Message::None)
        {
            Scene* scene = _scene;
            MessageObject param1 = BoxInt32(_data.CollectedMsgParam1);
            MessageObject param2 = BoxInt32(_data.CollectedMsgParam2);
            RequireReference(scene).SendMessage(
                _data.CollectedMessage,
                this,
                _pickupNotifyEntity.get(),
                std::move(param1),
                std::move(param2));
        }
    }

    void ItemSpawnEntity::HandleMessage(MessageInfo info)
    {
        if (info.Message == Message::Activate
            || (info.Message == Message::SetActive
                && UnboxInt32(info.Param1) != 0))
        {
            Active = true;
            _playKeySfx = true;
            if (GameState::Mode() == GameMode::SinglePlayer)
            {
                std::shared_ptr<StorySave> storySave = GameState::StorySave;
                const std::int32_t roomId = RequireReference(_scene).RoomId();
                if (storySave == nullptr)
                {
                    throw System::NullReferenceException();
                }
                storySave->SetRoomState(roomId, Id, 3);
            }
        }
        else if (info.Message == Message::SetActive
            && UnboxInt32(info.Param1) == 0)
        {
            Active = false;
            if (GameState::Mode() == GameMode::SinglePlayer)
            {
                std::shared_ptr<StorySave> storySave = GameState::StorySave;
                const std::int32_t roomId = RequireReference(_scene).RoomId();
                if (storySave == nullptr)
                {
                    throw System::NullReferenceException();
                }
                storySave->SetRoomState(roomId, Id, 1);
            }
            if (_item != nullptr)
            {
                _item->SetDespawnTimer(0);
                --_spawnCount;
            }
        }
        else if (info.Message == Message::MoveItemSpawner
            && info.Sender != nullptr)
        {
            if (info.Sender->Type == EntityType::EnemySpawn)
            {
                EnemySpawnEntity* enemySpawn
                    = dynamic_cast<EnemySpawnEntity*>(info.Sender);
                if (enemySpawn == nullptr)
                {
                    throw SceneDetail::InvalidCastException();
                }
                if (enemySpawn->Data.EnemyType == EnemyType::Hunter)
                {
                    auto enumerator = RequireReference(_scene).GetPlayerEntities().GetEnumerator();
                    while (enumerator.MoveNext())
                    {
                        std::shared_ptr<PlayerEntity> player = enumerator.Current();
                        PlayerEntity& playerRef = RequireReference(player);
                        if (playerRef.EnemySpawner().get() == info.Sender)
                        {
                            Vector3 position{};
                            playerRef.GetPosition(position);
                            Position = position;
                            break;
                        }
                    }
                }
                else
                {
                    Vector3 position{};
                    info.Sender->GetPosition(position);
                    Position = position;
                }
            }
            else
            {
                Vector3 position{};
                info.Sender->GetPosition(position);
                Position = position;
            }
            if (_item != nullptr)
            {
                _item->Position = TypeExtensions::AddY(Position, 1.0F);
            }
        }
    }

    void ItemSpawnEntity::GetDrawInfo()
    {
        if (IsVisible(NodeRef))
        {
            EntityBase::GetDrawInfo();
        }
    }

    std::shared_ptr<ItemInstanceEntity> ItemSpawnEntity::SpawnItemDrop(
        ItemType type,
        Vector3 position,
        Formats::Culling::NodeRef nodeRef,
        std::uint32_t chance,
        Scene* scene)
    {
        return SpawnItem(
            type,
            position,
            nodeRef,
            scene,
            chance,
            450 * 2);
    }

    std::shared_ptr<ItemInstanceEntity> ItemSpawnEntity::SpawnItem(
        ItemType type,
        Vector3 position,
        Formats::Culling::NodeRef nodeRef,
        std::int32_t despawnTime,
        Scene* scene)
    {
        return SpawnItem(
            type,
            position,
            nodeRef,
            scene,
            std::nullopt,
            despawnTime);
    }

    std::shared_ptr<ItemInstanceEntity> ItemSpawnEntity::SpawnItem(
        ItemType type,
        Vector3 position,
        Formats::Culling::NodeRef nodeRef,
        Scene* scene,
        std::optional<std::uint32_t> chance,
        std::int32_t despawnTime)
    {
        std::shared_ptr<ItemInstanceEntity> item{};
        if (type != ItemType::None
            && (!chance.has_value()
                || Rng::GetRandomInt2(100) < chance.value()))
        {
            item = std::make_shared<ItemInstanceEntity>(
                ItemInstanceEntityData(position, type, despawnTime),
                nodeRef,
                scene);
            RequireReference(scene).AddEntity(item);
        }
        return item;
    }

    FhItemSpawnEntity::FhItemSpawnEntity(
        FhItemSpawnEntityData data,
        Scene* scene)
        : EntityBase(EntityType::FhItemSpawn, scene),
          _data(data)
    {
        Id = data.Header.EntityId;
        SetTransform(
            data.Header.FacingVector,
            data.Header.UpVector,
            data.Header.Position);
        AddPlaceholderModel();
    }

    std::optional<OpenTK::Mathematics::Vector4> FhItemSpawnEntity::OverrideColor() const
    {
        return _overrideColor;
    }

    bool FhItemSpawnEntity::Process()
    {
        if (_spawn)
        {
            std::shared_ptr<FhItemEntity> item = SpawnItem(
                Position,
                _data.ItemType,
                _scene);
            RequireReference(_scene).AddEntity(item);
            _spawn = false;
        }
        return EntityBase::Process();
    }

    std::shared_ptr<FhItemEntity> FhItemSpawnEntity::SpawnItem(
        Vector3 position,
        FhItemType itemType,
        Scene* scene)
    {
        return std::make_shared<FhItemEntity>(
            FhItemInstanceEntityData(position, itemType),
            scene);
    }
}
