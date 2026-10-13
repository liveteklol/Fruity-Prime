#pragma once

#include "Mods/Network/Gameplay/Pickups/NetItemState.hpp"

#include "../Formats/Entity.hpp"
#include "EntityBase.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace MphRead::Formats
{
    class NodeData3;
}

namespace MphRead::Entities
{
    class FhItemEntity;
    class ItemInstanceEntity;
    class PlayerEntity;

    class ItemSpawnEntity : public EntityBase
    {
    public:
        // C# declares `new bool Active`, so this intentionally hides EntityBase::Active.
        bool Active = false;

        ItemSpawnEntity(ItemSpawnEntityData data, std::string nodeName, Scene* scene);

        ItemSpawnEntity(const ItemSpawnEntity&) = delete;
        ItemSpawnEntity& operator=(const ItemSpawnEntity&) = delete;
        ItemSpawnEntity(ItemSpawnEntity&&) = delete;
        ItemSpawnEntity& operator=(ItemSpawnEntity&&) = delete;

        [[nodiscard]] ItemSpawnEntityData Data() const;
        [[nodiscard]] bool AlwaysActive() const noexcept;
        void SetAlwaysActive(bool value) noexcept;
        [[nodiscard]] std::shared_ptr<ItemInstanceEntity> Item() const noexcept;
        void SetItem(std::shared_ptr<ItemInstanceEntity> value) noexcept;
        [[nodiscard]] std::shared_ptr<Formats::NodeData3> ClosestNode() const noexcept;
        void SetClosestNode(std::shared_ptr<Formats::NodeData3> value) noexcept;

        void Initialize() override;
        [[nodiscard]] bool Process() override;
        [[nodiscard]] Mods::Network::ItemSpawnState ModItemState() const;
        // How many items this spawner has put out: which spawn an item is.
        [[nodiscard]] std::uint16_t SpawnCount() const noexcept { return _spawnCount; }
        // Who took the last item an authority-owned spawner put out; -1: nobody.
        [[nodiscard]] std::int8_t LastPickerSlot() const noexcept { return _lastPickerSlot; }
        void OnItemPickedUp(PlayerEntity* picker = nullptr);
        void HandleMessage(MessageInfo info) override;
        void GetDrawInfo() override;

        [[nodiscard]] static std::shared_ptr<ItemInstanceEntity> SpawnItemDrop(
            ItemType type,
            OpenTK::Mathematics::Vector3 position,
            Formats::Culling::NodeRef nodeRef,
            std::uint32_t chance,
            Scene* scene);
        [[nodiscard]] static std::shared_ptr<ItemInstanceEntity> SpawnItem(
            ItemType type,
            OpenTK::Mathematics::Vector3 position,
            Formats::Culling::NodeRef nodeRef,
            std::int32_t despawnTime,
            Scene* scene);

    protected:
        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector4> OverrideColor() const override;

    private:
        [[nodiscard]] static std::shared_ptr<ItemInstanceEntity> SpawnItem(
            ItemType type,
            OpenTK::Mathematics::Vector3 position,
            Formats::Culling::NodeRef nodeRef,
            Scene* scene,
            std::optional<std::uint32_t> chance = std::nullopt,
            std::int32_t despawnTime = 0);

        const ItemSpawnEntityData _data;
        bool _playKeySfx = false;
        std::uint16_t _spawnCount = 0;
        std::uint16_t _spawnCooldown = 0;
        std::int8_t _lastPickerSlot = -1;
        bool _linkDone = false;
        std::shared_ptr<EntityBase> _parent{};
        OpenTK::Mathematics::Vector3 _invPos{};
        std::shared_ptr<EntityBase> _pickupNotifyEntity{};

        bool _alwaysActive = false;
        std::shared_ptr<ItemInstanceEntity> _item{};
        std::shared_ptr<Formats::NodeData3> _closestNode{};
        const std::optional<OpenTK::Mathematics::Vector4> _overrideColor
            = ColorRgb(0xC8, 0x00, 0xC8).AsVector4();
    };

    class FhItemSpawnEntity : public EntityBase
    {
    public:
        FhItemSpawnEntity(FhItemSpawnEntityData data, Scene* scene);

        FhItemSpawnEntity(const FhItemSpawnEntity&) = delete;
        FhItemSpawnEntity& operator=(const FhItemSpawnEntity&) = delete;
        FhItemSpawnEntity(FhItemSpawnEntity&&) = delete;
        FhItemSpawnEntity& operator=(FhItemSpawnEntity&&) = delete;

        [[nodiscard]] bool Process() override;

        [[nodiscard]] static std::shared_ptr<FhItemEntity> SpawnItem(
            OpenTK::Mathematics::Vector3 position,
            FhItemType itemType,
            Scene* scene);

    protected:
        [[nodiscard]] std::optional<OpenTK::Mathematics::Vector4> OverrideColor() const override;

    private:
        const FhItemSpawnEntityData _data;
        bool _spawn = true;
        const std::optional<OpenTK::Mathematics::Vector4> _overrideColor
            = ColorRgb(0xC8, 0x00, 0xC8).AsVector4();
    };
}
