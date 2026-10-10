// RendererPlatform on Qt: the game window is a QWindow and the frame loop is
// still the renderer's own (Run pumps Qt's events, then raises one frame), so
// nothing above RendererPlatform changes when FRUITY_UI=qt replaces GLFW.
//
// The surface follows the renderer: an OpenGL window carries its own context,
// and a NoApi one is a VulkanSurface whose native handle the RHI's Vulkan
// backend builds its swapchain on (WindowSystem, at the bottom of this file).
// The event side is the same for both.

#include "QtSwapchain.hpp"

#include "../../MphRead.Native/Renderer.hpp"
#include "../../MphRead.Native/NativeRuntime/FrameTelemetry.hpp"
#include "../../MphRead.Native/Mods/Chat/ChatBox.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/Swapchain.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/OpenGL/OpenGlDevice.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/PresentationScheduler.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/PresentationSleep.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/FullscreenExclusive.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include "../../MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanWindowSystem.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanResult.hpp"
#include <QtCore/QLibrary>
#include <QtCore/QThread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#undef CreateWindow
#elif defined(__linux__)
#include <xcb/xcb.h>
#include <wayland-client.h>
#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_wayland.h>
#elif defined(__APPLE__)
#include <QtCore/QDir>
#include <CoreGraphics/CGGeometry.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <cstdlib>
#include <vulkan/vulkan_metal.h>
#endif
#endif

#include "../../MphRead.Native/NativeRuntime/System/Heartbeat.hpp"
#include "QtApp.hpp"
#include "QtKeys.hpp"
#if defined(_WIN32)
#include "WindowsRawMouseInput.hpp"
#endif

#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QPlatformSurfaceEvent>
#include <QtGui/QScreen>
#include <QtGui/QSurfaceFormat>
#include <QtGui/QWheelEvent>
#include <QtGui/QWindow>

