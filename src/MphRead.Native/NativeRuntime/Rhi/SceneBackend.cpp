#include "SceneBackend.hpp"
#include "WindowUi.hpp"

#include "BackendSession.hpp"
#include "../Skia/VulkanInterop.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include "Vulkan/VulkanGraphicsDevice.hpp"
#endif

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace MphRead::NativeRuntime::Rhi
{
    namespace
    {
        GraphicsBackend selected = GraphicsBackend::OpenGl;
        SceneBackendRequest requested = SceneBackendRequest::OpenGL;
        bool requestExplicit = false;
        bool resolved = false;
        bool needsWindowUi = false;
        bool validation = false;

        std::unique_ptr<BackendSession> session;
        unsigned retiredValidationErrors = 0;

        BackendSession& Session()
        {
            const GraphicsBackend backend = SelectedSceneBackend();
            if (session && session->Backend() != backend)
                throw std::logic_error("Release the outgoing renderer session before selecting another backend.");
            if (!session)
            {
                const auto* provider = FindBackendProvider(backend);
                if (!provider) throw SceneBackendUnavailable(std::string(SceneBackendName(backend)) + " is not implemented.");
                session = provider->CreateSession({validation});
            }
            return *session;
        }
        BackendSession& SessionFor(GraphicsDevice& device)
        {
            auto& current = Session();
            if (device.GetBackend() != current.Backend() || &current.Device() != &device)
                throw std::invalid_argument("Scene resource belongs to another renderer session.");
            return current;
        }
    }

    void RequestSceneBackend(SceneBackendRequest request, bool explicitRequest) noexcept
    {
        if (resolved || (requestExplicit && !explicitRequest)) return;
        requested = request;
        requestExplicit = requestExplicit || explicitRequest;
    }

    SceneBackendRequest RequestedSceneBackend() noexcept { return requested; }

    void ReselectSceneBackend(SceneBackendRequest request) noexcept
    {
        requested = request;
        // Chosen by the person, now: an unavailable Vulkan is an error, as
        // -rhi vulkan is, never a quiet OpenGL.
        requestExplicit = true;
        resolved = false;
    }
    void SceneBackendNeedsWindowUi(bool value) noexcept { needsWindowUi = value; }

    bool ParseSceneBackendRequest(std::string_view text, SceneBackendRequest& request) noexcept
    {
        if (text == "opengl" || text == "gl") { request = SceneBackendRequest::OpenGL; return true; }
        if (text == "vulkan" || text == "vk") { request = SceneBackendRequest::Vulkan; return true; }
        if (text == "auto") { request = SceneBackendRequest::Auto; return true; }
        return false;
    }

    std::string_view SceneBackendRequestName(SceneBackendRequest request) noexcept
    {
        switch (request)
        {
        case SceneBackendRequest::Vulkan: return "vulkan";
        case SceneBackendRequest::Auto: return "auto";
        case SceneBackendRequest::OpenGL:
        default: return "opengl";
        }
    }

    std::string VulkanUnavailableReason(bool forWindow)
    {
        const auto* provider = FindBackendProvider(GraphicsBackend::Vulkan);
        return provider ? provider->ProbePassive(forWindow) : "this build has no Vulkan backend";
    }

    GraphicsBackend SelectedSceneBackend()
    {
        if (resolved) return selected;
        if (requested == SceneBackendRequest::OpenGL)
        {
            selected = GraphicsBackend::OpenGl;
            resolved = true;
            return selected;
        }
        const std::string why = VulkanUnavailableReason(needsWindowUi);
        if (requested == SceneBackendRequest::Vulkan && !why.empty())
            throw SceneBackendUnavailable("Vulkan was asked for and cannot start: " + why + ".");
        selected = why.empty() ? GraphicsBackend::Vulkan : GraphicsBackend::OpenGl;
        resolved = true;
        if (!why.empty()) std::cout << "[render] auto: OpenGL, since " << why << std::endl;
        return selected;
    }

    void SelectSceneBackend(GraphicsBackend kind)
    {
        if (kind != GraphicsBackend::OpenGl && kind != GraphicsBackend::Vulkan)
            throw SceneBackendUnavailable(std::string(SceneBackendName(kind)) + " is not implemented.");
        selected = kind;
        requested = kind == GraphicsBackend::Vulkan ? SceneBackendRequest::Vulkan : SceneBackendRequest::OpenGL;
        resolved = true;
    }
    void SetSceneValidation(bool enabled) noexcept { validation = enabled; }

    bool ParseSceneBackend(std::string_view text, GraphicsBackend& kind) noexcept
    {
        if (text == "opengl" || text == "gl") { kind = GraphicsBackend::OpenGl; return true; }
        if (text == "vulkan" || text == "vk") { kind = GraphicsBackend::Vulkan; return true; }
        return false;
    }

    std::string_view SceneBackendName(GraphicsBackend kind) noexcept
    {
        switch (kind)
        {
        case GraphicsBackend::OpenGl: return "opengl";
        case GraphicsBackend::Vulkan: return "vulkan";
        case GraphicsBackend::Metal: return "metal";
        case GraphicsBackend::D3D12: return "d3d12";
        }
        return "unknown";
    }

    GraphicsDevice& SceneDevice()
    {
        try { return Session().Device(); }
        catch (const SceneBackendUnavailable&) { throw; }
        catch (const BackendError& ex)
        { throw SceneBackendUnavailable(std::string(SceneBackendName(ex.Backend())) + " could not start: " + ex.what(), ex.Failure()); }
        catch (const std::exception& ex)
        { throw SceneBackendUnavailable(std::string(SceneBackendName(SelectedSceneBackend())) + " could not start: " + ex.what()); }
    }

    unsigned SceneValidationErrors() noexcept
    {
        return retiredValidationErrors + (session ? session->ValidationErrors() : 0U);
    }

    bool ScenePresentsWindow()
    {
        return Session().PresentsWindow();
    }

    std::unique_ptr<WindowUi> CreateSceneWindowUi(GraphicsDevice& device)
    {
        return SessionFor(device).CreateUi(device);
    }

    void ResetWindowViewport(std::int32_t width, std::int32_t height)
    {
        Session().ResetViewport(width, height);
    }

    std::string SceneBackendContract()
    {
        std::string text = "opengl=compiled\n";
#if defined(FRUITY_HAS_VULKAN)
        text += "vulkan=compiled\n";
        text += "vulkan-shader-stages=" + std::to_string(Vulkan::EmbeddedShaderStages()) + "\n";
#else
        text += "vulkan=absent\nvulkan-shader-stages=0\n";
#endif
#if defined(MPHREAD_QT)
        // The Qt menus draw through Qt Quick on the scene's own device.
        text += "skia-vulkan=absent\nui=qt\n";
#else
        text += std::string("skia-vulkan=") + (Skia::VulkanInterop::Available() ? "compiled" : "absent") + "\n";
#endif
#if defined(__ANDROID__)
        text += "platform=android\n";
#elif defined(_WIN32)
        text += "platform=windows\n";
#elif defined(__APPLE__)
        text += "platform=macos\n";
#else
        text += "platform=linux\n";
#endif
        return text;
    }

    std::string DescribeSceneBackend(const Swapchain* swapchain)
    {
        std::string line = "requested " + std::string(SceneBackendRequestName(requested))
            + (requestExplicit ? " (command line)" : "") + ", selected "
            + std::string(SceneBackendName(SelectedSceneBackend())) + ", " + Session().Describe();
        if (swapchain)
        {
            const auto& desc = swapchain->Desc();
            line += ", swapchain " + std::to_string(desc.width) + "x" + std::to_string(desc.height);
        }
        line += ", frames in flight " + std::to_string(FramesInFlight)
            + ", validation " + (Session().ValidationEnabled() ? "on" : "off");
        return line;
    }

    void AttachSceneSurface(void* nativeWindow) { Session().AttachSurface(nativeWindow); }
    void DetachSceneSurface() noexcept { if (session) session->DetachSurface(); }
    std::unique_ptr<Swapchain> CreateSceneSurfaceSwapchain(const SwapchainDesc& desc)
    { return Session().CreateSurfaceSwapchain(desc); }

    std::unique_ptr<Swapchain> CreateSceneWindowSwapchain(
        ::MphRead::RendererPlatform::Window& window, const SwapchainDesc& desc)
    {
        try { return Session().CreateSwapchain(window, desc); }
        catch (const SceneBackendUnavailable&) { throw; }
        catch (const BackendError& ex)
        {
            DetachSceneWindow();
            throw SceneBackendUnavailable(std::string(SceneBackendName(ex.Backend())) + " could not start: " + ex.what(), ex.Failure());
        }
        catch (const std::exception& ex)
        {
            DetachSceneWindow();
            throw SceneBackendUnavailable(std::string(SceneBackendName(SelectedSceneBackend())) + " could not start: " + ex.what());
        }
    }

    LowLatencyCapabilities SceneLowLatencyCaps() noexcept
    {
        if (!session) return {};
        try { return session->Device().LowLatencyCaps(); } catch (...) { return {}; }
    }

    PresentResult PresentSceneWindow(Swapchain& swapchain)
    {
        const auto result = Session().Present(swapchain);
        RequirePresentation(result, Session().Backend());
        return result;
    }

    void DetachSceneWindow() noexcept
    {
        if (!session) return;
        session->Shutdown();
        retiredValidationErrors += session->ValidationErrors();
        session.reset();
    }

    std::unique_ptr<SceneShaderSet> CreateSceneShaderSet(
        GraphicsDevice& device, CommandList& commands, const SceneShaderSources& sources)
    { return SessionFor(device).CreateShaders(device, commands, sources); }

    std::shared_ptr<MphRead::GpuMeshResource> CreateSceneGpuMesh(
        GraphicsDevice& device, CommandList& commands, const MphRead::RendererGeometry& geometry)
    { return SessionFor(device).CreateMesh(device, commands, geometry); }

    std::shared_ptr<MphRead::TransientGeometryResource> CreateSceneTransientGeometry(
        GraphicsDevice& device, CommandList& commands)
    { return SessionFor(device).CreateTransient(device, commands); }
}
