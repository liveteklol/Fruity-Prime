#pragma once
#if defined(FRUITY_HAS_VULKAN)
#include "../Submission.hpp"
#include <vulkan/vulkan.h>
#include <functional>
#include <vector>
struct VmaAllocation_T;

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // GPU-only transfer storage for one command slot. No mapping, host
    // pixels, flush, submission or wait. The slot supplies completion proof.
    class VulkanTransferScratch final
    {
    public:
        struct Page { VkBuffer Buffer{}; VmaAllocation_T* Allocation{}; VkDeviceSize Size{}; };
        struct Slice { VkBuffer Buffer{}; VkDeviceSize Offset{}, Size{}; };
        struct Dispatch
        {
            std::function<Page(VkDeviceSize)> Create;
            std::function<void(const Page&)> Destroy;
        };
        explicit VulkanTransferScratch(Dispatch dispatch, VkDeviceSize pageSize = 64U << 10U);
        ~VulkanTransferScratch() { Close(); }
        VulkanTransferScratch(const VulkanTransferScratch&) = delete;
        VulkanTransferScratch& operator=(const VulkanTransferScratch&) = delete;
        [[nodiscard]] Slice Allocate(VkDeviceSize bytes, VkDeviceSize alignment = 4);
        void Submitted(SubmissionSerial serial);
        void ResetAfterCompletion(SubmissionSerial completed);
        // Caller drains or discards the native command slot before closing.
        void Close() noexcept;
        [[nodiscard]] std::size_t PageCount() const noexcept { return _pages.size(); }
        [[nodiscard]] VkDeviceSize ReservedBytes() const noexcept { return _reserved; }
    private:
        struct Entry { Page Native; VkDeviceSize Used{}; };
        Dispatch _dispatch;
        std::vector<Entry> _pages;
        VkDeviceSize _pageSize{}, _reserved{};
        std::size_t _active{};
        SubmissionSerial _lastUse{};
        bool _pending = false, _closed = false;
    };
}
#endif
