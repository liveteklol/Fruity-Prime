#pragma once
#include "../Swapchain.hpp"
#include "VulkanResult.hpp"
namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    inline PresentResult NativePresentResult(VkResult result, bool recreateAfterAcquire = false)
    {
        if (result == VK_ERROR_OUT_OF_DATE_KHR) return {PresentationStatus::ResizeRequired};
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) Check(result, "vkQueuePresentKHR"); // Retains typed surface/device loss.
        return {result == VK_SUBOPTIMAL_KHR || recreateAfterAcquire
            ? PresentationStatus::ResizeRequired : PresentationStatus::Ready, std::nullopt, true};
    }
}
