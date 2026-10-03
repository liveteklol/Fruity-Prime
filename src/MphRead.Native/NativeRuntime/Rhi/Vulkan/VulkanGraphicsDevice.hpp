#pragma once

#include "../GraphicsDevice.hpp"
#include "../Swapchain.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    class Context;

    // The device closes native resources before its Context ends. Public
    // wrappers may survive only as inert CPU descriptors after that boundary.
    [[nodiscard]] std::unique_ptr<GraphicsDevice> CreateGraphicsDevice(Context& context);
    // Backend diagnostic; keeps native descriptor handles out of the RHI.
    void CheckBindingAllocations(GraphicsDevice& device);
    void CheckShaderModules(GraphicsDevice& device);
    void CheckGraphicsPipelines(GraphicsDevice& device);
    void CheckResourceRetirement(GraphicsDevice& device);
    void CheckUploadReuse(GraphicsDevice& device);
    void CheckMemoryAdmission(GraphicsDevice& device);
    // Keeps a CPU witness of native teardown, never the context/device itself.
    [[nodiscard]] std::function<void()> SessionReleaseCheck(GraphicsDevice& device);
    // Submit the device's recorded work and show its window target: blitted
    // upright into the swapchain's next image (black before anything drew).
    [[nodiscard]] PresentResult PresentWindow(GraphicsDevice& device, Swapchain& swapchain);
    // The SPIR-V stages compiled into this binary (two per scene program).
    [[nodiscard]] std::uint32_t EmbeddedShaderStages() noexcept;

    // Interop for a second Vulkan client on the same device and queue (Skia's
    // Ganesh). Handles are opaque integers here; only the interop adapter
    // (NativeRuntime/Skia/VulkanInterop) turns them back into Vk types.
    struct InteropDevice final
    {
        std::uint64_t Instance = 0;
        std::uint64_t PhysicalDevice = 0;
        std::uint64_t Device = 0;
        std::uint64_t Queue = 0;
        std::uint32_t QueueFamily = 0;
        std::uint32_t ApiVersion = 0;
        void* GetInstanceProcAddr = nullptr; // PFN_vkGetInstanceProcAddr
        void* GetDeviceProcAddr = nullptr;   // PFN_vkGetDeviceProcAddr
    };
    struct InteropImage final
    {
        std::uint64_t Image = 0;
        std::uint32_t Format = 0;  // VkFormat
        std::uint32_t Usage = 0;   // VkImageUsageFlags
        std::uint32_t Layout = 0;  // VkImageLayout of its tracked state
    };
    [[nodiscard]] InteropDevice DescribeDevice(GraphicsDevice& device);
    // Submit everything recorded on the device, so another client's
    // submissions come after it.
    void FlushDevice(GraphicsDevice& device);
    void BeginExternalSubmit(GraphicsDevice& device);
    // Put the texture in this state (recorded and submitted now) and report it.
    [[nodiscard]] InteropImage PrepareForExternal(GraphicsDevice& device, Texture& texture, ResourceState state);
    // The other client left the texture in this state.
    void AdoptExternalState(Texture& texture, ResourceState state);
}