#include <algorithm>
#include <array>
#include <span>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    QWindow* g_gameWindow = nullptr;
    std::uint64_t g_mousePositionEpoch = 0; // Qt GUI thread, across replacement windows.
    namespace Rhi = ::MphRead::NativeRuntime::Rhi;
    QEvent* g_currentEvent = nullptr;

    using MphRead::RendererPlatform::CursorState;
    using MphRead::RendererPlatform::FrameEventArgs;
    using MphRead::RendererPlatform::GraphicsWindowMode;
    using MphRead::RendererPlatform::GLFWException;
    using MphRead::RendererPlatform::MonitorArea;
    using MphRead::RendererPlatform::MouseButtonEventArgs;
    using MphRead::RendererPlatform::MouseMoveEventArgs;
    using MphRead::RendererPlatform::MouseWheelEventArgs;
    using MphRead::RendererPlatform::ResizeEventArgs;
    using MphRead::RendererPlatform::TextInputEventArgs;
    using MphRead::RendererPlatform::WindowBorderValue;
    using MphRead::RendererPlatform::WindowEvents;
    using MphRead::RendererPlatform::WindowPositionEventArgs;
    using MphRead::RendererPlatform::WindowSettings;
    using MphRead::RendererPlatform::WindowStateValue;
    using ::OpenTK::Mathematics::Vector2i;
    using ::OpenTK::Windowing::Common::KeyboardKeyEventArgs;
    using GlfwMouseButton = ::OpenTK::Windowing::GraphicsLibraryFramework::MouseButton;

    // GLFW's own numbering, which Keybind and every saved binding use.
    [[nodiscard]] int GlfwButton(Qt::MouseButton button) noexcept
    {
        switch (button)
        {
        case Qt::LeftButton: return 0;
        case Qt::RightButton: return 1;
        case Qt::MiddleButton: return 2;
        case Qt::BackButton: return 3;
        case Qt::ForwardButton: return 4;
        case Qt::ExtraButton3: return 5;
        case Qt::ExtraButton4: return 6;
        case Qt::ExtraButton5: return 7;
        default: return -1;
        }
    }

    [[nodiscard]] MonitorArea AreaOf(const QRect& rect, qreal scale)
    {
        // GLFW reports monitors in physical pixels on every platform the game
        // ships on; Qt reports device-independent ones.
        MonitorArea area;
        area.Min = Vector2i(static_cast<int>(rect.x() * scale), static_cast<int>(rect.y() * scale));
        area.Size = Vector2i(static_cast<int>(rect.width() * scale),
            static_cast<int>(rect.height() * scale));
        return area;
    }

    class QtWindow;

    class GameQWindow final : public QWindow
    {
    public:
        explicit GameQWindow(QtWindow& owner) : _owner(owner) {}

    protected:
        bool event(QEvent* event) override;

    private:
        QtWindow& _owner;
    };

    class QtWindow final : public MphRead::RendererPlatform::Window
    {
    public:
        explicit QtWindow(const WindowSettings& settings);
        ~QtWindow() override;

        void Run(WindowEvents& events) override;
        [[nodiscard]] Vector2i Size() const override;
        [[nodiscard]] MphRead::RendererPlatform::KeyboardState& Keyboard() override { return _keyboard; }
        [[nodiscard]] MphRead::RendererPlatform::MouseState& Mouse() override { return _mouse; }
        void Title(std::string value) override;
        void MinimumSize(Vector2i value) override;
        void Cursor(CursorState value) override;
        void UpdateFrequency(double value) override { _updateFrequency = value; }
        void Visible(bool value) override;
        void SetIcon(const MphRead::RendererPlatform::WindowIcon& icon) override;
        [[nodiscard]] void* NativeHandle() const override;
        [[nodiscard]] GraphicsWindowMode GraphicsMode() const noexcept override { return _graphicsMode; }
        void Close() override { _closeRequested = true; }
        void PresentationTiming(Rhi::PresentMode mode, std::int32_t cap, Rhi::PacingAuthority authority) override
        {
            const QScreen* const screen = _window->screen();
            const double refresh = screen && screen->refreshRate() > 0 ? screen->refreshRate() : 60.0;
            _presentation.Configure({cap, refresh, mode, authority});
        }
        // As the GLFW window: the deadline is anchored at frame admission and
        // advances only once a present was accepted.
        void PresentationAccepted() override { _presentation.Accepted(_presentationFrameStart); }
        void PresentationUnavailable() override { _presentation.Unavailable(); }

        // For WindowSystem (Vulkan presentation) and the OpenGL swapchain.
        [[nodiscard]] bool CloseRequested() const noexcept { return _closeRequested; }
        // Rendering availability follows Qt exposure, not the window-manager
        // state flag, which can lag behind a restore on X11/Openbox.
        [[nodiscard]] bool Iconified() const { return !_window->isExposed(); }

        // For QtOpenGlSwapchain.
        void SetSwapInterval(int interval);
        void SwapBuffers();

        void BaseOnClosing() override {}
        void BaseOnLoad() override {}
        void BaseOnRenderFrame(const FrameEventArgs& args) override { (void)args; }
        void BaseOnResize(const ResizeEventArgs& e) override { (void)e; }
        void BaseOnMove(const WindowPositionEventArgs& e) override { (void)e; }
        void BaseOnMaximizedChanged(bool maximized) override { (void)maximized; }
        void BaseOnFocusedChanged(bool focused) override { (void)focused; }
        void BaseOnMouseDown(const MouseButtonEventArgs& e) override { (void)e; }
        void BaseOnMouseUp(const MouseButtonEventArgs& e) override { (void)e; }
        void BaseOnMouseMove(const MouseMoveEventArgs& e) override { (void)e; }
        void BaseOnMouseWheel(const MouseWheelEventArgs& e) override { (void)e; }
        void BaseOnTextInput(const TextInputEventArgs& e) override { (void)e; }
        void BaseOnKeyDown(const KeyboardKeyEventArgs& e) override { (void)e; }
        void BaseOnKeyUp(const KeyboardKeyEventArgs& e) override { (void)e; }

        [[nodiscard]] std::int32_t WindowBorder() const override;
        void WindowBorder(std::int32_t value) override;
        [[nodiscard]] Vector2i Location() const override;
        void Location(Vector2i value) override;
        [[nodiscard]] Vector2i ClientSize() const override;
        void ClientSize(Vector2i value) override;
        [[nodiscard]] MonitorArea CurrentMonitorClientArea() const override;
        [[nodiscard]] MonitorArea CurrentMonitorWorkArea() const override;
        [[nodiscard]] std::vector<MonitorArea> MonitorClientAreas() const override;
        [[nodiscard]] WindowStateValue WindowState() const override;
        void WindowStateMinimized() override { _window->showMinimized(); }
        void WindowStateMaximized() override { _window->showMaximized(); }
        void WindowStateNormal() override
        {
            _exclusiveMonitor = nullptr;
            Rhi::FullscreenExclusive::Request(nullptr);
            _window->showNormal();
        }
        bool WindowStateFullscreen() override;
        [[nodiscard]] double RefreshRate() const override
        {
            const QScreen* const screen = _window->screen();
            return screen != nullptr ? screen->refreshRate() : 0.0;
        }
        void Floating(bool value) override;
        [[nodiscard]] bool IsFocused() const override { return _window->isActive(); }
        [[nodiscard]] Vector2i ClientLocation() const override;
        void Focus() override { _window->requestActivate(); }

        // From GameQWindow.
        bool HandleEvent(QEvent* event);
#if defined(_WIN32)
        static void InstallAltF4Filter();
#endif

    private:
        [[nodiscard]] qreal Scale() const { return _window->devicePixelRatio(); }
        [[nodiscard]] QScreen* ScreenOf() const;
        void Key(QKeyEvent* event, bool down);
        void MouseButton(QMouseEvent* event, bool down);
        void MouseMove(QMouseEvent* event);
        void Wheel(QWheelEvent* event);
        void RecentreGrabbedCursor();
        void CheckGrabbedMouse();
        void ApplyRawMouse();
        void ConfineCursor(bool confine);

        std::unique_ptr<GameQWindow> _window;
        // The monitor exclusive fullscreen was entered on, while it is the
        // mode. The swapchain is only asked to hold the display while the
        // window has focus: holding it after Alt+Tab is what kept the
        // desktop from coming back.
        void* _exclusiveMonitor = nullptr;
#if defined(_WIN32)
        std::unique_ptr<MphRead::Qt::WindowsRawMouseInput> _rawMouse;
        bool _rawMotionOwner = false;
        // The client rectangle the cursor is held in while aiming, as GLFW's
        // CURSOR_DISABLED does; empty when it is free.
        RECT _cursorClip{};
        bool _cursorClipped = false;
#endif
        std::unique_ptr<QOpenGLContext> _context;
        GraphicsWindowMode _graphicsMode = GraphicsWindowMode::OpenGL;
        WindowEvents* _events = nullptr;
        double _updateFrequency = 0.0;
        bool _closeRequested = false;
        bool _grabbed = false;
        bool _boundExposed = false;
        bool _maximized = false;
        MphRead::RendererPlatform::KeyboardState _keyboard{};
        MphRead::RendererPlatform::MouseState _mouse{};
        // The cursor position the game sees, in framebuffer-sized window
        // pixels. While grabbed it is virtual and unbounded, as GLFW's
        // CURSOR_DISABLED is. Raw capture uses device counts; the fallback
        // re-centres the real cursor after each move.
        float _cursorX = 0.0F;
        float _cursorY = 0.0F;
        float _lastReportedMouseX = 0.0F;
        float _lastReportedMouseY = 0.0F;
        QPointF _grabCentre{};
        bool _warping = false;
        bool _mouseChecked = false;
        Rhi::PresentationScheduler _presentation;
        Rhi::PresentationScheduler::Time _presentationFrameStart{};
    };

    // The one game window there is, for WindowSystem's native-handle queries.
    QtWindow* g_qtWindow = nullptr;

    bool GameQWindow::event(QEvent* event)
    {
        QEvent* const outer = g_currentEvent;
        g_currentEvent = event;
        const bool handled = _owner.HandleEvent(event);
        g_currentEvent = outer;
        return handled || QWindow::event(event);
    }

    QtWindow::QtWindow(const WindowSettings& settings)
    {
        MphRead::Qt::EnsureApplication();

        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        // ContextProfile.Compatability: the renderer still draws in immediate
        // mode, which a core profile does not have. Asked as 3.2 compatibility,
        // Qt's EGL path (Wayland) comes back core; asked as a legacy 2.1
        // context every driver returns its highest compatibility version.
        if (settings.Profile == WindowSettings::ContextProfile::Compatability)
        {
            format.setVersion(2, 1);
            format.setProfile(QSurfaceFormat::NoProfile);
        }
        else
        {
            format.setVersion(settings.ApiMajor, settings.ApiMinor);
        }
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        format.setSwapInterval(1);

        _graphicsMode = settings.GraphicsMode;
        const bool gl = _graphicsMode == GraphicsWindowMode::OpenGL;
        _window = std::make_unique<GameQWindow>(*this);
        // NoApi: a bare native surface the RHI builds its own swapchain on.
        _window->setSurfaceType(gl ? QSurface::OpenGLSurface : QSurface::VulkanSurface);
        if (gl)
        {
            _window->setFormat(format);
        }
        _window->setTitle(QString::fromStdString(settings.Title));
        _window->resize(settings.ClientSize.X, settings.ClientSize.Y);
        _window->create();
        g_gameWindow = _window.get();
        g_qtWindow = this;
#if defined(_WIN32)
        InstallAltF4Filter();
#endif

        if (gl)
        {
            _context = std::make_unique<QOpenGLContext>();
            _context->setFormat(format);
            if (!_context->create())
            {
                throw GLFWException("The OpenGL context could not be created.", 0x00010006);
            }
            if (!_context->makeCurrent(_window.get()))
            {
                throw GLFWException("The OpenGL context could not be made current.", 0x00010008);
            }
            const QSurfaceFormat made = _context->format();
            std::cout << "[window] OpenGL " << made.majorVersion() << '.' << made.minorVersion()
                      << (made.profile() == QSurfaceFormat::CompatibilityProfile ? " compatibility"
                          : made.profile() == QSurfaceFormat::CoreProfile ? " core" : " no profile")
                      << " (asked " << format.majorVersion() << '.' << format.minorVersion()
                      << (format.profile() == QSurfaceFormat::CompatibilityProfile ? " compatibility)" : ")")
                      << '\n';
        }
#if defined(_WIN32)
        _rawMouse = std::make_unique<MphRead::Qt::WindowsRawMouseInput>();
        _rawMouse->Attach(reinterpret_cast<void*>(_window->winId()));
#endif
        _updateFrequency = settings.UpdateFrequency;
        const QPointF cursor = _window->mapFromGlobal(QCursor::pos());
        _cursorX = _lastReportedMouseX = static_cast<float>(cursor.x() * Scale());
        _cursorY = _lastReportedMouseY = static_cast<float>(cursor.y() * Scale());
        _mouse.X = _cursorX;
        _mouse.Y = _cursorY;
        _mouse.PositionEpoch = ++g_mousePositionEpoch;
        if (settings.StartVisible)
        {
            _window->show();
        }
    }

    QtWindow::~QtWindow()
    {
#if defined(_WIN32)
        ConfineCursor(false);
        _rawMouse.reset(); // Detach while the native HWND still exists.
#endif
        if (_context != nullptr)
        {
            // Harnesses create successive windows while the lazy scene session
            // remains alive. Detach its native device before deleting the Qt
            // context, so a recycled QOpenGLContext address cannot reuse it.
            if (_context->makeCurrent(_window.get()))
                ::MphRead::NativeRuntime::Rhi::OpenGL::ReleaseContextDevice();
            _context->doneCurrent();
        }
        _context.reset();
        if (g_gameWindow == _window.get())
        {
            g_gameWindow = nullptr;
        }
        if (g_qtWindow == this)
        {
            g_qtWindow = nullptr;
        }
        _window.reset();
    }

