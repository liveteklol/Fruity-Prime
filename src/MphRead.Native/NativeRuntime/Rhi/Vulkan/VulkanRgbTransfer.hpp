#pragma once
#if defined(FRUITY_HAS_VULKAN)
#include "../CommandList.hpp"
#include <vulkan/vulkan.h>
#include <functional>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // Address-only GPU transfer plan for RGB8/RGB32F and native RGBA8/RGBA32F.
    // Buffer-copy regions gather/scatter logical RGB without CPU pixel copies.
    struct VulkanRgbTransfer final
    {
        BufferTextureCopy Region;
        VkDeviceSize RowPitch{}, SlicePitch{}, ScratchBytes{};
        std::uint32_t LogicalBytes = 3, NativeBytes = 4, AlphaWord = 0xFF000000U;
        static VulkanRgbTransfer Describe(const TextureDesc&, VkDeviceSize bufferBytes, const BufferTextureCopy&);
        [[nodiscard]] VkBufferImageCopy ImageCopy(VkDeviceSize scratchOffset) const;
        void BufferCopies(VkDeviceSize scratchOffset, bool upload,
            const std::function<void(std::span<const VkBufferCopy>)>& emit) const;
    };
}
#endif
