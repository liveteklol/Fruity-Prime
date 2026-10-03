#include "../NativeRuntime/Rhi/FrameContext.hpp"
#include "../NativeRuntime/Rhi/Swapchain.hpp"
#include "../NativeRuntime/Rhi/GpuDiagnostics.hpp"
#include "../Mods/Diagnostics/FrameStatistics.hpp"
#include "../NativeRuntime/Rhi/OpenGL/OpenGlFrameScheduler.hpp"
#include "../NativeRuntime/Rhi/PresentationScheduler.hpp"
#include <unordered_set>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Actual completion tokens control retirement; frame slots are independent.
// Fake native dispatch tests fence failures, timeout and completion ordering.
namespace
{
    using namespace MphRead::NativeRuntime::Rhi;

    [[noreturn]] void Fail(std::string_view message)
    {
        throw std::runtime_error(std::string(message));
    }

    void Expect(bool value, std::string_view message)
    {
        if (!value)
        {
            Fail(message);
        }
    }

    void TestRetiredObjectsWaitForCompletion()
    {
        RetirementQueue<int> queue;
        std::vector<int> destroyed;
        const auto destroy = [&destroyed](int value) { destroyed.push_back(value); };
        queue.Retire(1, {5});
        queue.Retire(2, {6});
        queue.Retire(3, {7});
        Expect(queue.Collect({4}, destroy) == 0 && destroyed.empty(),
            "nothing is destroyed before its submission completes");
        Expect(queue.Collect({6}, destroy) == 2, "submissions 5 and 6 complete");
        Expect(destroyed == std::vector<int>{1, 2}, "in the order they were retired");
        Expect(queue.Size() == 1, "submission 7's object is still waiting");
        Expect(queue.Collect({7}, destroy) == 1 && queue.Size() == 0, "and goes when submission 7 completes");
    }

    struct FakeGlStream final
    {
        using Scheduler = OpenGL::OpenGlFrameScheduler;
        std::uintptr_t Accepted = 0, Completed = 0;
        std::unordered_set<std::uintptr_t> Live;
        unsigned Finishes = 0, Flushes = 0, BlockingWaits = 0;
        bool Refuse = false, WaitFails = false, TimeoutOnce = false;
        unsigned NativeError = 0;
        Scheduler::Dispatch Dispatch(bool sync = true)
        {
            return {this,
                [](void* context) -> void* {
                    auto& state = *static_cast<FakeGlStream*>(context);
                    if (state.Refuse) return nullptr;
                    state.Live.insert(++state.Accepted);
                    return reinterpret_cast<void*>(state.Accepted);
                },
                [](void* context, void* fence, bool flush, std::uint64_t timeout) {
                    auto& state = *static_cast<FakeGlStream*>(context);
                    const auto token = reinterpret_cast<std::uintptr_t>(fence);
                    Expect(state.Live.contains(token), "wait uses a live native fence");
                    if (state.WaitFails) return Scheduler::WaitStatus::Failed;
                    if (token <= state.Completed) return Scheduler::WaitStatus::AlreadySignaled;
                    if (!timeout) return Scheduler::WaitStatus::Timeout;
                    Expect(flush, "a blocking wait submits buffered commands");
                    ++state.BlockingWaits;
                    if (state.TimeoutOnce) { state.TimeoutOnce = false; return Scheduler::WaitStatus::Timeout; }
                    state.Completed = token;
                    return Scheduler::WaitStatus::Satisfied;
                },
                [](void* context, void* fence) {
                    Expect(static_cast<FakeGlStream*>(context)->Live.erase(reinterpret_cast<std::uintptr_t>(fence)) == 1,
                        "native fence deleted exactly once");
                },
                [](void* context) { ++static_cast<FakeGlStream*>(context)->Flushes; },
                [](void* context) { auto& state = *static_cast<FakeGlStream*>(context); ++state.Finishes; state.Completed = state.Accepted; },
                [](void* context) { return std::exchange(static_cast<FakeGlStream*>(context)->NativeError, 0U); }, sync};
        }
    };

