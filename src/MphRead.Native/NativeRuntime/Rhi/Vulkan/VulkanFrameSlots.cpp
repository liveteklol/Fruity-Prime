#include "VulkanFrameSlots.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include "VulkanResult.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    VulkanFrameSlots::VulkanFrameSlots(Dispatch dispatch) : _dispatch(std::move(dispatch))
    {
        if (!_dispatch.Device || !_dispatch.CreateFence || !_dispatch.DestroyFence || !_dispatch.FenceStatus
            || !_dispatch.WaitFence || !_dispatch.ResetFence || !_dispatch.DestroyPool || !_dispatch.DestroyLayout
            || !_dispatch.Submit || !_dispatch.Completed || !_dispatch.MakeDescriptors)
            throw std::invalid_argument("Incomplete Vulkan frame-slot dispatch.");
    }
    void VulkanFrameSlots::RequireOpen() const
    { if (_closed) throw std::logic_error("Vulkan frame slots are closed."); }
    VulkanFrameSlots::Slot& VulkanFrameSlots::CurrentSlot()
    {
        RequireOpen();
        if (!_active) throw std::logic_error("Vulkan frame slots require an active frame.");
        return _slots[(_current.load() - 1) % _slots.size()];
    }
    void VulkanFrameSlots::Complete(Slot& slot)
    {
        if (!slot.SubmittedFrame) return;
        const auto status = _dispatch.FenceStatus(_dispatch.Device, slot.Fence);
        if (status != VK_SUCCESS)
        {
            if (status != VK_NOT_READY) Check(status, "vkGetFenceStatus(frame slots)");
            if (_dispatch.HostWait) _dispatch.HostWait();
            for (unsigned seconds = 2;; seconds += 2)
            {
                const auto result = _dispatch.WaitFence(_dispatch.Device, 1, &slot.Fence, VK_TRUE, 2'000'000'000ULL);
                if (result == VK_TIMEOUT)
                { std::cerr << "[vulkan] still waiting for frame slot after " << seconds << " s\n"; continue; }
                Check(result, "vkWaitForFences(frame slots)"); break;
            }
        }
        // A signaled fence and the queue timeline must agree before any reset.
        if (_dispatch.Completed() < slot.LastUse)
            throw std::logic_error("Vulkan frame-slot completion serial is still pending.");
        Check(_dispatch.ResetFence(_dispatch.Device, 1, &slot.Fence), "vkResetFences(frame slots)");
        _completed.store(std::max(_completed.load(), slot.SubmittedFrame));
        slot.SubmittedFrame = 0;
    }
    void VulkanFrameSlots::ReleaseTransients(Slot& slot) noexcept
    {
        for (const auto& transient : slot.Transients)
        {
            _dispatch.DestroyPool(_dispatch.Device, transient.Pool, nullptr);
            _dispatch.DestroyLayout(_dispatch.Device, transient.Layout, nullptr);
        }
        slot.Transients.clear();
    }
    FrameContext VulkanFrameSlots::Begin()
    {
        RequireOpen();
        if (_active) throw std::logic_error("Vulkan frame already active.");
        const auto frame = _current.load() + 1;
        const auto index = static_cast<std::uint32_t>((frame - 1) % _slots.size());
        auto& slot = _slots[index];
        if (!slot.Fence)
        {
            VkFenceCreateInfo create{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            Check(_dispatch.CreateFence(_dispatch.Device, &create, nullptr, &slot.Fence), "vkCreateFence(frame slots)");
        }
        Complete(slot);
        if (!slot.Descriptors)
        {
            slot.Descriptors = _dispatch.MakeDescriptors();
            if (!slot.Descriptors) throw std::invalid_argument("Empty Vulkan frame descriptor allocator.");
        }
        slot.Descriptors->ResetAfterCompletion(_dispatch.Completed());
        ReleaseTransients(slot);
        slot.LastUse = {};
        _current.store(frame); _active = true;
        return {frame, index};
    }
    void VulkanFrameSlots::End()
    {
        auto& slot = CurrentSlot();
        // Empty fenced marker follows all preceding graphics-queue producers.
        const VkSubmitInfo2 marker{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        const auto serial = _dispatch.Submit(marker, slot.Fence);
        slot.Descriptors->Submitted(serial);
        slot.LastUse = serial; slot.SubmittedFrame = _current.load(); _active = false;
    }
    VkDescriptorSet VulkanFrameSlots::Allocate(VkDescriptorSetLayout layout, const BindingLayoutDesc& desc)
    { return CurrentSlot().Descriptors->Allocate(layout, desc); }
    void VulkanFrameSlots::SubmitTransient(const VkSubmitInfo2& work, VkCommandPool pool, VkPipelineLayout layout)
    {
        auto& slot = CurrentSlot();
        if (!pool || !layout) throw std::invalid_argument("Vulkan transient submission needs live native objects.");
        slot.Transients.reserve(slot.Transients.size() + 1);
        (void)_dispatch.Submit(work, VK_NULL_HANDLE);
        slot.Transients.push_back({pool, layout});
    }
    void VulkanFrameSlots::WaitAll()
    {
        RequireOpen();
        if (_active) End();
        for (auto& slot : _slots) Complete(slot);
    }
    void VulkanFrameSlots::CloseAfterDrain() noexcept
    {
        if (_closed) return;
        for (auto& slot : _slots)
        {
            ReleaseTransients(slot);
            slot.Descriptors.reset();
            if (slot.Fence) _dispatch.DestroyFence(_dispatch.Device, slot.Fence, nullptr);
            slot.Fence = VK_NULL_HANDLE; slot.SubmittedFrame = 0; slot.LastUse = {};
        }
        _active = false; _closed = true;
        _dispatch = {};
    }
    std::size_t VulkanFrameSlots::CurrentDescriptorPages() const noexcept
    {
        if (!_current.load() || _closed) return 0;
        const auto& slot = _slots[(_current.load() - 1) % _slots.size()];
        return slot.Descriptors ? slot.Descriptors->PageCount() : 0;
    }
    std::size_t VulkanFrameSlots::NativeObjects() const noexcept
    {
        std::size_t count = 0;
        for (const auto& slot : _slots)
            count += (slot.Fence ? 1 : 0) + slot.Transients.size() * 2
                + (slot.Descriptors ? slot.Descriptors->PageCount() : 0);
        return count;
    }
}
#endif
