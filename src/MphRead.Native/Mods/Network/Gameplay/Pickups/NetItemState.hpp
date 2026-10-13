#pragma once

#include "Mods/Network/Gameplay/Pickups/ItemAuthority.hpp"
#include "Formats/Enums.hpp"

#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace MphRead::Entities
{
    class ItemInstanceEntity;
    class ItemSpawnEntity;
    class PlayerEntity;
}

namespace MphRead::Mods::Network
{
    struct ItemSpawnState final
    {
        bool Available = false;
        bool Active = false;
        std::uint16_t Cooldown = 0;
        std::uint16_t SpawnCount = 0;
        std::int8_t PickerSlot = 0;

        [[nodiscard]] friend bool operator==(const ItemSpawnState&, const ItemSpawnState&) = default;
    };

    // The items the authority hands out -- health and the power-ups -- and
    // the state of every spawner that puts them out, as a bounded tail on the
    // normal authoritative snapshot, not a second channel. Full state repeats
    // so loss and joining late repair themselves.
    class NetItemState final
    {
    public:
        NetItemState() = delete;

        static constexpr std::int32_t MaxSpawns = 56;
        static constexpr std::int32_t HeaderSize = 3;
        static constexpr std::int32_t EntrySize = 7;
        [[nodiscard]] static constexpr std::int32_t PacketEntries(std::size_t bytes) noexcept
        {
            return bytes < HeaderSize ? 0 : static_cast<std::int32_t>((bytes - HeaderSize) / EntrySize);
        }

        [[nodiscard]] static const std::vector<std::shared_ptr<::MphRead::Entities::ItemSpawnEntity>>&
            RegisteredSpawns() noexcept;
        [[nodiscard]] static bool IsReplica();
        // Health and the power-ups: what only the authority hands out.
        [[nodiscard]] static bool ServerOwned(::MphRead::ItemType type) noexcept;
        // A replica's spawner of such an item, which shows the authority's
        // state rather than running its own.
        [[nodiscard]] static bool Mirrors(const ::MphRead::Entities::ItemSpawnEntity& spawn);
        // Who settles `player` touching `item` (ItemAuthority).
        [[nodiscard]] static PickupRoute Route(const ::MphRead::Entities::PlayerEntity& player,
            const ::MphRead::Entities::ItemInstanceEntity& item);
        [[nodiscard]] static std::shared_ptr<::MphRead::Entities::ItemSpawnEntity> Find(std::int16_t id);
        // Authority: `spawn` has just put out its item -- the frame a claim's
        // contact must come after. Called by the spawner itself, so no spawn
        // can go unrecorded.
        static void NoteSpawned(const ::MphRead::Entities::ItemSpawnEntity& spawn);
        [[nodiscard]] static std::optional<std::uint32_t> SpawnedAt(std::int16_t id, std::uint16_t spawnCount);

        static void BeginRoom();
        static void Register(::MphRead::Entities::ItemSpawnEntity& spawn);

        [[nodiscard]] static bool TryGet(std::int16_t id, ItemSpawnState& state);
        [[nodiscard]] static bool IsCurrentMatch(std::span<const std::uint8_t> src);

        [[nodiscard]] static std::int32_t Write(std::span<std::uint8_t> dest);
        [[nodiscard]] static bool Validate(std::span<const std::uint8_t> src);
        static void Receive(std::span<const std::uint8_t> src);

    private:
        inline static std::size_t _nextSpawn = 0;
        static constexpr std::int32_t PickerShift = 2;
        static constexpr std::int32_t PickerMask = 0xF;
        static constexpr std::int32_t ReservedMask = 0xC0;

        static std::vector<std::shared_ptr<::MphRead::Entities::ItemSpawnEntity>> _spawns;
        static std::map<std::int16_t, ItemSpawnState> _states;

        struct Appearance
        {
            std::uint16_t SpawnCount = 0;
            std::uint32_t Frame = 0;
        };
        static std::map<std::int16_t, Appearance> _appeared;
    };
}
