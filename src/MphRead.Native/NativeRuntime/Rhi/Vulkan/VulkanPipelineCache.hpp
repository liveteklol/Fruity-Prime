#pragma once

#if defined(FRUITY_HAS_VULKAN)
#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <vector>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // Driver compilation data, independent of semantic pipeline object reuse.
    // A corrupt or unavailable cache changes compile cost, never eligibility.
    class VulkanPipelineCache final
    {
    public:
        struct Dispatch final
        {
            VkDevice Device = VK_NULL_HANDLE;
            PFN_vkCreatePipelineCache Create = nullptr;
            PFN_vkDestroyPipelineCache Destroy = nullptr;
            PFN_vkGetPipelineCacheData Data = nullptr;
            PFN_vkCreateGraphicsPipelines Pipelines = nullptr;
        };
        struct Statistics final
        {
            bool Native = false;
            bool Loaded = false; // Accepted initial data; not proof of a hit.
            std::size_t LoadedBytes = 0, SavedBytes = 0;
            std::uint64_t CachedCreations = 0, UncachedCreations = 0;
            // Wall time inside vkCreateGraphicsPipelines, all creations: what a
            // warm cache saves is read off this, cold run against warm run.
            std::uint64_t CreationNanoseconds = 0;
        };
        static constexpr std::size_t MaximumPayload = 64U * 1024U * 1024U;
        VulkanPipelineCache(Dispatch dispatch, const VkPhysicalDeviceProperties& identity,
            std::filesystem::path file = {});
        ~VulkanPipelineCache() { Close(); }
        VulkanPipelineCache(const VulkanPipelineCache&) = delete;
        VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;
        VkResult CreatePipeline(const VkGraphicsPipelineCreateInfo& info, VkPipeline& pipeline);
        void Close() noexcept;
        [[nodiscard]] Statistics Stats() const;

        // Application framing protects the opaque payload before native use.
        [[nodiscard]] static std::vector<std::byte> Frame(std::span<const std::byte> payload,
            const VkPhysicalDeviceProperties& identity);
        [[nodiscard]] static std::vector<std::byte> Unframe(std::span<const std::byte> file,
            const VkPhysicalDeviceProperties& identity);
    private:
        void Save();
        Dispatch _dispatch;
        VkPhysicalDeviceProperties _identity;
        std::filesystem::path _file;
        VkPipelineCache _cache = VK_NULL_HANDLE;
        mutable std::mutex _mutex;
        Statistics _stats;
        bool _closed = false;
    };
}
#endif
