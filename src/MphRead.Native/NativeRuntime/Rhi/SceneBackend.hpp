#pragma once

#include "../../RendererGpuMesh.hpp"
#include "CommandList.hpp"
#include "GraphicsDevice.hpp"
#include "SceneShaders.hpp"
#include "Swapchain.hpp"

namespace MphRead::RendererPlatform
{
    class Window;
}

#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

// Which backend the scene renderer draws with, and the four things the Scene
// asks a backend for: its device, its programs, its meshes and its transient
// geometry. The Scene names no backend; this is the only place that does.
namespace MphRead::NativeRuntime::Rhi
{
    // What the player (launcher.txt's renderer) or the command line (-rhi)
    // asked for. Auto takes Vulkan when this build and this machine can run
    // it -- the window and its launcher included -- and OpenGL otherwise.
    enum class SceneBackendRequest : std::uint8_t
    {
        OpenGL,
        Vulkan,
        Auto
    };

    // An explicitly requested backend that cannot start. Never answered by
    // quietly starting the other one: the message says what is missing.
    class SceneBackendUnavailable final : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
        SceneBackendUnavailable(std::string message, BackendFailure failure)
            : std::runtime_error(std::move(message)), _failure(std::move(failure)) {}
        [[nodiscard]] const std::optional<BackendFailure>& Failure() const noexcept { return _failure; }
    private:
        std::optional<BackendFailure> _failure;
    };

    // Both attempts failed. Keep the original typed exceptions available to
    // diagnostics instead of replacing them with a successful-recovery claim.
    class SceneBackendRecoveryFailed final : public std::runtime_error
    {
    public:
        SceneBackendRecoveryFailed(std::string message, std::exception_ptr incoming, std::exception_ptr recovery)
            : std::runtime_error(std::move(message)), _incoming(std::move(incoming)), _recovery(std::move(recovery)) {}
        [[nodiscard]] std::exception_ptr Incoming() const noexcept { return _incoming; }
        [[nodiscard]] std::exception_ptr Recovery() const noexcept { return _recovery; }
    private:
        std::exception_ptr _incoming, _recovery;
    };

    // explicitRequest: the command line's, which the preference cannot
    // replace. Takes effect before the first scene device is asked for.
    void RequestSceneBackend(SceneBackendRequest request, bool explicitRequest) noexcept;
    [[nodiscard]] SceneBackendRequest RequestedSceneBackend() noexcept;
    // Settings switched the renderer while the program runs: choose again
    // the next time a device or window is asked for. The caller has closed
    // the window and released every GPU resource of the old backend.
    void ReselectSceneBackend(SceneBackendRequest request) noexcept;
    // The launcher will draw into the window (the shell): Vulkan then also
    // needs a Skia that can draw through it. Harness windows never say so.
    void SceneBackendNeedsWindowUi(bool value) noexcept;
    // "opengl" / "gl" / "vulkan" / "vk" / "auto"; false for anything else.
    [[nodiscard]] bool ParseSceneBackendRequest(std::string_view text, SceneBackendRequest& request) noexcept;
    [[nodiscard]] std::string_view SceneBackendRequestName(SceneBackendRequest request) noexcept;

    // The backend the request resolved to, decided once. Throws
    // SceneBackendUnavailable for an explicit Vulkan request this build or
    // this machine cannot honour.
    [[nodiscard]] GraphicsBackend SelectedSceneBackend();
    void SelectSceneBackend(GraphicsBackend kind);
    // "opengl" / "gl" / "vulkan" / "vk"; false for anything else.
    [[nodiscard]] bool ParseSceneBackend(std::string_view text, GraphicsBackend& kind) noexcept;
    [[nodiscard]] std::string_view SceneBackendName(GraphicsBackend kind) noexcept;
    // Why Vulkan cannot be used here, or empty when it can. forWindow: the
    // presented window's launcher as well (Skia with Vulkan).
    [[nodiscard]] std::string VulkanUnavailableReason(bool forWindow);
    // What this binary carries, one "key=value" a line, for CI to assert:
    // the backends, Skia's Vulkan, the embedded SPIR-V stages and the
    // scene programs. Needs no GPU and no game files.
    [[nodiscard]] std::string SceneBackendContract();
    // One line for the startup log: requested, selected, GPU, API, driver,
    // swapchain and depth formats, frames in flight, validation.
    [[nodiscard]] std::string DescribeSceneBackend(const Swapchain* swapchain);
    // Vulkan's validation layers for the scene device, when it is created.
    void SetSceneValidation(bool enabled) noexcept;

    // The current session's device. A switch releases the outgoing session
    // before creating the incoming one; headless diagnostics create lazily.
    [[nodiscard]] GraphicsDevice& SceneDevice();
    // Validation errors the Vulkan scene device has reported (0 for OpenGL).
    [[nodiscard]] unsigned SceneValidationErrors() noexcept;

    // The game window, when the scene backend presents it itself: Vulkan
    // makes the scene device on this window's surface, and the swapchain on
    // that same device. OpenGL's window is its context and needs neither.
    [[nodiscard]] bool ScenePresentsWindow();
    [[nodiscard]] std::unique_ptr<Swapchain> CreateSceneWindowSwapchain(
        ::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc);
    // Android's Vulkan path: the GameView's ANativeWindow*. The first surface
    // makes the device; later ones replace only the surface, so the game and
    // every GPU resource survive a pause or a rotation. Detach releases the
    // surface (the caller has released its swapchain) and keeps the device.
    void AttachSceneSurface(void* nativeWindow);
    void DetachSceneSurface() noexcept;
    [[nodiscard]] std::unique_ptr<Swapchain> CreateSceneSurfaceSwapchain(const SwapchainDesc& desc);
    // A window that has just loaded a scene: the full-window viewport.
    // OpenGL's is context state that outlives a draw; Vulkan sets it per
    // pass, so there it is nothing.
    void ResetWindowViewport(std::int32_t width, std::int32_t height);
    // End the frame: submit, and show the scene device's window target.
    PresentResult PresentSceneWindow(Swapchain& swapchain);
    [[nodiscard]] LowLatencyCapabilities SceneLowLatencyCaps() noexcept;
    // Before the window goes: every scene is gone, and the device follows.
    void DetachSceneWindow() noexcept;

    [[nodiscard]] std::unique_ptr<SceneShaderSet> CreateSceneShaderSet(
        GraphicsDevice& device, CommandList& commands, const SceneShaderSources& sources);
    [[nodiscard]] std::shared_ptr<MphRead::GpuMeshResource> CreateSceneGpuMesh(
        GraphicsDevice& device, CommandList& commands, const MphRead::RendererGeometry& geometry);
    [[nodiscard]] std::shared_ptr<MphRead::TransientGeometryResource> CreateSceneTransientGeometry(
        GraphicsDevice& device, CommandList& commands);
}
