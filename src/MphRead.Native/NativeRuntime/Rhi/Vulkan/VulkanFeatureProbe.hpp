#pragma once

#if defined(FRUITY_HAS_VULKAN)
#include "../Capabilities.hpp"
#include "../GpuDiagnostics.hpp"
#include <vulkan/vulkan.h>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    struct ProbeFinding final
    {
        std::string Requirement;
        bool Required = true;
        bool Supported = false;
        std::string Detail;
    };
    struct InstanceSnapshot final
    {
        std::uint32_t LoaderVersion = VK_API_VERSION_1_0;
        std::vector<std::string> Extensions, Layers, WindowExtensions;
    };
    struct InstanceProbe final
    {
        std::vector<ProbeFinding> Findings;
        std::vector<std::string> EnabledExtensions;
        bool Eligible = false, Validation = false, DebugUtils = false;
        bool SurfaceMaintenance1 = false, PortabilityEnumeration = false;
    };
    struct QueueSnapshot final
    {
        VkQueueFamilyProperties Properties{};
        bool Presents = false;
    };
    struct PhysicalDeviceSnapshot final
    {
        VkPhysicalDevice Device = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties Properties{};
        VkPhysicalDeviceFeatures Features{};
        VkPhysicalDeviceMemoryProperties Memory{};
        VkFormatProperties Color{}, Depth{};
        std::vector<std::string> Extensions;
        std::uint32_t NvLowLatency2SpecVersion = 0;
        bool NvLowLatency2 = false, PresentId = false;
        std::vector<QueueSnapshot> Queues;
        bool DynamicRendering = false, Synchronization2 = false, TimelineSemaphore = false;
        bool SwapchainMaintenance1 = false;
    };
    struct PhysicalDeviceProbe final
    {
        PhysicalDeviceSnapshot Snapshot;
        std::vector<ProbeFinding> Findings;
        std::uint32_t GraphicsFamily = UINT32_MAX, PresentFamily = UINT32_MAX;
        std::uint64_t DeviceLocalBytes = 0, Score = 0;
        bool Eligible = false, MemoryBudget = false, PortabilitySubset = false, SwapchainMaintenance1 = false;
        bool NvLowLatency2 = false, PresentId = false;
        std::uint32_t NvLowLatency2SpecVersion = 0;
        std::string ReflexUnavailableReason;
        Capabilities Caps{};
        TimestampProperties Timestamps{};
    };
    // Query dispatch deliberately has no logical-device or queue creation.
    // Queries collect hardware facts; pure evaluation decides eligibility.
    struct InstanceProbeDispatch final
    {
        PFN_vkEnumerateInstanceVersion Version = nullptr; // Optional on 1.0 loaders.
        PFN_vkEnumerateInstanceExtensionProperties Extensions = nullptr;
        PFN_vkEnumerateInstanceLayerProperties Layers = nullptr;
    };
    struct PhysicalProbeDispatch final
    {
        VkInstance Instance = VK_NULL_HANDLE;
        PFN_vkEnumeratePhysicalDevices Devices = nullptr;
        PFN_vkGetPhysicalDeviceProperties Properties = nullptr;
        PFN_vkEnumerateDeviceExtensionProperties Extensions = nullptr;
        PFN_vkGetPhysicalDeviceFeatures2 Features = nullptr;
        PFN_vkGetPhysicalDeviceFormatProperties Formats = nullptr;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties Queues = nullptr;
        PFN_vkGetPhysicalDeviceMemoryProperties Memory = nullptr;
        PFN_vkGetPhysicalDeviceSurfaceSupportKHR SurfaceSupport = nullptr;
        std::function<bool(VkPhysicalDevice, std::uint32_t)> PlatformPresentation;
    };
    [[nodiscard]] InstanceSnapshot QueryInstanceSnapshot(const InstanceProbeDispatch& dispatch,
        std::span<const char* const> windowExtensions);
    [[nodiscard]] InstanceProbe EvaluateInstance(const InstanceSnapshot& snapshot, bool validation, bool allowMaintenance);
    [[nodiscard]] std::vector<PhysicalDeviceSnapshot> QueryPhysicalDevices(const PhysicalProbeDispatch& dispatch,
        VkSurfaceKHR surface, bool surfaceMaintenance);
    [[nodiscard]] PhysicalDeviceProbe EvaluatePhysicalDevice(PhysicalDeviceSnapshot snapshot, bool surfaceMaintenance);
    [[nodiscard]] std::optional<std::size_t> SelectPhysicalDevice(std::span<const PhysicalDeviceProbe> probes);
    [[nodiscard]] std::string RejectionReasons(std::span<const ProbeFinding> findings);
}
#endif
