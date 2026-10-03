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
#include "../../MphRead.Native/Mods/Chat/ChatBox.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/Swapchain.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/PresentationScheduler.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/PresentationSleep.hpp"
#if defined(FRUITY_HAS_VULKAN)
#include "../../MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanWindowSystem.hpp"
#include "../../MphRead.Native/NativeRuntime/Rhi/Vulkan/VulkanResult.hpp"
#include <QtCore/QLibrary>
#include <QtCore/QThread>
#if defined(_WIN32)
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#undef CreateWindow
#endif
#endif

#include "../../MphRead.Native/NativeRuntime/System/Heartbeat.hpp"
#include "QtApp.hpp"
#include "QtKeys.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
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

namespace
{
    QWindow* g_gameWindow = nullptr;
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
        [[nodiscard]] bool Iconified() const { return _window->windowState() == Qt::WindowMinimized; }

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
        void WindowStateNormal() override { _window->showNormal(); }
        void Floating(bool value) override { _window->setFlag(Qt::WindowStaysOnTopHint, value); }
        [[nodiscard]] bool IsFocused() const override { return _window->isActive(); }
        [[nodiscard]] Vector2i ClientLocation() const override;
        void Focus() override { _window->requestActivate(); }

        // From GameQWindow.
        bool HandleEvent(QEvent* event);

    private:
        [[nodiscard]] qreal Scale() const { return _window->devicePixelRatio(); }
        [[nodiscard]] QScreen* ScreenOf() const;
        void Key(QKeyEvent* event, bool down);
        void MouseButton(QMouseEvent* event, bool down);
        void MouseMove(QMouseEvent* event);
        void Wheel(QWheelEvent* event);
        void RecentreGrabbedCursor();

        std::unique_ptr<GameQWindow> _window;
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
        // CURSOR_DISABLED is: the real cursor is re-centred after each move.
        float _cursorX = 0.0F;
        float _cursorY = 0.0F;
        float _lastReportedMouseX = 0.0F;
        float _lastReportedMouseY = 0.0F;
        QPointF _grabCentre{};
        bool _warping = false;
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
        _updateFrequency = settings.UpdateFrequency;
        const QPointF cursor = _window->mapFromGlobal(QCursor::pos());
        _cursorX = _lastReportedMouseX = static_cast<float>(cursor.x() * Scale());
        _cursorY = _lastReportedMouseY = static_cast<float>(cursor.y() * Scale());
        _mouse.X = _cursorX;
        _mouse.Y = _cursorY;
        if (settings.StartVisible)
        {
            _window->show();
        }
    }

    QtWindow::~QtWindow()
    {
        if (_context != nullptr)
        {
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
            ::MphRead::NativeRuntime::FrameHeartbeat();
            // As the GLFW loop: native low-latency sleep precedes every fresh
            // input read, and a busy generic budget is polled with events
            // serviced but no frame recorded and discarded.
            if (!events.BeforeFrame())
            {
                if (events.CanSampleInputWhileWaiting())
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents);
                }
                continue;
            }
            Rhi::SleepForPresentation(_presentation.Deadline(Rhi::PresentationScheduler::Clock::now()));
            events.OnInputSample();
            // OpenTK's NewInputFrame: the frame sees the cursor where it was
            // before this frame's events arrived.
            _mouse.X = _cursorX;
            _mouse.Y = _cursorY;
            QCoreApplication::processEvents(QEventLoop::AllEvents);
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
        if (grab == _grabbed)
        {
            return;
        }
        _grabbed = grab;
        if (grab)
        {
            _window->setCursor(Qt::BlankCursor);
            _window->setMouseGrabEnabled(true);
            RecentreGrabbedCursor();
        }
        else
        {
            _window->setMouseGrabEnabled(false);
            _window->unsetCursor();
        }
    }

    void QtWindow::SetSwapInterval(int interval)
    {
        // The GLX, EGL and WGL contexts apply a changed swap interval on the
        // next makeCurrent against the window's format.
        QSurfaceFormat format = _window->format();
        format.setSwapInterval(interval);
        _window->setFormat(format);
        _context->makeCurrent(_window.get());
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
        else
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
            if (_events != nullptr)
            {
                _events->OnFocusedChanged(event->type() == QEvent::FocusIn);
            }
            return false;
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
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

    void QtWindow::RecentreGrabbedCursor()
    {
        // QCursor::setPos is a no-op on Wayland, where pointer lock needs the
        // relative-pointer protocol; X11, Windows and macOS warp.
        _grabCentre = QPointF(_window->width() / 2.0, _window->height() / 2.0);
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
            if (!library.load())
            {
                return nullptr;
            }
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
#if !defined(_WIN32)
        reason = "Vulkan presentation in the Qt build is implemented for Windows only";
        return false;
#else
        return true;
#endif
    }

    std::span<const char* const> RequiredInstanceExtensions()
    {
#if defined(_WIN32)
        static constexpr std::array<const char*, 2> names{
            VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
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
#else
        (void)instance;
        (void)nativeWindow;
        (void)instanceProc;
        throw std::runtime_error("Vulkan presentation in the Qt build is implemented for Windows only.");
#endif
    }

    bool PresentationSupport(VkInstance instance, VkPhysicalDevice physical, std::uint32_t family,
        PFN_vkGetInstanceProcAddr instanceProc)
    {
#if defined(_WIN32)
        const auto query = reinterpret_cast<PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR>(
            instanceProc(instance, "vkGetPhysicalDeviceWin32PresentationSupportKHR"));
        return query != nullptr && query(physical, family) == VK_TRUE;
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
