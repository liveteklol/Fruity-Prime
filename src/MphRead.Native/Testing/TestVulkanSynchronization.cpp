#include "../NativeRuntime/Rhi/Vulkan/VulkanSynchronization.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace MphRead::NativeRuntime::Rhi;
    using namespace MphRead::NativeRuntime::Rhi::Vulkan;
    void Expect(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
    template<class F> void Reject(F action)
    {
        bool rejected = false;
        try { action(); } catch (const std::invalid_argument&) { rejected = true; }
        Expect(rejected, "Invalid synchronization state was mapped.");
    }
    void CheckMappings()
    {
        const auto empty = ToVkState(ResourceState::Undefined, true);
        Expect(empty.Layout == VK_IMAGE_LAYOUT_UNDEFINED && !empty.Stages && !empty.Access,
            "Undefined must not invent prior GPU access.");
        const auto present = ToVkState(ResourceState::Present, true);
        Expect(present.Layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && !present.Stages && !present.Access,
            "Present ownership is external to the graphics access mask.");
        const auto common = ToVkState(ResourceState::Common, true);
        Expect(common.Layout == VK_IMAGE_LAYOUT_GENERAL && common.Stages == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
            && common.Access == (VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT),
            "Common must cover unknown prior access.");
        const auto sampled = ToVkState(ResourceState::ShaderRead, true);
        const auto storage = ToVkState(ResourceState::ShaderRead, true, true);
        Expect(sampled.Layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            && storage.Layout == VK_IMAGE_LAYOUT_GENERAL && !(storage.Access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT)
            && (storage.Access & VK_ACCESS_2_SHADER_STORAGE_READ_BIT), "Sampled/storage-only read layouts differ.");
        const auto mixed = ToVkState(ResourceState::ShaderRead | ResourceState::CopySrc, true);
        Expect(mixed.Layout == VK_IMAGE_LAYOUT_GENERAL
            && (mixed.Stages & VK_PIPELINE_STAGE_2_TRANSFER_BIT) && (mixed.Access & VK_ACCESS_2_TRANSFER_READ_BIT)
            && (mixed.Access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT), "Sampled/copy combined access was narrowed.");
        const auto depthSample = ToVkState(ResourceState::DepthStencilRead | ResourceState::ShaderRead, true);
        Expect(depthSample.Layout == VK_IMAGE_LAYOUT_GENERAL
            && (depthSample.Access & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT)
            && (depthSample.Access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT), "Depth/sample combined access was narrowed.");

        struct Write { ResourceState State; VkImageLayout Layout; VkAccessFlags2 Access; };
        for (const auto& write : std::array{
            Write{ResourceState::CopyDst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_2_TRANSFER_WRITE_BIT},
            Write{ResourceState::ColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT},
            Write{ResourceState::DepthStencilWrite, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
            Write{ResourceState::ShaderWrite, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT}})
        {
            const auto mapped = ToVkState(write.State, true);
            Expect(mapped.Layout == write.Layout && (mapped.Access & write.Access), "Exclusive write layout/access is incorrect.");
        }
        Expect(ToVkState(ResourceState::CopySrc, true).Layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            "Transfer-only reads must use their specialized layout.");
        Expect(ToVkState(ResourceState::DepthStencilRead, true).Layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            "Depth-only reads must use their specialized layout.");
        for (const auto state : {ResourceState::Undefined, ResourceState::CopySrc, ResourceState::CopyDst, ResourceState::ShaderWrite})
            Expect(ToVkSampledDescriptorLayout(state) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                "Preparatory binding promised an invalid sampled descriptor layout.");
        Expect(ToVkSampledDescriptorLayout(ResourceState::ShaderRead) == sampled.Layout
            && ToVkSampledDescriptorLayout(ResourceState::Common) == VK_IMAGE_LAYOUT_GENERAL
            && ToVkSampledDescriptorLayout(ResourceState::ShaderRead | ResourceState::CopySrc) == VK_IMAGE_LAYOUT_GENERAL,
            "Readable binding lost the actual sampled image layout.");

        // Every known-bit combination is accepted/rejected before dispatch,
        // independently of descriptor usage validation and physical hardware.
        for (unsigned bits = 0; bits < 4096; ++bits)
        {
            const auto state = static_cast<ResourceState>(bits);
            for (const bool image : {false, true})
            {
                const bool valid = image ? IsValidTextureState(state) : IsValidBufferState(state);
                if (!valid) { Reject([&] { (void)ToVkState(state, image); }); continue; }
                const auto mapped = ToVkState(state, image);
                if (state != ResourceState::Undefined && state != ResourceState::Present)
                    Expect(mapped.Stages != VK_PIPELINE_STAGE_2_NONE, "Usable state has no synchronization stage.");
                if (!HasSingleBit(state) && state != ResourceState::Undefined)
                    Expect(mapped.Layout == VK_IMAGE_LAYOUT_GENERAL, "Combined read state lost GENERAL layout.");
                if (state != ResourceState::Common && !HasAny(state, ResourceState::CopyDst | ResourceState::ShaderWrite
                    | ResourceState::ColorAttachment | ResourceState::DepthStencilWrite))
                    Expect(!(mapped.Access & (VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT
                        | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                        | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)), "Read-only state invented write access.");
            }
        }
        Reject([] { (void)ToVkState(static_cast<ResourceState>(1U << 31), true); });
    }
}

int main()
{
    try
    {
        CheckMappings();
        std::cout << "Vulkan synchronization policy PASS; 8192 known-bit type combinations; sampled/storage/combined read layouts; write access\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
