#pragma once

#include "../../RendererGpuMesh.hpp"
#include "GraphicsDevice.hpp"
#include "SceneShaders.hpp"
#include "WindowUi.hpp"

#include <memory>
#include <stdexcept>
#include <string>

namespace MphRead::RendererPlatform { class Window; }

namespace MphRead::NativeRuntime::Rhi
{
    struct BackendSessionOptions final { bool validation = false; };

    // One renderer's lifetime. The owner releases scene/UI objects and the
    // swapchain before Shutdown, and keeps the platform window alive until
    // Shutdown returns. A session never owns the match simulation.
    class BackendSession
    {
    public:
        virtual ~BackendSession() = default;
        [[nodiscard]] virtual GraphicsBackend Backend() const noexcept = 0;
        [[nodiscard]] virtual GraphicsDevice& Device() = 0;
        [[nodiscard]] virtual bool PresentsWindow() const noexcept = 0;
        [[nodiscard]] virtual unsigned ValidationErrors() const noexcept = 0;
        [[nodiscard]] virtual bool ValidationEnabled() const noexcept { return false; }
        [[nodiscard]] virtual std::string Describe() = 0;
        [[nodiscard]] virtual std::unique_ptr<Swapchain> CreateSwapchain(
            ::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc) = 0;
        [[nodiscard]] virtual PresentResult Present(Swapchain& swapchain) = 0;
        virtual void ResetViewport(std::int32_t width, std::int32_t height) = 0;
        virtual void Shutdown() noexcept = 0;
        [[nodiscard]] virtual std::unique_ptr<SceneShaderSet> CreateShaders(
            GraphicsDevice& device, CommandList& commands, const SceneShaderSources& sources) = 0;
        [[nodiscard]] virtual std::shared_ptr<MphRead::GpuMeshResource> CreateMesh(
            GraphicsDevice& device, CommandList& commands, const MphRead::RendererGeometry& geometry) = 0;
        [[nodiscard]] virtual std::shared_ptr<MphRead::TransientGeometryResource> CreateTransient(
            GraphicsDevice& device, CommandList& commands) = 0;
        [[nodiscard]] virtual std::unique_ptr<WindowUi> CreateUi(GraphicsDevice&) { return nullptr; }

        virtual void AttachSurface(void*) { throw std::runtime_error("This backend has no native surface attachment."); }
        virtual void DetachSurface() noexcept {}
        [[nodiscard]] virtual std::unique_ptr<Swapchain> CreateSurfaceSwapchain(const SwapchainDesc&)
        { throw std::runtime_error("This backend has no native surface swapchain."); }
    };

    class BackendProvider
    {
    public:
        virtual ~BackendProvider() = default;
        [[nodiscard]] virtual GraphicsBackend Backend() const noexcept = 0;
        // Eligibility only: no logical device, queue, or GPU resource creation.
        [[nodiscard]] virtual std::string ProbePassive(bool windowUi) const = 0;
        [[nodiscard]] virtual std::unique_ptr<BackendSession> CreateSession(BackendSessionOptions options) const = 0;
    };

    // Only implemented providers are registered. Future APIs are enum values,
    // never silently mapped to an existing renderer.
    [[nodiscard]] const BackendProvider* FindBackendProvider(GraphicsBackend backend) noexcept;
}
