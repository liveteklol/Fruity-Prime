#include "../NativeRuntime/Rhi/Vulkan/VulkanFeatureProbe.hpp"
#include "../NativeRuntime/Rhi/BackendError.hpp"
#include "../NativeRuntime/Rhi/PresentationScheduler.hpp"
#include "../NativeRuntime/Rhi/Vulkan/VulkanPresentResult.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace MphRead::NativeRuntime::Rhi;
    using namespace MphRead::NativeRuntime::Rhi::Vulkan;
    void Expect(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
    PhysicalDeviceSnapshot Supported()
    {
        PhysicalDeviceSnapshot facts;
        std::strcpy(facts.Properties.deviceName, "Synthetic GPU");
        facts.Properties.apiVersion = VK_API_VERSION_1_3;
        facts.Properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        auto& limits = facts.Properties.limits;
        limits.maxImageDimension2D = 16384; limits.maxImageArrayLayers = 2048;
        limits.maxColorAttachments = 8; limits.maxVertexInputBindings = 16; limits.maxBoundDescriptorSets = 8;
        limits.maxSamplerAnisotropy = 16; limits.timestampPeriod = 2;
        facts.Features.samplerAnisotropy = facts.Features.depthClamp = facts.Features.fillModeNonSolid = VK_TRUE;
        facts.DynamicRendering = facts.Synchronization2 = facts.TimelineSemaphore = true;
        facts.Extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        facts.Color.optimalTilingFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT
            | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        facts.Depth.optimalTilingFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        VkQueueFamilyProperties queue{}; queue.queueCount = 1;
        queue.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT; queue.timestampValidBits = 48;
        facts.Queues = {{queue, true}};
        facts.Memory.memoryHeapCount = 2;
        facts.Memory.memoryHeaps[0] = {4ULL * 1024 * 1024 * 1024, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};
        facts.Memory.memoryHeaps[1] = {16ULL * 1024 * 1024 * 1024, 0};
        return facts;
    }

    void CheckPolicy()
    {
        const auto facts = Supported();
        const auto good = EvaluatePhysicalDevice(facts, true);
        Expect(good.Eligible && good.GraphicsFamily == 0 && good.PresentFamily == 0, "Supported GPU rejected.");
        Expect(good.DeviceLocalBytes == 4ULL * 1024 * 1024 * 1024 && good.Score == 4097, "Memory score counts nonlocal heaps.");
        Expect(good.Caps.maxTexture2DDimension == 16384 && good.Caps.maxBindingGroups == 8
            && good.Caps.maxSamplerAnisotropy == 16 && good.Caps.supportsCompute && good.Caps.supportsTimestampQueries
            && good.Timestamps.validBits == 48 && good.Timestamps.nanosecondsPerTick == 2, "Capabilities or timestamps lost.");
        const auto missing = [&](auto mutate, const char* requirement) {
            auto copy = facts; mutate(copy); const auto result = EvaluatePhysicalDevice(std::move(copy), true);
            Expect(!result.Eligible && result.Score == 0, "Missing required feature remained eligible.");
            Expect(RejectionReasons(result.Findings).find(requirement) != std::string::npos, "Missing requirement lost its finding.");
        };
        missing([](auto& v) { v.Properties.apiVersion = VK_API_VERSION_1_2; }, "device API 1.3");
        missing([](auto& v) { v.Extensions.clear(); }, "swapchain extension");
        missing([](auto& v) { v.DynamicRendering = false; }, "dynamic rendering");
        missing([](auto& v) { v.Synchronization2 = false; }, "synchronization2");
        missing([](auto& v) { v.TimelineSemaphore = false; }, "timeline semaphore");
        for (auto bit : {VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT, VK_FORMAT_FEATURE_TRANSFER_DST_BIT})
            missing([bit](auto& v) { v.Color.optimalTilingFeatures &= ~bit; }, "RGBA8");
        missing([](auto& v) { v.Depth.optimalTilingFeatures = 0; }, "D32");
        missing([](auto& v) { v.Queues[0].Properties.queueFlags = VK_QUEUE_COMPUTE_BIT; }, "graphics queue");
        missing([](auto& v) { v.Queues[0].Presents = false; }, "presentation queue");
        missing([](auto& v) { v.Queues[0].Properties.queueCount = 0; }, "graphics queue");
        auto allMissing = facts; allMissing.Extensions.clear(); allMissing.DynamicRendering = allMissing.TimelineSemaphore = false;
        const auto rejected = EvaluatePhysicalDevice(allMissing, true);
        Expect(RejectionReasons(rejected.Findings).find("swapchain extension") != std::string::npos
            && RejectionReasons(rejected.Findings).find("dynamic rendering") != std::string::npos
            && RejectionReasons(rejected.Findings).find("timeline semaphore") != std::string::npos, "Rejection stopped at the first failure.");

        auto separate = facts; separate.Queues[0].Presents = false;
        VkQueueFamilyProperties presentation{}; presentation.queueCount = 1;
        separate.Queues.push_back({presentation, true});
        auto result = EvaluatePhysicalDevice(separate, true);
        Expect(result.Eligible && result.GraphicsFamily == 0 && result.PresentFamily == 1, "Separate queue families rejected.");
        separate.Queues.push_back(facts.Queues[0]); result = EvaluatePhysicalDevice(separate, true);
        Expect(result.GraphicsFamily == 2 && result.PresentFamily == 2, "Unified family not preferred.");
        auto optional = facts; optional.Features = {}; optional.Queues[0].Properties.timestampValidBits = 0;
        result = EvaluatePhysicalDevice(optional, false);
        Expect(result.Eligible && !result.Caps.supportsAnisotropy && result.Caps.maxSamplerAnisotropy == 1
            && !result.Caps.supportsTimestampQueries && !result.SwapchainMaintenance1, "Optional feature became a requirement.");
        optional = facts; optional.Extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        optional.Extensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME); optional.Extensions.push_back("VK_KHR_portability_subset");
        optional.SwapchainMaintenance1 = true; result = EvaluatePhysicalDevice(optional, true);
        Expect(result.SwapchainMaintenance1 && result.MemoryBudget && result.PortabilitySubset, "Optional features lost.");
        Expect(!EvaluatePhysicalDevice(optional, false).SwapchainMaintenance1, "Surface maintenance prerequisite ignored.");
        optional.SwapchainMaintenance1 = false;
        Expect(!EvaluatePhysicalDevice(optional, true).SwapchainMaintenance1, "Extension name substituted for feature support.");
        optional = facts; optional.Extensions.push_back(VK_NV_LOW_LATENCY_2_EXTENSION_NAME);
        optional.Extensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
        optional.NvLowLatency2 = optional.PresentId = true; optional.NvLowLatency2SpecVersion = 3;
        Expect(EvaluatePhysicalDevice(optional, true).NvLowLatency2, "Reflex dependencies not admitted.");
        optional.PresentId = false; result = EvaluatePhysicalDevice(optional, true);
        Expect(result.Eligible && !result.NvLowLatency2 && result.ReflexUnavailableReason.find("present_id") != std::string::npos, "Missing optional present ID rejected renderer or lost reason.");
        optional.PresentId = true; optional.NvLowLatency2SpecVersion = 1;
        Expect(!EvaluatePhysicalDevice(optional, true).NvLowLatency2, "Unusable extension revision admitted.");
        auto discrete = facts; discrete.Properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        std::array choices{good, EvaluatePhysicalDevice(discrete, true), rejected};
        Expect(SelectPhysicalDevice(choices) == 1, "Selection admitted unsupported or ignored discrete GPU.");
        choices[1] = good; Expect(SelectPhysicalDevice(choices) == 0, "Tie changed device enumeration preference.");
        const std::array invalid{rejected, rejected};
        Expect(!SelectPhysicalDevice(invalid) && !SelectPhysicalDevice({}), "Selection invented an eligible GPU.");
        auto huge = facts; huge.Memory.memoryHeaps[0].size = UINT64_MAX; huge.Memory.memoryHeaps[1] = {1024, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};
        Expect(EvaluatePhysicalDevice(huge, false).DeviceLocalBytes == UINT64_MAX, "Heap sum overflowed.");
    }

    void CheckInstance()
    {
        InstanceSnapshot facts; facts.LoaderVersion = VK_API_VERSION_1_3;
        facts.WindowExtensions = {VK_KHR_SURFACE_EXTENSION_NAME, "VK_KHR_test_surface"}; facts.Extensions = facts.WindowExtensions;
        Expect(EvaluateInstance(facts, true, true).Eligible && !EvaluateInstance(facts, true, true).Validation, "Optional absent validation rejected.");
        auto bad = facts; bad.LoaderVersion = VK_API_VERSION_1_2;
        Expect(!EvaluateInstance(bad, false, true).Eligible, "Old loader admitted.");
        bad = facts; bad.WindowExtensions.clear(); Expect(!EvaluateInstance(bad, false, true).Eligible, "Missing window system admitted.");
        bad = facts; bad.Extensions.pop_back();
        Expect(RejectionReasons(EvaluateInstance(bad, false, true).Findings).find("VK_KHR_test_surface") != std::string::npos, "Missing extension name discarded.");
        facts.Layers.push_back("VK_LAYER_KHRONOS_validation");
        Expect(!EvaluateInstance(facts, true, true).Eligible && EvaluateInstance(facts, false, true).Eligible, "Validation reporting contract ignored.");
        facts.Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        facts.Extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        Expect(!EvaluateInstance(facts, true, true).SurfaceMaintenance1, "Capabilities2 prerequisite ignored.");
        facts.Extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        facts.Extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        const auto full = EvaluateInstance(facts, true, true);
        Expect(full.Eligible && full.Validation && full.DebugUtils && full.PortabilityEnumeration && full.SurfaceMaintenance1, "Supported instance capabilities lost.");
        Expect(!EvaluateInstance(facts, true, false).SurfaceMaintenance1, "Maintenance override ignored.");
    }

    PhysicalDeviceSnapshot queried = Supported();
    unsigned featureCalls = 0, platformCalls = 0, surfaceCalls = 0, extensionDataCalls = 0;
    bool incompleteOnce = false, failSurface = false;
    VKAPI_ATTR VkResult VKAPI_CALL Devices(VkInstance, std::uint32_t* count, VkPhysicalDevice* data)
    { if (data && *count) data[0] = reinterpret_cast<VkPhysicalDevice>(1); *count = 1; return VK_SUCCESS; }
    VKAPI_ATTR void VKAPI_CALL Properties(VkPhysicalDevice, VkPhysicalDeviceProperties* data) { *data = queried.Properties; }
    VKAPI_ATTR VkResult VKAPI_CALL Extensions(VkPhysicalDevice, const char*, std::uint32_t* count, VkExtensionProperties* data)
    {
        if (data)
        {
            ++extensionDataCalls;
            if (incompleteOnce) { incompleteOnce = false; return VK_INCOMPLETE; }
            for (std::size_t i = 0; i < queried.Extensions.size(); ++i) { std::strcpy(data[i].extensionName, queried.Extensions[i].c_str()); data[i].specVersion = queried.Extensions[i] == VK_NV_LOW_LATENCY_2_EXTENSION_NAME ? queried.NvLowLatency2SpecVersion : 1; }
        }
        *count = static_cast<std::uint32_t>(queried.Extensions.size()); return VK_SUCCESS;
    }
    VKAPI_ATTR void VKAPI_CALL Features(VkPhysicalDevice, VkPhysicalDeviceFeatures2* data)
    {
        ++featureCalls; data->features = queried.Features;
        auto* features12 = static_cast<VkPhysicalDeviceVulkan12Features*>(data->pNext);
        features12->timelineSemaphore = queried.TimelineSemaphore;
        auto* features13 = static_cast<VkPhysicalDeviceVulkan13Features*>(features12->pNext);
        features13->dynamicRendering = queried.DynamicRendering; features13->synchronization2 = queried.Synchronization2;
        for (auto* node = static_cast<VkBaseOutStructure*>(features13->pNext); node; node = node->pNext) {
            if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT)
                reinterpret_cast<VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT*>(node)->swapchainMaintenance1 = queried.SwapchainMaintenance1;
            if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR)
                reinterpret_cast<VkPhysicalDevicePresentIdFeaturesKHR*>(node)->presentId = queried.PresentId;
        }
    }
    VKAPI_ATTR void VKAPI_CALL Formats(VkPhysicalDevice, VkFormat format, VkFormatProperties* data)
    { *data = format == VK_FORMAT_D32_SFLOAT ? queried.Depth : queried.Color; }
    VKAPI_ATTR void VKAPI_CALL Queues(VkPhysicalDevice, std::uint32_t* count, VkQueueFamilyProperties* data)
    { if (data) for (std::size_t i = 0; i < queried.Queues.size(); ++i) data[i] = queried.Queues[i].Properties; *count = static_cast<std::uint32_t>(queried.Queues.size()); }
    VKAPI_ATTR void VKAPI_CALL Memory(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* data) { *data = queried.Memory; }
    VKAPI_ATTR VkResult VKAPI_CALL Surface(VkPhysicalDevice, std::uint32_t family, VkSurfaceKHR, VkBool32* data)
    { ++surfaceCalls; if (failSurface) return VK_ERROR_SURFACE_LOST_KHR; *data = queried.Queues[family].Presents; return VK_SUCCESS; }
    VKAPI_ATTR VkResult VKAPI_CALL InstanceExtensions(const char*, std::uint32_t* count, VkExtensionProperties* data)
    { if (data) { std::strcpy(data[0].extensionName, VK_KHR_SURFACE_EXTENSION_NAME); } *count = 1; return VK_SUCCESS; }
    VKAPI_ATTR VkResult VKAPI_CALL Layers(std::uint32_t* count, VkLayerProperties*) { *count = 0; return VK_SUCCESS; }
    VKAPI_ATTR VkResult VKAPI_CALL Version(std::uint32_t* version) { *version = VK_API_VERSION_1_3; return VK_SUCCESS; }

    void CheckQueries()
    {
        const std::array window{VK_KHR_SURFACE_EXTENSION_NAME};
        const auto instance = QueryInstanceSnapshot({Version, InstanceExtensions, Layers}, window);
        Expect(EvaluateInstance(instance, false, false).Eligible, "Instance query dropped facts.");
        Expect(!EvaluateInstance(QueryInstanceSnapshot({nullptr, InstanceExtensions, Layers}, window), false, false).Eligible, "1.0 loader fallback became eligible.");
        PhysicalProbeDispatch dispatch{VK_NULL_HANDLE, Devices, Properties, Extensions, Features, Formats, Queues, Memory, Surface,
            [](VkPhysicalDevice, std::uint32_t family) { ++platformCalls; return queried.Queues[family].Presents; }};
        incompleteOnce = true;
        auto results = QueryPhysicalDevices(dispatch, VK_NULL_HANDLE, true);
        Expect(results.size() == 1 && featureCalls == 1 && extensionDataCalls == 2 && platformCalls == 1 && surfaceCalls == 0,
            "Query failed to retry enumeration or used surface path without a surface.");
        Expect(EvaluatePhysicalDevice(results[0], true).Eligible, "Native snapshot differs from pure policy facts.");
        queried.Extensions.push_back(VK_NV_LOW_LATENCY_2_EXTENSION_NAME);
        queried.Extensions.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
        queried.Extensions.push_back(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        queried.NvLowLatency2 = queried.PresentId = queried.SwapchainMaintenance1 = true; queried.NvLowLatency2SpecVersion = 3;
        results = QueryPhysicalDevices(dispatch, VK_NULL_HANDLE, true);
        Expect(results[0].NvLowLatency2SpecVersion == 3 && results[0].PresentId && results[0].SwapchainMaintenance1
            && EvaluatePhysicalDevice(results[0], true).NvLowLatency2, "Feature pNext chain lost present ID, revision, or maintenance.");
        const auto surface = reinterpret_cast<VkSurfaceKHR>(1);
        results = QueryPhysicalDevices(dispatch, surface, true);
        Expect(surfaceCalls == 1 && platformCalls == 2, "Real surface substituted by platform presentation support.");
        queried.Properties.apiVersion = VK_API_VERSION_1_2; const auto before = featureCalls;
        results = QueryPhysicalDevices(dispatch, VK_NULL_HANDLE, false);
        Expect(featureCalls == before && !EvaluatePhysicalDevice(results[0], false).Eligible, "Old device received a 1.3 feature chain.");
        queried = Supported(); failSurface = true; bool rejected = false;
        try { (void)QueryPhysicalDevices(dispatch, surface, false); }
        catch (const BackendError& error) { rejected = error.Kind() == BackendErrorKind::SurfaceLost; }
        Expect(rejected, "Native surface loss became an unsupported-feature finding.");
    }
}
int main()
{
    try
    {
        CheckPolicy(); CheckInstance(); CheckQueries();
        const auto ready = NativePresentResult(VK_SUCCESS);
        const auto suboptimal = NativePresentResult(VK_SUBOPTIMAL_KHR);
        const auto acquireSuboptimal = NativePresentResult(VK_SUCCESS, true);
        const auto outdated = NativePresentResult(VK_ERROR_OUT_OF_DATE_KHR);
        Expect(ready.accepted && ready.status == PresentationStatus::Ready
            && suboptimal.accepted && suboptimal.status == PresentationStatus::ResizeRequired
            && acquireSuboptimal.accepted && acquireSuboptimal.status == PresentationStatus::ResizeRequired
            && !outdated.accepted && outdated.status == PresentationStatus::ResizeRequired, "Native present acceptance inferred from status.");
        PresentationScheduler pacer; const PresentationScheduler::Time now{};
        pacer.Configure({60, 60, PresentMode::Fifo, PacingAuthority::Generic});
        pacer.Presented(ready, now); const auto deadline = pacer.Deadline(now);
        pacer.Presented(suboptimal, now);
        Expect(pacer.PreviousAcceptedPresentId() == 2 && pacer.Deadline(now) > deadline, "Accepted resize reset deadline/ID.");
        pacer.Presented(outdated, now);
        Expect(pacer.PreviousAcceptedPresentId() == 2 && pacer.Deadline(now) == now, "Rejected resize retained deadline or advanced ID.");
        for (const auto code : {VK_ERROR_SURFACE_LOST_KHR, VK_ERROR_DEVICE_LOST}) {
            bool caught = false; try { (void)NativePresentResult(code); }
            catch (const BackendError& error) { auto result = FailedPresent(error); caught = !result.accepted && result.failure->nativeCode == code; }
            Expect(caught, "Presentation loss lost typed native error.");
        }
        std::cout << "Vulkan feature probe PASS: requirements, optional features, queue selection, device scoring, enumeration, native-query boundaries\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
