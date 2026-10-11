#include "Renderer.hpp"
#include "Mods/Diagnostics/FramePerformance.hpp"
#include "NativeRuntime/Rhi/BackendFactory.hpp"

#if defined(__ANDROID__)

#include <stdexcept>
#include <utility>

namespace
{
    [[noreturn]] void ThrowDesktopWindowUnavailable()
    {
        throw std::runtime_error(
            "desktop RenderWindow is unavailable on Android; use GameView/AndroidMatch"
        );
    }
}

namespace MphRead::RendererPlatform
{
    std::shared_ptr<Window> CreateWindow(const WindowSettings&)
    {
        ThrowDesktopWindowUnavailable();
    }

    OpenTK::Mathematics::Vector2i WorkAreaForWindow(Window& window)
    {
        return window.CurrentMonitorWorkArea().Size;
    }

    void ProcessEvents()
    {
        // Android input/window events are driven by Activity/View callbacks.
    }

    void InstallGlfwErrorCallback(
        std::function<void(std::int32_t, std::string)>)
    {
        // GLFW is not part of the Android runtime.
    }

    std::int32_t GlfwFeatureUnavailableCode()
    {
        return 0x0001000C;
    }
}

namespace MphRead
{
    std::function<void(std::int32_t, std::string)>
        RenderWindow::_glfwErrorCallback{};

    const RendererPlatform::WindowSettings& RenderWindow::Settings()
    {
        static const RendererPlatform::WindowSettings settings{};
        return settings;
    }

    void RenderWindow::LogCreatingWindow()
    {
    }

    bool RenderWindow::BeforeFrame() { ThrowDesktopWindowUnavailable(); }
    bool RenderWindow::CanSampleInputWhileWaiting() const { return false; }
    void RenderWindow::OnInputSample() { ThrowDesktopWindowUnavailable(); }

    RenderWindow::RenderWindow(bool shell)
        : _shell(shell)
    {
        ThrowDesktopWindowUnavailable();
    }

    RenderWindow::~RenderWindow() = default;

    bool RenderWindow::HasScene() const noexcept
    {
        return _scene != nullptr;
    }

    MphRead::Scene& RenderWindow::Scene() const
    {
        if (_scene == nullptr)
        {
            ThrowDesktopWindowUnavailable();
        }
        return *_scene;
    }

    OpenTK::Mathematics::Vector2i RenderWindow::FramebufferSize() const
    {
        return {};
    }

    void* RenderWindow::WindowPtr() const
    {
        return nullptr;
    }

    void RenderWindow::Title(std::string)
    {
    }

    std::shared_ptr<MphRead::Scene> RenderWindow::NewScene()
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::EndOrClose()
    {
    }

    std::shared_ptr<MphRead::Scene> RenderWindow::NewSideScene()
    {
        ThrowDesktopWindowUnavailable();
    }

    MphRead::Scene& RenderWindow::BeginScene()
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::LoadScene()
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::EndScene()
    {
        _scene.reset();
        _sceneLoaded = false;
    }

    void RenderWindow::FeedKey(
        const RendererPlatform::KeyboardKeyEventArgs&)
    {
    }

    std::pair<double, double> RenderWindow::PointerPixels(
        double x,
        double y) const
    {
        return {x, y};
    }

    bool RenderWindow::OnWayland()
    {
        return false;
    }

    void RenderWindow::IgnoreUnavailableGlfwFeatures()
    {
    }

    void RenderWindow::FitToScreen()
    {
    }

    void RenderWindow::Run()
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::OnClosing()
    {
    }

    void RenderWindow::AddRoom(
        std::int32_t,
        GameMode,
        std::int32_t,
        BossFlags,
        std::int32_t,
        std::int32_t)
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::AddRoom(
        std::string,
        GameMode,
        std::int32_t,
        BossFlags,
        std::int32_t,
        std::int32_t)
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::AddModel(
        std::string,
        std::int32_t,
        bool,
        MetaDir,
        std::optional<OpenTK::Mathematics::Vector3>)
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::AddPlayer(
        Hunter,
        std::int32_t,
        std::int32_t,
        std::optional<OpenTK::Mathematics::Vector3>)
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::QueueMovie(std::int32_t)
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::OnLoad()
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::ApplyFrameRateSettings()
    {
    }

    void RenderWindow::Reveal()
    {
    }

    void RenderWindow::OnRenderFrame(
        const RendererPlatform::FrameEventArgs&)
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::OnResize(
        const RendererPlatform::ResizeEventArgs&)
    {
    }

    void RenderWindow::OnMove(
        const RendererPlatform::WindowPositionEventArgs&)
    {
    }

    void RenderWindow::OnMaximizedChanged(bool)
    {
    }

    void RenderWindow::OnFocusedChanged(bool)
    {
    }

    void RenderWindow::OnMouseDown(
        const RendererPlatform::MouseButtonEventArgs&)
    {
    }

    void RenderWindow::OnMouseUp(
        const RendererPlatform::MouseButtonEventArgs&)
    {
    }

    void RenderWindow::OnMouseMove(
        const RendererPlatform::MouseMoveEventArgs&)
    {
    }

    void RenderWindow::OnMouseWheel(
        const RendererPlatform::MouseWheelEventArgs&)
    {
    }

    void RenderWindow::OnTextInput(
        const RendererPlatform::TextInputEventArgs&)
    {
    }

    void RenderWindow::OnKeyDown(
        const RendererPlatform::KeyboardKeyEventArgs&)
    {
    }

    void RenderWindow::OnKeyUp(
        const RendererPlatform::KeyboardKeyEventArgs&)
    {
    }

    std::int32_t RenderWindow::WindowBorder() const
    {
        return static_cast<std::int32_t>(
            RendererPlatform::WindowBorderValue::Resizable
        );
    }

    void RenderWindow::WindowBorder(std::int32_t)
    {
    }

    OpenTK::Mathematics::Vector2i RenderWindow::Location() const
    {
        return {};
    }

    void RenderWindow::Location(OpenTK::Mathematics::Vector2i)
    {
    }

    OpenTK::Mathematics::Vector2i RenderWindow::ClientSize() const
    {
        return {};
    }

    void RenderWindow::ClientSize(OpenTK::Mathematics::Vector2i)
    {
    }

    RendererPlatform::MonitorArea
        RenderWindow::CurrentMonitorClientArea() const
    {
        return {};
    }

    std::vector<RendererPlatform::MonitorArea>
        RenderWindow::MonitorClientAreas() const
    {
        return {};
    }

    RendererPlatform::WindowStateValue RenderWindow::WindowState() const
    {
        return RendererPlatform::WindowStateValue::Normal;
    }

    void RenderWindow::WindowStateMinimized()
    {
        ThrowDesktopWindowUnavailable();
    }

    void RenderWindow::WindowStateMaximized()
    {
    }

    void RenderWindow::WindowStateNormal()
    {
    }

    bool RenderWindow::WindowStateFullscreen()
    {
        return false;
    }

    bool RenderWindow::WindowStateBorderless()
    {
        return false;
    }

    double RenderWindow::RefreshRate() const
    {
        return 0.0;
    }

    void RenderWindow::Floating(bool)
    {
    }

    bool RenderWindow::IsFocused() const
    {
        return true;
    }

    OpenTK::Mathematics::Vector2i
        RenderWindow::ClientLocation() const
    {
        return {};
    }

    void RenderWindow::Focus()
    {
    }

    void RenderWindow::Close()
    {
    }
}

#endif
