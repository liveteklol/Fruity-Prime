#include "VulkanUploadArena.hpp"

#if defined(FRUITY_HAS_VULKAN)
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    VulkanUploadArena::VulkanUploadArena(Dispatch dispatch, VkDeviceSize pageSize)
        : _dispatch(std::move(dispatch)), _pageSize(pageSize)
    {
        if (!_dispatch.Create || !_dispatch.Destroy || !_dispatch.Flush || !pageSize
            || pageSize > std::numeric_limits<std::size_t>::max())
            throw std::invalid_argument("Invalid Vulkan upload arena dispatch or page size.");
    }
    void VulkanUploadArena::RequireReady() const
    {
        if (_closed || !_ready) throw std::logic_error("Vulkan upload arena is closed or still submitted.");
    }
    VkDeviceSize VulkanUploadArena::UsedBytes() const noexcept
    {
        VkDeviceSize used = 0;
        for (const auto& page : _pages) used += page.Used;
        return used; // Each Used <= Size, and reserved Size sums are checked.
    }
    VulkanUploadArena::Slice VulkanUploadArena::Allocate(VkDeviceSize size, VkDeviceSize alignment)
    {
        RequireReady();
        if (!size || !alignment || size > std::numeric_limits<std::size_t>::max())
            throw std::invalid_argument("Invalid Vulkan upload allocation size or alignment.");
        for (; _active < _pages.size(); ++_active)
        {
            auto& entry = _pages[_active];
            const auto remainder = entry.Used % alignment;
            const auto padding = remainder ? alignment - remainder : 0;
            const auto available = entry.Native.Size - entry.Used;
            if (padding > available || size > available - padding) continue;
            const auto offset = entry.Used + padding;
            entry.Used = offset + size;
            entry.DirtyBegin = std::min(entry.DirtyBegin, offset);
            entry.DirtyEnd = entry.Used;
            return {entry.Native.Buffer, offset, entry.Native.Data + offset};
        }
        const auto capacity = std::max(_pageSize, size);
        if (capacity > std::numeric_limits<VkDeviceSize>::max() - _reserved)
            throw std::overflow_error("Vulkan upload reserved bytes overflow.");
        // Retention allocation happens before native allocation, so a vector
        // allocation failure cannot lose an already-created native page.
        _pages.reserve(_pages.size() + 1);
        auto page = _dispatch.Create(capacity);
        if (!page.Buffer || !page.Allocation || !page.Data || page.Size != capacity)
        {
            _dispatch.Destroy(page);
            throw std::runtime_error("Vulkan upload page was not fully mapped.");
        }
        _pages.push_back({page, size, 0, size});
        _reserved += capacity;
        return {page.Buffer, 0, page.Data};
    }
    void VulkanUploadArena::FlushPending()
    {
        RequireReady();
        for (auto& entry : _pages)
            if (entry.DirtyBegin != VK_WHOLE_SIZE)
            {
                _dispatch.Flush(entry.Native, entry.DirtyBegin, entry.DirtyEnd - entry.DirtyBegin);
                entry.DirtyBegin = VK_WHOLE_SIZE;
                entry.DirtyEnd = 0;
            }
    }
    void VulkanUploadArena::Submitted(SubmissionSerial serial)
    {
        RequireReady();
        if (!serial.Value || serial <= _lastUse)
            throw std::invalid_argument("Vulkan upload submission serial did not advance.");
        for (const auto& entry : _pages)
            if (entry.DirtyBegin != VK_WHOLE_SIZE)
                throw std::logic_error("Vulkan upload pages were not flushed before submission.");
        _lastUse = serial;
        _ready = false;
    }
    void VulkanUploadArena::ResetAfterCompletion(SubmissionSerial completed)
    {
        if (_closed || completed < _lastUse)
            throw std::logic_error("Vulkan upload pages reset before submission completion.");
        for (auto& entry : _pages)
        { entry.Used = 0; entry.DirtyBegin = VK_WHOLE_SIZE; entry.DirtyEnd = 0; }
        _active = 0;
        _ready = true;
    }
    void VulkanUploadArena::Close() noexcept
    {
        if (_closed) return;
        for (const auto& entry : _pages) _dispatch.Destroy(entry.Native);
        _pages.clear(); _reserved = 0; _active = 0;
        _ready = false; _closed = true;
        _dispatch = {};
    }
}
#endif
