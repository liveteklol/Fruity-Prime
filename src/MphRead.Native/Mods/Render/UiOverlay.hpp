#pragma once

#include <cstdint>

namespace MphRead
{
    class RenderWindow;
}

namespace MphRead::NativeRuntime::Rhi
{
    class Texture;
}

namespace MphRead::Mods::Render
{
    class UiOverlay final
    {
    public:
        UiOverlay() = delete;

        [[nodiscard]] static bool Visible() noexcept;
        static void Visible(bool value) noexcept;
        [[nodiscard]] static bool HasFrame() noexcept;
        static void Upload(const void* pixels, std::int32_t width, std::int32_t height);
        static void UseTexture(std::int32_t texture, std::int32_t width, std::int32_t height);
        // A scene-presented window's UI: an RHI texture the GPU UI surface drew,
        // top row at t = 0. Held, not owned.
        static void UseTexture(const ::MphRead::NativeRuntime::Rhi::Texture& texture,
            std::int32_t width, std::int32_t height);
        static void Draw(std::int32_t width, std::int32_t height);
        static void DrawAlone(::MphRead::RenderWindow& window, std::int32_t width, std::int32_t height);
        static void Release();

    private:
        static std::int32_t _width;
        static std::int32_t _height;
        static bool _hasFrame;
        static bool _visible;
        static bool _topRowAtTextureZero;
    };
}
