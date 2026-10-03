#include "VulkanWindowSystem.hpp"
#if defined(FRUITY_HAS_VULKAN) && !defined(__ANDROID__) && !defined(MPHREAD_QT)
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "VulkanResult.hpp"

namespace MphRead::NativeRuntime::Rhi::Vulkan::WindowSystem
{
    bool Available(std::string& reason)
    {
        if (::glfwInit() != GLFW_TRUE) { reason = "GLFW could not be initialised"; return false; }
        if (::glfwVulkanSupported() != GLFW_TRUE) { reason = "no Vulkan loader or driver was found"; return false; }
        return true;
    }

    PFN_vkGetInstanceProcAddr LoaderEntry()
    {
        return reinterpret_cast<PFN_vkGetInstanceProcAddr>(::glfwGetInstanceProcAddress);
    }

    std::span<const char* const> RequiredInstanceExtensions()
    {
        std::uint32_t count = 0;
        const char** names = ::glfwGetRequiredInstanceExtensions(&count);
        return {names, names ? count : 0};
    }

    VkSurfaceKHR CreateSurface(VkInstance instance, void* nativeWindow, PFN_vkGetInstanceProcAddr)
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        Check(::glfwCreateWindowSurface(instance, static_cast<GLFWwindow*>(nativeWindow), nullptr, &surface),
            "glfwCreateWindowSurface");
        return surface;
    }

    bool PresentationSupport(VkInstance instance, VkPhysicalDevice physical, std::uint32_t family,
        PFN_vkGetInstanceProcAddr)
    {
        return ::glfwGetPhysicalDevicePresentationSupport(instance, physical, family) == GLFW_TRUE;
    }

    bool Iconified(void* nativeWindow)
    {
        return ::glfwGetWindowAttrib(static_cast<GLFWwindow*>(nativeWindow), GLFW_ICONIFIED) != 0;
    }

    void FramebufferSize(void* nativeWindow, int& width, int& height)
    {
        ::glfwGetFramebufferSize(static_cast<GLFWwindow*>(nativeWindow), &width, &height);
    }

    bool ShouldClose(void* nativeWindow)
    {
        return ::glfwWindowShouldClose(static_cast<GLFWwindow*>(nativeWindow)) != 0;
    }

    void WaitEvents(double seconds)
    {
        ::glfwWaitEventsTimeout(seconds);
    }
}
#endif
