#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace OpenTK::Graphics::OpenGL
{
    // Everything the game's renderer leaves bound and expects to find again,
    // captured before another renderer (Qt Quick's, for the menus) draws on
    // the same context and put back exactly afterwards. Resetting to GL's
    // defaults is not the same thing: the RHI tracks what it bound, so the
    // state has to come back as it was, not as GL starts.
    //
    // The same set the Skia launcher path saves around Ganesh (SkiaGpu.cpp),
    // through raw entry points so that it needs nothing but a current context.
    class GlStateGuard final
    {
    public:
        GlStateGuard();
        ~GlStateGuard();
        GlStateGuard(const GlStateGuard&) = delete;
        GlStateGuard& operator=(const GlStateGuard&) = delete;

    private:
        void Capture();
        void Restore() noexcept;

        std::int32_t _framebuffer = 0;
        std::array<std::int32_t, 4> _viewport{};
        std::array<std::int32_t, 4> _scissor{};
        std::array<std::int32_t, 4> _colorWrite{1, 1, 1, 1};
        std::int32_t _program = 0;
        std::int32_t _activeTexture = 0x84C0;
        std::int32_t _blendSrcRgb = 1, _blendDstRgb = 0, _blendSrcAlpha = 1, _blendDstAlpha = 0;
        std::int32_t _blendEquationRgb = 0x8006, _blendEquationAlpha = 0x8006;
        std::int32_t _depthFunction = 0x0201;
        std::int32_t _depthWrite = 1;
        std::int32_t _frontFace = 0x0901;
        std::int32_t _arrayBuffer = 0;
        std::int32_t _pixelPackBuffer = 0, _pixelUnpackBuffer = 0;
        std::array<std::int32_t, 6> _pack{};
        std::array<std::int32_t, 8> _unpack{};
        // Enabled capabilities, in the order Capabilities() names them.
        std::vector<std::uint8_t> _enabled;
        std::vector<std::int32_t> _unitTexture2D;
        std::vector<std::int32_t> _unitRectangle;
        std::vector<std::uint8_t> _unitTexture2DEnabled;
    };
}