    void TestGlNativeSubmissionProof()
    {
        FakeGlStream stream;
        FakeGlStream::Scheduler scheduler(stream.Dispatch());
        RetirementQueue<int> queue;
        std::vector<int> destroyed;
        const auto destroy = [&destroyed](int value) { destroyed.push_back(value); };
        const auto first = scheduler.Submit();
        const auto second = scheduler.Submit(true);
        queue.Retire(1, first); queue.Retire(2, second);
        queue.Collect(scheduler.Poll(), destroy);
        Expect(destroyed.empty() && stream.Flushes == 1 && scheduler.Completed() == SubmissionSerial{},
            "accepted or flushed work is not proof of completion");
        stream.Refuse = true; stream.NativeError = 0x0505;
        bool failed = false;
        try { (void)scheduler.Submit(); }
        catch (const BackendError& error) { failed = error.Kind() == BackendErrorKind::OutOfMemory && error.NativeCode() == 0x0505; }
        Expect(failed && scheduler.Submitted() == second && stream.Live.size() == 2,
            "failed fence insertion consumes no serial and keeps existing fences");
        stream.Refuse = false;
        stream.Completed = 1;
        queue.Collect(scheduler.Poll(), destroy);
        Expect(destroyed == std::vector<int>{1} && queue.Size() == 1 && stream.Live.size() == 1,
            "only actual completed native work is collected");
        const auto third = scheduler.Submit();
        Expect(third == SubmissionSerial{3}, "retry has no submission gap");
        stream.TimeoutOnce = true;
        scheduler.Wait(third);
        queue.Collect(scheduler.Completed(), destroy);
        Expect(destroyed == std::vector<int>{1, 2} && stream.Live.empty()
            && stream.BlockingWaits == 2 && scheduler.DeviceWideWaits() == 0,
            "slot wait retries timeout without finishing the whole device");
        const auto fourth = scheduler.Submit();
        stream.WaitFails = true; stream.NativeError = 0x0502; failed = false;
        try { (void)scheduler.Poll(); }
        catch (const BackendError& error) { failed = error.NativeCode() == 0x0502; }
        Expect(failed && scheduler.Completed() == third && stream.Live.size() == 1,
            "WAIT_FAILED must not establish completion or discard a live fence");
        scheduler.Finish();
        Expect(scheduler.Completed() == fourth && scheduler.DeviceWideWaits() == 1 && stream.Live.empty(),
            "explicit idle establishes final completion and drains native fences");
    }

    void TestPresentationPolicy()
    {
        using namespace std::chrono_literals;
        const PresentationScheduler::Time start{};
        PresentationScheduler pacer;
        pacer.Configure({0, 100, PresentMode::Fifo, PacingAuthority::Generic});
        Expect(pacer.Deadline(start) == start && !pacer.PreviousAcceptedPresentId(), "No deadline before accepted present.");
        pacer.Accepted(start);
        Expect(pacer.Deadline(start) == start + 10ms && pacer.PreviousAcceptedPresentId() == 1, "Display rate deadline/ID differ.");
        Expect(pacer.Deadline(start + 10ms) == start + 10ms, "Blocking FIFO present must consume, not duplicate, the pacing period.");
        pacer.Accepted(start + 12ms);
        Expect(pacer.TargetDisplayTime(start + 12ms) == start + 20ms, "One late frame lost phase.");
        pacer.Accepted(start + 100ms);
        Expect(pacer.Deadline(start + 100ms) == start + 110ms, "Stall created catch-up burst.");
        pacer.Unavailable();
        Expect(pacer.Deadline(start) == start && pacer.PreviousAcceptedPresentId() == 3, "Unavailable advanced accepted ID.");
        pacer.Configure({50, 100, PresentMode::Immediate, PacingAuthority::Generic});
        pacer.Accepted(start); Expect(pacer.Deadline(start) == start + 20ms, "Explicit cap differs.");
        pacer.Configure({50, 100, PresentMode::Fifo, PacingAuthority::Native});
        pacer.Accepted(start); Expect(pacer.Deadline(start) == start, "Native authority received double pacing.");
        pacer.Configure({-1, 100, PresentMode::Immediate, PacingAuthority::Generic});
        pacer.Accepted(start); Expect(pacer.Deadline(start) == start, "Unlimited immediate was capped.");
        const LowLatencyCapabilities generic{true, false, LowLatencyProvider::Generic};
        auto state = ResolveLowLatency(LowLatencyMode::Off, generic);
        Expect(state.effective == LowLatencyMode::Off && state.provider == LowLatencyProvider::None, "Off enabled provider.");
        state = ResolveLowLatency(LowLatencyMode::OnBoost, generic);
        Expect(state.requested == LowLatencyMode::OnBoost && state.effective == LowLatencyMode::On
            && !state.boostSupported && !state.fallbackReason.empty() && state.authority == PacingAuthority::Generic, "Boost fallback lost request/reason.");
        state = ResolveLowLatency(state.requested, {true, true, LowLatencyProvider::Nvidia});
        Expect(state.effective == LowLatencyMode::OnBoost && state.authority == PacingAuthority::Native, "Capability switch lost logical request.");
        state = ResolveLowLatency(state.requested, generic);
        Expect(state.effective == LowLatencyMode::On && state.requested == LowLatencyMode::OnBoost, "Switch back lost request.");
        state = ResolveLowLatency(LowLatencyMode::On, {});
        Expect(state.effective == LowLatencyMode::Off && !state.fallbackReason.empty(), "Unavailable provider silently enabled.");
    }

