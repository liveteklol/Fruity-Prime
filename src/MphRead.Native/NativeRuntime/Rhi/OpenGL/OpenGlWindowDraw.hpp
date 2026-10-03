#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    struct WindowVertex final { float Position[3]; float TexCoord[2]; float NoiseCoord[2]; };
    // Explicit shaders and RHI buffers/VAOs for the window's borrowed Skia
    // texture or owned photograph. Destroy while the GL session is current.
    class OpenGlWindowDraw final
    {
    public:
        OpenGlWindowDraw();
        ~OpenGlWindowDraw();
        void Draw(std::int32_t texture, const std::array<WindowVertex, 4>& vertices,
            std::int32_t width, std::int32_t height, bool premultiplied,
            bool backdrop = false, float strength = 0, float seconds = 0);
    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}
