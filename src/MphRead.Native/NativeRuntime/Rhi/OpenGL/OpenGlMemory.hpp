#pragma once

#include "../MemoryBudget.hpp"
#include "../Resources.hpp"

namespace MphRead::NativeRuntime::Rhi::OpenGL
{
    // Context-thread policy boundary. GL exposes no portable heap budget;
    // NVX is optional, and tracked object sizes are only backing estimates.
    class OpenGlMemory final
    {
    public:
        OpenGlMemory(bool nvx, std::int32_t (*query)(std::int32_t)) : _nvx(nvx), _query(query) {}
        [[nodiscard]] MemoryBudgetSnapshot Snapshot(std::uint64_t reserved) const;
        [[nodiscard]] MemoryTelemetry Telemetry() const noexcept { return _telemetry; }
        void Admit(std::uint64_t bytes, std::uint64_t reserved);
        void CheckNativeResult(std::int32_t error, const char* operation);
        void SetBudgetCeilingForCheck(std::uint64_t bytes) noexcept { _checkCeiling = bytes; }
        void Close() noexcept { _query = nullptr; }
    private:
        bool _nvx;
        std::int32_t (*_query)(std::int32_t);
        MemoryTelemetry _telemetry{};
        std::uint64_t _checkCeiling = 0;
    };
    // Conservative GL storage estimate, including RGB/depth padding; no CPU
    // texture copy, mip/layer support or fragmentation guarantee is implied.
    [[nodiscard]] std::uint64_t TextureStorageEstimate(TextureFormat format, std::uint32_t width, std::uint32_t height);
}
