#pragma once

#include "Backend.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace MphRead::NativeRuntime::Rhi
{
    enum class BackendErrorKind : std::uint8_t
    { DeviceLost, SurfaceLost, OutOfMemory, Unsupported, Unknown };

    struct BackendFailure final
    {
        GraphicsBackend backend;
        BackendErrorKind kind;
        std::int64_t nativeCode;
        std::string message;
    };

    class BackendError final : public std::runtime_error
    {
    public:
        BackendError(GraphicsBackend backend, BackendErrorKind kind, std::int64_t nativeCode, std::string message)
            : std::runtime_error(std::move(message)), _backend(backend), _kind(kind), _nativeCode(nativeCode) {}
        explicit BackendError(const BackendFailure& failure)
            : BackendError(failure.backend, failure.kind, failure.nativeCode, failure.message) {}
        [[nodiscard]] GraphicsBackend Backend() const noexcept { return _backend; }
        [[nodiscard]] BackendErrorKind Kind() const noexcept { return _kind; }
        [[nodiscard]] std::int64_t NativeCode() const noexcept { return _nativeCode; }
        [[nodiscard]] BackendFailure Failure() const { return {_backend, _kind, _nativeCode, what()}; }
    private:
        GraphicsBackend _backend;
        BackendErrorKind _kind;
        std::int64_t _nativeCode;
    };
}
