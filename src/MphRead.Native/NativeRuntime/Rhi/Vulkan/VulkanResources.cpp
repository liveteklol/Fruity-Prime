#include "VulkanResources.hpp"

#if defined(FRUITY_HAS_VULKAN)
#include "../ResourceStatePolicy.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    namespace
    {
        [[nodiscard]] bool Has(BufferUsage value, BufferUsage flag) noexcept
        {
            return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] bool Has(TextureUsage value, TextureUsage flag) noexcept
        {
            return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
        }

        [[nodiscard]] VkSampleCountFlagBits ToVkSamples(std::uint32_t count)
        {
            switch (count)
            {
            case 1: return VK_SAMPLE_COUNT_1_BIT;
            case 2: return VK_SAMPLE_COUNT_2_BIT;
            case 4: return VK_SAMPLE_COUNT_4_BIT;
            case 8: return VK_SAMPLE_COUNT_8_BIT;
            case 16: return VK_SAMPLE_COUNT_16_BIT;
            case 32: return VK_SAMPLE_COUNT_32_BIT;
            case 64: return VK_SAMPLE_COUNT_64_BIT;
            default: throw std::invalid_argument("Vulkan RHI: invalid texture sample count.");
            }
        }

        [[nodiscard]] VkBufferUsageFlags ToVkBufferUsage(BufferUsage usage)
        {
            VkBufferUsageFlags result = 0;
            if (Has(usage, BufferUsage::Vertex)) result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            if (Has(usage, BufferUsage::Index)) result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            if (Has(usage, BufferUsage::Uniform)) result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            if (Has(usage, BufferUsage::Storage)) result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            if (Has(usage, BufferUsage::TransferSrc)) result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            if (Has(usage, BufferUsage::TransferDst)) result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            return result;
        }

        [[nodiscard]] VkImageUsageFlags ToVkImageUsage(TextureUsage usage)
        {
            VkImageUsageFlags result = 0;
            if (Has(usage, TextureUsage::Sampled)) result |= VK_IMAGE_USAGE_SAMPLED_BIT;
            if (Has(usage, TextureUsage::Storage)) result |= VK_IMAGE_USAGE_STORAGE_BIT;
            if (Has(usage, TextureUsage::ColorAttachment)) result |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            if (Has(usage, TextureUsage::DepthStencilAttachment))
                result |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if (Has(usage, TextureUsage::TransferSrc)) result |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            if (Has(usage, TextureUsage::TransferDst)) result |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            return result;
        }

        [[nodiscard]] VmaAllocationCreateInfo ToVmaAllocation(MemoryUsage usage)
        {
            VmaAllocationCreateInfo result{};
            result.usage = VMA_MEMORY_USAGE_AUTO;
            if (usage == MemoryUsage::CpuToGpu)
            {
                result.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            }
            else if (usage == MemoryUsage::GpuToCpu)
            {
                result.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            }
            return result;
        }
    }

    VulkanResources::VulkanResources(Dispatch dispatch, Capabilities caps)
        : _dispatch(std::move(dispatch)), _caps(caps)
    {
        if (!_dispatch.Physical || !_dispatch.Device || !_dispatch.ImageProperties || !_dispatch.FormatProperties
            || !_dispatch.CreateView || !_dispatch.DestroyView || !_dispatch.CreateSampler || !_dispatch.DestroySampler
            || !_dispatch.CheckResult || !_dispatch.AllocateBuffer || !_dispatch.DestroyBuffer
            || !_dispatch.AllocateImage || !_dispatch.DestroyImage)
            throw std::invalid_argument("Vulkan resources require complete native creation/allocation dispatch.");
    }

    void VulkanResources::RequireOpen() const
    { if (_closed) throw std::logic_error("The Vulkan resource factory has closed."); }

    VkFormat VulkanResources::Format(TextureFormat format)
    {

        switch (format)
        {
        case TextureFormat::R8Unorm: return VK_FORMAT_R8_UNORM;
        case TextureFormat::RG8Unorm: return VK_FORMAT_R8G8_UNORM;
        // Logical RGB8 uses renderable RGBA8 storage with alpha held at one
        // (sampled views swizzle it, pipelines mask it).
        case TextureFormat::RGB8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::BGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::BGRA8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
        case TextureFormat::R16Float: return VK_FORMAT_R16_SFLOAT;
        case TextureFormat::RG16Float: return VK_FORMAT_R16G16_SFLOAT;
        case TextureFormat::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::R32Float: return VK_FORMAT_R32_SFLOAT;
        case TextureFormat::RG32Float: return VK_FORMAT_R32G32_SFLOAT;
        case TextureFormat::RGB32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TextureFormat::RGBA32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TextureFormat::D16Unorm: return VK_FORMAT_D16_UNORM;
        case TextureFormat::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::D32Float: return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::D32FloatS8Uint: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case TextureFormat::Undefined: break;
        }
        throw std::invalid_argument("Vulkan RHI: undefined or unsupported texture format.");

    }

    VkImageAspectFlags VulkanResources::AspectMask(TextureFormat format)
    {

        switch (format)
        {
        case TextureFormat::D16Unorm:
        case TextureFormat::D32Float:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case TextureFormat::D24UnormS8Uint:
        case TextureFormat::D32FloatS8Uint:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        case TextureFormat::Undefined:
            throw std::invalid_argument("Vulkan RHI: an image view needs a defined format.");
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }

    }

    VkImageUsageFlags VulkanResources::ImageUsage(TextureUsage usage)
    { return ToVkImageUsage(usage); }

    VulkanResources::BufferStorage VulkanResources::CreateBuffer(const BufferDesc& desc) const
    {
        RequireOpen();
        if (desc.size == 0 || desc.usage == BufferUsage::None || !IsValidBufferState(desc, desc.initialState))
            throw std::invalid_argument("Vulkan RHI: buffers need a nonzero size and compatible usage/state.");
        VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        create.size = desc.size;
        create.usage = ToVkBufferUsage(desc.usage);
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        auto allocation = ToVmaAllocation(desc.memoryUsage);
        if (desc.memoryUsage == MemoryUsage::CpuToGpu) allocation.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        auto storage = _dispatch.AllocateBuffer(create, allocation);
        if (desc.memoryUsage == MemoryUsage::CpuToGpu && !storage.Mapped)
        {
            _dispatch.DestroyBuffer(storage);
            throw std::runtime_error("Vulkan upload buffer is not persistently mapped.");
        }
        return storage;
    }

    VulkanResources::ImageStorage VulkanResources::CreateImage(const TextureDesc& desc) const
    {
        RequireOpen();
        if (desc.width == 0 || desc.height == 0 || desc.depth == 0
            || desc.mipLevels == 0 || desc.arrayLayers == 0 || desc.usage == TextureUsage::None
            || !IsValidTextureState(desc, desc.initialState))
            throw std::invalid_argument("Vulkan RHI: textures need nonzero extents/subresources and compatible usage/format/state.");
        if (desc.depth > 1 && desc.arrayLayers != 1)
            throw std::invalid_argument("Vulkan RHI: 3D texture arrays are not supported.");
        if (desc.memoryUsage != MemoryUsage::GpuOnly)
            throw std::invalid_argument("Vulkan RHI: images must use GPU-only memory; use a buffer for host access.");
        if (desc.width > _caps.maxTexture2DDimension || desc.height > _caps.maxTexture2DDimension
            || desc.arrayLayers > _caps.maxTextureArrayLayers || desc.mipLevels > _caps.maxTextureMipLevels
            || (desc.depth > 1 && desc.depth > _caps.maxTexture3DDimension))
            throw std::out_of_range("Vulkan RHI: texture extent or layer count exceeds device limits.");
        std::uint32_t maxExtent = std::max({desc.width, desc.height, desc.depth});
        std::uint32_t maxMipLevels = 1;
        while (maxExtent > 1)
        {
            maxExtent >>= 1;
            ++maxMipLevels;
        }
        if (desc.mipLevels > maxMipLevels)
            throw std::out_of_range("Vulkan RHI: texture mip count exceeds its extent.");
        if (desc.sampleCount != 1
            && (Has(desc.usage, TextureUsage::TransferSrc)
                || Has(desc.usage, TextureUsage::TransferDst)))
            throw std::invalid_argument("Vulkan RHI: multisampled images cannot be transfer resources.");
        const VkFormat format = Format(desc.format);
        VkImageFormatProperties imageProperties{};
        _dispatch.CheckResult(_dispatch.ImageProperties(_dispatch.Physical, format,
            desc.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_OPTIMAL, ImageUsage(desc.usage), 0, &imageProperties),
            "vkGetPhysicalDeviceImageFormatProperties");
        if (desc.width > imageProperties.maxExtent.width
            || desc.height > imageProperties.maxExtent.height
            || desc.depth > imageProperties.maxExtent.depth
            || desc.arrayLayers > imageProperties.maxArrayLayers
            || desc.mipLevels > imageProperties.maxMipLevels
            || (imageProperties.sampleCounts & ToVkSamples(desc.sampleCount)) == 0)
            throw std::out_of_range("Vulkan RHI: image description exceeds format/type limits.");
        VkFormatProperties properties{};
        _dispatch.FormatProperties(_dispatch.Physical, format, &properties);
        VkFormatFeatureFlags required = 0;
        if (Has(desc.usage, TextureUsage::Sampled)) required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if (Has(desc.usage, TextureUsage::Storage)) required |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        if (Has(desc.usage, TextureUsage::ColorAttachment)) required |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        if (Has(desc.usage, TextureUsage::DepthStencilAttachment))
            required |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if ((properties.optimalTilingFeatures & required) != required)
            throw std::runtime_error("Vulkan RHI: the selected texture format lacks a requested image usage.");

        VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        create.imageType = desc.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        create.format = format;
        create.extent = {desc.width, desc.height, desc.depth};
        create.mipLevels = desc.mipLevels;
        create.arrayLayers = desc.arrayLayers;
        create.samples = ToVkSamples(desc.sampleCount);
        create.tiling = VK_IMAGE_TILING_OPTIMAL;
        create.usage = ImageUsage(desc.usage);
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        const VmaAllocationCreateInfo allocation = ToVmaAllocation(desc.memoryUsage);
        auto storage = _dispatch.AllocateImage(create, allocation);
        try { if (_dispatch.Name) _dispatch.Name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(storage.Image), "RHI texture"); }
        catch (...) { _dispatch.DestroyImage(storage); throw; }
        return storage;
    }

    VkSampler VulkanResources::CreateSampler(const SamplerDesc& desc) const
    {
        RequireOpen();
        const auto filter = [](Filter value)
        {
            return value == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        };
        const auto address = [](SamplerAddressMode value)
        {
            switch (value)
            {
            case SamplerAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case SamplerAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case SamplerAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case SamplerAddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            }
            throw std::invalid_argument("Vulkan RHI: invalid sampler address mode.");
        };
        VkSamplerCreateInfo create{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        create.magFilter = filter(desc.magFilter);
        create.minFilter = filter(desc.minFilter);
        create.mipmapMode = desc.mipFilter == Filter::Nearest
            ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
        create.addressModeU = address(desc.addressU);
        create.addressModeV = address(desc.addressV);
        create.addressModeW = address(desc.addressW);
        create.minLod = desc.minLod;
        create.maxLod = desc.maxLod;
        create.maxAnisotropy = 1.0F;
        if (desc.maxAnisotropy > 1.0F)
        {
            if (!_caps.supportsAnisotropy)
                throw std::runtime_error("Vulkan RHI: anisotropic filtering is unavailable.");
            create.anisotropyEnable = VK_TRUE;
            create.maxAnisotropy = std::min(desc.maxAnisotropy,
                _caps.maxSamplerAnisotropy);
        }
        switch (desc.borderColor)
        {
        case BorderColor::TransparentBlack:
            create.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK; break;
        case BorderColor::OpaqueBlack:
            create.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK; break;
        case BorderColor::OpaqueWhite:
            create.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; break;
        }
        VkSampler sampler = VK_NULL_HANDLE;
        _dispatch.CheckResult(_dispatch.CreateSampler(_dispatch.Device, &create, nullptr, &sampler), "vkCreateSampler");
        try { if (_dispatch.Name) _dispatch.Name(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<std::uint64_t>(sampler), "RHI sampler"); }
        catch (...) { _dispatch.DestroySampler(_dispatch.Device, sampler, nullptr); throw; }
        return sampler;
    }

    TextureViewDesc VulkanResources::ResolveViewDesc(const TextureDesc& texture, TextureViewDesc view)
    {
        if (view.format == TextureFormat::Undefined)
            view.format = texture.format;
        if (view.format != texture.format)
            throw std::invalid_argument("Vulkan RHI: texture view format reinterpretation is unsupported.");
        if (view.mipLevelCount == 0 || view.baseMipLevel >= texture.mipLevels
            || view.mipLevelCount > texture.mipLevels - view.baseMipLevel
            || view.arrayLayerCount == 0 || view.baseArrayLayer >= texture.arrayLayers
            || view.arrayLayerCount > texture.arrayLayers - view.baseArrayLayer)
            throw std::invalid_argument("Vulkan RHI: texture view range is outside the image.");
        return view;
    }

    VkImageView VulkanResources::CreateView(VkImage image, const TextureDesc& texture,
        const TextureViewDesc& view, bool sampled) const
    {
        RequireOpen();
        if (!image) throw std::invalid_argument("A Vulkan image view needs a live image.");
        const auto resolved = ResolveViewDesc(texture, view);
        VkImageViewCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        create.image = image;
        create.viewType = texture.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D
            : (texture.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
        create.format = Format(resolved.format);
        create.subresourceRange.aspectMask = AspectMask(resolved.format);
        create.subresourceRange.baseMipLevel = resolved.baseMipLevel;
        create.subresourceRange.levelCount = resolved.mipLevelCount;
        create.subresourceRange.baseArrayLayer = resolved.baseArrayLayer;
        create.subresourceRange.layerCount = resolved.arrayLayerCount;
        if (sampled)
        {
            if (create.subresourceRange.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
                create.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (IsRgbTextureFormat(texture.format)) create.components.a = VK_COMPONENT_SWIZZLE_ONE;
        }
        VkImageView result = VK_NULL_HANDLE;
        _dispatch.CheckResult(_dispatch.CreateView(_dispatch.Device, &create, nullptr, &result), "vkCreateImageView");
        try { if (_dispatch.Name) _dispatch.Name(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(result), "RHI texture view"); }
        catch (...) { _dispatch.DestroyView(_dispatch.Device, result, nullptr); throw; }
        return result;
    }
}
#endif