    void TestGlPresentationBudget()
    {
        FakeGlStream stream;
        FakeGlStream::Scheduler scheduler(stream.Dispatch());
        Expect(scheduler.WaitForLatest(2'000'000) && !stream.BlockingWaits, "Idle GL budget waited.");
        (void)scheduler.Submit(); const auto latest = scheduler.Submit();
        const auto pendingRetirement = scheduler.SubmitForRetirement();
        stream.Completed = 1; stream.TimeoutOnce = true;
        Expect(!scheduler.WaitForLatest(2'000'000) && scheduler.Completed() == SubmissionSerial{1}
            && scheduler.Submitted() == latest && !stream.Finishes, "Busy GL budget mutated work or waited device-wide.");
        Expect(scheduler.WaitForLatest(2'000'000) && scheduler.Completed() == latest
            && stream.BlockingWaits == 2 && stream.Live.empty(), "GL budget used next slot instead of latest fence.");
        Expect(pendingRetirement == SubmissionSerial{3} && scheduler.Submitted() == latest
            && scheduler.RetirementPending(), "Budget inserted a new retirement submission.");
        const auto waits = stream.BlockingWaits;
        Expect(scheduler.WaitForLatest(2'000'000) && stream.BlockingWaits == waits, "Completed GL budget waited again.");
        scheduler.Finish();
    }

    void TestLegacyGlCompletionFallback()
    {
        FakeGlStream stream;
        FakeGlStream::Scheduler scheduler(stream.Dispatch(false));
        const auto token = scheduler.Submit();
        Expect(token == SubmissionSerial{1} && scheduler.Completed() == token
            && stream.Finishes == 1 && stream.Accepted == 0,
            "unsupported legacy sync uses real synchronous completion, never a frame counter");
    }

