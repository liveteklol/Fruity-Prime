#pragma once

#if defined(FRUITY_HAS_VULKAN)
#include "../Bindings.hpp"
#include "../Submission.hpp"
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <map>
#include <cstdlib>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // One reusable submission slot's descriptor pages. Queue/fence ownership
    // stays with the caller; only actual queue completion permits recycling.
    // Host calls are confined to the slot's recording thread.
    class VulkanDescriptorAllocator final
    {
    public:
        struct Dispatch final
        {
            VkDevice Device;
            PFN_vkCreateDescriptorPool Create;
            PFN_vkDestroyDescriptorPool Destroy;
            PFN_vkResetDescriptorPool Reset;
            PFN_vkAllocateDescriptorSets Allocate;
            void (*CheckResult)(VkResult, const char*);
        };
        struct Capacity final
        {
            std::uint32_t Sets = 1024;
            // Uniform buffer, storage buffer, sampled image, storage image,
            // sampler. These are native pool counts, not logical bindings.
            std::array<std::uint32_t, 5> Counts{4096, 4096, 4096, 4096, 4096};
        };
        VulkanDescriptorAllocator(Dispatch dispatch, Capacity capacity);
        ~VulkanDescriptorAllocator() { Close(); }
        VulkanDescriptorAllocator(const VulkanDescriptorAllocator&) = delete;
        VulkanDescriptorAllocator& operator=(const VulkanDescriptorAllocator&) = delete;

        VkDescriptorSet Allocate(VkDescriptorSetLayout layout, const BindingLayoutDesc& desc);
        // Fixed scene ABI slots: native batch allocation at program admission,
        // cursor recycling only after this owner slot's queue completion.
        bool Preallocate(std::uint64_t identity, VkDescriptorSetLayout layout,
            const BindingLayoutDesc& desc, std::uint32_t count);
        VkDescriptorSet AllocateFixed(std::uint64_t identity);
        void RetireFixed(std::uint64_t identity);
        void Submitted(SubmissionSerial serial);
        // Caller first finishes/discards any unsubmitted recording. Does not
        // wait or reset a fence; rejects a still-pending submitted generation.
        void ResetAfterCompletion(SubmissionSerial completed);
        // Owner drains/discards the slot before closing and before VkDevice
        // destruction. Late wrapper destruction is inert after explicit close.
        void Close() noexcept;
        [[nodiscard]] std::size_t PageCount() const noexcept { return _pages.size(); }
        [[nodiscard]] SubmissionSerial LastUse() const noexcept { return _lastUse; }
    private:
        struct Page final
        {
            VkDescriptorPool Pool = VK_NULL_HANDLE;
            std::uint32_t Sets = 0;
            std::array<std::uint64_t, 5> Used{};
            std::array<std::uint32_t, 5> Capacity{};
        };
        using Counts = std::array<std::uint64_t, 5>;
        static Counts Requirements(const BindingLayoutDesc& desc);
        void AddPage(const Counts& needed);
        void RequireOpen() const;
        Dispatch _dispatch;
        Capacity _capacity;
        std::vector<Page> _pages;
        struct FixedPage final
        {
            VkDescriptorPool Pool = VK_NULL_HANDLE;
            std::vector<VkDescriptorSet> Sets;
            std::uint32_t Cursor = 0;
            bool Retired = false;
            std::uint32_t HighWater = 0;
        };
        std::map<std::uint64_t, FixedPage> _fixed;
        bool _metrics = std::getenv("FRUITY_RENDER_METRICS") != nullptr;
        std::uint64_t _fixedPools = 0, _fixedReserved = 0, _fixedHighWater = 0;
        std::uint64_t _fixedFailures = 0, _fixedOverflow = 0, _fixedSetupNs = 0;
        std::size_t _active = 0;
        SubmissionSerial _lastUse{};
        bool _ready = false, _closed = false;
    };
}
#endif
