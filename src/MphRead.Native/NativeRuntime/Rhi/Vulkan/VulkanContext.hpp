#pragma once

#include "../Capabilities.hpp"
#include <memory>
#include <string>

namespace MphRead::RendererPlatform
{
    class Window;
}

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    class VulkanSwapchain;
    class VulkanGraphicsDevice;
    class VulkanCommandList;
    class VulkanDeviceState;
    class VulkanSampler;
    class VulkanTexture;
    class VulkanTextureView;

    // Native API objects stay behind the backend's implementation boundary.
    class Context final
    {
    public:
        explicit Context(bool validation);
        Context(bool validation, ::MphRead::RendererPlatform::Window& window, bool allowMaintenance = true);
        // Android: a device with a surface on this ANativeWindow*.
        struct AndroidWindow final { void* Native = nullptr; };
        Context(bool validation, AndroidWindow window);
        // Android: the surface goes with its window (the device and every
        // resource on it stay); a new window gets a new surface.
        void ReplaceAndroidSurface(void* nativeWindow);
        // Desktop: present to another window (a GLFWwindow*), or to none
        // (null) once the window it had is about to go. The device stays.
        void ReplaceWindowSurface(void* window);
        ~Context();
        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        [[nodiscard]] const Capabilities& Caps() const noexcept;
        [[nodiscard]] const std::string& DeviceName() const noexcept;
        // "GPU, Vulkan a.b.c, driver d": what the startup log reports.
        [[nodiscard]] std::string Describe() const;
        [[nodiscard]] unsigned ValidationErrors() const noexcept;
        [[nodiscard]] bool ValidationEnabled() const noexcept;
        // Instance/physical-device queries only; no device, queues or VMA.
        [[nodiscard]] static std::string ProbePassive();
        void WaitIdle();
        void Shutdown();
        void CheckCommandBufferDebugName();
    private:
        friend class VulkanSwapchain;
        friend class VulkanGraphicsDevice;
        friend class VulkanCommandList;
        friend class VulkanTimestampSet;
        friend class VulkanDeviceState;
        friend class VulkanSampler;
        friend class VulkanTexture;
        friend class VulkanTextureView;
        friend class VulkanBindingLayout;
        friend class VulkanBindingSet;
        friend class VulkanShader;
        friend class VulkanGraphicsPipeline;
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };

    // Explicit diagnostic only; does not select a backend for ordinary games.
    int RunFoundationCheck();
    int RunResourceCheck();
}
