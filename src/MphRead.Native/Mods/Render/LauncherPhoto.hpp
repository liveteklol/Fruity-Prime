#pragma once

#include <cstdint>

namespace MphRead::Mods::Render
{
    class LauncherPhoto final
    {
    public:
        LauncherPhoto() = delete;

        static void Enabled(bool value) noexcept;
        [[nodiscard]] static bool Enabled() noexcept;
        static void Draw(std::int32_t width, std::int32_t height);
        // Before the window goes for a renderer switch: the photograph's
        // texture (the scene device's, under Vulkan) and the GL names.
        static void Release() noexcept;
    };
}
