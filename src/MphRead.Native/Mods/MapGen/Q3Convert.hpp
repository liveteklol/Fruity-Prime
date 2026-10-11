#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MphRead::Mods::MapGen
{
    class MapDefinition;
    class Q3Bsp;

    class Q3Convert final
    {
    public:
        static constexpr float TargetExtent = 130.0F;

        [[nodiscard]] static std::int32_t Run(
            const std::string& source,
            const std::optional<std::string>& mapName,
            const std::optional<std::string>& roomName,
            const std::optional<std::string>& outputDir,
            bool dropClip,
            bool dropItems,
            const std::optional<float>& forcedScale,
            std::int32_t textureSize,
            const std::vector<std::string>& texturePaks = {});

        [[nodiscard]] static float AutoScale(float widestExtent);
        [[nodiscard]] static float WidestExtent(Q3Bsp* bsp);

        Q3Convert() = delete;
        Q3Convert(const Q3Convert&) = delete;
        Q3Convert& operator=(const Q3Convert&) = delete;

    private:
        static void Bounds(
            Q3Bsp* bsp,
            std::shared_ptr<std::vector<float>>& min,
            std::shared_ptr<std::vector<float>>& max,
            bool sky);

        [[nodiscard]] static std::int32_t ScaleFactor(
            const std::vector<float>* min,
            const std::vector<float>* max,
            float unit);

        static void AddItems(MapDefinition* definition, Q3Bsp* bsp, float unit, bool dropItems);

        static void AddSpawns(
            MapDefinition* definition,
            Q3Bsp* bsp,
            float unit);

        [[nodiscard]] static float Round(float value) noexcept;

        [[nodiscard]] static std::shared_ptr<std::vector<float>> ParseVector(
            const std::string& value);
    };
}
