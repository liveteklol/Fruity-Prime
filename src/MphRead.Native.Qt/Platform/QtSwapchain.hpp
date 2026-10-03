#pragma once

#include <memory>

namespace MphRead::RendererPlatform
{
    class Window;
}

namespace MphRead::NativeRuntime::Rhi
{
    class Swapchain;
    struct SwapchainDesc;
}

namespace MphRead::Qt
{
    // BackendFactory's OpenGL swapchain when the window is Qt's.
    [[nodiscard]] std::unique_ptr<NativeRuntime::Rhi::Swapchain> CreateOpenGlSwapchain(
        RendererPlatform::Window& window, const NativeRuntime::Rhi::SwapchainDesc& desc);
}
