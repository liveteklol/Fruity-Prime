#pragma once

#include "../../Formats/Enums.hpp"
#include "../../Formats/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace MphRead
{
    class CollisionVolume;
}

namespace MphRead::Mods::MapGen
{
    class BuiltMap;
    class MapBrush;
    class MapDefinition;
    class MapJumpPad;

    class ItemTypeHashSet final
    {
    private:
        struct Slot final
        {
            ItemType Value = ItemType::None;
            bool Occupied = false;
            std::int32_t NextFree = -1;
        };

    public:
        class const_iterator final
        {
        public:
            using value_type = ItemType;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::forward_iterator_tag;

            const_iterator() noexcept = default;

            [[nodiscard]] ItemType operator*() const noexcept;
            const_iterator& operator++() noexcept;
            void operator++(int) noexcept;

            [[nodiscard]] friend bool operator==(
                const const_iterator& left, const const_iterator& right) noexcept
            {
                return left._owner == right._owner && left._index == right._index;
            }

            [[nodiscard]] friend bool operator!=(
                const const_iterator& left, const const_iterator& right) noexcept
            {
                return !(left == right);
            }

        private:
            friend class ItemTypeHashSet;

            const_iterator(const ItemTypeHashSet* owner, std::size_t index) noexcept;
            void SkipFree() noexcept;

            const ItemTypeHashSet* _owner = nullptr;
            std::size_t _index = 0;
        };

        ItemTypeHashSet() = default;
        ItemTypeHashSet(std::initializer_list<ItemType> values);
        ItemTypeHashSet(const ItemTypeHashSet&) = delete;
        ItemTypeHashSet& operator=(const ItemTypeHashSet&) = delete;
        ItemTypeHashSet(ItemTypeHashSet&&) = delete;
        ItemTypeHashSet& operator=(ItemTypeHashSet&&) = delete;

        [[nodiscard]] bool Add(ItemType value);
        [[nodiscard]] bool Remove(ItemType value) noexcept;
        [[nodiscard]] bool Contains(ItemType value) const noexcept;
        void Clear() noexcept;
        [[nodiscard]] std::int32_t Count() const noexcept;

        [[nodiscard]] const_iterator begin() const noexcept;
        [[nodiscard]] const_iterator end() const noexcept;

    private:
        std::vector<Slot> _slots{};
        std::int32_t _freeList = -1;
        std::int32_t _count = 0;
    };

    class MapBuilder final
    {
    public:
        static ItemTypeHashSet MultiplayerItems;

        [[nodiscard]] static std::shared_ptr<BuiltMap> Build(MapDefinition* def);
        static void AddEntities(BuiltMap* map, MapDefinition* def);
        [[nodiscard]] static std::pair<OpenTK::Mathematics::Vector3, float> SolveJumpPad(
            MapJumpPad* pad);
        [[nodiscard]] static OpenTK::Mathematics::Vector3 ToVector(
            const std::vector<float>* values);

        MapBuilder() = delete;
        MapBuilder(const MapBuilder&) = delete;
        MapBuilder& operator=(const MapBuilder&) = delete;

    private:
        static const std::array<float, 6> _faceShades;

        static void AddBrush(BuiltMap* map, MapDefinition* def, MapBrush* brush);
        [[nodiscard]] static OpenTK::Mathematics::Vector2 Project(
            OpenTK::Mathematics::Vector3 point,
            OpenTK::Mathematics::Vector3 normal,
            OpenTK::Mathematics::Vector3 origin,
            float texScale) noexcept;
        [[nodiscard]] static CollisionVolume MakeBox(const std::vector<float>* size);
        [[nodiscard]] static CollisionVolume MakeBox(const std::vector<float>& box);
    };
}
