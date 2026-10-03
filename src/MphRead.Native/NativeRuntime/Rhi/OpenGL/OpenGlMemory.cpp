#include "OpenGlMemory.hpp"
#include "../BackendError.hpp"
#include <string>
#include <stdexcept>

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    MemoryBudgetSnapshot OpenGlMemory::Snapshot(std::uint64_t reserved) const
    {
        if (!_query) return {};
        MemoryBudgetSnapshot snapshot{};
        snapshot.HeapCount = 1;
        auto& heap = snapshot.Heaps[0];
        heap.ReservedBytes = reserved;
        if (_nvx)
        {
            // Both counters describe dedicated memory. TOTAL_AVAILABLE also
            // includes other storage, so it cannot be paired with this free count.
            const auto capacity = _query(0x9047); // DEDICATED_VIDMEM_NVX, KiB
            const auto free = _query(0x9049); // CURRENT_AVAILABLE_VIDMEM_NVX, KiB
            if (capacity > 0 && free >= 0 && free <= capacity)
            {
                heap.Source = MemoryBudgetSource::DriverLive;
                heap.CapacityBytes = heap.BudgetBytes = static_cast<std::uint64_t>(capacity) * 1024;
                heap.UsageBytes = static_cast<std::uint64_t>(capacity - free) * 1024;
                heap.DeviceLocal = true;
            }
        }
        if (_checkCeiling)
        {
            heap.Source = MemoryBudgetSource::Estimated;
            heap.BudgetBytes = heap.CapacityBytes ? std::min(heap.BudgetBytes, _checkCeiling) : _checkCeiling;
        }
        return snapshot;
    }
    void OpenGlMemory::Admit(std::uint64_t bytes, std::uint64_t reserved)
    {
        if (!_query) throw std::logic_error("OpenGL memory owner is closed.");
        const auto decision = EvaluateAllocation(Snapshot(reserved), {bytes});
        if (!decision.Allowed)
        {
            ++_telemetry.DeniedRequests;
            throw BackendError(GraphicsBackend::OpenGl, BackendErrorKind::OutOfMemory, 0,
                "OpenGL memory admission denied: " + std::string(AllocationReasonName(decision.Reason))
                + " (" + std::to_string(bytes) + " bytes).");
        }
        ++_telemetry.AcceptedRequests;
    }
    void OpenGlMemory::CheckNativeResult(std::int32_t error, const char* operation)
    {
        if (!error) return;
        ++_telemetry.NativeFailures;
        throw BackendError(GraphicsBackend::OpenGl,
            error == 0x0507 ? BackendErrorKind::DeviceLost
                : error == 0x0505 ? BackendErrorKind::OutOfMemory : BackendErrorKind::Unknown, error,
            std::string(operation) + " failed: " + std::to_string(error));
    }
    std::uint64_t TextureStorageEstimate(TextureFormat format, std::uint32_t width, std::uint32_t height)
    {
        std::uint32_t bytes;
        switch (format)
        {
        case TextureFormat::R8Unorm: bytes = 1; break;
        case TextureFormat::RG8Unorm: case TextureFormat::R16Float: case TextureFormat::D16Unorm: bytes = 2; break;
        case TextureFormat::RGBA16Float: case TextureFormat::RG32Float: case TextureFormat::D32FloatS8Uint: bytes = 8; break;
        case TextureFormat::RGB32Float: case TextureFormat::RGBA32Float: bytes = 16; break;
        case TextureFormat::RGBA8Unorm: case TextureFormat::RGBA8Srgb: case TextureFormat::BGRA8Unorm:
        case TextureFormat::BGRA8Srgb: case TextureFormat::RGB8Unorm: case TextureFormat::RG16Float:
        case TextureFormat::R32Float: case TextureFormat::D32Float: case TextureFormat::D24UnormS8Uint: bytes = 4; break;
        default: throw std::invalid_argument("Unsupported GL texture storage estimate.");
        }
        const auto pixels = static_cast<std::uint64_t>(width) * height;
        if (!pixels || pixels > UINT64_MAX / bytes) throw std::out_of_range("GL texture storage size overflows.");
        return pixels * bytes;
    }
}
