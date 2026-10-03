#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace MphRead::NativeRuntime::Rhi
{
    class GraphicsDevice;
    class Sampler;
    class Texture;

    // One corner of a window-level quad, in clip space, laid out in OpenGL's
    // rows: TexCoord samples the texture drawn, TexCoord1 is the backdrop's
    // second (moving) layer.
    struct WindowQuadVertex final
    {
        float Position[2]{};
        float TexCoord[2]{};
        float TexCoord1[2]{};
    };

    // The launcher's window-level draws when the scene device presents the
    // window itself: the photograph and a texture (the UI) laid over whatever
    // the frame holds, into the device's window target. A backend whose
    // window is a context it draws into directly has none, and says so by
    // CreateSceneWindowUi returning null.
    class WindowUi
    {
    public:
        virtual ~WindowUi() = default;

        // Open the window target at this size; clear it first when asked.
        virtual void Begin(std::uint32_t width, std::uint32_t height, bool clear) = 0;
        virtual void DrawTexture(const Texture& texture, const Sampler& sampler,
            std::span<const WindowQuadVertex, 4> strip, bool premultiplied) = 0;
        virtual void DrawBackdrop(const Texture& photo, const Sampler& sampler,
            std::span<const WindowQuadVertex, 4> strip, float strength, float seconds) = 0;
        virtual void End() = 0;
    };

    // The scene backend's WindowUi, or null when its window needs none.
    [[nodiscard]] std::unique_ptr<WindowUi> CreateSceneWindowUi(GraphicsDevice& device);
}
