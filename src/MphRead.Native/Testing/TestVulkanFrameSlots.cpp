#include "../NativeRuntime/Rhi/Vulkan/VulkanFrameSlots.hpp"
#include "../NativeRuntime/Rhi/Vulkan/VulkanResult.hpp"
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace
{
    using namespace MphRead::NativeRuntime::Rhi;
    using namespace MphRead::NativeRuntime::Rhi::Vulkan;
    void Expect(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
    template<class T> T Handle(std::uint64_t value)
    { if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value); else return static_cast<T>(value); }
    template<class F> void Reject(F action)
    { bool caught = false; try { action(); } catch (const std::exception&) { caught = true; } Expect(caught, "Invalid frame operation succeeded."); }
    const auto Layout = Handle<VkDescriptorSetLayout>(3);
    const BindingLayoutDesc Need{{{0, BindingType::UniformBuffer, ShaderStage::Vertex, 1}}};
    struct Fake;
    Fake* active = nullptr;
    struct Fake final
    {
        struct Fence { std::uint64_t Use = 0; };
        struct Transient { VkCommandPool Pool; VkPipelineLayout Layout; };
        std::map<VkFence, Fence> Fences;
        std::map<VkDescriptorPool, std::uint64_t> Pages;
        std::map<VkCommandPool, std::uint64_t> Pools;
        std::map<VkPipelineLayout, std::uint64_t> Layouts;
        std::map<VkCommandBuffer, Transient> Commands;
        std::set<VkDescriptorPool> PendingPages;
        std::uint64_t Next = 10, Submitted = 0, Completed = 0;
        unsigned Creates = 0, PoolResets = 0, FenceResets = 0, Waits = 0, HostWaits = 0, SubmitCalls = 0, Timeouts = 0;
        bool FailFence = false, FailDescriptors = false, FailSubmit = false, FailResetFence = false;
        bool FailResetPool = false, LagCounter = false, Lost = false, BadOrder = false;
        Fake() { active = this; }
        ~Fake() { active = nullptr; }
        void CheckUse(std::uint64_t serial) { if (!Lost && serial > Completed) BadOrder = true; }
        void Clean() const { Expect(Fences.empty() && Pages.empty() && Pools.empty() && Layouts.empty() && !BadOrder, "Frame objects leaked or recycled before GPU completion."); }
        static VKAPI_ATTR VkResult VKAPI_CALL CreateFence(VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence* name)
        { auto& f = *active; ++f.Creates; if (f.FailFence) return VK_ERROR_OUT_OF_HOST_MEMORY; *name = Handle<VkFence>(++f.Next); f.Fences[*name] = {}; return VK_SUCCESS; }
        static VKAPI_ATTR void VKAPI_CALL DestroyFence(VkDevice, VkFence name, const VkAllocationCallbacks*)
        { auto& f = *active; f.CheckUse(f.Fences.at(name).Use); f.Fences.erase(name); }
        static VKAPI_ATTR VkResult VKAPI_CALL Status(VkDevice, VkFence name)
        { auto& f = *active; if (f.Lost) return VK_ERROR_DEVICE_LOST; const auto use = f.Fences.at(name).Use; return use && use <= f.Completed ? VK_SUCCESS : VK_NOT_READY; }
        static VKAPI_ATTR VkResult VKAPI_CALL Wait(VkDevice, std::uint32_t count, const VkFence* names, VkBool32 all, std::uint64_t timeout)
        {
            auto& f = *active; ++f.Waits; Expect(count == 1 && all && timeout == 2'000'000'000ULL, "Wrong frame wait policy.");
            if (f.Lost) return VK_ERROR_DEVICE_LOST;
            if (f.Timeouts) { --f.Timeouts; return VK_TIMEOUT; }
            f.Completed = std::max(f.Completed, f.Fences.at(*names).Use); return VK_SUCCESS;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL ResetFence(VkDevice, std::uint32_t count, const VkFence* names)
        {
            auto& f = *active; if (f.FailResetFence) return VK_ERROR_OUT_OF_HOST_MEMORY;
            Expect(count == 1, "Wrong reset fence count."); f.CheckUse(f.Fences.at(*names).Use);
            f.Fences.at(*names).Use = 0; ++f.FenceResets; return VK_SUCCESS;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL CreatePage(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool* name)
        { auto& f = *active; *name = Handle<VkDescriptorPool>(++f.Next); f.Pages[*name] = 0; return VK_SUCCESS; }
        static VKAPI_ATTR void VKAPI_CALL DestroyPage(VkDevice, VkDescriptorPool name, const VkAllocationCallbacks*)
        { auto& f = *active; f.CheckUse(f.Pages.at(name)); f.PendingPages.erase(name); f.Pages.erase(name); }
        static VKAPI_ATTR VkResult VKAPI_CALL ResetPage(VkDevice, VkDescriptorPool name, VkDescriptorPoolResetFlags)
        {
            auto& f = *active; if (f.FailResetPool) return VK_ERROR_OUT_OF_HOST_MEMORY;
            f.CheckUse(f.Pages.at(name)); f.Pages.at(name) = 0; ++f.PoolResets; return VK_SUCCESS;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Allocate(VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* name)
        { auto& f = *active; Expect(f.Pages.contains(info->descriptorPool), "Allocation on a destroyed page."); f.PendingPages.insert(info->descriptorPool); *name = Handle<VkDescriptorSet>(++f.Next); return VK_SUCCESS; }
        static VKAPI_ATTR void VKAPI_CALL DestroyPool(VkDevice, VkCommandPool name, const VkAllocationCallbacks*)
        { auto& f = *active; f.CheckUse(f.Pools.at(name)); f.Pools.erase(name); }
        static VKAPI_ATTR void VKAPI_CALL DestroyLayout(VkDevice, VkPipelineLayout name, const VkAllocationCallbacks*)
        { auto& f = *active; f.CheckUse(f.Layouts.at(name)); f.Layouts.erase(name); }
        VulkanFrameSlots::Dispatch Dispatch()
        {
            return {Handle<VkDevice>(1), CreateFence, DestroyFence, Status, Wait, ResetFence, DestroyPool, DestroyLayout,
                [this](const VkSubmitInfo2& work, VkFence fence) {
                    ++SubmitCalls;
                    if (FailSubmit) throw BackendError(GraphicsBackend::Vulkan, BackendErrorKind::OutOfMemory, VK_ERROR_OUT_OF_HOST_MEMORY, "Injected submit failure.");
                    const auto serial = ++Submitted;
                    for (const auto page : PendingPages) Pages.at(page) = serial;
                    for (unsigned i = 0; i < work.commandBufferInfoCount; ++i)
                    {
                        const auto& transient = Commands.at(work.pCommandBufferInfos[i].commandBuffer);
                        Pools.at(transient.Pool) = serial; Layouts.at(transient.Layout) = serial;
                    }
                    if (fence) { Fences.at(fence).Use = serial; PendingPages.clear(); }
                    return SubmissionSerial{serial};
                },
                [this] { return SubmissionSerial{LagCounter ? 0 : Completed}; },
                [this] {
                    if (FailDescriptors) throw std::runtime_error("Injected allocator creation failure.");
                    return std::make_unique<VulkanDescriptorAllocator>(VulkanDescriptorAllocator::Dispatch{
                        Handle<VkDevice>(1), CreatePage, DestroyPage, ResetPage, Allocate, Check}, VulkanDescriptorAllocator::Capacity{1, {1, 1, 1, 1, 1}});
                }, [this] { ++HostWaits; }};
        }
        void TransientUse(VulkanFrameSlots& slots)
        {
            auto pool = Handle<VkCommandPool>(++Next); Pools[pool] = 0;
            auto layout = Handle<VkPipelineLayout>(++Next); Layouts[layout] = 0;
            auto command = Handle<VkCommandBuffer>(++Next); Commands[command] = {pool, layout};
            VkCommandBufferSubmitInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}; info.commandBuffer = command;
            VkSubmitInfo2 work{VK_STRUCTURE_TYPE_SUBMIT_INFO_2}; work.commandBufferInfoCount = 1; work.pCommandBufferInfos = &info;
            try { slots.SubmitTransient(work, pool, layout); }
            catch (...) { DestroyPool({}, pool, nullptr); DestroyLayout({}, layout, nullptr); throw; }
        }
    };
    void Rotation()
    {
        Fake f; VulkanFrameSlots slots(f.Dispatch());
        Reject([&] { slots.End(); }); Reject([&] { (void)slots.Allocate(Layout, Need); });
        for (unsigned cycle = 0; cycle < 64; ++cycle)
        {
            const auto frame = slots.Begin();
            Expect(frame.Number == cycle + 1 && frame.Slot == cycle % FramesInFlight, "Frame generation/slot drifted.");
            const auto calls = f.SubmitCalls; Reject([&] { (void)slots.Begin(); });
            Expect(f.SubmitCalls == calls, "Nested frame submitted work.");
            (void)slots.Allocate(Layout, Need); (void)slots.Allocate(Layout, Need);
            Expect(slots.CurrentDescriptorPages() == 2, "Overflow descriptor pages not exercised/reused.");
            f.TransientUse(slots);
            slots.End();
            if (cycle == 1) f.Timeouts = 1;
        }
        slots.WaitAll(); Expect(slots.Completed() == 64 && f.Waits > f.HostWaits && !f.BadOrder, "Wait/completion proof drifted.");
        slots.CloseAfterDrain(); slots.CloseAfterDrain(); Expect(!slots.NativeObjects(), "Closed slots retained native objects.");
        const auto calls = f.SubmitCalls + f.Creates + f.FenceResets + f.PoolResets;
        Reject([&] { (void)slots.Begin(); }); Reject([&] { slots.End(); }); Reject([&] { slots.WaitAll(); });
        Expect(f.SubmitCalls + f.Creates + f.FenceResets + f.PoolResets == calls, "Closed frame owner touched dispatch."); f.Clean();
    }
    void CompletionAndFailures()
    {
        Fake f; VulkanFrameSlots slots(f.Dispatch());
        f.FailFence = true; Reject([&] { (void)slots.Begin(); }); f.FailFence = false;
        Expect(!slots.Active() && !slots.Current() && !slots.NativeObjects(), "Failed fence creation advanced a frame.");
        f.FailDescriptors = true; Reject([&] { (void)slots.Begin(); }); f.FailDescriptors = false;
        Expect(!slots.Active() && !slots.Current() && slots.NativeObjects() == 1, "Failed allocator creation lost its reusable fence.");
        (void)slots.Begin(); (void)slots.Allocate(Layout, Need);
        f.FailSubmit = true; Reject([&] { f.TransientUse(slots); }); Reject([&] { slots.End(); }); f.FailSubmit = false;
        Expect(slots.Active() && !f.Submitted && f.Pools.empty() && f.Layouts.empty(), "Rejected submit adopted transient work or ended the frame.");
        f.TransientUse(slots); slots.End();
        (void)slots.Begin(); (void)slots.Allocate(Layout, Need); slots.End();
        f.Completed = f.Submitted; f.LagCounter = true;
        const auto resets = f.FenceResets + f.PoolResets;
        Reject([&] { (void)slots.Begin(); }); f.LagCounter = false;
        Expect(slots.Current() == 2 && !slots.Active() && f.FenceResets + f.PoolResets == resets && f.Pools.size() == 1,
            "Fence alone recycled a still-pending timeline generation.");
        f.FailResetFence = true; Reject([&] { (void)slots.Begin(); }); f.FailResetFence = false;
        f.FailResetPool = true; Reject([&] { (void)slots.Begin(); }); f.FailResetPool = false;
        Expect(slots.Current() == 2 && !slots.Active() && f.Pools.size() == 1, "Failed reset advanced a frame or destroyed transients.");
        const auto frame = slots.Begin(); Expect(frame.Number == 3 && f.Pools.empty() && !f.HostWaits, "Ready fences blocked or retry changed frame identity.");
        slots.WaitAll(); slots.ObserveDeviceIdle(); Expect(slots.Completed() == slots.Current(), "Device idle did not publish completed frames.");
        slots.CloseAfterDrain(); f.Clean();
    }
    void DeviceLoss()
    {
        Fake f; VulkanFrameSlots slots(f.Dispatch());
        for (unsigned i = 0; i < 2; ++i) { (void)slots.Begin(); (void)slots.Allocate(Layout, Need); slots.End(); }
        f.Lost = true; bool lost = false;
        try { (void)slots.Begin(); } catch (const BackendError& error) { lost = error.Kind() == BackendErrorKind::DeviceLost && error.NativeCode() == VK_ERROR_DEVICE_LOST; }
        Expect(lost && !f.FenceResets && !f.PoolResets && slots.Current() == 2, "Device loss was hidden or recycled live pages.");
        slots.CloseAfterDrain(); f.Clean();
    }
    void ExternalDrain()
    {
        Fake f; VulkanFrameSlots slots(f.Dispatch());
        (void)slots.Begin(); (void)slots.Allocate(Layout, Need); f.TransientUse(slots);
        Expect(slots.Active() && f.Submitted == 1, "Unframed diagnostic use did not reach the queue.");
        // The session's idle boundary can close an active frame whose marker
        // was never submitted. It must finish the already queued transient use.
        f.Completed = f.Submitted; slots.ObserveDeviceIdle(); slots.CloseAfterDrain();
        Expect(slots.Completed() == 1 && f.SubmitCalls == 1 && !f.HostWaits && !slots.Active(),
            "Closing a drained active frame added work or blocked again.");
        f.Clean();
    }
}
int main()
{
    try { Rotation(); CompletionAndFailures(); DeviceLoss(); ExternalDrain(); std::cout << "Vulkan frame slots PASS; 64 rotations; fenced descriptor/transient lifetime; timeline proof; timeout; retries; loss; drained active-frame close\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