    void TestGlFailedShutdownAndRetirement()
    {
        FakeGlStream stream;
        FakeGlStream::Scheduler scheduler(stream.Dispatch());
        const auto submitted = scheduler.Submit();
        stream.NativeError = 0x0507;
        bool failed = false;
        try { scheduler.Finish(); }
        catch (const BackendError& error)
        { failed = error.Kind() == BackendErrorKind::DeviceLost && error.NativeCode() == 0x0507; }
        Expect(failed && scheduler.Completed() == SubmissionSerial{} && stream.Live.size() == 1,
            "failed glFinish must not establish completion or discard the fence");
        failed = false;
        try { scheduler.Finish(); }
        catch (const BackendError& error)
        { failed = error.Kind() == BackendErrorKind::DeviceLost && error.NativeCode() == 0x0507; }
        Expect(failed && stream.Finishes == 1 && scheduler.Completed() == SubmissionSerial{},
            "consuming glGetError must not clear the lost-context boundary");

        RetirementQueue<int> queue;
        stream.Refuse = true; stream.NativeError = 0x0507;
        queue.Retire(42, scheduler.SubmitForRetirement());
        queue.Retire(43, scheduler.SubmitForRetirement());
        Expect(scheduler.Submitted() == submitted && queue.Size() == 2,
            "native destructor failure must retain resources without accepting a marker");
        failed = false;
        try { (void)scheduler.Poll(); }
        catch (const BackendError& error)
        { failed = error.Kind() == BackendErrorKind::DeviceLost && error.NativeCode() == 0x0507; }
        Expect(failed, "next explicit operation must report the first retirement failure");
        std::vector<int> destroyed;
        const auto destroy = [&destroyed](int value) { destroyed.push_back(value); };
        queue.Collect(submitted, destroy);
        Expect(destroyed.empty(), "failed retirement has no collectable completion proof");
        // Explicit context destruction closes ownership without claiming that
        // the failed stream completed. Detached wrappers cannot use it again.
        queue.CollectAll(destroy);
        Expect(destroyed == std::vector<int>{42, 43} && scheduler.Completed() == SubmissionSerial{},
            "context shutdown releases ownership without falsifying GPU completion");

        FakeGlStream fenceStream;
        FakeGlStream::Scheduler fenceScheduler(fenceStream.Dispatch());
        fenceStream.Refuse = true; fenceStream.NativeError = 0x0505;
        const auto unproven = fenceScheduler.SubmitForRetirement();
        failed = false;
        try { (void)fenceScheduler.Submit(); }
        catch (const BackendError& error)
        { failed = error.Kind() == BackendErrorKind::OutOfMemory && error.NativeCode() == 0x0505; }
        Expect(failed && unproven > fenceScheduler.Submitted() && fenceStream.Accepted == 0,
            "failed destructor fence must defer its exact error without inventing a submission");

        FakeGlStream queryStream;
        FakeGlStream::Scheduler queryScheduler(queryStream.Dispatch());
        (void)queryScheduler.Submit();
        try { throw BackendError(GraphicsBackend::OpenGl, BackendErrorKind::DeviceLost, 0x0507, "glQueryCounter"); }
        catch (...) { queryScheduler.NoteDeviceLost(std::current_exception()); }
        failed = false;
        try { queryScheduler.Finish(); }
        catch (const BackendError& error) { failed = error.NativeCode() == 0x0507; }
        Expect(failed && queryStream.Finishes == 0 && queryScheduler.Completed() == SubmissionSerial{},
            "a consumed query/storage context loss must prevent false completion");
    }

    void TestFramesInFlightSlots()
    {
        // BeginFrame for frame N may reuse the slot of frame N - FramesInFlight.
        static_assert(FramesInFlight == 2, "the plan's two frames in flight");
        for (std::uint64_t frame = 1; frame < 10; ++frame)
        {
            const auto slot = static_cast<std::uint32_t>(frame % FramesInFlight);
            const auto earlier = static_cast<std::uint32_t>((frame - FramesInFlight + 10) % FramesInFlight);
            Expect(slot == earlier, "a slot is shared by frames FramesInFlight apart");
        }
    }

