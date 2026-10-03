#include "../NativeRuntime/Rhi/Vulkan/VulkanFrameScheduler.hpp"
#include "../NativeRuntime/Rhi/Vulkan/VulkanResult.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace
{
    using namespace MphRead::NativeRuntime::Rhi;
    using namespace MphRead::NativeRuntime::Rhi::Vulkan;
    void Expect(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
    template<class T> T Handle(std::uint64_t value)
    { if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(value); else return static_cast<T>(value); }
    template<class F> void Reject(F action)
    { bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; } Expect(rejected, "Invalid scheduler operation succeeded."); }
    template<class F> void NativeFailure(F action, VkResult code, BackendErrorKind kind)
    {
        bool rejected = false;
        try { action(); } catch (const BackendError& error)
        { rejected = true; Expect(error.Backend() == GraphicsBackend::Vulkan && error.NativeCode() == code && error.Kind() == kind, "Native scheduler error was changed."); }
        Expect(rejected, "Native scheduler failure was accepted.");
    }
    struct Fake;
    Fake* active = nullptr;
    struct Fake final
    {
        VkDevice Device = Handle<VkDevice>(1);
        VkQueue Queue = Handle<VkQueue>(2);
        VkSemaphore Timeline = Handle<VkSemaphore>(3);
        VkResult CreateResult = VK_SUCCESS, SubmitResult = VK_SUCCESS, PollResult = VK_SUCCESS, WaitResult = VK_SUCCESS;
        unsigned BudgetWaits = 0;
        bool NullTimeline = false, Live = false;
        unsigned Creates = 0, Destroys = 0, Submits = 0, Polls = 0;
        std::uint64_t Complete = 0, LastSignal = 0;
        VkFence Fence = VK_NULL_HANDLE;
        VkSubmitInfo2 Work{};
        bool ExpectAttribution = false;
        std::uint64_t AttributedId = 0;
        const void* AttributedNext = nullptr;
        std::vector<VkSemaphoreSubmitInfo> Signals, Waits;
        std::vector<VkCommandBufferSubmitInfo> Commands;
        Fake() { active = this; }
        ~Fake() { active = nullptr; }
        static VKAPI_ATTR VkResult VKAPI_CALL Create(VkDevice device, const VkSemaphoreCreateInfo* create,
            const VkAllocationCallbacks*, VkSemaphore* name)
        {
            auto& f = *active; ++f.Creates;
            const auto* type = static_cast<const VkSemaphoreTypeCreateInfo*>(create->pNext);
            Expect(device == f.Device && create->sType == VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
                && type && type->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO
                && type->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE && type->initialValue == 0,
                "Scheduler timeline creation differs.");
            if (f.CreateResult != VK_SUCCESS) return f.CreateResult;
            *name = f.NullTimeline ? VK_NULL_HANDLE : f.Timeline; f.Live = !f.NullTimeline;
            return VK_SUCCESS;
        }
        static VKAPI_ATTR void VKAPI_CALL Destroy(VkDevice device, VkSemaphore name, const VkAllocationCallbacks*)
        {
            auto& f = *active; Expect(device == f.Device && name == f.Timeline && f.Live, "Invalid/double timeline destruction.");
            f.Live = false; ++f.Destroys;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Submit(VkQueue queue, std::uint32_t count, const VkSubmitInfo2* work, VkFence fence)
        {
            auto& f = *active; ++f.Submits;
            Expect(queue == f.Queue && count == 1 && f.Live && work->signalSemaphoreInfoCount, "Invalid queue dispatch.");
            if (f.ExpectAttribution) {
                const auto* id = static_cast<const VkLatencySubmissionPresentIdNV*>(work->pNext);
                Expect(id && id->sType == VK_STRUCTURE_TYPE_LATENCY_SUBMISSION_PRESENT_ID_NV, "Attribution pNext missing.");
                f.AttributedId = id->presentID; f.AttributedNext = id->pNext;
            }
            f.Work = *work; f.Fence = fence;
            f.Signals.assign(work->pSignalSemaphoreInfos, work->pSignalSemaphoreInfos + work->signalSemaphoreInfoCount);
            f.Waits.clear(); f.Commands.clear();
            if (work->waitSemaphoreInfoCount) f.Waits.assign(work->pWaitSemaphoreInfos, work->pWaitSemaphoreInfos + work->waitSemaphoreInfoCount);
            if (work->commandBufferInfoCount) f.Commands.assign(work->pCommandBufferInfos, work->pCommandBufferInfos + work->commandBufferInfoCount);
            const auto& marker = f.Signals.back();
            Expect(marker.sType == VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO && marker.semaphore == f.Timeline
                && marker.value == f.LastSignal + 1 && marker.stageMask == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                "Submission marker is unordered or does not cover all commands.");
            if (f.SubmitResult == VK_SUCCESS) f.LastSignal = marker.value;
            return f.SubmitResult;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Counter(VkDevice device, VkSemaphore name, std::uint64_t* value)
        {
            auto& f = *active; ++f.Polls;
            Expect(device == f.Device && name == f.Timeline && f.Live, "Invalid timeline poll.");
            if (f.PollResult == VK_SUCCESS) *value = f.Complete;
            return f.PollResult;
        }
        static VKAPI_ATTR VkResult VKAPI_CALL Wait(VkDevice device, const VkSemaphoreWaitInfo* info, std::uint64_t timeout)
        {
            auto& f = *active; ++f.BudgetWaits;
            Expect(device == f.Device && info->semaphoreCount == 1 && *info->pSemaphores == f.Timeline
                && *info->pValues == f.LastSignal && timeout == 2'000'000, "Budget must wait for latest submission with bounded timeout.");
            if (f.WaitResult == VK_SUCCESS) f.Complete = f.LastSignal;
            return f.WaitResult;
        }
        VulkanFrameScheduler::Dispatch Dispatch()
        { return {Device, Queue, Create, Destroy, Submit, Counter, Check, Wait}; }
    };
    void Run()
    {
        Fake f;
        for (unsigned field = 0; field < 7; ++field)
        {
            auto dispatch = f.Dispatch();
            switch (field)
            {
            case 0: dispatch.Device = VK_NULL_HANDLE; break;
            case 1: dispatch.Queue = VK_NULL_HANDLE; break;
            case 2: dispatch.CreateSemaphore = nullptr; break;
            case 3: dispatch.DestroySemaphore = nullptr; break;
            case 4: dispatch.QueueSubmit = nullptr; break;
            case 5: dispatch.CounterValue = nullptr; break;
            case 6: dispatch.CheckResult = nullptr; break;
            }
            Reject([&] { VulkanFrameScheduler invalid(dispatch); });
        }
        Expect(!f.Creates && !f.Live, "Incomplete dispatch created native storage.");
        f.CreateResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        NativeFailure([&] { VulkanFrameScheduler invalid(f.Dispatch()); }, f.CreateResult, BackendErrorKind::OutOfMemory);
        f.CreateResult = VK_SUCCESS; f.NullTimeline = true;
        Reject([&] { VulkanFrameScheduler invalid(f.Dispatch()); });
        f.NullTimeline = false;
        Expect(!f.Live && !f.Destroys, "Failed construction destroyed an unowned semaphore.");
        {
            VulkanFrameScheduler scheduler(f.Dispatch());
            Expect(f.Live && scheduler.Submitted().Value == 0 && scheduler.Poll().Value == 0, "Initial queue progress differs.");
            std::array<VkSemaphoreSubmitInfo, 2> waits{};
            std::array<VkSemaphoreSubmitInfo, 2> signals{};
            std::array<VkCommandBufferSubmitInfo, 2> commands{};
            for (unsigned i = 0; i < 2; ++i)
            {
                waits[i] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                waits[i].semaphore = Handle<VkSemaphore>(10 + i); waits[i].value = 17 + i;
                waits[i].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT; waits[i].deviceIndex = i;
                signals[i] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
                signals[i].semaphore = Handle<VkSemaphore>(20 + i); signals[i].value = 27 + i;
                signals[i].stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT; signals[i].deviceIndex = i;
                commands[i] = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
                commands[i].commandBuffer = Handle<VkCommandBuffer>(30 + i); commands[i].deviceMask = i + 1;
            }
            int extension = 1;
            VkSubmitInfo2 work{VK_STRUCTURE_TYPE_SUBMIT_INFO_2}; work.pNext = &extension;
            work.flags = VK_SUBMIT_PROTECTED_BIT;
            work.waitSemaphoreInfoCount = 2; work.pWaitSemaphoreInfos = waits.data();
            work.signalSemaphoreInfoCount = 2; work.pSignalSemaphoreInfos = signals.data();
            work.commandBufferInfoCount = 2; work.pCommandBufferInfos = commands.data();
            for (unsigned kind = 0; kind < 4; ++kind)
            {
                auto invalid = work;
                if (kind == 0) invalid.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                if (kind == 1) invalid.pWaitSemaphoreInfos = nullptr;
                if (kind == 2) invalid.pCommandBufferInfos = nullptr;
                if (kind == 3) invalid.pSignalSemaphoreInfos = nullptr;
                Reject([&] { (void)scheduler.Submit(invalid); });
            }
            Expect(!f.Submits && !scheduler.Submitted().Value, "Rejected work reached the queue.");
            for (std::uint64_t serial = 1; serial <= 64; ++serial)
            {
                const bool external = serial % 3 == 0;
                const auto fence = Handle<VkFence>(40 + serial);
                const auto issue = [&] { return external ? scheduler.MarkExternalWork() : scheduler.Submit(work, fence); };
                if (serial == 7 || serial == 18)
                {
                    f.SubmitResult = serial == 7 ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_DEVICE_LOST;
                    const auto kind = serial == 7 ? BackendErrorKind::OutOfMemory : BackendErrorKind::DeviceLost;
                    NativeFailure(issue, f.SubmitResult, kind);
                    Expect(scheduler.Submitted().Value == serial - 1 && f.LastSignal == serial - 1,
                        "Failed queue submission consumed a serial.");
                    f.SubmitResult = VK_SUCCESS;
                }
                Expect(issue().Value == serial && scheduler.Submitted().Value == serial, "Successful serial differs.");
                if (external)
                    Expect(!f.Fence && f.Waits.empty() && f.Commands.empty() && f.Signals.size() == 1,
                        "External-work marker copied stale native work.");
                else
                {
                    Expect(f.Fence == fence && f.Work.pNext == work.pNext && f.Work.flags == work.flags
                        && f.Work.pWaitSemaphoreInfos == waits.data() && f.Work.pCommandBufferInfos == commands.data()
                        && f.Waits.size() == 2 && f.Commands.size() == 2 && f.Signals.size() == 3
                        && work.signalSemaphoreInfoCount == 2 && work.pSignalSemaphoreInfos == signals.data(),
                        "Queue wrapper changed caller work, extension, flags or fence.");
                    for (unsigned i = 0; i < 2; ++i)
                        Expect(f.Waits[i].semaphore == waits[i].semaphore && f.Waits[i].value == waits[i].value
                            && f.Waits[i].stageMask == waits[i].stageMask && f.Waits[i].deviceIndex == waits[i].deviceIndex
                            && f.Signals[i].semaphore == signals[i].semaphore && f.Signals[i].value == signals[i].value
                            && f.Signals[i].stageMask == signals[i].stageMask && f.Signals[i].deviceIndex == signals[i].deviceIndex
                            && f.Commands[i].commandBuffer == commands[i].commandBuffer && f.Commands[i].deviceMask == commands[i].deviceMask,
                            "Caller wait/signal/command records were changed.");
                }
                const auto previous = scheduler.Completed();
                Expect(scheduler.Poll() == previous, "Pending GPU work completed without a timeline value.");
                f.Complete = serial; Expect(scheduler.Poll().Value == serial, "Real completion was lost.");
                f.Complete = serial - 1; Expect(scheduler.Poll().Value == serial, "Completion moved backwards.");
            }
            f.Complete = 65; Reject([&] { (void)scheduler.Poll(); });
            Expect(scheduler.Completed().Value == 64, "Future completion corrupted queue progress.");
            f.PollResult = VK_ERROR_DEVICE_LOST;
            NativeFailure([&] { (void)scheduler.Poll(); }, f.PollResult, BackendErrorKind::DeviceLost);
            Expect(scheduler.Completed().Value == 64 && scheduler.Submitted().Value == 64, "Failed poll changed progress.");
            f.PollResult = VK_SUCCESS; f.Complete = 64;
        }
        Expect(!f.Live && f.Destroys == 1 && f.LastSignal == 64, "Drained scheduler leaked or destroyed twice.");
    }
}
void Budget()
{
    Fake f;
    VulkanFrameScheduler scheduler(f.Dispatch());
    Expect(scheduler.WaitForLatest(2'000'000) && !f.BudgetWaits, "Idle budget must not wait.");
    (void)scheduler.MarkExternalWork(); (void)scheduler.MarkExternalWork();
    f.Complete = 1; f.WaitResult = VK_TIMEOUT;
    Expect(!scheduler.WaitForLatest(2'000'000) && scheduler.Submitted().Value == 2
        && scheduler.Completed().Value == 1 && f.Submits == 2, "Busy budget changed submissions or established completion.");
    f.WaitResult = VK_ERROR_DEVICE_LOST;
    NativeFailure([&] { (void)scheduler.WaitForLatest(2'000'000); }, f.WaitResult, BackendErrorKind::DeviceLost);
    Expect(scheduler.Completed().Value == 1, "Failed budget established completion.");
    f.WaitResult = VK_SUCCESS;
    Expect(scheduler.WaitForLatest(2'000'000) && scheduler.Completed().Value == 2, "Latest work completion was lost.");
    const auto waits = f.BudgetWaits;
    Expect(scheduler.WaitForLatest(2'000'000) && waits == f.BudgetWaits, "Completed budget waited again.");
}
void Attribution()
{
    Fake f; f.ExpectAttribution = true; auto dispatch = f.Dispatch();
    std::uint64_t id = 5; unsigned markers = 0;
    dispatch.Attribution = [&] { return std::optional(id); };
    dispatch.RenderSubmitStart = [&] { ++markers; };
    VulkanFrameScheduler scheduler(dispatch);
    VkSubmitInfo2 work{VK_STRUCTURE_TYPE_SUBMIT_INFO_2}; int original = 4; work.pNext = &original;
    (void)scheduler.Submit(work);
    Expect(f.AttributedId == 5 && f.AttributedNext == &original && markers == 1, "Attribution replaced existing extension chain.");
    (void)scheduler.MarkExternalWork(false); Expect(f.AttributedId == 5 && markers == 1, "Attribution-establishing marker counted as real render span.");
    ++id; (void)scheduler.Submit(work); Expect(f.AttributedId == 6, "Next frame submission reused stale identity.");
    id = 0; (void)scheduler.MarkExternalWork(); Expect(f.AttributedId == 0, "Off/fallback did not clear explicit queue attribution.");
}
int main()
{
    try { Run(); Budget(); Attribution(); std::cout << "Vulkan queue scheduler PASS; latest-submission bounded budget; 64 serials; external markers; native arrays/fence preserved; submit/poll failure; completion guards; creation rollback\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
