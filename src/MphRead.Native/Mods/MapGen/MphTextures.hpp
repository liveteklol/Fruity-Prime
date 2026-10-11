#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::MapGen
{
    // The cartridge's own art, for a surface of a Quake 3 level whose image
    // is nowhere to be found: a wall is painted with a wall of the game's
    // rooms, lava with its lava, the sky with its space. Which one is decided
    // by the shader's name and surface flags alone, so the same level always
    // comes out the same.
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

        MphTextures() = delete;
        MphTextures(const MphTextures&) = delete;
        MphTextures& operator=(const MphTextures&) = delete;
    };
}
