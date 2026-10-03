#include "VulkanFeatureProbe.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include "VulkanResult.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    namespace
    {
        bool Has(const std::vector<std::string>& names, const char* name)
        { return std::find(names.begin(), names.end(), name) != names.end(); }
        bool Eligible(std::span<const ProbeFinding> findings)
        {
            return std::none_of(findings.begin(), findings.end(), [](const auto& value) {
                return value.Required && !value.Supported;
            });
        }
        template<class T, class Enumerate> std::vector<T> EnumerateList(Enumerate enumerate, const char* operation)
        {
            // Extension/device lists can grow between count and data calls.
            for (unsigned attempt = 0; attempt < 8; ++attempt)
            {
                std::uint32_t count = 0;
                Check(enumerate(&count, nullptr), operation);
                std::vector<T> values(count);
                if (!count) return values;
                const auto result = enumerate(&count, values.data());
                if (result == VK_INCOMPLETE) continue;
                Check(result, operation); values.resize(count); return values;
            }
            throw std::runtime_error(std::string(operation) + ": enumeration did not stabilize.");
        }
    }

    InstanceSnapshot QueryInstanceSnapshot(const InstanceProbeDispatch& dispatch, std::span<const char* const> windowExtensions)
    {
        if (!dispatch.Extensions || !dispatch.Layers) throw std::invalid_argument("Incomplete Vulkan instance probe dispatch.");
        InstanceSnapshot result;
        if (dispatch.Version) Check(dispatch.Version(&result.LoaderVersion), "vkEnumerateInstanceVersion");
        for (const auto& value : EnumerateList<VkExtensionProperties>([&](auto count, auto data) {
            return dispatch.Extensions(nullptr, count, data);
        }, "instance extensions")) result.Extensions.emplace_back(value.extensionName);
        for (const auto& value : EnumerateList<VkLayerProperties>([&](auto count, auto data) {
            return dispatch.Layers(count, data);
        }, "instance layers")) result.Layers.emplace_back(value.layerName);
        for (const auto* value : windowExtensions) if (value) result.WindowExtensions.emplace_back(value);
        return result;
    }

    InstanceProbe EvaluateInstance(const InstanceSnapshot& snapshot, bool validation, bool allowMaintenance)
    {
        InstanceProbe result;
        result.Findings.push_back({"loader API 1.3", true, snapshot.LoaderVersion >= VK_API_VERSION_1_3, {}});
        result.Findings.push_back({"window-system extensions", true, !snapshot.WindowExtensions.empty(), {}});
        for (const auto& extension : snapshot.WindowExtensions)
        {
            const bool supported = Has(snapshot.Extensions, extension.c_str());
            result.Findings.push_back({"instance extension", true, supported, extension});
            if (supported && !Has(result.EnabledExtensions, extension.c_str())) result.EnabledExtensions.push_back(extension);
        }
        result.DebugUtils = Has(snapshot.Extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        result.Validation = validation && Has(snapshot.Layers, "VK_LAYER_KHRONOS_validation");
        result.Findings.push_back({"validation layer", false, Has(snapshot.Layers, "VK_LAYER_KHRONOS_validation"), {}});
        result.Findings.push_back({"debug utils", result.Validation, result.DebugUtils, {}});
        if (result.DebugUtils) result.EnabledExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        result.SurfaceMaintenance1 = allowMaintenance
            && Has(snapshot.Extensions, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)
            && Has(snapshot.Extensions, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        result.Findings.push_back({"surface maintenance1", false, result.SurfaceMaintenance1, {}});
        if (result.SurfaceMaintenance1)
        {
            result.EnabledExtensions.emplace_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
            result.EnabledExtensions.emplace_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        }
        result.PortabilityEnumeration = Has(snapshot.Extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        if (result.PortabilityEnumeration) result.EnabledExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        result.Eligible = Eligible(result.Findings);
        return result;
    }

    std::vector<PhysicalDeviceSnapshot> QueryPhysicalDevices(const PhysicalProbeDispatch& dispatch,
        VkSurfaceKHR surface, bool surfaceMaintenance)
    {
        if (!dispatch.Devices || !dispatch.Properties || !dispatch.Extensions || !dispatch.Features
            || !dispatch.Formats || !dispatch.Queues || !dispatch.Memory
            || (surface ? !dispatch.SurfaceSupport : !dispatch.PlatformPresentation))
            throw std::invalid_argument("Incomplete Vulkan physical-device probe dispatch.");
        std::vector<PhysicalDeviceSnapshot> result;
        for (const auto device : EnumerateList<VkPhysicalDevice>([&](auto count, auto data) {
            return dispatch.Devices(dispatch.Instance, count, data);
        }, "physical devices"))
        {
            PhysicalDeviceSnapshot snapshot; snapshot.Device = device;
            dispatch.Properties(device, &snapshot.Properties);
            for (const auto& value : EnumerateList<VkExtensionProperties>([&](auto count, auto data) {
                return dispatch.Extensions(device, nullptr, count, data);
            }, "device extensions"))
            {
                snapshot.Extensions.emplace_back(value.extensionName);
                if (std::string_view(value.extensionName) == VK_NV_LOW_LATENCY_2_EXTENSION_NAME)
                { snapshot.NvLowLatency2 = true; snapshot.NvLowLatency2SpecVersion = value.specVersion; }
            }
            // Older devices still get a structured rejection without asking
            // them for the application's 1.3 feature chain.
            if (snapshot.Properties.apiVersion >= VK_API_VERSION_1_3)
            {
                VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT};
                VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
                VkPhysicalDevicePresentIdFeaturesKHR presentId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
                if (Has(snapshot.Extensions, VK_KHR_PRESENT_ID_EXTENSION_NAME)) features13.pNext = &presentId;
                if (surfaceMaintenance && Has(snapshot.Extensions, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME))
                { maintenance.pNext = features13.pNext; features13.pNext = &maintenance; }
                VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
                features12.pNext = &features13;
                VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext = &features12;
                dispatch.Features(device, &features); snapshot.Features = features.features;
                snapshot.DynamicRendering = features13.dynamicRendering != 0;
                snapshot.Synchronization2 = features13.synchronization2 != 0;
                snapshot.TimelineSemaphore = features12.timelineSemaphore != 0;
                snapshot.SwapchainMaintenance1 = maintenance.swapchainMaintenance1 != 0;
                snapshot.PresentId = presentId.presentId != 0;
            }
            dispatch.Formats(device, VK_FORMAT_R8G8B8A8_UNORM, &snapshot.Color);
            dispatch.Formats(device, VK_FORMAT_D32_SFLOAT, &snapshot.Depth);
            std::uint32_t count = 0; dispatch.Queues(device, &count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(count); dispatch.Queues(device, &count, queues.data()); queues.resize(count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                bool presents = false;
                if (queues[i].queueCount)
                {
                    if (surface)
                    {
                        VkBool32 supported = VK_FALSE;
                        Check(dispatch.SurfaceSupport(device, i, surface, &supported), "vkGetPhysicalDeviceSurfaceSupportKHR");
                        presents = supported == VK_TRUE;
                    }
                    else presents = dispatch.PlatformPresentation(device, i);
                }
                snapshot.Queues.push_back({queues[i], presents});
            }
            dispatch.Memory(device, &snapshot.Memory);
            result.push_back(std::move(snapshot));
        }
        return result;
    }

    PhysicalDeviceProbe EvaluatePhysicalDevice(PhysicalDeviceSnapshot snapshot, bool surfaceMaintenance)
    {
        PhysicalDeviceProbe result; result.Snapshot = std::move(snapshot);
        const auto& facts = result.Snapshot;
        const auto required = [&](const char* name, bool supported) { result.Findings.push_back({name, true, supported, {}}); };
        required("device API 1.3", facts.Properties.apiVersion >= VK_API_VERSION_1_3);
        required("swapchain extension", Has(facts.Extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME));
        required("dynamic rendering", facts.DynamicRendering); required("synchronization2", facts.Synchronization2);
        required("timeline semaphore", facts.TimelineSemaphore);
        constexpr auto color = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
            | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        required("RGBA8 color/sample/transfer", (facts.Color.optimalTilingFeatures & color) == color);
        required("D32 depth attachment", (facts.Depth.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0);
        for (std::uint32_t i = 0; i < facts.Queues.size(); ++i)
        {
            const auto& queue = facts.Queues[i]; if (!queue.Properties.queueCount) continue;
            const bool graphics = (queue.Properties.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            if (graphics && queue.Presents) { result.GraphicsFamily = result.PresentFamily = i; break; }
            if (graphics && result.GraphicsFamily == UINT32_MAX) result.GraphicsFamily = i;
            if (queue.Presents && result.PresentFamily == UINT32_MAX) result.PresentFamily = i;
        }
        required("graphics queue", result.GraphicsFamily != UINT32_MAX); required("presentation queue", result.PresentFamily != UINT32_MAX);
        result.MemoryBudget = Has(facts.Extensions, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        result.NvLowLatency2SpecVersion = facts.NvLowLatency2SpecVersion;
        result.PresentId = facts.PresentId && Has(facts.Extensions, VK_KHR_PRESENT_ID_EXTENSION_NAME);
        result.NvLowLatency2 = facts.NvLowLatency2 && facts.NvLowLatency2SpecVersion >= 2 && result.PresentId && facts.TimelineSemaphore;
        if (!facts.NvLowLatency2) result.ReflexUnavailableReason = "NVIDIA Reflex: VK_NV_low_latency2 is not exposed by this driver.";
        else if (facts.NvLowLatency2SpecVersion < 2) result.ReflexUnavailableReason = "NVIDIA Reflex: VK_NV_low_latency2 revision 2 is required for latency timings.";
        else if (!result.PresentId) result.ReflexUnavailableReason = "NVIDIA Reflex: VK_KHR_present_id extension/feature is unavailable.";
        else if (!facts.TimelineSemaphore) result.ReflexUnavailableReason = "NVIDIA Reflex: timelineSemaphore is unavailable.";
        result.Findings.push_back({"NVIDIA Reflex", false, result.NvLowLatency2, result.ReflexUnavailableReason});
        result.PortabilitySubset = Has(facts.Extensions, "VK_KHR_portability_subset");
        result.SwapchainMaintenance1 = surfaceMaintenance && facts.SwapchainMaintenance1
            && Has(facts.Extensions, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        result.Findings.push_back({"memory budget", false, result.MemoryBudget, {}});
        result.Findings.push_back({"swapchain maintenance1", false, result.SwapchainMaintenance1, {}});
        for (std::uint32_t i = 0; i < facts.Memory.memoryHeapCount; ++i)
            if (facts.Memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            {
                const auto bytes = facts.Memory.memoryHeaps[i].size;
                result.DeviceLocalBytes += std::min(bytes, std::numeric_limits<std::uint64_t>::max() - result.DeviceLocalBytes);
            }
        result.Eligible = Eligible(result.Findings);
        if (!result.Eligible) return result;
        result.Score = result.DeviceLocalBytes / (1024 * 1024) + 1;
        if (facts.Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) result.Score += 1000000;
        const auto& limits = facts.Properties.limits; auto& caps = result.Caps;
        caps.backend = GraphicsBackend::Vulkan;
        caps.maxTexture2DDimension = limits.maxImageDimension2D; caps.maxTextureArrayLayers = limits.maxImageArrayLayers;
        caps.maxTexture3DDimension = limits.maxImageDimension3D;
        for (std::uint32_t extent = limits.maxImageDimension2D; extent > 0; extent >>= 1) ++caps.maxTextureMipLevels;
        caps.maxColorAttachments = limits.maxColorAttachments; caps.maxVertexBuffers = limits.maxVertexInputBindings;
        caps.maxBindingGroups = limits.maxBoundDescriptorSets;
        caps.supportsAnisotropy = facts.Features.samplerAnisotropy != 0;
        caps.maxSamplerAnisotropy = caps.supportsAnisotropy ? limits.maxSamplerAnisotropy : 1.0F;
        caps.supportsWireframe = facts.Features.fillModeNonSolid != 0; caps.supportsDepthClamp = facts.Features.depthClamp != 0;
        caps.supportsPackedDepthStencilTransfer = true;
        const auto& queue = facts.Queues[result.GraphicsFamily].Properties;
        caps.supportsCompute = (queue.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        caps.supportsTimestampQueries = queue.timestampValidBits != 0;
        result.Timestamps = {queue.timestampValidBits, limits.timestampPeriod};
        return result;
    }

    std::optional<std::size_t> SelectPhysicalDevice(std::span<const PhysicalDeviceProbe> probes)
    {
        std::optional<std::size_t> selected;
        for (std::size_t i = 0; i < probes.size(); ++i)
            if (probes[i].Eligible && (!selected || probes[i].Score > probes[*selected].Score)) selected = i;
        return selected;
    }
    std::string RejectionReasons(std::span<const ProbeFinding> findings)
    {
        std::string result;
        for (const auto& finding : findings) if (finding.Required && !finding.Supported)
        {
            if (!result.empty()) result += "; ";
            result += finding.Requirement;
            if (!finding.Detail.empty()) result += " (" + finding.Detail + ")";
        }
        return result;
    }
}
#endif