    void TestGpuTimestampPolicy()
    {
        Expect(TimestampNanoseconds(250, 5, {8, 2.5}) == 27.5, "GPU timestamp wrap or fractional period differs");
        Expect(TimestampNanoseconds(UINT64_MAX - 2, 4, {64, 1}) == 7, "64-bit GPU timestamp wrap differs");
        bool rejected = false;
        try { (void)TimestampNanoseconds(0, 1, {0, 1}); } catch (const std::invalid_argument&) { rejected = true; }
        Expect(rejected, "unsupported timestamps must not appear to have a duration");
        TimestampWriteState writes(64);
        rejected = false;
        try { writes.Write(0); } catch (const std::logic_error&) { rejected = true; }
        Expect(rejected, "timestamp was written before initialization");
        writes.Initialize();
        for (unsigned index = 0; index < 64; ++index) writes.Write(index);
        Expect(writes.AllWritten(), "64-bit timestamp mask is incomplete");
        rejected = false;
        try { writes.Initialize(); } catch (const std::logic_error&) { rejected = true; }
        Expect(rejected, "in-flight timestamp reset was accepted");
        rejected = false;
        try { writes.Write(0); } catch (const std::logic_error&) { rejected = true; }
        Expect(rejected, "timestamp query overwrite was accepted");
        auto budget = std::make_shared<TimestampBudget>();
        std::vector<std::shared_ptr<void>> leases;
        for (unsigned i = 0; i < TimestampBudget::Limit; ++i) leases.push_back(budget->TryReserve());
        Expect(budget->Sets() == 8 && !budget->TryReserve(), "GPU timestamp capacity was exceeded");
        auto nativeRetirement = std::move(leases.back()); leases.pop_back();
        Expect(!budget->TryReserve(), "retired native query escaped capacity accounting");
        nativeRetirement.reset();
        Expect(budget->TryReserve() != nullptr, "native destruction did not return timestamp capacity");
    }
    void TestMeasuredFrameStatistics()
    {
        MphRead::Mods::Diagnostics::FrameStatistics statistics;
        for (int i = 0; i < 99; ++i) statistics.Add(0.001, 0.0005, 0.0001);
        statistics.Add(0.01, 0.002, 0.001);
        const auto result = statistics.Result();
        Expect(result.frames == 100 && std::abs(result.fps - 100 / 0.109) < 0.001
            && result.p50Ms == 1 && result.p95Ms == 1 && result.p99Ms == 1,
            "FPS is not total presented intervals / elapsed seconds or percentiles differ");
        statistics.Reset();
        for (int i = 0; i < 9000; ++i) statistics.Add(0.0001, 0.00001, 0);
        Expect(statistics.Result().frames == 9000 && statistics.Result().percentileSamples == 8192,
            "bounded percentile storage lost the exact FPS count");
    }

    void TestCancelledObjectsAreNotDestroyed()
    {
        RetirementQueue<int> queue;
        queue.Retire(10, {1});
        queue.Retire(11, {1});
        queue.Retire(10, {2});
        Expect(queue.Cancel([](int value) { return value == 10; }) == 2, "both entries for 10 are taken back");
        std::vector<int> destroyed;
        (void)queue.CollectAll([&destroyed](int value) { destroyed.push_back(value); });
        Expect(destroyed == std::vector<int>{11}, "only 11 is destroyed");
    }

    void TestIdleDestroysEverything()
    {
        RetirementQueue<int> queue;
        for (int i = 0; i < 100; ++i)
        {
            queue.Retire(i, {static_cast<std::uint64_t>(1000 + i)});
        }
        std::size_t count = 0;
        Expect(queue.CollectAll([&count](int) { ++count; }) == 100 && count == 100 && queue.Size() == 0,
            "waiting for idle destroys every retired object");
    }

    void TestPresentationFailuresStayTyped()
    {
        const BackendError lost(GraphicsBackend::Vulkan, BackendErrorKind::DeviceLost, -4, "device lost");
        Expect(PresentationFailure(lost) == PresentationStatus::DeviceLost, "device loss status");
        Expect(lost.Backend() == GraphicsBackend::Vulkan && lost.NativeCode() == -4,
            "native diagnostic information is preserved");
        const BackendError surface(GraphicsBackend::OpenGl, BackendErrorKind::SurfaceLost, 0, "surface lost");
        Expect(PresentationFailure(surface) == PresentationStatus::SurfaceLost, "surface loss status");
        bool propagated = false;
        try
        {
            (void)PresentationFailure(BackendError(GraphicsBackend::Vulkan,
                BackendErrorKind::OutOfMemory, -2, "allocation failed"));
        }
        catch (const BackendError& error) { propagated = error.Kind() == BackendErrorKind::OutOfMemory; }
        Expect(propagated, "allocation errors must not masquerade as surface unavailability");
    }

    class FaultSwapchain final : public Swapchain
    {
    public:
        explicit FaultSwapchain(BackendError error) : Error(std::move(error)) {}
        BackendError Error;
        const SwapchainDesc& Desc() const noexcept override { return Description; }
        void Resize(std::uint32_t, std::uint32_t) override {}
        Texture& AcquireNextTexture() override { throw Error; }
        void Present() override { throw Error; }
        void SetPresentMode(PresentMode) override {}
        PresentationCapabilities PresentationCaps() const noexcept override { return {}; }
        PresentMode RequestedPresentMode() const noexcept override { return PresentMode::Fifo; }
    private:
        SwapchainDesc Description;
    };