#if defined(_WIN32)
    namespace
    {
        // Alt+F4 at the Win32 message, before Qt decides who gets the key.
        // A QKeyEvent check on the game window alone was not enough: in a
        // match the key went somewhere the game window's handler never saw,
        // and Qt passes Windows' own Alt+F4 handling only keys nobody
        // accepted. Any window of this process, the game window closes.
        class AltF4Filter final : public QAbstractNativeEventFilter
        {
        public:
            bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
            {
                const MSG* msg = static_cast<const MSG*>(message);
                if (msg->message == WM_SYSKEYDOWN && msg->wParam == VK_F4
                    && (msg->lParam & (1 << 29)) != 0 && g_qtWindow != nullptr)
                {
                    g_qtWindow->Close();
                    return true;
                }
                return false;
            }
        };
    }

    void QtWindow::InstallAltF4Filter()
    {
        static AltF4Filter* filter = nullptr;
        if (filter == nullptr && QCoreApplication::instance() != nullptr)
        {
            filter = new AltF4Filter();
            QCoreApplication::instance()->installNativeEventFilter(filter);
        }
    }
#endif

    void QtWindow::Run(WindowEvents& events)
    {
        _events = &events;
        if (_context != nullptr)
        {
            _context->makeCurrent(_window.get());
        }
        events.OnLoad();
        ResizeEventArgs resize;
        resize.Size = Size();
        events.OnResize(resize);

        auto previous = std::chrono::steady_clock::now();
        while (!_closeRequested)
        {
            using namespace ::MphRead::NativeRuntime::FrameTelemetry;
            Frame measuredFrame;
            ::MphRead::NativeRuntime::FrameHeartbeat();
            // As the GLFW loop: native low-latency sleep precedes every fresh
            // input read, and a busy generic budget is polled with events
            // serviced but no frame recorded and discarded.
            const bool admitted = [&] {
                Scope phase(Phase::Admission);
                return events.BeforeFrame();
            }();
            if (!admitted)
            {
                if (events.CanSampleInputWhileWaiting())
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents);
                }
                continue;
            }
            Rhi::SleepForPresentation(_presentation.Deadline(Rhi::PresentationScheduler::Clock::now()));
            {
                Scope inputPhase(Phase::Events);
                events.OnInputSample();
                // OpenTK's NewInputFrame: the frame sees the cursor where it was
                // before this frame's events arrived.
                _mouse.X = _cursorX;
                _mouse.Y = _cursorY;
                Count(Counter::EventPumps);
                QCoreApplication::processEvents(QEventLoop::AllEvents);
                events.OnInputEventsProcessed();
            }
            if (_closeRequested)
            {
                break;
            }
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - previous).count();
            if (_updateFrequency > 0.0 && elapsed < 1.0 / _updateFrequency)
            {
                std::this_thread::sleep_for(
                    std::chrono::duration<double>(1.0 / _updateFrequency - elapsed));
                continue;
            }
            ApplyRawMouse();
            previous = now;
            _presentationFrameStart = now;
            // Qt creates a window's EGL surface when it is first exposed; a
            // context made current before that has no surface to draw to.
            if (_context != nullptr && _window->isExposed() && !_boundExposed)
            {
                _boundExposed = _context->makeCurrent(_window.get());
            }
            FrameEventArgs args;
            args.Time = elapsed;
            events.OnRenderFrame(args);
            if (!_mouseChecked && _grabbed && qEnvironmentVariableIntValue("FRUITY_MOUSECHECK") != 0)
            {
                _mouseChecked = true;
                CheckGrabbedMouse();
            }
        }
        events.OnClosing();
        _events = nullptr;
    }

    Vector2i QtWindow::Size() const
    {
        const QSize size = _window->size() * Scale();
        return Vector2i(size.width(), size.height());
    }

    void QtWindow::Title(std::string value)
    {
        _window->setTitle(QString::fromStdString(value));
    }

    void QtWindow::MinimumSize(Vector2i value)
    {
        _window->setMinimumSize(QSize(value.X, value.Y));
    }

    void QtWindow::Cursor(CursorState value)
    {
        const bool grab = value == CursorState::Grabbed;
#if defined(_WIN32)
        const bool wasRaw = _rawMotionOwner;
        const bool raw = grab && _window->isActive() && _rawMouse && _rawMouse->Available();
        const bool sourceChanged = raw != _rawMotionOwner;
        if (_rawMouse) _rawMouse->SetCapture(raw);
        _rawMotionOwner = raw;
#else
        const bool sourceChanged = false;
#endif
        if (grab == _grabbed && !sourceChanged) return;
        _mouse.PositionEpoch = ++g_mousePositionEpoch;
        const bool wasGrabbed = _grabbed;
        _grabbed = grab;
        if (grab)
        {
            _window->setCursor(Qt::BlankCursor);
            _window->setMouseGrabEnabled(_window->isActive());
            // Once on capture/source transition, never once per raw packet.
            RecentreGrabbedCursor();
        }
        else
        {
            ConfineCursor(false);
            _window->setMouseGrabEnabled(false);
            _window->unsetCursor();
            _warping = false;
#if defined(_WIN32)
            if (wasRaw)
            {
                // The next menu click needs the real absolute pointer, rather
                // than the unbounded device-count position used for aiming.
                const QPointF local = _window->mapFromGlobal(QCursor::pos());
                _mouse.X = _lastReportedMouseX = _cursorX = static_cast<float>(local.x() * Scale());
                _mouse.Y = _lastReportedMouseY = _cursorY = static_cast<float>(local.y() * Scale());
            }
#endif
        }
        if (grab && (!wasGrabbed || sourceChanged))
        {
            _mouse.X = _lastReportedMouseX = _cursorX;
            _mouse.Y = _lastReportedMouseY = _cursorY;
        }
    }

    void QtWindow::ConfineCursor(bool confine)
    {
#if defined(_WIN32)
        // Raw capture never moves the real cursor back to the centre, so
        // without a clip the hidden pointer wanders off the window while
        // aiming and the next click lands on whatever is under it. Checked
        // every frame: Windows drops a clip on its own (focus changes,
        // Ctrl+Alt+Del), and the window can move or be resized.
        const HWND hwnd = reinterpret_cast<HWND>(_window->winId());
        RECT client{};
        if (confine && hwnd != nullptr && ::GetClientRect(hwnd, &client) != FALSE
            && client.right > client.left && client.bottom > client.top)
        {
            ::MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&client), 2);
            RECT current{};
            const bool held = ::GetClipCursor(&current) != FALSE && ::EqualRect(&current, &client) != FALSE;
            if (!held || !_cursorClipped)
            {
                if (::ClipCursor(&client) != FALSE)
                {
                    _cursorClip = client;
                    _cursorClipped = true;
                }
            }
            return;
        }
        if (_cursorClipped)
        {
            // Only our own clip is let go of.
            RECT current{};
            if (::GetClipCursor(&current) != FALSE && ::EqualRect(&current, &_cursorClip) != FALSE)
            {
                (void)::ClipCursor(nullptr);
            }
            _cursorClipped = false;
            _cursorClip = RECT{};
        }
#else
        (void)confine;
