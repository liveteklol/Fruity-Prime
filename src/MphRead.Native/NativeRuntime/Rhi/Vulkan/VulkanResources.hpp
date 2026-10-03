#pragma once

#if defined(FRUITY_HAS_VULKAN)
#include "../Capabilities.hpp"
#include "../Resources.hpp"
#include <vk_mem_alloc.h>
#include <functional>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // Native creation policy, independent of recording, submission and public
    // resource wrappers. VMA admission belongs to VulkanMemory; successful
    // values are adopted by wrappers/upload arenas and retired by their owner.
    class VulkanResources final
    {
    public:
        struct BufferStorage final
        {
            VkBuffer Buffer = VK_NULL_HANDLE;
            VmaAllocation Allocation = VK_NULL_HANDLE;
            std::byte* Mapped = nullptr;
        };
        struct ImageStorage final
        {
            VkImage Image = VK_NULL_HANDLE;
            VmaAllocation Allocation = VK_NULL_HANDLE;
        };
        struct Dispatch final
        {
            VkPhysicalDevice Physical;
            VkDevice Device;
            PFN_vkGetPhysicalDeviceImageFormatProperties ImageProperties;
            PFN_vkGetPhysicalDeviceFormatProperties FormatProperties;
            PFN_vkCreateImageView CreateView;
            PFN_vkDestroyImageView DestroyView;
            PFN_vkCreateSampler CreateSampler;
            PFN_vkDestroySampler DestroySampler;
            void (*CheckResult)(VkResult, const char*);
            // Allocate either returns complete storage or cleans up and throws.
            std::function<BufferStorage(const VkBufferCreateInfo&, const VmaAllocationCreateInfo&)> AllocateBuffer;
            std::function<void(BufferStorage)> DestroyBuffer;
            std::function<ImageStorage(const VkImageCreateInfo&, const VmaAllocationCreateInfo&)> AllocateImage;
            std::function<void(ImageStorage)> DestroyImage;
            std::function<void(VkObjectType, std::uint64_t, const char*)> Name;
        };
        VulkanResources(Dispatch dispatch, Capabilities caps);
        VulkanResources(const VulkanResources&) = delete;
        VulkanResources& operator=(const VulkanResources&) = delete;
        [[nodiscard]] BufferStorage CreateBuffer(const BufferDesc& desc) const;
        [[nodiscard]] ImageStorage CreateImage(const TextureDesc& desc) const;
        [[nodiscard]] VkSampler CreateSampler(const SamplerDesc& desc) const;
        [[nodiscard]] VkImageView CreateView(VkImage image, const TextureDesc& texture,
            const TextureViewDesc& view, bool sampled = false) const;
        [[nodiscard]] static TextureViewDesc ResolveViewDesc(const TextureDesc& texture, TextureViewDesc view);
        [[nodiscard]] static VkFormat Format(TextureFormat format);
        [[nodiscard]] static VkImageAspectFlags AspectMask(TextureFormat format);
        [[nodiscard]] static VkImageUsageFlags ImageUsage(TextureUsage usage);
        void Close() noexcept { _closed = true; _dispatch = {}; }
    private:
        void RequireOpen() const;
        Dispatch _dispatch;
        Capabilities _caps;
        bool _closed = false;
    };
}
#endif
