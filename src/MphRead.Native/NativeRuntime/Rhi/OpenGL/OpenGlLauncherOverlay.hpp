#pragma once

#include <cstdint>
#include <memory>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    // The launcher overlay on an OpenGL window: the UI raster uploaded to a
    // texture (or a texture Skia drew, adopted) and composited over the
    // frame. The backend half of Mods::Render::UiOverlay, which holds no GL.
    class OpenGlWindowDraw;
    class OpenGlLauncherOverlay final
    {
    public:
        OpenGlLauncherOverlay() = delete;

        static void Upload(const void* pixels, std::int32_t width, std::int32_t height);
        static void Adopt(std::int32_t texture);
        static void Draw(std::int32_t width, std::int32_t height, bool topRowAtTextureZero);
        static void Clear(std::int32_t width, std::int32_t height);
        static void Release();

    private:
        static constexpr std::int32_t Name = 1'000'000;
        static std::int32_t _texture;
#if defined(__ANDROID__)
        static std::int32_t _vertexBuffer, _indexBuffer;
#else
        static std::unique_ptr<OpenGlWindowDraw> _draw;
#endif
        static std::int32_t _width;
        static std::int32_t _height;
        static bool _ownsTexture;
    };
}
