#pragma once

#if defined(FRUITY_HAS_VULKAN)
#include "../Submission.hpp"
#include <vulkan/vulkan.h>
#include <cstddef>
#include <functional>
#include <vector>

struct VmaAllocation_T;

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // Persistently mapped pages owned by one submission slot. The caller owns
    // recording/queue/fence order and drains or discards before explicit close.
    class VulkanUploadArena final
    {
    public:
        struct Page final
        {
            VkBuffer Buffer = VK_NULL_HANDLE;
            VmaAllocation_T* Allocation = nullptr;
            std::byte* Data = nullptr;
            VkDeviceSize Size = 0;
        };
        struct Slice final
        {
            VkBuffer Buffer = VK_NULL_HANDLE;
            VkDeviceSize Offset = 0;
            std::byte* Data = nullptr;
        };
        struct Dispatch final
        {
            // Create returns a complete mapped page or cleans up before throwing.
            std::function<Page(VkDeviceSize)> Create;
            std::function<void(const Page&)> Destroy;
            std::function<void(const Page&, VkDeviceSize, VkDeviceSize)> Flush;
        };
        explicit VulkanUploadArena(Dispatch dispatch, VkDeviceSize pageSize = 8U << 20U);
        ~VulkanUploadArena() { Close(); }
        VulkanUploadArena(const VulkanUploadArena&) = delete;
        VulkanUploadArena& operator=(const VulkanUploadArena&) = delete;
        Slice Allocate(VkDeviceSize size, VkDeviceSize alignment = 16);
        void FlushPending();
        void Submitted(SubmissionSerial serial);
        // Caller finishes/discards unsubmitted recording before resetting.
        void ResetAfterCompletion(SubmissionSerial completed);
        void Close() noexcept;
        [[nodiscard]] std::size_t PageCount() const noexcept { return _pages.size(); }
        [[nodiscard]] VkDeviceSize ReservedBytes() const noexcept { return _reserved; }
        [[nodiscard]] VkDeviceSize UsedBytes() const noexcept;
        [[nodiscard]] SubmissionSerial LastUse() const noexcept { return _lastUse; }
    private:
        struct Entry final
        {
            Page Native;
            VkDeviceSize Used = 0, DirtyBegin = VK_WHOLE_SIZE, DirtyEnd = 0;
        };
        void RequireReady() const;
        Dispatch _dispatch;
        VkDeviceSize _pageSize, _reserved = 0;
        std::vector<Entry> _pages;
        std::size_t _active = 0;
        SubmissionSerial _lastUse{};
        bool _ready = true, _closed = false;
    };
}
#endif
