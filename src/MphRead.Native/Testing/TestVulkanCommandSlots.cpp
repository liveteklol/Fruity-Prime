#include "../NativeRuntime/Rhi/Vulkan/VulkanCommandSlots.hpp"
#include "../NativeRuntime/Rhi/Vulkan/VulkanResult.hpp"
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <type_traits>

namespace
{
    using namespace MphRead::NativeRuntime::Rhi;
    using namespace MphRead::NativeRuntime::Rhi::Vulkan;
    void Expect(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
    template<class T> T Handle(std::uint64_t value)
    { if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value); else return static_cast<T>(value); }
    template<class F> void Reject(F action)
    { bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; } Expect(rejected, "Invalid slot operation was accepted."); }
    struct Fake;
    Fake* active = nullptr;
    struct Fake final
    {
        struct Pool { VkCommandBuffer Buffer{}; std::uint64_t Use = 0; bool Recording = false; };
        struct Fence { std::uint64_t Use = 0; bool Signaled = false; };
        std::map<VkCommandPool, Pool> Pools;
        std::map<VkCommandBuffer, VkCommandPool> Buffers;
        std::map<VkFence, Fence> Fences;
        std::map<VkDescriptorPool, VkCommandPool> Descriptors;
        std::map<VkBuffer, std::vector<std::byte>> Pages;
        VkCommandPool RecordingPool{};
        std::uint64_t Next = 0, Submitted = 0, Completed = 0;
        unsigned Waits = 0, HostWaits = 0, Resets = 0, TimeoutOnce = 0, PoolCreates = 0, FenceCreates = 0, Allocates = 0;
        unsigned FailPool = 0, FailFence = 0, FailAllocate = 0;
        bool BadOrder = false, BadTimeline = false, FailSubmit = false, Lost = false;
        Fake() { active = this; }
        ~Fake() { active = nullptr; }
        bool Pending(const Pool& pool) const
        {
            for (const auto& [name, fence] : Fences) if (fence.Use == pool.Use && fence.Use && !fence.Signaled) return true;
            return false;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL CreatePool(VkDevice, const VkCommandPoolCreateInfo* info, const VkAllocationCallbacks*, VkCommandPool* name)
        {
            auto& f = *active; if (++f.PoolCreates == f.FailPool) return VK_ERROR_OUT_OF_HOST_MEMORY;
            Expect(info->queueFamilyIndex == 7, "Wrong queue family.");
            *name = Handle<VkCommandPool>(++f.Next); f.Pools[*name] = {}; return VK_SUCCESS;
        }
        static VKAPI_ATTR void VKAPI_CALL DestroyPool(VkDevice, VkCommandPool name, const VkAllocationCallbacks*)
        { auto& f = *active; if (!f.Lost && f.Pending(f.Pools.at(name))) f.BadOrder = true; f.Buffers.erase(f.Pools.at(name).Buffer); f.Pools.erase(name); }
        static VKAPI_ATTR VkResult VKAPI_CALL Allocate(VkDevice, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* name)
        {
            auto& f = *active; if (++f.Allocates == f.FailAllocate) return VK_ERROR_OUT_OF_HOST_MEMORY;
            *name = Handle<VkCommandBuffer>(++f.Next); f.Pools.at(info->commandPool).Buffer = *name; f.Buffers[*name] = info->commandPool; return VK_SUCCESS;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL CreateFence(VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence* name)
        {
            auto& f = *active; if (++f.FenceCreates == f.FailFence) return VK_ERROR_OUT_OF_HOST_MEMORY;
            *name = Handle<VkFence>(++f.Next); f.Fences[*name] = {}; return VK_SUCCESS;
        }
        static VKAPI_ATTR void VKAPI_CALL DestroyFence(VkDevice, VkFence name, const VkAllocationCallbacks*)
        { auto& f = *active; const auto fence = f.Fences.at(name); if (!f.Lost && fence.Use && !fence.Signaled) f.BadOrder = true; f.Fences.erase(name); }
        static VKAPI_ATTR VkResult VKAPI_CALL Status(VkDevice, VkFence name)
        { if (active->Lost) return VK_ERROR_DEVICE_LOST; return active->Fences.at(name).Signaled ? VK_SUCCESS : VK_NOT_READY; }
        static VKAPI_ATTR VkResult VKAPI_CALL Wait(VkDevice, std::uint32_t count, const VkFence* names, VkBool32 all, std::uint64_t timeout)
        {
            auto& f = *active; ++f.Waits; Expect(count == 1 && all && timeout == 2'000'000'000ULL, "Wrong fence wait policy.");
            if (f.Lost) return VK_ERROR_DEVICE_LOST;
            if (f.TimeoutOnce) { --f.TimeoutOnce; return VK_TIMEOUT; }
            auto& fence = f.Fences.at(*names); fence.Signaled = true;
            if (!f.BadTimeline) f.Completed = std::max(f.Completed, fence.Use);
            return VK_SUCCESS;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL ResetFence(VkDevice, std::uint32_t, const VkFence* name)
        { auto& f = *active; auto& fence = f.Fences.at(*name); if (!fence.Signaled) f.BadOrder = true; fence = {}; return VK_SUCCESS; }
        static VKAPI_ATTR VkResult VKAPI_CALL ResetPool(VkDevice, VkCommandPool name, VkCommandPoolResetFlags)
        { auto& f = *active; if (f.Pending(f.Pools.at(name))) f.BadOrder = true; ++f.Resets; return VK_SUCCESS; }
        static VKAPI_ATTR VkResult VKAPI_CALL Begin(VkCommandBuffer name, const VkCommandBufferBeginInfo*)
        { auto& f = *active; f.RecordingPool = f.Buffers.at(name); f.Pools.at(f.RecordingPool).Recording = true; return VK_SUCCESS; }
        static VKAPI_ATTR VkResult VKAPI_CALL End(VkCommandBuffer name)
        { auto& f = *active; auto& pool = f.Pools.at(f.Buffers.at(name)); Expect(pool.Recording, "Native end without begin."); pool.Recording = false; return VK_SUCCESS; }
        static VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptors(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool* name)
        { auto& f = *active; *name = Handle<VkDescriptorPool>(++f.Next); f.Descriptors[*name] = f.RecordingPool; return VK_SUCCESS; }
        static VKAPI_ATTR void VKAPI_CALL DestroyDescriptors(VkDevice, VkDescriptorPool name, const VkAllocationCallbacks*)
        { auto& f = *active; if (!f.Lost && f.Pending(f.Pools.at(f.Descriptors.at(name)))) f.BadOrder = true; f.Descriptors.erase(name); }
        static VKAPI_ATTR VkResult VKAPI_CALL ResetDescriptors(VkDevice, VkDescriptorPool name, VkDescriptorPoolResetFlags)
        { auto& f = *active; if (f.Pending(f.Pools.at(f.Descriptors.at(name)))) f.BadOrder = true; return VK_SUCCESS; }
        static VKAPI_ATTR VkResult VKAPI_CALL AllocateDescriptors(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet* name)
        { *name = Handle<VkDescriptorSet>(++active->Next); return VK_SUCCESS; }
        VulkanCommandSlots::Dispatch Dispatch()
        {
            const auto device = Handle<VkDevice>(1);
            return {device, 7, CreatePool, DestroyPool, Allocate, CreateFence, DestroyFence, Status, Wait, ResetFence, ResetPool, Begin, End,
                [this](const VkSubmitInfo2& work, VkFence fence) {
                    if (FailSubmit) throw BackendError(GraphicsBackend::Vulkan, BackendErrorKind::DeviceLost, VK_ERROR_DEVICE_LOST, "injected submit loss");
                    Expect(work.commandBufferInfoCount == 1, "Wrong submission count.");
                    auto& pool = Pools.at(Buffers.at(work.pCommandBufferInfos[0].commandBuffer));
                    Expect(!pool.Recording, "Submitted a recording native buffer.");
                    pool.Use = ++Submitted; Fences.at(fence) = {Submitted, false}; return SubmissionSerial{Submitted};
                }, [this] { return SubmissionSerial{Completed}; }, [this] { ++HostWaits; }, {}, {},
                [this] { return std::make_unique<VulkanUploadArena>(VulkanUploadArena::Dispatch{
                    [this](VkDeviceSize size) { const auto buffer = Handle<VkBuffer>(++Next); auto& page = Pages[buffer]; page.resize(size); return VulkanUploadArena::Page{buffer, Handle<VmaAllocation_T*>(++Next), page.data(), size}; },
                    [this](const auto& page) { Pages.erase(page.Buffer); }, [](const auto&, VkDeviceSize, VkDeviceSize) {}}, 64); },
                [device] { return std::make_unique<VulkanDescriptorAllocator>(VulkanDescriptorAllocator::Dispatch{
                    device, CreateDescriptors, DestroyDescriptors, ResetDescriptors, AllocateDescriptors, Check}, VulkanDescriptorAllocator::Capacity{2, {2, 0, 0, 0, 0}}); },
                [this] { return std::make_unique<VulkanTransferScratch>(VulkanTransferScratch::Dispatch{
                    [this](VkDeviceSize size) {
                        const auto buffer = Handle<VkBuffer>(++Next); Pages[buffer].resize(size);
                        return VulkanTransferScratch::Page{buffer, Handle<VmaAllocation_T*>(++Next), size};
                    }, [this](const auto& page) { Pages.erase(page.Buffer); }}, 64); }};
        }
        void Clean() const { Expect(Pools.empty() && Buffers.empty() && Fences.empty() && Descriptors.empty() && Pages.empty() && !BadOrder, "Slot teardown leaked or destroyed pending resources."); }
    };

    void CheckRotation()
    {
        Fake f; VulkanCommandSlots slots(f.Dispatch());
        Reject([&] { slots.End(); }); Reject([&] { slots.Submit(); });
        VkCommandBuffer first{};
        VkBuffer firstScratch{};
        BindingLayoutDesc layout{{{0, BindingType::UniformBuffer, ShaderStage::Vertex, 1}}};
        for (unsigned cycle = 0; cycle < 64; ++cycle)
        {
            slots.Begin(); if (cycle == 0) first = slots.Buffer(); else if (cycle == 2) Expect(slots.Buffer() == first, "Slot did not rotate to its first native buffer.");
            const auto waits = f.Waits; Reject([&] { slots.Begin(); }); Reject([&] { slots.Submit(); });
            const auto slice = slots.Uploads().Allocate(32); slice.Data[0] = std::byte(cycle);
            const auto scratch = slots.Scratch().Allocate(31);
            if (cycle == 0) firstScratch = scratch.Buffer;
            if (cycle == 2) Expect(scratch.Buffer == firstScratch && scratch.Offset == 0,
                "Completed command slot did not reuse its GPU scratch page.");
            (void)slots.Descriptors().Allocate(Handle<VkDescriptorSetLayout>(1), layout);
            slots.WaitAll(); // Must leave this unsubmitted native recording alone.
            Expect(f.Waits - waits <= 1, "Drain waited on an unsubmitted recording.");
            slots.End(); slots.Submit();
            Reject([&] { (void)slots.Scratch(); });
            Expect(!slots.PollComplete(), "Unsignaled work reported ready.");
            const auto before = f.Waits; (void)slots.CanBeginWithoutWait(); (void)slots.PollComplete();
            Expect(f.Waits == before, "Nonblocking poll waited for GPU completion.");
        }
        slots.WaitAll(); Expect(slots.PollComplete(), "Drain left a submitted slot pending.");
        slots.Close(); slots.Close(); Reject([&] { slots.Begin(); }); Reject([&] { slots.WaitAll(); });
        f.Clean();
    }
    void CheckBackpressure()
    {
        Fake f; VulkanCommandSlots slots(f.Dispatch());
        for (int i = 0; i < 2; ++i) { slots.Begin(); slots.End(); slots.Submit(); }
        const auto resets = f.Resets, waits = f.Waits;
        Expect(!slots.CanBeginWithoutWait() && !slots.PollComplete() && f.Resets == resets && f.Waits == waits, "Backpressure reset or waited for an occupied slot.");
        f.TimeoutOnce = 1; slots.Begin(); Expect(f.Waits == waits + 2 && f.HostWaits == 1, "Slot throttle did not retry timeout or count wait once.");
        slots.End(); slots.Submit(); slots.Close(); f.Clean();
    }
    void CheckCompletionProof()
    {
        Fake f; VulkanCommandSlots slots(f.Dispatch());
        for (int i = 0; i < 2; ++i) { slots.Begin(); slots.End(); slots.Submit(); }
        const auto resets = f.Resets; f.BadTimeline = true;
        Reject([&] { slots.Begin(); }); Expect(f.Resets == resets, "Fence alone reset a slot whose serial was still pending.");
        f.BadTimeline = false; f.Completed = f.Submitted;
        for (auto& [name, fence] : f.Fences) fence.Signaled = true;
        const auto waits = f.Waits; slots.Begin(); Expect(f.Waits == waits, "Already-signaled fence added a blocking wait.");
        slots.Close(); f.Clean();
    }
    void CheckFailures()
    {
        for (int failure = 0; failure < 3; ++failure)
        {
            Fake f; if (failure == 0) f.FailPool = 2; if (failure == 1) f.FailAllocate = 2; if (failure == 2) f.FailFence = 2;
            Reject([&] { VulkanCommandSlots slots(f.Dispatch()); }); f.Clean();
        }
        { Fake f; VulkanCommandSlots slots(f.Dispatch()); slots.Begin(); slots.End(); f.FailSubmit = true;
          Reject([&] { slots.Submit(); }); Expect(f.Submitted == 0 && slots.PollComplete(), "Failed submission advanced the stream."); slots.Close(); f.Clean(); }
        { Fake f; VulkanCommandSlots slots(f.Dispatch()); slots.Begin(); slots.End(); slots.Submit(); f.Lost = true;
          bool lost = false; try { (void)slots.PollComplete(); } catch (const BackendError& error) { lost = error.Kind() == BackendErrorKind::DeviceLost; }
          Expect(lost, "Fence query loss was replaced by not-ready."); slots.Close(); f.Clean(); }
        { Fake f; { VulkanCommandSlots slots(f.Dispatch()); slots.Begin(); slots.End(); slots.Submit(); }
          Expect(f.Waits == 1, "Destructor did not drain its pending slot."); f.Clean(); }
        { Fake f; VulkanCommandSlots slots(f.Dispatch()); slots.Begin(); slots.End(); slots.Submit();
          // A session idle boundary has completed the queue before its owners close.
          f.Completed = f.Submitted; for (auto& [name, fence] : f.Fences) fence.Signaled = true;
          slots.Close(true); Expect(f.Waits == 0 && f.HostWaits == 0, "Session teardown redundantly waited on a drained device.");
          Expect(!slots.Buffer(), "Closed slots retained a native command buffer.");
          Reject([&] { slots.PollComplete(); }); Reject([&] { slots.Uploads(); }); Reject([&] { slots.Descriptors(); }); f.Clean(); }
        { Fake f; auto dispatch = f.Dispatch(); dispatch.MakeDescriptors = []() -> std::unique_ptr<VulkanDescriptorAllocator> {
              throw std::runtime_error("injected allocator initialization failure"); };
          Reject([&] { VulkanCommandSlots slots(std::move(dispatch)); }); f.Clean(); }
        { Fake f; auto dispatch = f.Dispatch(); dispatch.MakeScratch = []() -> std::unique_ptr<VulkanTransferScratch> {
              throw std::runtime_error("injected scratch initialization failure"); };
          Reject([&] { VulkanCommandSlots slots(std::move(dispatch)); }); f.Clean(); }
        { Fake f; auto dispatch = f.Dispatch(); dispatch.MakeScratch = [] { return std::unique_ptr<VulkanTransferScratch>{}; };
          Reject([&] { VulkanCommandSlots slots(std::move(dispatch)); }); f.Clean(); }
    }
}
int main()
{
    try { CheckRotation(); CheckBackpressure(); CheckCompletionProof(); CheckFailures(); std::cout << "Vulkan command slots PASS: 64 rotations, allocator lifetime, backpressure, completion proof, timeout, failures, teardown\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
