#pragma once

#include "../Swapchain.hpp"

#include <memory>

namespace MphRead::RendererPlatform
{
    class Window;
}

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // The implementation owns its Vulkan context, surface, and swapchain.
    // The GLFW window must outlive the returned swapchain.
    [[nodiscard]] std::unique_ptr<Swapchain> CreateSwapchain(
        ::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc);
    class Context;
    // The same, presenting through a context the caller owns and shares with
    // its graphics device. The context must outlive the swapchain.
    [[nodiscard]] std::unique_ptr<Swapchain> CreateSwapchain(Context& context,
        ::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc);
    // A swapchain on the context's own surface, sized by that surface: the
    // Android head's, whose surface is an ANativeWindow and not a GLFW window.
    [[nodiscard]] std::unique_ptr<Swapchain> CreateSurfaceSwapchain(Context& context, const SwapchainDesc& desc);

    // Visible desktop diagnostic for Phase 13. It does not load game data.
    int RunPresentationCheck(bool forceFallback = false, bool reflexCheck = false);
}
