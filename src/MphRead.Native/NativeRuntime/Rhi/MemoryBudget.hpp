#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace MphRead::NativeRuntime::Rhi
{
    enum class MemoryBudgetSource : std::uint8_t { Unknown, Estimated, DriverLive };
    struct MemoryHeapBudget final
    {
        MemoryBudgetSource Source = MemoryBudgetSource::Unknown;
        std::uint64_t CapacityBytes = 0, BudgetBytes = 0, UsageBytes = 0;
        // Reserved backing storage is already part of Usage when the driver
        // sees it. Admission uses max(Usage, Reserved), never their sum.
        std::uint64_t ReservedBytes = 0, PendingBytes = 0;
        bool DeviceLocal = false;
    };
    struct MemoryBudgetSnapshot final
    {
        static constexpr std::uint32_t MaxHeaps = 32;
        std::array<MemoryHeapBudget, MaxHeaps> Heaps{};
        std::uint32_t HeapCount = 0;
        // 0 means unavailable, not a zero-size/count limit. Counts are the
        // observed native blocks; a backend must document external owners.
        std::uint64_t MaxAllocationBytes = 0;
        std::uint64_t NativeAllocationCount = 0, PendingNativeAllocations = 0, MaxNativeAllocations = 0;
    };
    struct MemoryTelemetry final
    {
        std::uint64_t AcceptedRequests = 0, DeniedRequests = 0, NativeFailures = 0;
        std::uint64_t PendingRequests = 0, PendingBytes = 0;
    };
    struct AllocationRequest final
    {
        std::uint64_t Bytes = 0;
        std::uint32_t Heap = 0;
        std::uint64_t NewNativeAllocations = 1;
        // Keep min(128 MiB, 1/16 of budget) by default. Caller may choose 0
        // explicitly for a diagnostic, without inventing a driver budget.
        bool KeepSafetyReserve = true;
    };
    enum class AllocationReason : std::uint8_t
    { Allowed, BudgetUnavailable, InvalidRequest, InvalidSnapshot, AllocationSize, AllocationCount, ArithmeticOverflow, HeapBudget };
    struct AllocationDecision final
    {
        bool Allowed = false;
        AllocationReason Reason = AllocationReason::InvalidRequest;
        std::uint64_t AvailableBytes = 0;
        MemoryBudgetSource Source = MemoryBudgetSource::Unknown;
    };
    [[nodiscard]] inline std::string_view AllocationReasonName(AllocationReason reason) noexcept
    {
        switch (reason)
        {
        case AllocationReason::Allowed: return "allowed";
        case AllocationReason::BudgetUnavailable: return "budget unavailable";
        case AllocationReason::InvalidRequest: return "invalid allocation request";
        case AllocationReason::InvalidSnapshot: return "invalid memory snapshot";
        case AllocationReason::AllocationSize: return "maximum allocation size";
        case AllocationReason::AllocationCount: return "maximum allocation count";
        case AllocationReason::ArithmeticOverflow: return "memory accounting overflow";
        case AllocationReason::HeapBudget: return "heap budget and safety reserve";
        }
        return "unknown admission reason";
    }
    // Pure policy. Source selection, eligible memory types, native requirements,
    // reservations and native failure handling belong to the backend owner.
    [[nodiscard]] inline AllocationDecision EvaluateAllocation(
        const MemoryBudgetSnapshot& snapshot, const AllocationRequest& request) noexcept
    {
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        if (!request.Bytes) return {};
        if (snapshot.HeapCount > snapshot.Heaps.size() || (snapshot.HeapCount && request.Heap >= snapshot.HeapCount))
            return {false, AllocationReason::InvalidSnapshot};
        if (snapshot.MaxAllocationBytes && request.Bytes > snapshot.MaxAllocationBytes)
            return {false, AllocationReason::AllocationSize};
        if (snapshot.PendingNativeAllocations > maximum - snapshot.NativeAllocationCount
            || request.NewNativeAllocations > maximum - snapshot.NativeAllocationCount - snapshot.PendingNativeAllocations)
            return {false, AllocationReason::ArithmeticOverflow};
        if (snapshot.MaxNativeAllocations && snapshot.NativeAllocationCount + snapshot.PendingNativeAllocations
            + request.NewNativeAllocations > snapshot.MaxNativeAllocations)
            return {false, AllocationReason::AllocationCount};
        if (!snapshot.HeapCount) return {true, AllocationReason::BudgetUnavailable};
        const auto& heap = snapshot.Heaps[request.Heap];
        if (heap.Source != MemoryBudgetSource::Unknown && heap.Source != MemoryBudgetSource::Estimated
            && heap.Source != MemoryBudgetSource::DriverLive)
            return {false, AllocationReason::InvalidSnapshot};
        const auto occupied = std::max(heap.UsageBytes, heap.ReservedBytes);
        if (heap.PendingBytes > maximum - occupied || request.Bytes > maximum - occupied - heap.PendingBytes)
            return {false, AllocationReason::ArithmeticOverflow, 0, heap.Source};
        if (heap.Source == MemoryBudgetSource::Unknown) return {true, AllocationReason::BudgetUnavailable};
        if (heap.CapacityBytes && heap.BudgetBytes > heap.CapacityBytes)
            return {false, AllocationReason::InvalidSnapshot, 0, heap.Source};
        const auto reserve = request.KeepSafetyReserve ? std::min<std::uint64_t>(128ULL << 20U, heap.BudgetBytes / 16) : 0;
        const auto usable = heap.BudgetBytes - reserve;
        const auto used = occupied + heap.PendingBytes;
        const auto available = used <= usable ? usable - used : 0;
        return {used <= usable && request.Bytes <= available,
            used <= usable && request.Bytes <= available ? AllocationReason::Allowed : AllocationReason::HeapBudget,
            available, heap.Source};
    }
}
