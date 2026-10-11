#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::MapGen
{
    // The cartridge's own art, for a surface of a Quake 3 level whose image
    // is nowhere to be found. A shader Quake 3 and its community levels use
    // is painted with the cartridge texture that looks most like it, chosen
    // by comparing the two images (tools/q3-texture-match.py, which writes
    // Q3TextureMatches.inc); any other by its name and surface flags -- a
    // wall with a wall, lava with lava, the sky with space. Either way the
    // same level always comes out the same.
    class MphTextures final
    {
    public:
        class Image final
        {
        public:
            std::int32_t Width = 0;
            std::int32_t Height = 0;
            // RGB, row by row.
            std::vector<std::uint8_t> Pixels;
            // "ROOM/material", for the log.
            std::string Source;
        };

        // shader: the level's shader name; flags and contents: its
        // surfaceparm bits from the .bsp.
        [[nodiscard]] static std::optional<Image> StandIn(
            const std::string& shader, std::int32_t flags, std::int32_t contents);

        // -mphtexdump DIR: every texture of the cartridge's rooms as PNG,
        // with index.tsv, for tools/q3-texture-match.py.
        [[nodiscard]] static std::int32_t Dump(const std::string& directory);

        MphTextures() = delete;
        MphTextures(const MphTextures&) = delete;
        MphTextures& operator=(const MphTextures&) = delete;
    };
}
