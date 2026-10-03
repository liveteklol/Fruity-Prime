#pragma once

#if defined(FRUITY_HAS_VULKAN)
#include "../MemoryBudget.hpp"
#include <vk_mem_alloc.h>
#include <mutex>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // Sole VMA owner and allocation admission boundary. Resources may borrow
    // Allocator() for transfers/destruction, but must close before this owner.
    class VulkanMemory final
    {
    public:
        struct Dispatch final
        {
            PFN_vkGetDeviceBufferMemoryRequirements BufferRequirements;
            PFN_vkGetDeviceImageMemoryRequirements ImageRequirements;
            PFN_vkGetPhysicalDeviceMemoryProperties2 MemoryProperties;
            void (*CheckResult)(VkResult, const char*);
        };
        struct Limits final
        {
            std::uint64_t MaxAllocationSize, MaxAllocationCount, MaxBufferSize;
            bool LiveBudget;
        };
        VulkanMemory(const VmaAllocatorCreateInfo& create, Dispatch dispatch, Limits limits);
        ~VulkanMemory() { Close(); }
        VulkanMemory(const VulkanMemory&) = delete;
        VulkanMemory& operator=(const VulkanMemory&) = delete;
        [[nodiscard]] VmaAllocator Allocator() const noexcept { return _allocator; }
        [[nodiscard]] MemoryBudgetSnapshot Snapshot() const;
        [[nodiscard]] MemoryTelemetry Telemetry() const;
        void CreateBuffer(const VkBufferCreateInfo& create, const VmaAllocationCreateInfo& allocation,
            VkBuffer& buffer, VmaAllocation& memory, VmaAllocationInfo* info = nullptr);
        void CreateImage(const VkImageCreateInfo& create, const VmaAllocationCreateInfo& allocation,
            VkImage& image, VmaAllocation& memory);
        // Diagnostic admission ceiling, applied to snapshots before native
        // allocation. Not a driver OOM injection or a product setting.
        void SetBudgetCeilingForCheck(std::uint64_t bytes);
        // Caller completes native work and quiesces allocation calls first.
        void Close() noexcept;
    private:
        struct Reservation final
        {
            VulkanMemory& Owner;
            std::uint32_t Heap;
            std::uint64_t Bytes;
            VmaAllocationCreateInfo Allocation;
            bool Failed = false;
            ~Reservation();
            Reservation(const Reservation&) = delete;
            Reservation(VulkanMemory& owner, std::uint32_t heap, std::uint64_t bytes, VmaAllocationCreateInfo allocation)
                : Owner(owner), Heap(heap), Bytes(bytes), Allocation(allocation) {}
        };
        template<class Find> Reservation Reserve(VkMemoryRequirements requirements,
            VmaAllocationCreateInfo allocation, Find&& find);
        MemoryBudgetSnapshot SnapshotLocked() const;
        void Finish(std::uint32_t heap, std::uint64_t bytes, bool failed) noexcept;
        Dispatch _dispatch;
        Limits _limits;
        VkDevice _device;
        VkPhysicalDevice _physical;
        VmaAllocator _allocator = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties _properties{};
        mutable std::mutex _mutex;
        MemoryTelemetry _telemetry{};
        std::array<std::uint64_t, VK_MAX_MEMORY_HEAPS> _pending{};
        std::uint64_t _checkCeiling = 0;
    };
}
#endif
