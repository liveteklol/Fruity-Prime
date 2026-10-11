#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace MphRead::Mods::MapGen
{
    class Q3Bsp;

    class MapTextureBake final
    {
    public:
        // 128 rather than the 64 this started at: a Quake texture is
        // painted at 256 and up, and at 64 a converted level reads as a
        // blur of its own art.
        static constexpr std::int32_t DefaultSize = 128;
        // The six sides of a level's sky box are baked as entries of their
        // own, SourceIndex SkyBoxSourceIndex + side (rt lf bk ft up dn, the
        // axes +X -X +Y -Y +Z -Z in Quake's space), at the size they were
        // painted at up to SkyBoxMaxSize. The pack stores indices in 16
        // bits, and no level has this many shaders.
        static constexpr std::int32_t SkyBoxSourceIndex = 0xFF00;
        static constexpr std::int32_t SkyBoxMaxSize = 512;

        class Result final
        {
        public:
            std::int32_t Baked = 0;
            std::shared_ptr<const std::vector<std::string>> Missing;
            // "shader <- image", for each shader painted with a neighbour's
            // image because nothing answered its own name.
            std::shared_ptr<const std::vector<std::string>> Substituted;
            std::int64_t Bytes = 0;

            Result();
            Result(const Result&) = delete;
            Result& operator=(const Result&) = delete;
            Result(Result&&) = delete;
            Result& operator=(Result&&) = delete;
        };

        [[nodiscard]] static std::shared_ptr<Result> Bake(
            const std::shared_ptr<Q3Bsp>& bsp,
            const std::shared_ptr<const std::vector<std::optional<std::string>>>& archivePaths,
            const std::optional<std::string>& outputPath,
            std::int32_t size = DefaultSize,
            bool sky = true,
            const std::vector<std::pair<std::string, std::string>>* aliases = nullptr);

        MapTextureBake() = delete;
        MapTextureBake(const MapTextureBake&) = delete;
        MapTextureBake& operator=(const MapTextureBake&) = delete;
    };
}
