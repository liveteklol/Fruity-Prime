#pragma once
#if defined(FRUITY_HAS_VULKAN) && !defined(__ANDROID__)
#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string>

// The window toolkit's half of Vulkan presentation. The backend asks these
// rather than any one toolkit, so the same device, swapchain and renderer
// present into a GLFW window (the Avalonia shell) or a QWindow (the Qt one).
// `nativeWindow` is whatever RendererPlatform::Window::NativeHandle returns.
namespace MphRead::NativeRuntime::Rhi::Vulkan::WindowSystem
{
    // Before any window exists: is there a loader and a driver at all.
    [[nodiscard]] bool Available(std::string& reason);
    // The loader's vkGetInstanceProcAddr, found the way this toolkit finds it.
    [[nodiscard]] PFN_vkGetInstanceProcAddr LoaderEntry();
    [[nodiscard]] std::span<const char* const> RequiredInstanceExtensions();
    [[nodiscard]] VkSurfaceKHR CreateSurface(VkInstance instance, void* nativeWindow,
        PFN_vkGetInstanceProcAddr instanceProc);
    [[nodiscard]] bool PresentationSupport(VkInstance instance, VkPhysicalDevice physical,
        std::uint32_t family, PFN_vkGetInstanceProcAddr instanceProc);
    [[nodiscard]] bool Iconified(void* nativeWindow);
    void FramebufferSize(void* nativeWindow, int& width, int& height);
    [[nodiscard]] bool ShouldClose(void* nativeWindow);
    // Wait for window events while nothing can be presented (minimised).
    void WaitEvents(double seconds);
}
#endif
