#pragma once
#include "LowLatency.hpp"

#include "Resources.hpp"
#include "BackendError.hpp"

#include <cstdint>
#include <optional>

namespace MphRead::NativeRuntime::Rhi
{
    enum class PresentMode : std::uint8_t
    {
        Immediate,
        Fifo,
        Mailbox
    };

    struct SwapchainDesc final
    {
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        std::uint32_t imageCount = 2;
        TextureFormat format = TextureFormat::BGRA8Unorm;
        PresentMode presentMode = PresentMode::Fifo;

        bool operator==(const SwapchainDesc&) const = default;
    };

    enum class PresentationStatus : std::uint8_t
    { Ready, ResizeRequired, TemporarilyUnavailable, SurfaceLost, DeviceLost };

    struct AcquireResult final
    {
        PresentationStatus status = PresentationStatus::Ready;
        Texture* texture = nullptr;
        std::optional<BackendFailure> failure;
    };
    struct PresentResult final
    {
        PresentationStatus status = PresentationStatus::Ready;
        std::optional<BackendFailure> failure;
        bool accepted = false; // Independent of recreation/loss status.
    };

    struct PresentationCapabilities final
    {
        bool immediate = false;
        bool fifo = true;
        bool mailbox = false;
        std::uint32_t minImageCount = 1;
        // Zero means that the surface does not impose a maximum.
        std::uint32_t maxImageCount = 0;
    };

    [[nodiscard]] inline PresentationStatus PresentationFailure(const BackendError& error)
    {
        if (error.Kind() == BackendErrorKind::DeviceLost) return PresentationStatus::DeviceLost;
        if (error.Kind() == BackendErrorKind::SurfaceLost) return PresentationStatus::SurfaceLost;
        throw error;
    }

    [[nodiscard]] inline AcquireResult FailedAcquire(const BackendError& error)
    { return {PresentationFailure(error), nullptr, error.Failure()}; }
    [[nodiscard]] inline PresentResult FailedPresent(const BackendError& error)
    { return {PresentationFailure(error), error.Failure()}; }
    inline void RequirePresentation(const PresentResult& result, GraphicsBackend backend)
    {
        if (result.failure) throw BackendError(*result.failure);
        if (result.status == PresentationStatus::DeviceLost || result.status == PresentationStatus::SurfaceLost)
            throw BackendError(backend, result.status == PresentationStatus::DeviceLost
                ? BackendErrorKind::DeviceLost : BackendErrorKind::SurfaceLost, 0, "Renderer presentation was lost.");
    }

    class Swapchain
    {
    public:
        virtual ~Swapchain() = default;
        Swapchain(const Swapchain&) = delete;
        Swapchain& operator=(const Swapchain&) = delete;
        Swapchain(Swapchain&&) = delete;
        Swapchain& operator=(Swapchain&&) = delete;

        [[nodiscard]] virtual const SwapchainDesc& Desc() const noexcept = 0;
        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;
        [[nodiscard]] virtual Texture& AcquireNextTexture() = 0;
        virtual void SetPresentMode(PresentMode mode) = 0;
        virtual void Present() = 0;
        virtual void ConfigureLowLatency(LowLatencyMode, std::uint32_t) {}
        [[nodiscard]] virtual LowLatencyCapabilities LowLatencyCaps() const noexcept
        { return {true, false, LowLatencyProvider::Generic}; }
        [[nodiscard]] virtual bool BeginLowLatencyFrame() { return true; }
        virtual void MarkLowLatency(LowLatencyMarker) {}
        [[nodiscard]] virtual LowLatencyDiagnostics LowLatencyStats() const noexcept { return {}; }
        // Nonblocking for unavailable/minimized surfaces. The synchronous
        // diagnostic entry point above remains available to capture callers.
        [[nodiscard]] virtual AcquireResult TryAcquireTexture()
        {
            try { return {PresentationStatus::Ready, &AcquireNextTexture()}; }
            catch (const BackendError& error) { return FailedAcquire(error); }
        }
        [[nodiscard]] virtual PresentResult TryPresent()
        {
            try { Present(); return {PresentationStatus::Ready, std::nullopt, true}; }
            catch (const BackendError& error) { return FailedPresent(error); }
        }
        [[nodiscard]] virtual PresentationCapabilities PresentationCaps() const noexcept = 0;
        [[nodiscard]] virtual PresentMode RequestedPresentMode() const noexcept = 0;

    protected:
        Swapchain() = default;
    };
}
