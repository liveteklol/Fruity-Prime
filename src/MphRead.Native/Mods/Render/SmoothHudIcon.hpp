#pragma once

#include "../../HUD/HudInfo.hpp"

#include <cstdint>
#include <memory>

namespace MphRead
{
    class Scene;
}

namespace MphRead::Mods::Render
{
    class SmoothHudIcon final
    {
    public:
        SmoothHudIcon() = delete;
        SmoothHudIcon(const SmoothHudIcon&) = delete;
        SmoothHudIcon(SmoothHudIcon&&) = delete;
        SmoothHudIcon& operator=(const SmoothHudIcon&) = delete;
        SmoothHudIcon& operator=(SmoothHudIcon&&) = delete;

        static constexpr std::int32_t Factor = 8;

        [[nodiscard]] static std::shared_ptr<Hud::HudObjectInstance> Create(
            const std::shared_ptr<Hud::HudObject>& sheet);
        static void Tint(const std::shared_ptr<Hud::HudObjectInstance>& inst,
            Hud::ReadOnlyList<std::uint8_t> data, std::int32_t frame,
            ColorRgba color, Scene& scene);

    private:
        static void Build(Hud::HudObjectInstance& inst,
            const Hud::ReadOnlyList<std::uint8_t>& data, std::int32_t frame,
            ColorRgba color, Scene& scene);
        // The pixel's palette brightness (0 for transparent), 0-255.
        [[nodiscard]] static float Shade(const Hud::HudObjectInstance& inst,
            const Hud::ReadOnlyList<std::uint8_t>& data, std::int32_t image,
            std::int32_t tilesX, std::int32_t width, std::int32_t height,
            std::int32_t x, std::int32_t y);
        [[nodiscard]] static std::int32_t Index(const Hud::ReadOnlyList<std::uint8_t>& data,
            std::int32_t image, std::int32_t tilesX, std::int32_t width,
            std::int32_t height, std::int32_t x, std::int32_t y);
    };
}
