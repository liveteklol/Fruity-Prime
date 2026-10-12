#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace MphRead::Mods::MapGen
{
    class MapTexturePack final
    {
    public:
        class Entry final
        {
        public:
            // How its pixels are stored: one byte each, a palette index
            // (Palette8), or an index and an alpha (A5I3: 8 colours, 32
            // levels of see-through; A3I5: 32 colours, 8 levels).
            enum class PixelFormat : std::uint8_t
            {
                Palette8 = 0,
                A5I3 = 1,
                A3I5 = 2,
            };

            explicit Entry(
                std::int32_t sourceIndex = 0,
                std::string name = {},
                std::uint16_t width = 0,
                std::uint16_t height = 0,
                std::vector<std::uint16_t> palette = {},
                std::vector<std::uint8_t> pixels = {},
                PixelFormat format = PixelFormat::Palette8,
                std::string meta = {});

            [[nodiscard]] std::int32_t SourceIndex() const noexcept;
            [[nodiscard]] const std::string& Name() const noexcept;
            [[nodiscard]] std::uint16_t Width() const noexcept;
            [[nodiscard]] std::uint16_t Height() const noexcept;
            [[nodiscard]] const std::vector<std::uint16_t>& Palette() const noexcept;
            [[nodiscard]] const std::vector<std::uint8_t>& Pixels() const noexcept;
            [[nodiscard]] PixelFormat Format() const noexcept;
            // ShaderAnim::Encode's text: what the entry is to its Quake
            // texture and how it moves; empty for a plain image.
            [[nodiscard]] const std::string& Meta() const noexcept;

        private:
            std::int32_t _sourceIndex = 0;
            std::string _name{};
            std::uint16_t _width = 0;
            std::uint16_t _height = 0;
            std::vector<std::uint16_t> _palette{};
            std::vector<std::uint8_t> _pixels{};
            PixelFormat _format = PixelFormat::Palette8;
            std::string _meta{};
        };

        // Minimal insertion-ordered adapter for IReadOnlyDictionary<int, int>.
        // It intentionally exposes no mutating operation.
        class SourceIndexDictionary final
        {
        public:
            using Value = std::pair<std::int32_t, std::int32_t>;
            using const_iterator = std::vector<Value>::const_iterator;

            [[nodiscard]] std::int32_t Count() const noexcept;
            [[nodiscard]] bool TryGetValue(std::int32_t key, std::int32_t& value) const noexcept;
            [[nodiscard]] const std::int32_t& operator[](std::int32_t key) const;
            [[nodiscard]] const_iterator begin() const noexcept;
            [[nodiscard]] const_iterator end() const noexcept;

        private:
            friend class MapTexturePack;

            void Set(std::int32_t key, std::int32_t value);

            std::vector<Value> _values{};
        };

        [[nodiscard]] const std::vector<Entry>& Entries() const noexcept;
        [[nodiscard]] const SourceIndexDictionary& BySourceIndex() const noexcept;

        static MapTexturePack Load(const std::vector<std::uint8_t>& bytes, const std::string& name);
        static MapTexturePack Load(const std::string& path);

        MapTexturePack(const MapTexturePack&) = delete;
        MapTexturePack& operator=(const MapTexturePack&) = delete;
        MapTexturePack(MapTexturePack&&) = delete;
        MapTexturePack& operator=(MapTexturePack&&) = delete;

    private:
        explicit MapTexturePack(std::vector<Entry> entries);
        static MapTexturePack Load(std::istream& stream, const std::string& path);

        std::vector<Entry> _entries;
        SourceIndexDictionary _bySourceIndex{};
    };
}