#endif
    }

    void QtWindow::ApplyRawMouse()
    {
        _mouse.MotionValid = _window->isActive();
#if defined(_WIN32)
        ConfineCursor(_grabbed && _window->isActive() && _window->isExposed());
        if (!_rawMouse) return;
        // A read failure can change the source during processEvents. Re-centre
        // before admitting any fallback move and discard this frame's raw data.
        if (_rawMotionOwner && !_rawMouse->CaptureActive()) Cursor(_grabbed ? CursorState::Grabbed : CursorState::Normal);
        const auto [dx, dy] = _rawMouse->TakeDelta();
        if (_rawMotionOwner && (dx != 0 || dy != 0))
        {
            _cursorX += static_cast<float>(dx);
            _cursorY += static_cast<float>(dy);
            _mouse.X = _lastReportedMouseX = _cursorX;
            _mouse.Y = _lastReportedMouseY = _cursorY;
            MouseMoveEventArgs args{};
            args.X = _cursorX;
            args.Y = _cursorY;
            args.DeltaX = static_cast<float>(dx);
            args.DeltaY = static_cast<float>(dy);
            if (_events) _events->OnMouseMove(args);
        }
        _rawMouse->EndFrame(_grabbed && _window->isActive() && !_rawMotionOwner);
#endif
    }

    void QtWindow::SetSwapInterval(int interval)
    {
        // The GLX, EGL and WGL contexts apply a changed swap interval on the
        // next makeCurrent against the window's format.
        QSurfaceFormat format = _window->format();
        format.setSwapInterval(interval);
        _window->setFormat(format);
        _context->makeCurrent(_window.get());
        // ...but the platform window keeps the format it was created with,
        // so a later change never reached the driver: VSync off still ran
        // at the refresh rate in OpenGL. Set it on the context directly; Qt
        // only reapplies its own interval when that format changes.
#if defined(_WIN32)
        using SwapIntervalWgl = BOOL(WINAPI*)(int);
        if (const auto set = reinterpret_cast<SwapIntervalWgl>(_context->getProcAddress("wglSwapIntervalEXT")))
        {
            set(interval);
        }
#elif defined(__linux__)
        if (QGuiApplication::platformName() == QLatin1String("xcb"))
        {
            using SwapIntervalMesa = int (*)(unsigned int);
            if (const auto set = reinterpret_cast<SwapIntervalMesa>(_context->getProcAddress("glXSwapIntervalMESA")))
            {
                set(static_cast<unsigned int>(interval));
            }
        }
        else
        {
            using CurrentDisplay = void* (*)();
            using SwapIntervalEgl = unsigned int (*)(void*, int);
            const auto display = reinterpret_cast<CurrentDisplay>(_context->getProcAddress("eglGetCurrentDisplay"));
            const auto set = reinterpret_cast<SwapIntervalEgl>(_context->getProcAddress("eglSwapInterval"));
            if (display != nullptr && set != nullptr)
            {
                set(display(), interval);
            }
        }
#endif
    }

    void QtWindow::Visible(bool value)
    {
        if (value)
        {
            _window->show();
        }
        else
        {
            _window->hide();
        }
    }

    void QtWindow::SetIcon(const MphRead::RendererPlatform::WindowIcon& icon)
    {
        QIcon result;
        for (const MphRead::RendererPlatform::WindowIconImage& image : icon.Images)
        {
            const QImage rgba(image.Pixels.data(), image.Width, image.Height,
                image.Width * 4, QImage::Format_RGBA8888);
            result.addPixmap(QPixmap::fromImage(rgba.copy()));
        }
        _window->setIcon(result);
    }

    void* QtWindow::NativeHandle() const
    {
        return reinterpret_cast<void*>(_window->winId());
    }

    void QtWindow::SwapBuffers()
    {
        // The renderer draws its first frames before revealing the window;
        // GLFW swaps a hidden window harmlessly, Qt's Wayland one does not.
        if (!_window->isExposed())
        {
            _context->functions()->glFlush();
            return;
        }
        _context->swapBuffers(_window.get());
    }

    std::int32_t QtWindow::WindowBorder() const
    {
        const Qt::WindowFlags flags = _window->flags();
        if (flags.testFlag(Qt::FramelessWindowHint))
        {
            return static_cast<std::int32_t>(WindowBorderValue::Hidden);
        }
        if (_window->minimumSize() == _window->maximumSize())
        {
            return static_cast<std::int32_t>(WindowBorderValue::Fixed);
        }
        return static_cast<std::int32_t>(WindowBorderValue::Resizable);
    }

    void QtWindow::WindowBorder(std::int32_t value)
    {
        const auto border = static_cast<WindowBorderValue>(value);
        _window->setFlag(Qt::FramelessWindowHint, border == WindowBorderValue::Hidden);
        if (border == WindowBorderValue::Resizable)
        {
            _window->setMaximumSize(QSize(16777215, 16777215));
        }
        else if (border == WindowBorderValue::Fixed)
        {
            _window->setMinimumSize(_window->size());
            _window->setMaximumSize(_window->size());
        }
    }

    Vector2i QtWindow::Location() const
    {
        const QRect frame = _window->frameGeometry();
        return Vector2i(static_cast<int>(frame.x() * Scale()), static_cast<int>(frame.y() * Scale()));
    }

    void QtWindow::Location(Vector2i value)
    {
        _window->setFramePosition(QPoint(static_cast<int>(value.X / Scale()),
            static_cast<int>(value.Y / Scale())));
    }

    Vector2i QtWindow::ClientSize() const
    {
        // GLFW's window size is in screen coordinates, which are the logical
        // size Qt reports, and the framebuffer is the physical one (Size()).
        return Vector2i(_window->width(), _window->height());
    }

    void QtWindow::ClientSize(Vector2i value)
    {
        _window->resize(value.X, value.Y);
    }

    void QtWindow::Floating(bool value)
    {
#if defined(_WIN32)
        // Changing QWindow flags reapplies the native window style during a
        // focus transition. Change only its Z order: keep the Vulkan surface,
        // fullscreen geometry and presentation state intact.
        ::SetWindowPos(reinterpret_cast<HWND>(_window->winId()),
            value ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
#else
        _window->setFlag(Qt::WindowStaysOnTopHint, value);
#endif
    }

    bool QtWindow::WindowStateFullscreen()
    {
#if defined(_WIN32)
        // Not Qt's showFullScreen: on Windows that keeps a one-pixel border
        // round an OpenGL/Vulkan surface, which is exactly what stops the
        // driver from giving it the display. A game's fullscreen is a popup
        // with no frame covering the monitor to the pixel, and -- on Vulkan
        // -- a swapchain that asks for the display outright
        // (VK_EXT_full_screen_exclusive, see FullscreenExclusive.hpp).
        _window->showNormal();
        _window->setFlag(Qt::FramelessWindowHint, true);
        _window->show();
        const HWND hwnd = reinterpret_cast<HWND>(_window->winId());
        const HMONITOR monitor = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (monitor == nullptr || !::GetMonitorInfoW(monitor, &info))
        {
            return false;
        }
        const RECT& r = info.rcMonitor;
        ::SetWindowPos(hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
        _exclusiveMonitor = monitor;
        Rhi::FullscreenExclusive::Active(_window->isActive());
        Rhi::FullscreenExclusive::Request(monitor);
        return true;
#else
        _window->showFullScreen();
        return true;
#endif
    }

    QScreen* QtWindow::ScreenOf() const
    {
        QScreen* screen = _window->screen();
        return screen != nullptr ? screen : QGuiApplication::primaryScreen();
    }

    MonitorArea QtWindow::CurrentMonitorClientArea() const
    {
        QScreen* screen = ScreenOf();
        return screen == nullptr ? MonitorArea{} : AreaOf(screen->geometry(), screen->devicePixelRatio());
    }

    MonitorArea QtWindow::CurrentMonitorWorkArea() const
    {
        QScreen* screen = ScreenOf();
        return screen == nullptr ? MonitorArea{}
                                 : AreaOf(screen->availableGeometry(), screen->devicePixelRatio());
    }

    std::vector<MonitorArea> QtWindow::MonitorClientAreas() const
    {
        std::vector<MonitorArea> areas;
        for (QScreen* screen : QGuiApplication::screens())
        {
            areas.push_back(AreaOf(screen->geometry(), screen->devicePixelRatio()));
        }
        return areas;
    }

    WindowStateValue QtWindow::WindowState() const
    {
        switch (_window->windowState())
        {
        case Qt::WindowMinimized: return WindowStateValue::Minimized;
        case Qt::WindowMaximized: return WindowStateValue::Maximized;
        case Qt::WindowFullScreen: return WindowStateValue::Fullscreen;
        default: return WindowStateValue::Normal;
        }
    }

    Vector2i QtWindow::ClientLocation() const
    {
        const QPoint origin = _window->position();
        return Vector2i(static_cast<int>(origin.x() * Scale()), static_cast<int>(origin.y() * Scale()));
    }

    bool QtWindow::HandleEvent(QEvent* event)
    {
        switch (event->type())
        {
#if defined(_WIN32)
        case QEvent::PlatformSurface:
            if (_rawMouse)
            {
                _rawMouse->SetCapture(false);
                _rawMouse->Discard();
                if (static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
                    _rawMouse->Detach();
                else
                    _rawMouse->Attach(reinterpret_cast<void*>(_window->winId()));
                _rawMotionOwner = false;
            }
            return false;
#endif
        case QEvent::Close:
            // The close button asks; the loop ends and the renderer closes.
            _closeRequested = true;
            event->ignore();
            return true;
        case QEvent::Resize:
        case QEvent::Expose:
            if (_events != nullptr && _window->isExposed())
            {
                ResizeEventArgs args;
                args.Size = Size();
                _events->OnResize(args);
            }
            return false;
        case QEvent::Move:
            if (_events != nullptr)
            {
                WindowPositionEventArgs args;
                args.Position = ClientLocation();
                _events->OnMove(args);
            }
            return false;
        case QEvent::WindowStateChange:
        {
            const bool maximized = _window->windowState() == Qt::WindowMaximized;
            if (maximized != _maximized)
            {
                _maximized = maximized;
                if (_events != nullptr)
                {
                    _events->OnMaximizedChanged(maximized);
                }
            }
            return false;
        }
        case QEvent::FocusIn:
        case QEvent::FocusOut:
#if defined(_WIN32)
            if (event->type() == QEvent::FocusOut && _rawMouse)
            {
                _mouse.MotionValid = false;
                _rawMouse->SetCapture(false);
                _rawMouse->Discard();
                _window->setMouseGrabEnabled(false);
            }
#endif
            if (_exclusiveMonitor != nullptr)
            {
                // Give the display back the moment something else is in
                // front (Alt+Tab, Win key, a notification taking focus) and
                // stop being topmost, so whatever took focus is drawn over
                // us; take the display again on the way back in. Not
                // minimised: a window minimised from here came back with its
                // frame loop stalled.
                Rhi::FullscreenExclusive::Active(event->type() == QEvent::FocusIn);
            }
            if (_events != nullptr)
            {
                _events->OnFocusedChanged(event->type() == QEvent::FocusIn);
            }
            return false;
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            // Qt hands Windows its default handling only for keys nobody
            // accepted, and this window accepts every key -- so Alt+F4 never
            // became a close. Answer it here, on every platform.
            if (event->type() == QEvent::KeyPress)
            {
                const auto* key = static_cast<QKeyEvent*>(event);
                if (key->key() == Qt::Key_F4 && key->modifiers().testFlag(Qt::AltModifier))
                {
                    _closeRequested = true;
                    return true;
                }
            }
            Key(static_cast<QKeyEvent*>(event), event->type() == QEvent::KeyPress);
            return true;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseButtonRelease:
            MouseButton(static_cast<QMouseEvent*>(event), event->type() != QEvent::MouseButtonRelease);
            return true;
        case QEvent::MouseMove:
            MouseMove(static_cast<QMouseEvent*>(event));
            return true;
        case QEvent::Wheel:
            Wheel(static_cast<QWheelEvent*>(event));
            return true;
        default:
            return false;
        }
    }

    void QtWindow::Key(QKeyEvent* event, bool down)
    {
        // Auto-repeat raises OnKeyDown again, as GLFW_REPEAT does.
        const int key = MphRead::Qt::GlfwKey(*event);
        if (key >= 0)
        {
            _keyboard.SetKeyDown(static_cast<MphRead::RendererPlatform::Key>(key), down);
            if (_events != nullptr)
            {
                KeyboardKeyEventArgs args;
                args.Key = static_cast<MphRead::RendererPlatform::Key>(key);
                const Qt::KeyboardModifiers mods = event->modifiers();
                args.Shift = mods.testFlag(Qt::ShiftModifier);
                args.Control = mods.testFlag(Qt::ControlModifier);
                args.Alt = mods.testFlag(Qt::AltModifier);
                args.Command = mods.testFlag(Qt::MetaModifier);
                if (down)
                {
                    _events->OnKeyDown(args);
                }
                else
                {
                    _events->OnKeyUp(args);
                }
            }
        }
        // GLFW's char callback: typed text, once per character, repeats included.
        if (down && _events != nullptr)
        {
            const QString text = event->text();
            for (const char32_t codePoint : text.toUcs4())
            {
                if (codePoint >= 0x20 && codePoint != 0x7F)
                {
                    TextInputEventArgs args;
                    args.Unicode = static_cast<std::uint32_t>(codePoint);
                    _events->OnTextInput(args);
                }
            }
        }
        // A chat/menu key can close capture and reopen it in the same pump.
        // Apply the renderer's policy here too, so motion between those keys
        // cannot survive in the accumulator when the final policy is grabbed.
        if (_events) _events->OnInputEventsProcessed();
    }

    void QtWindow::MouseButton(QMouseEvent* event, bool down)
    {
        const int button = GlfwButton(event->button());
        if (button < 0)
        {
            return;
        }
        const auto value = static_cast<GlfwMouseButton>(button);
        const bool wasDown = _mouse.IsButtonDown(value);
        _mouse.SetButtonDown(value, down);
        if (_events == nullptr || wasDown == down)
        {
            return;
        }
        MouseButtonEventArgs args;
        args.Button = value;
        if (down)
        {
            _events->OnMouseDown(args);
        }
        else
        {
            _events->OnMouseUp(args);
        }
    }

    void QtWindow::MouseMove(QMouseEvent* event)
    {
#if defined(_WIN32)
        // Keep ownership for the whole pump even if a raw read failed midway.
        if (_grabbed && (_rawMotionOwner || !_window->isActive())) return;
#endif
        const QPointF local = event->position();
        if (_grabbed)
        {
            if (_warping)
            {
                // The move our own re-centring caused.
                _warping = false;
                if ((local - _grabCentre).manhattanLength() < 1.0)
                {
                    return;
                }
            }
            _cursorX += static_cast<float>((local.x() - _grabCentre.x()) * Scale());
            _cursorY += static_cast<float>((local.y() - _grabCentre.y()) * Scale());
            RecentreGrabbedCursor();
        }
        else
        {
            _cursorX = static_cast<float>(local.x() * Scale());
            _cursorY = static_cast<float>(local.y() * Scale());
        }
        MouseMoveEventArgs args;
        args.X = _cursorX;
        args.Y = _cursorY;
        args.DeltaX = _cursorX - _lastReportedMouseX;
        args.DeltaY = _cursorY - _lastReportedMouseY;
        _lastReportedMouseX = _cursorX;
        _lastReportedMouseY = _cursorY;
        if (_events != nullptr && (args.DeltaX != 0.0F || args.DeltaY != 0.0F))
        {
            _events->OnMouseMove(args);
        }
    }

    void QtWindow::CheckGrabbedMouse()
    {
#if defined(_WIN32)
        if (_rawMotionOwner)
        {
            const auto start = std::pair{_cursorX, _cursorY};
            QMouseEvent move(QEvent::MouseMove, QPointF(_grabCentre + QPointF(50, 50)),
                QPointF(_window->mapToGlobal(_grabCentre.toPoint())), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(_window.get(), &move);
            if (start != std::pair{_cursorX, _cursorY})
                throw std::runtime_error("Qt motion was added during raw mouse capture.");
            std::cout << "[mousecheck] PASS raw capture excludes Qt motion\n";
            return;
        }
#endif
        const auto startX = _cursorX;
        const auto startY = _cursorY;
        const QPoint centre = _grabCentre.toPoint();
        const auto move = [&](QPoint local)
        {
            QMouseEvent event(QEvent::MouseMove, QPointF(local),
                QPointF(_window->mapToGlobal(local)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(_window.get(), &event);
        };
        for (int i = 0; i < 256; ++i)
        {
            move(centre + QPoint(i % 2 ? -8 : 8, 0));
            move(centre); // Include every synthetic re-centre event.
        }
        const auto deltaX = _cursorX - startX;
        const auto deltaY = _cursorY - startY;
        move(centre + QPoint(0, 4));
        move(centre);
        const bool verticalWorks = _cursorY - startY == 4.0F * Scale();
        move(centre - QPoint(0, 4));
        move(centre);
        const bool pass = deltaX == 0.0F && deltaY == 0.0F
            && verticalWorks && _cursorY == startY;
        std::cout << "[mousecheck] " << (pass ? "PASS" : "FAIL")
            << " horizontal moves=256 client=" << _window->width() << 'x' << _window->height()
            << " scale=" << Scale() << " accumulated_delta=" << deltaX << ',' << deltaY
            << " vertical_motion=" << verticalWorks << '\n';
        if (!pass) throw std::runtime_error("Horizontal mouse motion accumulated aim drift.");
    }

    void QtWindow::RecentreGrabbedCursor()
    {
        // QCursor::setPos is a no-op on Wayland, where pointer lock needs the
        // relative-pointer protocol; X11, Windows and macOS warp.
        // Use exactly the integer position passed to QCursor::setPos. A half
        // pixel centre in an odd-height window adds +0.5 to every horizontal
        // move after Qt rounds the warp target, steadily pitching aim down.
        _grabCentre = QPoint(_window->width() / 2, _window->height() / 2);
        _warping = true;
        QCursor::setPos(_window->screen(), _window->mapToGlobal(_grabCentre.toPoint()));
    }

    void QtWindow::Wheel(QWheelEvent* event)
    {
        // GLFW reports one notch as 1.0; Qt as 120 eighths of a degree.
        const QPoint delta = event->angleDelta();
        const float x = static_cast<float>(delta.x()) / 120.0F;
        const float y = static_cast<float>(delta.y()) / 120.0F;
        _mouse.Scroll.X += x;
        _mouse.Scroll.Y += y;
        if (_events != nullptr)
        {
            MouseWheelEventArgs args;
            args.OffsetX = x;
            args.OffsetY = y;
            _events->OnMouseWheel(args);
        }
    }
}

namespace
{
    class BackbufferTexture final : public Rhi::Texture
    {
    public:
        explicit BackbufferTexture(const Rhi::SwapchainDesc& desc) { Resize(desc); }

        [[nodiscard]] const Rhi::TextureDesc& Desc() const noexcept override { return _desc; }
        // The window's own surface has no texture handle to bind.
        [[nodiscard]] Rhi::TextureHandle Handle() const noexcept override { return {}; }

        void Resize(const Rhi::SwapchainDesc& desc) noexcept
        {
            _desc.width = desc.width;
            _desc.height = desc.height;
            _desc.depth = 1;
            _desc.mipLevels = 1;
            _desc.arrayLayers = 1;
            _desc.sampleCount = 1;
            _desc.format = desc.format;
            _desc.usage = Rhi::TextureUsage::ColorAttachment;
            _desc.memoryUsage = Rhi::MemoryUsage::GpuOnly;
            _desc.initialState = Rhi::ResourceState::Present;
        }

    private:
        Rhi::TextureDesc _desc{};
    };

    // The OpenGL swapchain over the QWindow's own context: the GLFW one's
    // counterpart, presenting the implicit default framebuffer.
    class QtOpenGlSwapchain final : public Rhi::Swapchain
    {
    public:
        QtOpenGlSwapchain(QtWindow& window, Rhi::SwapchainDesc desc)
            : _window(window), _desc(std::move(desc)), _backbuffer(_desc)
        {
        }

        [[nodiscard]] const Rhi::SwapchainDesc& Desc() const noexcept override { return _desc; }

        void Resize(std::uint32_t width, std::uint32_t height) override
        {
            _desc.width = width;
            _desc.height = height;
            _backbuffer.Resize(_desc);
        }

        [[nodiscard]] Rhi::Texture& AcquireNextTexture() override { return _backbuffer; }

        Rhi::AcquireResult TryAcquireTexture() override
        {
            // As GLFW's: a close request or a minimised window has nothing to draw.
            if (_window.CloseRequested() || _window.Iconified())
            {
                return {Rhi::PresentationStatus::TemporarilyUnavailable, nullptr};
            }
            const auto size = _window.Size();
            if (size.X <= 0 || size.Y <= 0)
            {
                return {Rhi::PresentationStatus::TemporarilyUnavailable, nullptr};
            }
            return {Rhi::PresentationStatus::Ready, &_backbuffer};
        }
        [[nodiscard]] Rhi::PresentationCapabilities PresentationCaps() const noexcept override
        {
            return {true, true, false, 2, 2};
        }
        [[nodiscard]] Rhi::PresentMode RequestedPresentMode() const noexcept override { return _requestedMode; }

        void SetPresentMode(Rhi::PresentMode mode) override
        {
            // GL has no mailbox: asked for, it is FIFO, as on GLFW, and the
            // request is still reported back as made.
            _requestedMode = mode;
            if (mode == Rhi::PresentMode::Mailbox)
            {
                mode = Rhi::PresentMode::Fifo;
            }
            _window.SetSwapInterval(mode == Rhi::PresentMode::Fifo ? 1 : 0);
            _desc.presentMode = mode;
        }

        void Present() override { _window.SwapBuffers(); }
        Rhi::PresentResult TryPresent() override
        {
            const auto acquire = TryAcquireTexture();
            if (!acquire.texture)
            {
                return {acquire.status, acquire.failure};
            }
            Present();
            return {Rhi::PresentationStatus::Ready, std::nullopt, true};
        }

    private:
        QtWindow& _window;
        Rhi::SwapchainDesc _desc{};
        BackbufferTexture _backbuffer;
        Rhi::PresentMode _requestedMode = Rhi::PresentMode::Fifo;
    };
}

namespace MphRead::Qt
{
    QWindow* GameWindow() noexcept
    {
        return g_gameWindow;
    }

    QEvent* CurrentEvent() noexcept
    {
        return g_currentEvent;
    }

    std::unique_ptr<NativeRuntime::Rhi::Swapchain> CreateOpenGlSwapchain(
        RendererPlatform::Window& window, const NativeRuntime::Rhi::SwapchainDesc& desc)
    {
        auto* const qt = dynamic_cast<QtWindow*>(&window);
        if (qt == nullptr || window.GraphicsMode() != RendererPlatform::GraphicsWindowMode::OpenGL)
        {
            throw std::invalid_argument("An OpenGL swapchain requires an OpenGL Qt window.");
        }
        return std::make_unique<QtOpenGlSwapchain>(*qt, desc);
    }
}

namespace MphRead::RendererPlatform
{
    std::shared_ptr<Window> CreateWindow(const WindowSettings& settings)
    {
        return std::make_shared<QtWindow>(settings);
    }

    OpenTK::Mathematics::Vector2i WorkAreaForWindow(Window& window)
    {
        return window.CurrentMonitorWorkArea().Size;
    }

    void ProcessEvents()
    {
        MphRead::Qt::EnsureApplication();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }

    void* CurrentGlContext() noexcept
    {
        return QOpenGLContext::currentContext();
    }

    void MakeGlContextCurrent(void* context) noexcept
    {
        auto* const target = static_cast<QOpenGLContext*>(context);
        if (target == nullptr)
        {
            if (QOpenGLContext* const current = QOpenGLContext::currentContext())
            {
                current->doneCurrent();
            }
            return;
        }
        // The surface the context was last current on: the game window's.
        if (QSurface* const surface = target->surface())
        {
            (void)target->makeCurrent(surface);
        }
    }

    void InstallGlfwErrorCallback(std::function<void(std::int32_t, std::string)> callback)
    {
        // Qt reports its own failures through its message handler.
        (void)callback;
    }

    std::int32_t GlfwFeatureUnavailableCode()
    {
        return 0x0001000C;
    }
}

#if defined(FRUITY_HAS_VULKAN)
// The Qt half of Vulkan presentation: the RHI's backend creates its instance,
// device and swapchain itself and asks the window toolkit only for these.
namespace MphRead::NativeRuntime::Rhi::Vulkan::WindowSystem
{
    namespace
    {
        [[nodiscard]] QtWindow* Owner(void* nativeWindow) noexcept
        {
            return g_qtWindow != nullptr && g_qtWindow->NativeHandle() == nativeWindow ? g_qtWindow : nullptr;
        }

#if defined(__linux__)
        [[nodiscard]] bool IsXcbPlatform()
        {
            MphRead::Qt::EnsureApplication();
            return QGuiApplication::platformName() == QStringLiteral("xcb");
        }

        [[nodiscard]] bool IsWaylandPlatform()
        {
            MphRead::Qt::EnsureApplication();
            return QGuiApplication::platformName().startsWith(QStringLiteral("wayland"));
        }

        [[nodiscard]] xcb_connection_t* XcbConnection()
        {
            MphRead::Qt::EnsureApplication();
            if (auto* const native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
            {
                return reinterpret_cast<xcb_connection_t*>(native->connection());
            }
            return nullptr;
        }

        [[nodiscard]] xcb_visualid_t XcbVisual()
        {
            xcb_connection_t* const connection = XcbConnection();
            if (connection == nullptr)
            {
                return XCB_NONE;
            }
            const xcb_setup_t* const setup = xcb_get_setup(connection);
            if (setup == nullptr)
            {
                return XCB_NONE;
            }
            const xcb_screen_iterator_t screen = xcb_setup_roots_iterator(setup);
            return screen.data != nullptr ? screen.data->root_visual : XCB_NONE;
        }

        [[nodiscard]] wl_display* WaylandDisplay()
        {
            MphRead::Qt::EnsureApplication();
            if (auto* const native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
            {
                return native->display();
            }
            return nullptr;
        }
#elif defined(__APPLE__)
        // winId() of a VulkanSurface QWindow is its NSView, which Qt backs with
        // a CAMetalLayer -- the one thing VK_EXT_metal_surface takes. Reached
        // through the Objective-C runtime so this file stays C++.
        [[nodiscard]] void* MetalLayer(void* nsView)
        {
            if (nsView == nullptr)
            {
                throw std::runtime_error("The Qt window has no NSView.");
            }
            const auto send = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend);
            const auto isKind = reinterpret_cast<BOOL (*)(id, SEL, Class)>(objc_msgSend);
            Class const metalLayer = objc_getClass("CAMetalLayer");
            if (metalLayer == nil)
            {
                throw std::runtime_error("QuartzCore has no CAMetalLayer.");
            }
            const auto view = static_cast<id>(nsView);
            id layer = send(view, sel_registerName("layer"));
            if (layer != nil && isKind(layer, sel_registerName("isKindOfClass:"), metalLayer))
            {
                std::cout << "[vulkan] macOS surface: Qt's own CAMetalLayer" << std::endl;
                return layer;
            }
            // A Qt built without Vulkan backs the view with a plain CALayer.
            // Host a Metal layer instead, the way GLFW and SDL do. A hosted
            // layer is sized by AppKit only when the view's frame next
            // changes, and this view already has its size: without a frame of
            // its own the layer stays 0x0, the surface reports a zero extent
            // and no swapchain can ever be made. Autoresizing keeps it
            // matched to the view after that.
            layer = send(reinterpret_cast<id>(metalLayer), sel_registerName("layer"));
            reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(view, sel_registerName("setLayer:"), layer);
            reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(view, sel_registerName("setWantsLayer:"), YES);
            if (g_gameWindow != nullptr)
            {
                const CGRect frame{{0, 0}, {static_cast<CGFloat>(g_gameWindow->width()),
                    static_cast<CGFloat>(g_gameWindow->height())}};
                reinterpret_cast<void (*)(id, SEL, CGRect)>(objc_msgSend)(layer, sel_registerName("setFrame:"), frame);
                reinterpret_cast<void (*)(id, SEL, double)>(objc_msgSend)(layer,
                    sel_registerName("setContentsScale:"), g_gameWindow->devicePixelRatio());
            }
            constexpr unsigned kCALayerWidthSizable = 1U << 1, kCALayerHeightSizable = 1U << 4;
            reinterpret_cast<void (*)(id, SEL, unsigned)>(objc_msgSend)(layer,
                sel_registerName("setAutoresizingMask:"), kCALayerWidthSizable | kCALayerHeightSizable);
            std::cout << "[vulkan] macOS surface: hosted CAMetalLayer" << std::endl;
            return layer;
        }

        // Where a Mac keeps the loader. A program started from Finder has no
        // DYLD path, so Homebrew's prefix is not searched unless named here;
        // MoltenVK on its own exports vkGetInstanceProcAddr too, and is the
        // last resort when no loader is installed at all.
        [[nodiscard]] std::vector<QString> LoaderCandidates()
        {
            std::vector<QString> directories;
            const QString app = QCoreApplication::applicationDirPath();
            directories.push_back(app);
            directories.push_back(QDir(app).filePath(QStringLiteral("../Frameworks")));
            if (const char* sdk = std::getenv("VULKAN_SDK"); sdk != nullptr && *sdk != '\0')
            {
                directories.push_back(QDir(QString::fromLocal8Bit(sdk)).filePath(QStringLiteral("lib")));
            }
            directories.push_back(QStringLiteral("/opt/homebrew/lib"));
            directories.push_back(QStringLiteral("/usr/local/lib"));
            std::vector<QString> candidates;
            for (const auto& name : {QStringLiteral("libvulkan.1.dylib"), QStringLiteral("libMoltenVK.dylib")})
            {
                for (const auto& directory : directories)
                {
                    candidates.push_back(QDir(directory).filePath(name));
                }
            }
            return candidates;
        }
#endif
    }

    PFN_vkGetInstanceProcAddr LoaderEntry()
    {
        // The system loader, found the way Qt's own QVulkanInstance finds it.
        static const PFN_vkGetInstanceProcAddr entry = []() -> PFN_vkGetInstanceProcAddr
        {
#if defined(_WIN32)
            static QLibrary library(QStringLiteral("vulkan-1"));
#else
            static QLibrary library(QStringLiteral("vulkan"), 1);
#endif
#if defined(__APPLE__)
            if (!library.load())
            {
                for (const auto& candidate : LoaderCandidates())
                {
                    library.setFileName(candidate);
                    if (library.load())
                    {
                        break;
                    }
                }
            }
#endif
            if (!library.isLoaded() && !library.load())
            {
                return nullptr;
            }
#if defined(__APPLE__)
            // Qt Quick's QVulkanInstance adopts the renderer's VkInstance, so
            // it has to call into this same loader -- and left to itself it
            // asks dyld for a bare name, which a program started from Finder
            // cannot resolve ("Qt could not adopt the renderer's Vulkan
            // instance"). QT_VULKAN_LIB is Qt's documented override; a value
            // the user set is theirs.
            if (!qEnvironmentVariableIsSet("QT_VULKAN_LIB"))
            {
                qputenv("QT_VULKAN_LIB", QFile::encodeName(library.fileName()));
            }
#endif
            return reinterpret_cast<PFN_vkGetInstanceProcAddr>(library.resolve("vkGetInstanceProcAddr"));
        }();
        if (entry == nullptr)
        {
            throw std::runtime_error("No Vulkan loader was found.");
        }
        return entry;
    }

    bool Available(std::string& reason)
    {
        try
        {
            (void)LoaderEntry();
        }
        catch (const std::exception&)
        {
            reason = "no Vulkan loader or driver was found";
            return false;
        }
#if defined(_WIN32)
        return true;
#elif defined(__linux__)
        if (IsXcbPlatform())
        {
            if (XcbConnection() != nullptr)
            {
                return true;
            }
            reason = "Qt xcb platform has no XCB connection";
            return false;
        }
        if (IsWaylandPlatform())
        {
            if (WaylandDisplay() != nullptr)
            {
                return true;
            }
            reason = "Qt Wayland platform has no wl_display";
            return false;
        }
        reason = "Qt Vulkan presentation requires the xcb or Wayland platform plugin on Linux";
        return false;
#elif defined(__APPLE__)
        return true;
#else
        reason = "Vulkan presentation in the Qt build is not implemented on this platform";
        return false;
#endif
    }

    std::span<const char* const> RequiredInstanceExtensions()
    {
#if defined(_WIN32)
        static constexpr std::array<const char*, 2> names{
            VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        return names;
#elif defined(__linux__)
        if (IsXcbPlatform())
        {
            static constexpr std::array<const char*, 2> names{
                VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME};
            return names;
        }
        if (IsWaylandPlatform())
        {
            static constexpr std::array<const char*, 2> names{
                VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
            return names;
        }
        return {};
#elif defined(__APPLE__)
        static constexpr std::array<const char*, 2> names{
            VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME};
        return names;
#else
        return {};
#endif
    }

    VkSurfaceKHR CreateSurface(VkInstance instance, void* nativeWindow, PFN_vkGetInstanceProcAddr instanceProc)
    {
#if defined(_WIN32)
        const auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
            instanceProc(instance, "vkCreateWin32SurfaceKHR"));
        if (create == nullptr)
        {
            throw std::runtime_error("vkCreateWin32SurfaceKHR is unavailable.");
        }
        VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        info.hinstance = ::GetModuleHandleW(nullptr);
        info.hwnd = static_cast<HWND>(nativeWindow);
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        Check(create(instance, &info, nullptr, &surface), "vkCreateWin32SurfaceKHR");
        return surface;
#elif defined(__linux__)
        if (IsXcbPlatform())
        {
            xcb_connection_t* const connection = XcbConnection();
            if (connection == nullptr)
            {
                throw std::runtime_error("Qt xcb platform has no XCB connection.");
            }
            const auto create = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
                instanceProc(instance, "vkCreateXcbSurfaceKHR"));
            if (create == nullptr)
            {
                throw std::runtime_error("vkCreateXcbSurfaceKHR is unavailable.");
            }
            VkXcbSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
            info.connection = connection;
            info.window = static_cast<xcb_window_t>(reinterpret_cast<std::uintptr_t>(nativeWindow));
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            Check(create(instance, &info, nullptr, &surface), "vkCreateXcbSurfaceKHR");
            return surface;
        }
        if (IsWaylandPlatform())
        {
            wl_display* const display = WaylandDisplay();
            if (display == nullptr)
            {
                throw std::runtime_error("Qt Wayland platform has no wl_display.");
            }
            const auto create = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
                instanceProc(instance, "vkCreateWaylandSurfaceKHR"));
            if (create == nullptr)
            {
                throw std::runtime_error("vkCreateWaylandSurfaceKHR is unavailable.");
            }
            VkWaylandSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
            info.display = display;
            info.surface = reinterpret_cast<wl_surface*>(nativeWindow);
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            Check(create(instance, &info, nullptr, &surface), "vkCreateWaylandSurfaceKHR");
            return surface;
        }
        throw std::runtime_error("Qt Vulkan presentation requires the xcb or Wayland platform plugin on Linux.");
#elif defined(__APPLE__)
        const auto create = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(
            instanceProc(instance, "vkCreateMetalSurfaceEXT"));
        if (create == nullptr)
        {
            throw std::runtime_error("vkCreateMetalSurfaceEXT is unavailable.");
        }
        VkMetalSurfaceCreateInfoEXT info{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
        info.pLayer = static_cast<const CAMetalLayer*>(MetalLayer(nativeWindow));
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        Check(create(instance, &info, nullptr, &surface), "vkCreateMetalSurfaceEXT");
        return surface;
#else
        (void)instance;
        (void)nativeWindow;
        (void)instanceProc;
        throw std::runtime_error("Vulkan presentation in the Qt build is not implemented on this platform.");
#endif
    }

    bool PresentationSupport(VkInstance instance, VkPhysicalDevice physical, std::uint32_t family,
        PFN_vkGetInstanceProcAddr instanceProc)
    {
#if defined(_WIN32)
        const auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR>(
            instanceProc(instance, "vkGetPhysicalDeviceWin32PresentationSupportKHR"));
        return query != nullptr && query(physical, family) == VK_TRUE;
#elif defined(__linux__)
        if (IsXcbPlatform())
        {
            xcb_connection_t* const connection = XcbConnection();
            const xcb_visualid_t visual = XcbVisual();
            if (connection == nullptr || visual == XCB_NONE)
            {
                return false;
            }
            const auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR>(
                instanceProc(instance, "vkGetPhysicalDeviceXcbPresentationSupportKHR"));
            return query != nullptr && query(physical, family, connection, visual) == VK_TRUE;
        }
        if (IsWaylandPlatform())
        {
            wl_display* const display = WaylandDisplay();
            if (display == nullptr)
            {
                return false;
            }
            const auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR>(
                instanceProc(instance, "vkGetPhysicalDeviceWaylandPresentationSupportKHR"));
            return query != nullptr && query(physical, family, display) == VK_TRUE;
        }
        return false;
#elif defined(__APPLE__)
        // Metal has no per-queue-family presentation query; MoltenVK presents
        // from every graphics family, and vkGetPhysicalDeviceSurfaceSupportKHR
        // still decides once the surface exists.
        (void)instance;
        (void)physical;
        (void)family;
        (void)instanceProc;
        return true;
#else
        (void)instance;
        (void)physical;
        (void)family;
        (void)instanceProc;
        return false;
#endif
    }

    bool Iconified(void* nativeWindow)
    {
        const QtWindow* const window = Owner(nativeWindow);
        return window != nullptr && window->Iconified();
    }

    void FramebufferSize(void* nativeWindow, int& width, int& height)
    {
        width = 0;
        height = 0;
        if (QtWindow* const window = Owner(nativeWindow))
        {
            const auto size = window->Size();
            width = size.X;
            height = size.Y;
        }
    }

    bool ShouldClose(void* nativeWindow)
    {
        const QtWindow* const window = Owner(nativeWindow);
        return window == nullptr || window->CloseRequested();
    }

    void WaitEvents(double seconds)
    {
        ::MphRead::Qt::EnsureApplication();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QThread::msleep(static_cast<unsigned long>(std::max(1.0, seconds * 1000.0)));
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}
#endif
