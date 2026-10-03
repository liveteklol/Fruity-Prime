#include "VulkanCommandSlots.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include "VulkanResult.hpp"
#include <iostream>
#include <stdexcept>

namespace MphRead::NativeRuntime::Rhi::Vulkan
{
    VulkanCommandSlots::VulkanCommandSlots(Dispatch dispatch) : _dispatch(std::move(dispatch))
    {
        if (!_dispatch.Device || !_dispatch.CreatePool || !_dispatch.DestroyPool || !_dispatch.Allocate
            || !_dispatch.CreateFence || !_dispatch.DestroyFence || !_dispatch.FenceStatus || !_dispatch.Wait
            || !_dispatch.ResetFence || !_dispatch.ResetPool || !_dispatch.Begin || !_dispatch.End
            || !_dispatch.Submit || !_dispatch.Completed || !_dispatch.MakeUploads || !_dispatch.MakeDescriptors)
            throw std::invalid_argument("Incomplete Vulkan command-slot dispatch.");
        try
        {
            for (auto& slot : _slots)
            {
                VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                pool.queueFamilyIndex = _dispatch.QueueFamily;
                Check(_dispatch.CreatePool(_dispatch.Device, &pool, nullptr, &slot.Pool), "vkCreateCommandPool(slots)");
                VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                allocate.commandPool = slot.Pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
                Check(_dispatch.Allocate(_dispatch.Device, &allocate, &slot.Buffer), "vkAllocateCommandBuffers(slots)");
                VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                Check(_dispatch.CreateFence(_dispatch.Device, &fence, nullptr, &slot.Fence), "vkCreateFence(slots)");
                if (_dispatch.Name) _dispatch.Name(slot.Buffer);
                slot.Uploads = _dispatch.MakeUploads(); slot.Descriptors = _dispatch.MakeDescriptors();
                if (_dispatch.MakeScratch) slot.Scratch = _dispatch.MakeScratch();
                if (_dispatch.MakeScratch && !slot.Scratch) throw std::invalid_argument("Empty GPU transfer scratch allocator.");
                if (!slot.Uploads || !slot.Descriptors) throw std::invalid_argument("Empty Vulkan slot allocators.");
            }
        }
        catch (...) { DestroyNative(); throw; }
    }
    void VulkanCommandSlots::RequireOpen() const
    { if (_closed) throw std::logic_error("Vulkan command slots are closed."); }
    VulkanUploadArena& VulkanCommandSlots::Uploads() const
    { RequireOpen(); return *_slots[_current].Uploads; }
    VulkanDescriptorAllocator& VulkanCommandSlots::Descriptors() const
    { RequireOpen(); return *_slots[_current].Descriptors; }
    VulkanTransferScratch& VulkanCommandSlots::Scratch() const
    {
        RequireOpen(); const auto& slot = _slots[_current];
        if (slot.Status != State::Recording || !slot.Scratch)
            throw std::logic_error("GPU transfer scratch requires a configured recording slot.");
        return *slot.Scratch;
    }
    void VulkanCommandSlots::Complete(Slot& slot)
    {
        if (slot.Status != State::Pending) return;
        const auto status = _dispatch.FenceStatus(_dispatch.Device, slot.Fence);
        if (status != VK_SUCCESS)
        {
            if (status != VK_NOT_READY) Check(status, "vkGetFenceStatus(slots)");
            if (_dispatch.HostWait) _dispatch.HostWait();
            for (unsigned seconds = 2;; seconds += 2)
            {
                const auto result = _dispatch.Wait(_dispatch.Device, 1, &slot.Fence, VK_TRUE, 2'000'000'000ULL);
                if (result == VK_TIMEOUT)
                { std::cerr << "[vulkan] still waiting for command slot after " << seconds << " s\n"; continue; }
                Check(result, "vkWaitForFences(slots)"); break;
            }
        }
        const auto completed = _dispatch.Completed();
        if (completed < slot.LastUse) throw std::logic_error("Vulkan slot completion serial is still pending.");
        slot.Uploads->ResetAfterCompletion(completed); slot.Descriptors->ResetAfterCompletion(completed);
        if (slot.Scratch) slot.Scratch->ResetAfterCompletion(completed);
        Check(_dispatch.ResetFence(_dispatch.Device, 1, &slot.Fence), "vkResetFences(slots)");
        slot.Status = State::Idle;
        if (_dispatch.Collect) _dispatch.Collect();
    }
    bool VulkanCommandSlots::FenceReady(const Slot& slot) const
    {
        if (slot.Status != State::Pending) return true;
        const auto status = _dispatch.FenceStatus(_dispatch.Device, slot.Fence);
        if (status == VK_NOT_READY) return false;
        Check(status, "vkGetFenceStatus(slots poll)"); return true;
    }
    bool VulkanCommandSlots::CanBeginWithoutWait() const
    { RequireOpen(); return FenceReady(_slots[_current]); }
    bool VulkanCommandSlots::PollComplete() const
    {
        RequireOpen();
        for (const auto& slot : _slots) if (!FenceReady(slot)) return false;
        return true;
    }
    void VulkanCommandSlots::Begin()
    {
        RequireOpen(); auto& slot = _slots[_current];
        if (slot.Status == State::Recording || slot.Status == State::Executable)
            throw std::logic_error("Vulkan command slot has unsubmitted recording.");
        const bool pending = slot.Status == State::Pending;
        Complete(slot);
        if (!pending)
        {
            const auto completed = _dispatch.Completed();
            slot.Uploads->ResetAfterCompletion(completed); slot.Descriptors->ResetAfterCompletion(completed);
            if (slot.Scratch) slot.Scratch->ResetAfterCompletion(completed);
        }
        Check(_dispatch.ResetPool(_dispatch.Device, slot.Pool, 0), "vkResetCommandPool(slots)");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(_dispatch.Begin(slot.Buffer, &begin), "vkBeginCommandBuffer(slots)"); slot.Status = State::Recording;
    }
    void VulkanCommandSlots::End()
    {
        RequireOpen(); auto& slot = _slots[_current];
        if (slot.Status != State::Recording) throw std::logic_error("Vulkan command slot is not recording.");
        slot.Uploads->FlushPending(); Check(_dispatch.End(slot.Buffer), "vkEndCommandBuffer(slots)"); slot.Status = State::Executable;
    }
    void VulkanCommandSlots::Submit()
    {
        RequireOpen(); auto& slot = _slots[_current];
        if (slot.Status != State::Executable) throw std::logic_error("Vulkan command slot is not executable.");
        VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}; command.commandBuffer = slot.Buffer;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2}; submit.commandBufferInfoCount = 1; submit.pCommandBufferInfos = &command;
        slot.LastUse = _dispatch.Submit(submit, slot.Fence); slot.Status = State::Pending;
        slot.Descriptors->Submitted(slot.LastUse); slot.Uploads->Submitted(slot.LastUse);
        if (slot.Scratch) slot.Scratch->Submitted(slot.LastUse);
        _current = (_current + 1) % _slots.size();
    }
    void VulkanCommandSlots::WaitAll()
    {
        RequireOpen();
        // The most recently submitted slot was the old current slot. Preserve
        // its drain order while leaving any unsubmitted current recording alone.
        for (std::size_t i = 1; i <= _slots.size(); ++i) Complete(_slots[(_current + i) % _slots.size()]);
        if (_dispatch.Collect) _dispatch.Collect();
    }
    void VulkanCommandSlots::DestroyNative() noexcept
    {
        for (auto& slot : _slots)
        {
            slot.Uploads.reset(); slot.Descriptors.reset();
            slot.Scratch.reset();
            if (slot.Fence) _dispatch.DestroyFence(_dispatch.Device, slot.Fence, nullptr);
            if (slot.Pool) _dispatch.DestroyPool(_dispatch.Device, slot.Pool, nullptr);
            slot.Fence = VK_NULL_HANDLE; slot.Pool = VK_NULL_HANDLE; slot.Buffer = VK_NULL_HANDLE; slot.Status = State::Idle;
        }
        _closed = true;
    }
    void VulkanCommandSlots::Close(bool deviceDrained) noexcept
    {
        if (_closed) return;
        if (!deviceDrained) { try { WaitAll(); } catch (...) {} }
        DestroyNative();
    }
}
#endif
