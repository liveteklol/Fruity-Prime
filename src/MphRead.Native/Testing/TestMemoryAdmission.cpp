#include "../NativeRuntime/Rhi/MemoryBudget.hpp"
#include "../NativeRuntime/Rhi/OpenGL/OpenGlMemory.hpp"
#include "../NativeRuntime/Rhi/BackendError.hpp"
#include <iostream>
#include <stdexcept>

using namespace MphRead::NativeRuntime::Rhi;
namespace
{
    void Expect(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
    std::int32_t capacityKiB = 1024, freeKiB = 512;
    std::int32_t MemoryCounter(std::int32_t counter)
    {
        if (counter == 0x9047) return capacityKiB;
        if (counter == 0x9049) return freeKiB;
        throw std::runtime_error("GL memory query combined different native pools.");
    }
    void CheckGlSource()
    {
        OpenGL::OpenGlMemory unknown{false, MemoryCounter};
        Expect(unknown.Snapshot(64).Heaps[0].Source == MemoryBudgetSource::Unknown, "GL invented a portable budget.");
        unknown.Admit(1, 64);
        OpenGL::OpenGlMemory live{true, MemoryCounter};
        const auto heap = live.Snapshot(64).Heaps[0];
        Expect(heap.Source == MemoryBudgetSource::DriverLive && heap.CapacityBytes == 1024 * 1024
            && heap.BudgetBytes == heap.CapacityBytes && heap.UsageBytes == 512 * 1024 && heap.ReservedBytes == 64,
            "NVX memory pool or KiB conversion differs.");
        freeKiB = 0;
        bool rejected = false;
        try { live.Admit(1, 0); }
        catch (const BackendError& error) { rejected = error.Kind() == BackendErrorKind::OutOfMemory && error.NativeCode() == 0; }
        Expect(rejected && live.Telemetry().DeniedRequests == 1 && !live.Telemetry().NativeFailures,
            "GL admission failure was treated as native OOM.");
        freeKiB = -1;
        Expect(live.Snapshot(0).Heaps[0].Source == MemoryBudgetSource::Unknown, "Invalid NVX memory count accepted.");
        freeKiB = 2048;
        Expect(live.Snapshot(0).Heaps[0].Source == MemoryBudgetSource::Unknown, "Invalid NVX counter pair accepted.");
        freeKiB = 512;
        bool nativeOom = false;
        try { live.CheckNativeResult(0x0505, "test"); } catch (const BackendError& error)
        { nativeOom = error.NativeCode() == 0x0505 && error.Kind() == BackendErrorKind::OutOfMemory
            && error.Backend() == GraphicsBackend::OpenGl; }
        Expect(nativeOom && live.Telemetry().NativeFailures == 1, "GL native OOM/error telemetry was lost.");
        bool contextLost = false;
        try { live.CheckNativeResult(0x0507, "injected lost context"); }
        catch (const BackendError& error)
        { contextLost = error.Kind() == BackendErrorKind::DeviceLost && error.NativeCode() == 0x0507; }
        Expect(contextLost && live.Telemetry().NativeFailures == 2, "GL context loss became an unknown storage error.");
        Expect(OpenGL::TextureStorageEstimate(TextureFormat::RGBA16Float, 4, 8) == 256
            && OpenGL::TextureStorageEstimate(TextureFormat::RGB8Unorm, 4, 8) == 128,
            "GL float/RGB padding estimate differs.");
        live.Close(); Expect(live.Snapshot(0).HeapCount == 0, "Closed GL owner queried native memory.");
    }
    void Check()
    {
        MemoryBudgetSnapshot snapshot{};
        Expect(EvaluateAllocation(snapshot, {1}).Reason == AllocationReason::BudgetUnavailable,
            "Missing budget was reported as a live guarantee.");
        Expect(!EvaluateAllocation(snapshot, {0}).Allowed, "Empty admission request accepted.");
        snapshot.HeapCount = 2;
        snapshot.Heaps[0] = {MemoryBudgetSource::DriverLive, 1024, 1000, 700, 600, 100, true};
        snapshot.Heaps[1] = {MemoryBudgetSource::DriverLive, 2048, 1900, 100, 200, 0, false};
        auto decision = EvaluateAllocation(snapshot, {200, 0, 1, false});
        Expect(decision.Allowed && decision.AvailableBytes == 200, "Usage/reserved were double counted.");
        Expect(!EvaluateAllocation(snapshot, {201, 0, 1, false}).Allowed, "Pending heap reservations ignored.");
        Expect(EvaluateAllocation(snapshot, {201, 1, 1, false}).Allowed, "Independent heap did not retain its own budget.");
        Expect(!EvaluateAllocation(snapshot, {200, 0}).Allowed, "Safety reserve was ignored.");
        snapshot.Heaps[0].ReservedBytes = 800;
        Expect(EvaluateAllocation(snapshot, {100, 0, 1, false}).Allowed, "Known backing storage did not dominate lagging usage.");
        snapshot.Heaps[0].BudgetBytes = 0;
        Expect(!EvaluateAllocation(snapshot, {1, 0}).Allowed, "Live zero budget was treated as unavailable.");
        snapshot.Heaps[0].Source = MemoryBudgetSource::Unknown;
        Expect(EvaluateAllocation(snapshot, {1, 0}).Reason == AllocationReason::BudgetUnavailable, "Unknown is not distinct from exhausted.");
        snapshot.Heaps[0].Source = MemoryBudgetSource::Estimated;
        snapshot.Heaps[0].BudgetBytes = 1024;
        snapshot.Heaps[0].Source = static_cast<MemoryBudgetSource>(255);
        Expect(EvaluateAllocation(snapshot, {1}).Reason == AllocationReason::InvalidSnapshot, "Invalid budget source accepted.");
        snapshot.Heaps[0].Source = MemoryBudgetSource::Estimated;
        snapshot.Heaps[0].UsageBytes = 0; snapshot.Heaps[0].ReservedBytes = 0; snapshot.Heaps[0].PendingBytes = 0;
        Expect(EvaluateAllocation(snapshot, {960}).Allowed && !EvaluateAllocation(snapshot, {961}).Allowed,
            "Estimated/UMA budget reserve differs at its boundary.");
        snapshot.MaxAllocationBytes = 16;
        Expect(EvaluateAllocation(snapshot, {17}).Reason == AllocationReason::AllocationSize, "Maximum allocation size ignored.");
        snapshot.MaxAllocationBytes = 0;
        snapshot.MaxNativeAllocations = 8; snapshot.NativeAllocationCount = 6; snapshot.PendingNativeAllocations = 1;
        Expect(EvaluateAllocation(snapshot, {1}).Allowed, "Exact native count boundary rejected.");
        Expect(EvaluateAllocation(snapshot, {1, 0, 2}).Reason == AllocationReason::AllocationCount, "Pending native allocations ignored.");
        snapshot.HeapCount = 33;
        Expect(EvaluateAllocation(snapshot, {1}).Reason == AllocationReason::InvalidSnapshot, "Oversized heap snapshot accepted.");
        snapshot.HeapCount = 2;
        Expect(EvaluateAllocation(snapshot, {1, 2}).Reason == AllocationReason::InvalidSnapshot, "Wrong heap index accepted.");
        snapshot.Heaps[0].BudgetBytes = 1025;
        Expect(EvaluateAllocation(snapshot, {1}).Reason == AllocationReason::InvalidSnapshot, "Budget larger than capacity accepted.");
        snapshot.Heaps[0].BudgetBytes = 1024;
        snapshot.MaxNativeAllocations = 0; snapshot.NativeAllocationCount = UINT64_MAX;
        Expect(EvaluateAllocation(snapshot, {1}).Reason == AllocationReason::ArithmeticOverflow, "Native count overflow accepted.");
        snapshot.NativeAllocationCount = 0; snapshot.PendingNativeAllocations = 0;
        snapshot.Heaps[0].UsageBytes = UINT64_MAX; snapshot.Heaps[0].PendingBytes = 1;
        Expect(EvaluateAllocation(snapshot, {1}).Reason == AllocationReason::ArithmeticOverflow, "Byte accounting overflow accepted.");
        // A multi-heap device cannot pool free system RAM into the requested
        // device-local heap. UMA is represented as one heap, not counted twice.
        snapshot = {}; snapshot.HeapCount = 1;
        snapshot.Heaps[0] = {MemoryBudgetSource::DriverLive, 4096, 3000, 2048, 2048, 0, true};
        Expect(EvaluateAllocation(snapshot, {760}).Allowed && !EvaluateAllocation(snapshot, {766}).Allowed,
            "Unified heap safety reserve/accounting differs.");
    }
}
int main()
{
    try { Check(); CheckGlSource(); std::cout << "Memory admission: live/estimated/unknown, heaps, UMA, reservations, limits, overflow, NVX source/units/errors PASS\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
