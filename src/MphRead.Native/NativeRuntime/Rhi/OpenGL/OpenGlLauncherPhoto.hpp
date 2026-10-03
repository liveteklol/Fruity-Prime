#pragma once

#include <cstdint>
#include <memory>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    class OpenGlWindowDraw;
    class OpenGlLauncherPhoto final
    {
    public:
        OpenGlLauncherPhoto() = delete;

        static void Enabled(bool value) noexcept;
        [[nodiscard]] static bool Enabled() noexcept;
        static void Draw(std::int32_t width, std::int32_t height);
        // Release draw resources while the outgoing context is still current;
        // forget texture names so the replacement reloads the photograph.
        static void Forget() noexcept;

        // How strongly the moving layer shows through; the Vulkan window's
        // backdrop draws with the same value.
        static constexpr float Strength = 0.62F;

    private:
        [[nodiscard]] static bool Ensure();
#if defined(__ANDROID__)
        [[nodiscard]] static bool EnsureProgram();
#endif

        static constexpr std::int32_t Name = 1'000'001;
        static bool _enabled;
        static std::int32_t _texture;
        static std::int32_t _width;
        static std::int32_t _height;
        static bool _tried;
#if !defined(__ANDROID__)
        static std::unique_ptr<OpenGlWindowDraw> _draw;
#else
        static std::int32_t _program;
        static bool _programTried;
        static std::int32_t _photoUniform;
        static std::int32_t _strengthUniform;
        static std::int32_t _timeUniform;
        static std::int32_t _viewWidthUniform;
        static std::int32_t _viewHeightUniform;
#endif
    };
}
