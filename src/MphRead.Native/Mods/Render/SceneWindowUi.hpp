#pragma once

namespace MphRead::NativeRuntime::Rhi
{
    class WindowUi;
}

namespace MphRead::Mods::Render
{
    // The launcher's window-level drawing when the scene backend presents the
    // window itself: one Rhi::WindowUi on the scene device, shared by the
    // photograph and the overlay, and gone with the window. Null whenever the
    // window is a context drawn into directly (OpenGL), which keeps its own
    // code paths untouched.
    class SceneWindowUi final
    {
    public:
        SceneWindowUi() = delete;

        [[nodiscard]] static bool Active() noexcept;
        [[nodiscard]] static NativeRuntime::Rhi::WindowUi* Get();
        static void Release() noexcept;
    };
}
