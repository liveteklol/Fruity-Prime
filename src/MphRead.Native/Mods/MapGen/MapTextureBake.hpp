#pragma once

#include <cstdint>
#include <unordered_map>
#include <array>
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
        // A portal's view, entry PortalSourceIndex + portal.
        static constexpr std::int32_t PortalSourceIndex = 0xFE00;
        // What a moving shader adds to its base image -- the layers laid
        // over it, an animMap's frames -- from AnimatedSourceIndex up, each
        // saying what it is (MapTexturePack::Entry::Meta).
        static constexpr std::int32_t AnimatedSourceIndex = 0x8000;
        // A shader of a model drawn on a mover (Q3Md3), ModelSourceIndex +
        // its place in Q3Md3::Shaders.
        static constexpr std::int32_t ModelSourceIndex = 0xF000;

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

        // The fog shaders the archives' scripts define, lower-case name to
        // red, green, blue and the depth the fog is opaque at.
        [[nodiscard]] static std::unordered_map<std::string, std::array<float, 4>> FogParms(
            const std::vector<std::string>& archivePaths);

        [[nodiscard]] static std::shared_ptr<Result> Bake(
            const std::shared_ptr<Q3Bsp>& bsp,
            const std::shared_ptr<const std::vector<std::optional<std::string>>>& archivePaths,
            const std::optional<std::string>& outputPath,
            std::int32_t size = DefaultSize,
            bool sky = true,
            const std::vector<std::pair<std::string, std::string>>* aliases = nullptr,
            const std::vector<std::string>* portalImages = nullptr,
            const std::vector<std::string>* modelShaders = nullptr);

        MapTextureBake() = delete;
        MapTextureBake(const MapTextureBake&) = delete;
        MapTextureBake& operator=(const MapTextureBake&) = delete;
    };
}
