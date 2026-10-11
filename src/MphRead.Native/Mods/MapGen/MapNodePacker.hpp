#pragma once

#include <cstdint>
#include <tuple>
#include <vector>

namespace MphRead::Mods::MapGen
{
    class BuiltFace;
    struct BotLink;

    class MapNodePacker final
    {
    public:
        [[nodiscard]] static std::tuple<std::vector<std::uint8_t>, std::int32_t, std::int32_t> Pack(
            const std::vector<BuiltFace*>* solid,
            const std::vector<BotLink>* links = nullptr);

        MapNodePacker() = delete;
        MapNodePacker(const MapNodePacker&) = delete;
        MapNodePacker& operator=(const MapNodePacker&) = delete;
    };
}
