#include "VulkanMemory.hpp"

#if defined(FRUITY_HAS_VULKAN)
#include "../BackendError.hpp"
#include <string>
#include <stdexcept>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    VulkanMemory::VulkanMemory(const VmaAllocatorCreateInfo& create, Dispatch dispatch, Limits limits)
        : _dispatch(dispatch), _limits(limits), _device(create.device), _physical(create.physicalDevice)
    {
        if (!dispatch.BufferRequirements || !dispatch.ImageRequirements || !dispatch.MemoryProperties || !dispatch.CheckResult)
            throw std::invalid_argument("Missing Vulkan memory dispatch.");
        VkPhysicalDeviceMemoryProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
        _dispatch.MemoryProperties(_physical, &properties);
        _properties = properties.memoryProperties;
        _dispatch.CheckResult(vmaCreateAllocator(&create, &_allocator), "vmaCreateAllocator");
    }
    MemoryBudgetSnapshot VulkanMemory::SnapshotLocked() const
    {
        if (!_allocator) return {};
        std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
        vmaGetHeapBudgets(_allocator, budgets.data());
        VkPhysicalDeviceMemoryBudgetPropertiesEXT live{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
        VkPhysicalDeviceMemoryProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
        if (_limits.LiveBudget)
        {
            properties.pNext = &live;
            _dispatch.MemoryProperties(_physical, &properties);
        }
        MemoryBudgetSnapshot snapshot{};
        snapshot.HeapCount = _properties.memoryHeapCount;
        snapshot.MaxAllocationBytes = _limits.MaxAllocationSize;
        snapshot.MaxNativeAllocations = _limits.MaxAllocationCount;
        snapshot.PendingNativeAllocations = _telemetry.PendingRequests;
        for (std::uint32_t i = 0; i < snapshot.HeapCount; ++i)
        {
            auto& heap = snapshot.Heaps[i];
            heap.Source = _limits.LiveBudget ? MemoryBudgetSource::DriverLive : MemoryBudgetSource::Estimated;
            heap.CapacityBytes = _properties.memoryHeaps[i].size;
            heap.BudgetBytes = std::min(heap.CapacityBytes, _limits.LiveBudget ? live.heapBudget[i] : budgets[i].budget);
            heap.UsageBytes = _limits.LiveBudget ? live.heapUsage[i] : budgets[i].usage;
            heap.ReservedBytes = budgets[i].statistics.blockBytes;
            heap.PendingBytes = _pending[i];
            heap.DeviceLocal = (_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
            snapshot.NativeAllocationCount += budgets[i].statistics.blockCount;
            if (_checkCeiling)
            { heap.Source = MemoryBudgetSource::Estimated; heap.BudgetBytes = std::min(heap.BudgetBytes, _checkCeiling); }
        }
        return snapshot;
    }
    MemoryBudgetSnapshot VulkanMemory::Snapshot() const { std::lock_guard lock(_mutex); return SnapshotLocked(); }
    MemoryTelemetry VulkanMemory::Telemetry() const { std::lock_guard lock(_mutex); return _telemetry; }
    void VulkanMemory::SetBudgetCeilingForCheck(std::uint64_t bytes) { std::lock_guard lock(_mutex); _checkCeiling = bytes; }
    template<class Find> VulkanMemory::Reservation VulkanMemory::Reserve(VkMemoryRequirements requirements,
        VmaAllocationCreateInfo allocation, Find&& find)
    {
        std::unique_lock lock(_mutex);
        if (!_allocator) throw std::logic_error("Vulkan memory owner is closed.");
        auto bits = requirements.memoryTypeBits & (allocation.memoryTypeBits ? allocation.memoryTypeBits : UINT32_MAX);
        const auto snapshot = SnapshotLocked();
        AllocationDecision denied{};
        bool sawCompatible = false;
        while (bits)
        {
            allocation.memoryTypeBits = bits;
            std::uint32_t type = 0;
            const auto result = find(allocation, type);
            if (result != VK_SUCCESS)
            {
                if (sawCompatible && result == VK_ERROR_FEATURE_NOT_PRESENT) break;
                ++_telemetry.NativeFailures;
                _dispatch.CheckResult(result, "VMA memory type selection");
            }
            if (type >= _properties.memoryTypeCount || !(bits & (1U << type)))
                throw std::runtime_error("VMA selected an incompatible memory type.");
            const auto heap = _properties.memoryTypes[type].heapIndex;
            denied = EvaluateAllocation(snapshot, {requirements.size, heap});
            if (denied.Allowed && requirements.size > UINT64_MAX - _telemetry.PendingBytes)
                denied = {false, AllocationReason::ArithmeticOverflow};
            sawCompatible = true;
            if (denied.Allowed)
            {
                allocation.memoryTypeBits = 1U << type; // Native allocation uses the admitted heap.
                // VMA may reserve a larger backing block than this resource's
                // requirements. Keep its native block-level budget check too.
                allocation.flags |= VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
                _pending[heap] += requirements.size;
                _telemetry.PendingBytes += requirements.size;
                ++_telemetry.PendingRequests; ++_telemetry.AcceptedRequests;
                return Reservation{*this, heap, requirements.size, allocation};
            }
            // Never combine free bytes from independent heaps. Ask VMA for its
            // next compatible placement after excluding this exhausted heap.
            for (std::uint32_t i = 0; i < _properties.memoryTypeCount; ++i)
                if (_properties.memoryTypes[i].heapIndex == heap) bits &= ~(1U << i);
        }
        ++_telemetry.DeniedRequests;
        throw BackendError(GraphicsBackend::Vulkan, BackendErrorKind::OutOfMemory, 0,
            "Vulkan memory admission denied: " + std::string(AllocationReasonName(denied.Reason))
            + " (" + std::to_string(requirements.size) + " bytes).");
    }
    VulkanMemory::Reservation::~Reservation() { Owner.Finish(Heap, Bytes, Failed); }
    void VulkanMemory::Finish(std::uint32_t heap, std::uint64_t bytes, bool failed) noexcept
    {
        std::lock_guard lock(_mutex);
        _pending[heap] -= bytes; _telemetry.PendingBytes -= bytes; --_telemetry.PendingRequests;
        if (failed) ++_telemetry.NativeFailures;
    }
    void VulkanMemory::CreateBuffer(const VkBufferCreateInfo& create, const VmaAllocationCreateInfo& allocation,
        VkBuffer& buffer, VmaAllocation& memory, VmaAllocationInfo* info)
    {
        if (!_allocator) throw std::logic_error("Vulkan memory owner is closed.");
        // Invalid giant sizes must not reach a native requirements query.
        if ((_limits.MaxAllocationSize && create.size > _limits.MaxAllocationSize)
            || (_limits.MaxBufferSize && create.size > _limits.MaxBufferSize))
        {
            std::lock_guard lock(_mutex); ++_telemetry.DeniedRequests;
            throw BackendError(GraphicsBackend::Vulkan, BackendErrorKind::OutOfMemory, 0,
                "Vulkan buffer size exceeds the device allocation limit.");
        }
        VkDeviceBufferMemoryRequirements request{VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS};
        request.pCreateInfo = &create;
        VkMemoryRequirements2 requirements{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
        _dispatch.BufferRequirements(_device, &request, &requirements);
        auto reserved = Reserve(requirements.memoryRequirements, allocation, [&](const auto& desc, auto& type) {
            return vmaFindMemoryTypeIndexForBufferInfo(_allocator, &create, &desc, &type);
        });
        const auto result = vmaCreateBuffer(_allocator, &create, &reserved.Allocation, &buffer, &memory, info);
        reserved.Failed = result != VK_SUCCESS;
        _dispatch.CheckResult(result, "vmaCreateBuffer");
    }
    void VulkanMemory::CreateImage(const VkImageCreateInfo& create, const VmaAllocationCreateInfo& allocation,
        VkImage& image, VmaAllocation& memory)
    {
        if (!_allocator) throw std::logic_error("Vulkan memory owner is closed.");
        VkDeviceImageMemoryRequirements request{VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS};
        request.pCreateInfo = &create;
        VkMemoryRequirements2 requirements{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
        _dispatch.ImageRequirements(_device, &request, &requirements);
        auto reserved = Reserve(requirements.memoryRequirements, allocation, [&](const auto& desc, auto& type) {
            return vmaFindMemoryTypeIndexForImageInfo(_allocator, &create, &desc, &type);
        });
        const auto result = vmaCreateImage(_allocator, &create, &reserved.Allocation, &image, &memory, nullptr);
        reserved.Failed = result != VK_SUCCESS;
        _dispatch.CheckResult(result, "vmaCreateImage");
    }
    void VulkanMemory::Close() noexcept
    {
        std::lock_guard lock(_mutex);
        if (!_allocator) return;
        vmaDestroyAllocator(_allocator); _allocator = VK_NULL_HANDLE;
        _device = VK_NULL_HANDLE; _physical = VK_NULL_HANDLE; _dispatch = {};
    }
}
#endif