    void TestPresentationFailureRoundTrip()
    {
        for (const auto backend : {GraphicsBackend::OpenGl, GraphicsBackend::Vulkan})
            for (const auto kind : {BackendErrorKind::DeviceLost, BackendErrorKind::SurfaceLost,
                BackendErrorKind::OutOfMemory, BackendErrorKind::Unsupported, BackendErrorKind::Unknown})
            {
                FaultSwapchain source(BackendError(backend, kind, -987654, "injected native operation failure"));
                const auto verify = [&](const BackendError& error)
                {
                    Expect(error.Backend() == backend && error.Kind() == kind && error.NativeCode() == -987654
                        && std::string_view(error.what()) == source.Error.what(), "facade replaced native failure information");
                };
                bool acquireFailed = false, presentFailed = false;
                try
                {
                    const auto acquired = source.TryAcquireTexture();
                    Expect(!acquired.texture && acquired.failure.has_value(), "failed acquisition lost its error payload");
                    RequirePresentation({acquired.status, acquired.failure}, backend);
                }
                catch (const BackendError& error) { acquireFailed = true; verify(error); }
                try { RequirePresentation(source.TryPresent(), backend); }
                catch (const BackendError& error) { presentFailed = true; verify(error); }
                Expect(acquireFailed && presentFailed, "failure became successful or temporary presentation");
            }
        RequirePresentation({PresentationStatus::Ready}, GraphicsBackend::OpenGl);
        RequirePresentation({PresentationStatus::ResizeRequired}, GraphicsBackend::Vulkan);
        RequirePresentation({PresentationStatus::TemporarilyUnavailable}, GraphicsBackend::Vulkan);
    }

    void TestSubmissionCompletionIsIndependentOfFrames()
    {
        SubmissionProgress progress;
        RetirementQueue<int> queue;
        std::vector<int> destroyed;
        const auto destroy = [&destroyed](int value) { destroyed.push_back(value); };
        // A failed native submit does not consume a serial.
        Expect(progress.Next() == SubmissionSerial{1} && progress.Next() == SubmissionSerial{1},
            "only accepted submissions advance progress");
        for (int submit = 1; submit <= 4; ++submit) progress.Submitted(progress.Next());
        queue.Retire(40, SubmissionSerial{4});
        queue.Retire(20, SubmissionSerial{2});
        progress.Complete({2});
        queue.Collect(progress.Completed(), destroy);
        Expect(destroyed == std::vector<int>{20} && queue.Size() == 1,
            "collection uses actual completion even with unordered retirement");
        progress.Complete({1});
        Expect(progress.Completed() == SubmissionSerial{2}, "stale observations cannot regress completion");
        bool rejected = false;
        try { progress.Complete({5}); } catch (const std::logic_error&) { rejected = true; }
        Expect(rejected && progress.Completed() == SubmissionSerial{2}, "future completion rejected");
        rejected = false;
        try { progress.Submitted({6}); } catch (const std::logic_error&) { rejected = true; }
        Expect(rejected && progress.Submitted() == SubmissionSerial{4}, "submission gaps rejected");
        progress.Complete({4});
        queue.Collect(progress.Completed(), destroy);
        Expect(destroyed == std::vector<int>{20, 40} && queue.Size() == 0,
            "last use survives until its submission completes");
    }
}

int main()
{
    try
    {
        TestRetiredObjectsWaitForCompletion();
        TestFramesInFlightSlots();
        TestGpuTimestampPolicy();
        TestMeasuredFrameStatistics();
        TestGlNativeSubmissionProof();
        TestPresentationPolicy();
        TestGlPresentationBudget();
        TestLegacyGlCompletionFallback();
        TestGlFailedShutdownAndRetirement();
        TestCancelledObjectsAreNotDestroyed();
        TestIdleDestroysEverything();
        TestPresentationFailuresStayTyped();
        TestPresentationFailureRoundTrip();
        TestSubmissionCompletionIsIndependentOfFrames();
        std::cout << "RhiLifetime tests passed.\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "RhiLifetime test failure: " << ex.what() << '\n';
        return 1;
    }
}
