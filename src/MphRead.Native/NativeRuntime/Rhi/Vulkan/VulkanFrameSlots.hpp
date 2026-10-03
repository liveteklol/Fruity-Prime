#pragma once
#if defined(FRUITY_HAS_VULKAN)
#include "VulkanDescriptorAllocator.hpp"
#include "../FrameContext.hpp"
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    // Per-frame descriptor/fence generations. Queue submission serials belong
    // to VulkanFrameScheduler; recording stream slots belong to CommandSlots.
    // This owner alone decides when frame descriptor pages/transients recycle.
    // Host calls are confined to the recording thread.
    class VulkanFrameSlots final
    {
    public:
        struct Dispatch final
        {
            VkDevice Device;
            PFN_vkCreateFence CreateFence;
            PFN_vkDestroyFence DestroyFence;
            PFN_vkGetFenceStatus FenceStatus;
            PFN_vkWaitForFences WaitFence;
            PFN_vkResetFences ResetFence;
            PFN_vkDestroyCommandPool DestroyPool;
            PFN_vkDestroyPipelineLayout DestroyLayout;
            std::function<SubmissionSerial(const VkSubmitInfo2&, VkFence)> Submit;
            std::function<SubmissionSerial()> Completed;
            std::function<std::unique_ptr<VulkanDescriptorAllocator>()> MakeDescriptors;
            std::function<void()> HostWait;
        };
        explicit VulkanFrameSlots(Dispatch dispatch);
        // The session drains/discards GPU work before destruction/explicit close.
        ~VulkanFrameSlots() { CloseAfterDrain(); }
        VulkanFrameSlots(const VulkanFrameSlots&) = delete;
        VulkanFrameSlots& operator=(const VulkanFrameSlots&) = delete;
        [[nodiscard]] FrameContext Begin();
        void End();
        [[nodiscard]] VkDescriptorSet Allocate(VkDescriptorSetLayout layout, const BindingLayoutDesc& desc);
        // Adopts these transient native objects only after successful submit;
        // vector reservation is performed before any work reaches the queue.
        void SubmitTransient(const VkSubmitInfo2& work, VkCommandPool pool, VkPipelineLayout layout);
        void WaitAll();
        // Explicit idle/device-loss boundary, never an ordinary allocation wait.
        void CloseAfterDrain() noexcept;
        void ObserveDeviceIdle() noexcept { _completed.store(_current.load()); }
        [[nodiscard]] bool Active() const noexcept { return _active; }
        [[nodiscard]] std::uint64_t Current() const noexcept { return _current.load(); }
        [[nodiscard]] std::uint64_t Completed() const noexcept { return _completed.load(); }
        [[nodiscard]] std::size_t CurrentDescriptorPages() const noexcept;
        [[nodiscard]] std::size_t NativeObjects() const noexcept;
    private:
        struct Transient final { VkCommandPool Pool; VkPipelineLayout Layout; };
        struct Slot final
        {
            VkFence Fence = VK_NULL_HANDLE;
            std::unique_ptr<VulkanDescriptorAllocator> Descriptors;
            std::vector<Transient> Transients;
            std::uint64_t SubmittedFrame = 0;
            SubmissionSerial LastUse{};
        };
        void RequireOpen() const;
        Slot& CurrentSlot();
        void Complete(Slot& slot);
        void ReleaseTransients(Slot& slot) noexcept;
        Dispatch _dispatch;
        std::array<Slot, FramesInFlight> _slots{};
        std::atomic<std::uint64_t> _current{0}, _completed{0};
        bool _active = false, _closed = false;
    };
}
#endif
