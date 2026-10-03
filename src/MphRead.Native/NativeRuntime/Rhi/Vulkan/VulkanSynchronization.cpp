#include "VulkanSynchronization.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include <stdexcept>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    VkImageLayout ToVkSampledDescriptorLayout(ResourceState state)
    {
        const auto mapped = ToVkState(state, true);
        return state == ResourceState::Common || HasAny(state, ResourceState::ShaderRead | ResourceState::DepthStencilRead)
            ? mapped.Layout : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    // All Vulkan resource-state to synchronization2 mappings live here.
    [[nodiscard]] StateMapping ToVkState(ResourceState state, bool image, bool storageOnly)
    {
        if (!IsValidResourceState(state))
            throw std::invalid_argument("Vulkan RHI: invalid resource-state combination.");
        if (state == ResourceState::Undefined)
            return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
        if (state == ResourceState::Present)
        {
            if (!image) throw std::invalid_argument("Vulkan RHI: Present is valid only for images.");
            return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
        }

        StateMapping result{};
        const auto has = [state](ResourceState flag) { return HasAny(state, flag); };
        constexpr ResourceState bufferOnly = ResourceState::VertexBuffer
            | ResourceState::IndexBuffer | ResourceState::ConstantBuffer;
        constexpr ResourceState imageOnly = ResourceState::ColorAttachment
            | ResourceState::DepthStencilRead | ResourceState::DepthStencilWrite;
        if ((image && HasAny(state, bufferOnly)) || (!image && HasAny(state, imageOnly)))
            throw std::invalid_argument("Vulkan RHI: resource state does not apply to this resource type.");
        if (has(ResourceState::Common))
        {
            result.Stages |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            result.Access |= VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        }
        if (has(ResourceState::VertexBuffer))
        {
            result.Stages |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            result.Access |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        }
        if (has(ResourceState::IndexBuffer))
        {
            result.Stages |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            result.Access |= VK_ACCESS_2_INDEX_READ_BIT;
        }
        if (has(ResourceState::ConstantBuffer))
        {
            result.Stages |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
            result.Access |= VK_ACCESS_2_UNIFORM_READ_BIT;
        }
        if (has(ResourceState::ShaderRead))
        {
            result.Stages |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
            result.Access |= image && !storageOnly ? (VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                | VK_ACCESS_2_SHADER_STORAGE_READ_BIT) : VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        }
        if (has(ResourceState::ShaderWrite))
        {
            result.Stages |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
            result.Access |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        }
        if (has(ResourceState::ColorAttachment))
        {
            result.Stages |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            result.Access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            result.Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        if (has(ResourceState::DepthStencilRead))
        {
            result.Stages |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            result.Access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            result.Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        if (has(ResourceState::DepthStencilWrite))
        {
            result.Stages |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            result.Access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            result.Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        if (has(ResourceState::CopySrc))
        {
            result.Stages |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            result.Access |= VK_ACCESS_2_TRANSFER_READ_BIT;
        }
        if (has(ResourceState::CopyDst))
        {
            result.Stages |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            result.Access |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
        }

        // Sampled-only reads use the specialized layout. Storage-only
        // reads and combined read states use GENERAL.
        if (image && state == ResourceState::ShaderRead && !storageOnly)
            result.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        else if (image && state == ResourceState::CopySrc)
            result.Layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        else if (image && state == ResourceState::CopyDst)
            result.Layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        else if (image && state != ResourceState::ColorAttachment
            && state != ResourceState::DepthStencilRead
            && state != ResourceState::DepthStencilWrite
            && state != ResourceState::Common)
        {
            result.Layout = VK_IMAGE_LAYOUT_GENERAL;
        }
        if (result.Stages == VK_PIPELINE_STAGE_2_NONE)
            throw std::invalid_argument("Vulkan RHI: resource state has no pipeline stage mapping.");
        return result;
    }
}
#endif
