#include "VulkanRgbTransfer.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include "../ResourceStatePolicy.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    namespace
    {
        VkDeviceSize Multiply(VkDeviceSize left, VkDeviceSize right)
        {
            if (right && left > std::numeric_limits<VkDeviceSize>::max() / right)
                throw std::out_of_range("RGB transfer size overflows.");
            return left * right;
        }
        VkDeviceSize Add(VkDeviceSize left, VkDeviceSize right)
        {
            if (right > std::numeric_limits<VkDeviceSize>::max() - left)
                throw std::out_of_range("RGB transfer offset overflows.");
            return left + right;
        }
    }
    VulkanRgbTransfer VulkanRgbTransfer::Describe(const TextureDesc& texture, VkDeviceSize bufferBytes, const BufferTextureCopy& region)
    {
        if (!IsRgbTextureFormat(texture.format) || !texture.width || !texture.height || !texture.depth || texture.sampleCount != 1
            || region.mipLevel >= texture.mipLevels || region.arrayLayer >= texture.arrayLayers
            || (region.aspect != TextureAspect::Color && region.aspect != TextureAspect::Automatic))
            throw std::invalid_argument("Invalid RGB transfer format, sample count, subresource or aspect.");
        if (!region.width || !region.height || !region.depth
            || region.x > std::numeric_limits<std::int32_t>::max() || region.y > std::numeric_limits<std::int32_t>::max()
            || region.z > std::numeric_limits<std::int32_t>::max()
            || (texture.depth == 1 && (region.z || region.depth != 1))
            || (texture.depth > 1 && region.arrayLayer))
            throw std::invalid_argument("Invalid RGB transfer dimensions.");
        const auto mip = [level = region.mipLevel](std::uint32_t value) { return level >= 32 ? 1U : std::max(1U, value >> level); };
        if (region.x > mip(texture.width) || region.width > mip(texture.width) - region.x
            || region.y > mip(texture.height) || region.height > mip(texture.height) - region.y
            || region.z > mip(texture.depth) || region.depth > mip(texture.depth) - region.z)
            throw std::out_of_range("RGB transfer is outside its image mip.");
        VulkanRgbTransfer result{region};
        if (texture.format == TextureFormat::RGB32Float)
        { result.LogicalBytes = 12; result.NativeBytes = 16; result.AlphaWord = 0x3F800000U; }
        const auto rowBytes = Multiply(region.width, result.LogicalBytes);
        result.RowPitch = region.bytesPerRow ? region.bytesPerRow : rowBytes;
        if (result.RowPitch < rowBytes || result.RowPitch % result.LogicalBytes
            || (region.rowsPerImage && region.rowsPerImage < region.height))
            throw std::invalid_argument("Invalid RGB transfer pitch.");
        result.SlicePitch = Multiply(result.RowPitch, region.rowsPerImage ? region.rowsPerImage : region.height);
        const auto end = Add(region.bufferOffset, Add(Multiply(region.depth - 1, result.SlicePitch),
            Add(Multiply(region.height - 1, result.RowPitch), rowBytes)));
        if (end > bufferBytes) throw std::out_of_range("RGB transfer exceeds its buffer.");
        result.ScratchBytes = Multiply(Multiply(Multiply(region.width, region.height), region.depth), result.NativeBytes);
        return result;
    }
    VkBufferImageCopy VulkanRgbTransfer::ImageCopy(VkDeviceSize scratchOffset) const
    {
        if (scratchOffset % NativeBytes) throw std::invalid_argument("RGB native scratch offset must be texel aligned.");
        (void)Add(scratchOffset, ScratchBytes);
        VkBufferImageCopy result{}; result.bufferOffset = scratchOffset;
        result.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, Region.mipLevel, Region.arrayLayer, 1};
        result.imageOffset = {static_cast<std::int32_t>(Region.x), static_cast<std::int32_t>(Region.y), static_cast<std::int32_t>(Region.z)};
        result.imageExtent = {Region.width, Region.height, Region.depth}; return result;
    }
    void VulkanRgbTransfer::BufferCopies(VkDeviceSize scratchOffset, bool upload,
        const std::function<void(std::span<const VkBufferCopy>)>& emit) const
    {
        (void)ImageCopy(scratchOffset);
        std::array<VkBufferCopy, 256> batch{}; std::size_t count = 0;
        for (std::uint32_t z = 0; z < Region.depth; ++z)
            for (std::uint32_t y = 0; y < Region.height; ++y)
                for (std::uint32_t x = 0; x < Region.width; ++x)
                {
                    const auto packed = Region.bufferOffset + z * SlicePitch + y * RowPitch + VkDeviceSize{x} * LogicalBytes;
                    const auto rgba = scratchOffset + ((VkDeviceSize{z} * Region.height + y) * Region.width + x) * NativeBytes;
                    batch[count++] = upload ? VkBufferCopy{packed, rgba, LogicalBytes} : VkBufferCopy{rgba, packed, LogicalBytes};
                    if (count == batch.size()) { emit(batch); count = 0; }
                }
        if (count) emit(std::span(batch.data(), count));
    }
}
#endif
