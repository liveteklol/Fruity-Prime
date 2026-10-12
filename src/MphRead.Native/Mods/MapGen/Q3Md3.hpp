#pragma once

#include "../../Formats/Types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace MphRead::Mods::MapGen
{
    class Q3Bsp;

    // The models Quake 3 draws on a mover at run time (an entity's model2,
    // an MD3: q3dm19's jets, q3dm0's turning statues), read from the level's
    // archives and placed as Quake places them: about the entity's origin,
    // moving with it (Q3Movers).
    class Q3Md3 final
    {
    public:
        struct Surface
        {
            // Its shader, without the image's extension, as Quake names it.
            std::string Shader;
            // Its triangles in Quake's space and units, already where the
            // mover's brush model is (about the origin for one stored so).
            std::vector<std::array<OpenTK::Mathematics::Vector3, 3>> Triangles;
            std::vector<std::array<OpenTK::Mathematics::Vector2, 3>> Texcoords;
        };

        struct Placed
        {
            // The mover's brush model it is drawn with.
            std::int32_t Model = 0;
            std::vector<Surface> Surfaces;
        };

        // Every model2 on a mover that the archives hold, first frame.
        [[nodiscard]] static std::vector<Placed> Load(
            const Q3Bsp* bsp, const std::vector<std::string>& archives);

        // The shaders they use, each once, in the order met: the bake gives
        // them entries from ModelSourceIndex up, in this order.
        [[nodiscard]] static std::vector<std::string> Shaders(const std::vector<Placed>& placed);

        Q3Md3() = delete;
    };
}
