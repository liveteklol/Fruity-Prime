#pragma once

#include "../ResourceState.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include <vulkan/vulkan.h>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    struct StateMapping final
    {
        VkImageLayout Layout = VK_IMAGE_LAYOUT_GENERAL;
        VkPipelineStageFlags2 Stages = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2 Access = VK_ACCESS_2_NONE;
    };

    // Pure backend mapping, shared by barriers, descriptors and interop.
    // No device, native dispatch, recording or session ownership is required.
    [[nodiscard]] StateMapping ToVkState(ResourceState state, bool image, bool storageOnly = false);
    // Binding may precede an explicit read transition. Such a descriptor
    // promises the normal sampled layout; draw validates readable access.
    [[nodiscard]] VkImageLayout ToVkSampledDescriptorLayout(ResourceState state);
}
#endif
