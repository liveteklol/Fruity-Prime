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
        // Each image at the size it was painted at (NativeSize), each side a
        // power of two up to NativeMaxSize, as Quake loads it: a level is
        // drawn with its own art, not a blur of it.
        static constexpr std::int32_t NativeSize = 0;
        static constexpr std::int32_t NativeMaxSize = 512;
        static constexpr std::int32_t DefaultSize = NativeSize;
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
            // "shader <- ROOM/material", for each shader painted with the
            // cartridge's art because the level carries no image for it.
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
