// OpenTK.Windowing.Desktop.GameWindow, over the same GLFW the managed build
// runs on. What this file owns is only the toolkit binding: every frame, every
// event and every state read is handed straight to the Scene's own members,
// which is what GameWindow does for the C# renderer.

#include "../../Renderer.hpp"
#include "../Rhi/OpenGL/OpenGlDevice.hpp"
#include "../Rhi/PresentationSleep.hpp"

#include "../../Mods/Chat/ChatBox.hpp"
#include "../System/Heartbeat.hpp"
#include "../System/IO.hpp"
#include "../System/Console.hpp"
#include "../System/Exceptions.hpp"
#include "../System/Managed.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#include <windows.h>
#undef CreateWindow
#endif

using ::MphRead::NativeRuntime::ConsoleWrite;
using ::MphRead::NativeRuntime::ConsoleWriteLine;

namespace
{
    namespace Rhi = MphRead::NativeRuntime::Rhi;
    using Rhi::SleepForPresentation;
    using MphRead::RendererPlatform::CursorState;
    using MphRead::RendererPlatform::FrameEventArgs;
    using MphRead::RendererPlatform::GraphicsWindowMode;
    using MphRead::RendererPlatform::GLFWException;
    using MphRead::RendererPlatform::MouseButtonEventArgs;
    using MphRead::RendererPlatform::MouseMoveEventArgs;
    using MphRead::RendererPlatform::MouseWheelEventArgs;
    using MphRead::RendererPlatform::ResizeEventArgs;
    using MphRead::RendererPlatform::TextInputEventArgs;
    using MphRead::RendererPlatform::WindowEvents;
    using MphRead::RendererPlatform::WindowPositionEventArgs;
    using MphRead::RendererPlatform::WindowSettings;
    using ::OpenTK::Windowing::Common::KeyboardKeyEventArgs;

    std::function<void(std::int32_t, std::string)>& ErrorCallback()
    {
        static std::function<void(std::int32_t, std::string)> callback;
        return callback;
    }

    void ForwardGlfwError(int code, const char* description)
    {
        if (ErrorCallback())
        {
            ErrorCallback()(static_cast<std::int32_t>(code),
                description == nullptr ? std::string() : std::string(description));
        }
    }

    // GLFW is initialized once for the process, as OpenTK's toolkit is.
    void EnsureGlfw()
    {
        static const bool ready = []()
        {
            ::glfwSetErrorCallback(&ForwardGlfwError);
            if (::glfwInit() == GLFW_FALSE)
            {
                const char* description = nullptr;
                const int code = ::glfwGetError(&description);
                throw GLFWException(
                    description == nullptr ? std::string("GLFW could not start.")
                                           : std::string(description),
                    static_cast<std::int32_t>(code));
            }
            return true;
        }();
        (void)ready;
    }

    // Monitors.GetMonitorFromWindow: prefer an assigned fullscreen monitor;
    // otherwise choose the monitor with the largest ClientArea intersection.
    [[nodiscard]] GLFWmonitor* MonitorForWindow(GLFWwindow* handle)
    {
        GLFWmonitor* monitor = ::glfwGetWindowMonitor(handle);
        if (monitor != nullptr)
        {
            return monitor;
        }
        int windowX = 0;
        int windowY = 0;
        ::glfwGetWindowPos(handle, &windowX, &windowY);
        int windowWidth = 0;
        int windowHeight = 0;
        ::glfwGetWindowSize(handle, &windowWidth, &windowHeight);
        const int windowRight = ::MphRead::NativeRuntime::UncheckedAdd(windowX, windowWidth);
        const int windowBottom = ::MphRead::NativeRuntime::UncheckedAdd(windowY, windowHeight);
        const int windowMinX = std::min(windowX, windowRight);
        const int windowMinY = std::min(windowY, windowBottom);
        const int windowMaxX = std::max(windowX, windowRight);
        const int windowMaxY = std::max(windowY, windowBottom);

        int count = 0;
        GLFWmonitor** const monitors = ::glfwGetMonitors(&count);
        if (monitors == nullptr || count <= 0)
        {
            // OpenTK's GetMonitorFromWindow indexes monitor 0 after building
            // the list, so an empty list throws ArgumentOutOfRangeException.
            throw System::ArgumentOutOfRangeException();
        }

        const auto intersectionArea = [=](GLFWmonitor* candidate)
        {
            int monitorX = 0;
            int monitorY = 0;
            ::glfwGetMonitorPos(candidate, &monitorX, &monitorY);
            const GLFWvidmode* const mode = ::glfwGetVideoMode(candidate);
            if (mode == nullptr)
            {
                return 0;
            }
            const int monitorRight = ::MphRead::NativeRuntime::UncheckedAdd(
                monitorX, mode->width);
            const int monitorBottom = ::MphRead::NativeRuntime::UncheckedAdd(
                monitorY, mode->height);
            const int monitorMinX = std::min(monitorX, monitorRight);
            const int monitorMinY = std::min(monitorY, monitorBottom);
            const int monitorMaxX = std::max(monitorX, monitorRight);
            const int monitorMaxY = std::max(monitorY, monitorBottom);

            const int minX = std::max(monitorMinX, windowMinX);
            const int minY = std::max(monitorMinY, windowMinY);
            const int maxX = std::min(monitorMaxX, windowMaxX);
            const int maxY = std::min(monitorMaxY, windowMaxY);
            if (maxX < minX || maxY < minY)
            {
                return 0;
            }
            const int width = ::MphRead::NativeRuntime::UncheckedSubtract(maxX, minX);
            const int height = ::MphRead::NativeRuntime::UncheckedSubtract(maxY, minY);
            return ::MphRead::NativeRuntime::UncheckedMultiply(width, height);
        };

        int selectedIndex = 0;
        int selectedArea = intersectionArea(monitors[selectedIndex]);
        for (int i = 0; i < count; ++i)
        {
            const int area = intersectionArea(monitors[i]);
            if (area > selectedArea)
            {
                selectedIndex = i;
                selectedArea = area;
            }
        }
        return monitors[selectedIndex];
    }

    class GlfwWindow final : public MphRead::RendererPlatform::Window
    {
    public:
        explicit GlfwWindow(const WindowSettings& settings)
        {
            EnsureGlfw();
            _graphicsMode = settings.GraphicsMode;
            ::glfwWindowHint(GLFW_CLIENT_API,
                _graphicsMode == GraphicsWindowMode::OpenGL ? GLFW_OPENGL_API : GLFW_NO_API);
            if (_graphicsMode == GraphicsWindowMode::OpenGL)
            {
                ::glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, settings.ApiMajor);
                ::glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, settings.ApiMinor);
                // ContextProfile.Compatability, and it has to be asked for by
                // name: the renderer is written in immediate mode, which a core
                // profile does not have, and a driver handed ANY_PROFILE with a
                // version of 3.2 or above gives a core one -- every frame then
                // comes out black with nothing in any log to say why.
                ::glfwWindowHint(GLFW_OPENGL_PROFILE,
                    settings.Profile == WindowSettings::ContextProfile::Compatability
                        ? GLFW_OPENGL_COMPAT_PROFILE
                        : GLFW_OPENGL_ANY_PROFILE);
            }
            ::glfwWindowHint(GLFW_VISIBLE, settings.StartVisible ? GLFW_TRUE : GLFW_FALSE);
            _handle = ::glfwCreateWindow(
                settings.ClientSize.X, settings.ClientSize.Y,
                settings.Title.c_str(), nullptr, nullptr);
            if (_handle == nullptr)
            {
                const char* description = nullptr;
                const int code = ::glfwGetError(&description);
                throw GLFWException(
                    description == nullptr ? std::string("The window could not be created.")
                                           : std::string(description),
                    static_cast<std::int32_t>(code));
            }
            double cursorX = 0.0;
            double cursorY = 0.0;
            ::glfwGetCursorPos(_handle, &cursorX, &cursorY);
            _mouse.X = _lastReportedMouseX = static_cast<float>(cursorX);
            _mouse.Y = _lastReportedMouseY = static_cast<float>(cursorY);
            _updateFrequency = settings.UpdateFrequency;
            if (_graphicsMode == GraphicsWindowMode::OpenGL)
            {
                ::glfwMakeContextCurrent(_handle);
                ::glfwSwapInterval(1);
            }
            ::glfwSetWindowUserPointer(_handle, this);
            ::glfwSetFramebufferSizeCallback(_handle, &OnFramebufferSize);
            ::glfwSetWindowPosCallback(_handle, &OnWindowPos);
            ::glfwSetWindowMaximizeCallback(_handle, &OnWindowMaximize);
            ::glfwSetWindowFocusCallback(_handle, &OnWindowFocus);
            ::glfwSetKeyCallback(_handle, &OnKey);
            ::glfwSetCharCallback(_handle, &OnChar);
            ::glfwSetMouseButtonCallback(_handle, &OnMouseButton);
            ::glfwSetCursorPosCallback(_handle, &OnCursorPos);
            ::glfwSetScrollCallback(_handle, &OnScroll);
        }

        ~GlfwWindow() override
        {
            if (_handle != nullptr)
            {
                if (_graphicsMode == GraphicsWindowMode::OpenGL)
                {
                    ::glfwMakeContextCurrent(_handle);
                    MphRead::NativeRuntime::Rhi::OpenGL::ReleaseContextDevice();
                }
                ::glfwDestroyWindow(_handle);
                _handle = nullptr;
            }
        }

        [[nodiscard]] GLFWwindow* Handle() const noexcept { return _handle; }

        void Run(WindowEvents& events) override
        {
            _events = &events;
            if (_graphicsMode == GraphicsWindowMode::OpenGL)
            {
                ::glfwMakeContextCurrent(_handle);
            }
            events.OnLoad();
            // The size the window actually got, which OnLoad's callers read.
            ResizeEventArgs resize;
            resize.Size = Size();
            events.OnResize(resize);

            auto previous = std::chrono::steady_clock::now();
            while (::glfwWindowShouldClose(_handle) == GLFW_FALSE)
            {
                ::MphRead::NativeRuntime::FrameHeartbeat();
                // Native sleep precedes every fresh input read. Poll a busy
                // generic budget with events serviced, but never sample input
                // while the driver's native frame-start signal is pending.
                if (!events.BeforeFrame())
                {
                    if (events.CanSampleInputWhileWaiting()) ::glfwPollEvents();
                    continue;
                }
                SleepForPresentation(_presentation.Deadline(Rhi::PresentationScheduler::Clock::now()));
                events.OnInputSample();
                // OpenTK's NewInputFrame polls the current cursor position
                // separately from the cursor callback's last-reported point.
                double cursorX = 0.0;
                double cursorY = 0.0;
                ::glfwGetCursorPos(_handle, &cursorX, &cursorY);
                _mouse.X = static_cast<float>(cursorX);
                _mouse.Y = static_cast<float>(cursorY);
                ::glfwPollEvents();
                // Bounded budget waits run with events serviced between them.
                // A busy GPU postpones admission; no primary draw or simulation
                // step is recorded and discarded. Elapsed time is retained.

                const auto now = std::chrono::steady_clock::now();
                const double elapsed
                    = std::chrono::duration<double>(now - previous).count();
                // UpdateFrequency caps how often the loop runs; zero is
                // OpenTK's "as fast as the frames arrive".
                if (_updateFrequency > 0.0 && elapsed < 1.0 / _updateFrequency)
                {
                    std::this_thread::sleep_for(std::chrono::duration<double>(
                        1.0 / _updateFrequency - elapsed));
                    continue;
                }
                previous = now;
                _presentationFrameStart = now;
                FrameEventArgs args;
                args.Time = elapsed;
                events.OnRenderFrame(args);
            }
            events.OnClosing();
            _events = nullptr;
        }

        [[nodiscard]] OpenTK::Mathematics::Vector2i Size() const override
        {
            int width = 0;
            int height = 0;
            ::glfwGetFramebufferSize(_handle, &width, &height);
            return OpenTK::Mathematics::Vector2i(width, height);
        }

        [[nodiscard]] MphRead::RendererPlatform::KeyboardState& Keyboard() override
        {
            return _keyboard;
        }

        [[nodiscard]] MphRead::RendererPlatform::MouseState& Mouse() override
        {
            return _mouse;
        }

        void Title(std::string value) override
        {
            ::glfwSetWindowTitle(_handle, value.c_str());
        }

        void MinimumSize(OpenTK::Mathematics::Vector2i value) override
        {
            ::glfwSetWindowSizeLimits(
                _handle, value.X, value.Y, GLFW_DONT_CARE, GLFW_DONT_CARE);
        }

        void Cursor(CursorState value) override
        {
            ::glfwSetInputMode(_handle, GLFW_CURSOR,
                value == CursorState::Grabbed ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }

        void UpdateFrequency(double value) override
        {
            _updateFrequency = value;
        }
        void PresentationTiming(Rhi::PresentMode mode, std::int32_t cap, Rhi::PacingAuthority authority) override
        {
            const auto monitor = MonitorForWindow(_handle);
            const auto* video = monitor ? glfwGetVideoMode(monitor) : nullptr;
            _presentation.Configure({cap, video && video->refreshRate > 0 ? static_cast<double>(video->refreshRate) : 60.0, mode, authority});
        }
        // Anchor the deadline at frame admission. A blocking FIFO present
        // already consumes that period; do not sleep a second whole period
        // after it returns. Advance only after native present accepts work.
        void PresentationAccepted() override { _presentation.Accepted(_presentationFrameStart); }
        void PresentationUnavailable() override { _presentation.Unavailable(); }

        void Visible(bool value) override
        {
            if (value)
            {
                ::glfwShowWindow(_handle);
            }
            else
            {
                ::glfwHideWindow(_handle);
            }
        }

        void SetIcon(const ::MphRead::RendererPlatform::WindowIcon& icon) override
        {
            std::vector<GLFWimage> images;
            images.reserve(icon.Images.size());
            for (const ::MphRead::RendererPlatform::WindowIconImage& image : icon.Images)
            {
                images.push_back(GLFWimage{image.Width, image.Height,
                    const_cast<unsigned char*>(image.Pixels.data())});
            }
            ::glfwSetWindowIcon(_handle, static_cast<int>(images.size()), images.data());
        }

        void* NativeHandle() const override
        {
            return _handle;
        }

        GraphicsWindowMode GraphicsMode() const noexcept override
        {
            return _graphicsMode;
        }

        void Close() override
        {
            ::glfwSetWindowShouldClose(_handle, GLFW_TRUE);
        }

        // GameWindow's own handlers raise events nothing here subscribes to.
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

        std::int32_t WindowBorder() const override
        {
            if (::glfwGetWindowAttrib(_handle, GLFW_DECORATED) == GLFW_FALSE)
            {
                return static_cast<std::int32_t>(
                    MphRead::RendererPlatform::WindowBorderValue::Hidden);
            }
            if (::glfwGetWindowAttrib(_handle, GLFW_RESIZABLE) == GLFW_FALSE)
            {
                return static_cast<std::int32_t>(
                    MphRead::RendererPlatform::WindowBorderValue::Fixed);
            }
            return static_cast<std::int32_t>(
                MphRead::RendererPlatform::WindowBorderValue::Resizable);
        }

        void WindowBorder(std::int32_t value) override
        {
            const auto border
                = static_cast<MphRead::RendererPlatform::WindowBorderValue>(value);
            ::glfwSetWindowAttrib(_handle, GLFW_DECORATED,
                border == MphRead::RendererPlatform::WindowBorderValue::Hidden
                    ? GLFW_FALSE : GLFW_TRUE);
            ::glfwSetWindowAttrib(_handle, GLFW_RESIZABLE,
                border == MphRead::RendererPlatform::WindowBorderValue::Resizable
                    ? GLFW_TRUE : GLFW_FALSE);
        }

        OpenTK::Mathematics::Vector2i Location() const override
        {
            int left = 0;
            int top = 0;
            int right = 0;
            int bottom = 0;
            ::glfwGetWindowFrameSize(_handle, &left, &top, &right, &bottom);
            int x = 0;
            int y = 0;
            ::glfwGetWindowPos(_handle, &x, &y);
            return OpenTK::Mathematics::Vector2i(
                ::MphRead::NativeRuntime::UncheckedSubtract(x, left),
                ::MphRead::NativeRuntime::UncheckedSubtract(y, top));
        }

        void Location(OpenTK::Mathematics::Vector2i value) override
        {
            int left = 0;
            int top = 0;
            int right = 0;
            int bottom = 0;
            ::glfwGetWindowFrameSize(_handle, &left, &top, &right, &bottom);
            ::glfwSetWindowPos(_handle,
                ::MphRead::NativeRuntime::UncheckedAdd(value.X, left),
                ::MphRead::NativeRuntime::UncheckedAdd(value.Y, top));
        }

        OpenTK::Mathematics::Vector2i ClientSize() const override
        {
            int width = 0;
            int height = 0;
            ::glfwGetWindowSize(_handle, &width, &height);
            return OpenTK::Mathematics::Vector2i(width, height);
        }

        void ClientSize(OpenTK::Mathematics::Vector2i value) override
        {
            ::glfwSetWindowSize(_handle, value.X, value.Y);
        }

        MphRead::RendererPlatform::MonitorArea CurrentMonitorClientArea() const override
        {
            MphRead::RendererPlatform::MonitorArea area;
            GLFWmonitor* monitor = MonitorForWindow(_handle);
            if (monitor == nullptr)
            {
                return area;
            }
            int x = 0;
            int y = 0;
            ::glfwGetMonitorPos(monitor, &x, &y);
            const GLFWvidmode* mode = ::glfwGetVideoMode(monitor);
            if (mode == nullptr)
            {
                return area;
            }
            area.Min = OpenTK::Mathematics::Vector2i(x, y);
            area.Size = OpenTK::Mathematics::Vector2i(mode->width, mode->height);
            return area;
        }

        MphRead::RendererPlatform::MonitorArea CurrentMonitorWorkArea() const override
        {
            MphRead::RendererPlatform::MonitorArea area;
            GLFWmonitor* monitor = MonitorForWindow(_handle);
            if (monitor == nullptr)
            {
                return area;
            }
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            ::glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
            area.Min = OpenTK::Mathematics::Vector2i(x, y);
            area.Size = OpenTK::Mathematics::Vector2i(width, height);
            return area;
        }

        std::vector<MphRead::RendererPlatform::MonitorArea> MonitorClientAreas() const override
        {
            std::vector<MphRead::RendererPlatform::MonitorArea> areas;
            int count = 0;
            GLFWmonitor** const monitors = ::glfwGetMonitors(&count);
            if (monitors == nullptr)
            {
                return areas;
            }
            for (int i = 0; i < count; ++i)
            {
                int x = 0;
                int y = 0;
                ::glfwGetMonitorPos(monitors[i], &x, &y);
                const GLFWvidmode* mode = ::glfwGetVideoMode(monitors[i]);
                if (mode == nullptr)
                {
                    continue;
                }
                MphRead::RendererPlatform::MonitorArea area;
                area.Min = OpenTK::Mathematics::Vector2i(x, y);
                area.Size = OpenTK::Mathematics::Vector2i(mode->width, mode->height);
                areas.push_back(area);
            }
            return areas;
        }

        MphRead::RendererPlatform::WindowStateValue WindowState() const override
        {
            if (::glfwGetWindowAttrib(_handle, GLFW_ICONIFIED) != GLFW_FALSE)
            {
                return MphRead::RendererPlatform::WindowStateValue::Minimized;
            }
            if (::glfwGetWindowAttrib(_handle, GLFW_MAXIMIZED) != GLFW_FALSE)
            {
                return MphRead::RendererPlatform::WindowStateValue::Maximized;
            }
            return MphRead::RendererPlatform::WindowStateValue::Normal;
        }

        void WindowStateMinimized() override
        {
            ::glfwIconifyWindow(_handle);
        }

        void WindowStateMaximized() override
        {
            ::glfwMaximizeWindow(_handle);
        }

        void WindowStateNormal() override
        {
            ::glfwRestoreWindow(_handle);
        }

        void Floating(bool value) override
        {
            ::glfwSetWindowAttrib(_handle, GLFW_FLOATING, value ? GLFW_TRUE : GLFW_FALSE);
        }

        bool IsFocused() const override
        {
            return ::glfwGetWindowAttrib(_handle, GLFW_FOCUSED) != GLFW_FALSE;
        }

        OpenTK::Mathematics::Vector2i ClientLocation() const override
        {
            // The client area's own origin, which is the window position plus
            // the frame GLFW reports around it.
            int x = 0;
            int y = 0;
            ::glfwGetWindowPos(_handle, &x, &y);
            return OpenTK::Mathematics::Vector2i(x, y);
        }

        void Focus() override
        {
            ::glfwFocusWindow(_handle);
        }

    private:
        [[nodiscard]] static GlfwWindow* From(GLFWwindow* handle)
        {
            return static_cast<GlfwWindow*>(::glfwGetWindowUserPointer(handle));
        }

        static void OnFramebufferSize(GLFWwindow* handle, int width, int height)
        {
            GlfwWindow* const self = From(handle);
            if (self == nullptr || self->_events == nullptr)
            {
                return;
            }
            ResizeEventArgs args;
            args.Size = OpenTK::Mathematics::Vector2i(width, height);
            self->_events->OnResize(args);
        }

        static void OnWindowPos(GLFWwindow* handle, int x, int y)
        {
            GlfwWindow* const self = From(handle);
            if (self == nullptr || self->_events == nullptr)
            {
                return;
            }
            WindowPositionEventArgs args;
            args.Position = OpenTK::Mathematics::Vector2i(x, y);
            self->_events->OnMove(args);
        }

        static void OnWindowFocus(GLFWwindow* handle, int focused)
        {
            GlfwWindow* const self = From(handle);
            if (self != nullptr && self->_events != nullptr)
            {
                self->_events->OnFocusedChanged(focused != GLFW_FALSE);
            }
        }

        static void OnWindowMaximize(GLFWwindow* handle, int maximized)
        {
            GlfwWindow* const self = From(handle);
            if (self != nullptr && self->_events != nullptr)
            {
                self->_events->OnMaximizedChanged(maximized != GLFW_FALSE);
            }
        }

        static void OnKey(GLFWwindow* handle, int key, int scancode, int action, int mods)
        {
            (void)scancode;
            GlfwWindow* const self = From(handle);
            if (self == nullptr || key < 0)
            {
                return;
            }
            const bool down = action != GLFW_RELEASE;
            self->_keyboard.SetKeyDown(
                static_cast<MphRead::RendererPlatform::Key>(key), down);
            if (self->_events == nullptr)
            {
                return;
            }
            KeyboardKeyEventArgs args;
            args.Key = static_cast<MphRead::RendererPlatform::Key>(key);
            args.Shift = (mods & GLFW_MOD_SHIFT) != 0;
            args.Control = (mods & GLFW_MOD_CONTROL) != 0;
            args.Alt = (mods & GLFW_MOD_ALT) != 0;
            args.Command = (mods & GLFW_MOD_SUPER) != 0;
            if (action == GLFW_RELEASE)
            {
                self->_events->OnKeyUp(args);
            }
            else
            {
                self->_events->OnKeyDown(args);
            }
        }

        static void OnChar(GLFWwindow* handle, unsigned int codepoint)
        {
            GlfwWindow* const self = From(handle);
            if (self == nullptr || self->_events == nullptr)
            {
                return;
            }
            TextInputEventArgs args;
            args.Unicode = static_cast<std::uint32_t>(codepoint);
            self->_events->OnTextInput(args);
        }

        static void OnMouseButton(GLFWwindow* handle, int button, int action, int mods)
        {
            (void)mods;
            GlfwWindow* const self = From(handle);
            if (self == nullptr || button < 0)
            {
                return;
            }
            const auto value = static_cast<MphRead::RendererPlatform::MouseButton>(button);
            self->_mouse.SetButtonDown(value, action != GLFW_RELEASE);
            if (self->_events == nullptr)
            {
                return;
            }
            MouseButtonEventArgs args;
            args.Button = value;
            if (action == GLFW_PRESS)
            {
                self->_events->OnMouseDown(args);
            }
            else if (action == GLFW_RELEASE)
            {
                self->_events->OnMouseUp(args);
            }
        }

        static void OnCursorPos(GLFWwindow* handle, double x, double y)
        {
            GlfwWindow* const self = From(handle);
            if (self == nullptr)
            {
                return;
            }
            const float newX = static_cast<float>(x);
            const float newY = static_cast<float>(y);
            // Match OpenTK's event position and delta from the previous point.
            MouseMoveEventArgs args;
            args.X = newX;
            args.Y = newY;
            args.DeltaX = newX - self->_lastReportedMouseX;
            args.DeltaY = newY - self->_lastReportedMouseY;
            self->_lastReportedMouseX = newX;
            self->_lastReportedMouseY = newY;
            // NativeWindow.CursorPosCallback reports the event and updates its
            // separate last-reported position. MouseState.Position is sampled
            // by NewInputFrame before GLFW processes events, so leave _mouse at
            // that frame's polled position until the next loop iteration.
            if (self->_events != nullptr)
            {
                self->_events->OnMouseMove(args);
            }
        }

        static void OnScroll(GLFWwindow* handle, double offsetX, double offsetY)
        {
            GlfwWindow* const self = From(handle);
            if (self == nullptr)
            {
                return;
            }
            self->_mouse.Scroll.X += static_cast<float>(offsetX);
            self->_mouse.Scroll.Y += static_cast<float>(offsetY);
            if (self->_events == nullptr)
            {
                return;
            }
            MouseWheelEventArgs args;
            args.OffsetX = static_cast<float>(offsetX);
            args.OffsetY = static_cast<float>(offsetY);
            self->_events->OnMouseWheel(args);
        }

        GLFWwindow* _handle = nullptr;
        GraphicsWindowMode _graphicsMode = GraphicsWindowMode::OpenGL;
        WindowEvents* _events = nullptr;
        double _updateFrequency = 0.0;
        Rhi::PresentationScheduler _presentation;
        Rhi::PresentationScheduler::Time _presentationFrameStart{};
        MphRead::RendererPlatform::KeyboardState _keyboard{};
        MphRead::RendererPlatform::MouseState _mouse{};
        float _lastReportedMouseX = 0.0F;
        float _lastReportedMouseY = 0.0F;
    };
}

namespace MphRead::RendererPlatform
{
    void* CurrentGlContext() noexcept
    {
        return ::glfwGetCurrentContext();
    }

    void MakeGlContextCurrent(void* context) noexcept
    {
        ::glfwMakeContextCurrent(static_cast<GLFWwindow*>(context));
    }

    std::shared_ptr<Window> CreateWindow(const WindowSettings& settings)
    {
        return std::make_shared<GlfwWindow>(settings);
    }

    OpenTK::Mathematics::Vector2i WorkAreaForWindow(Window& window)
    {
        EnsureGlfw();
        const MonitorArea area = window.CurrentMonitorWorkArea();
        return area.Size;
    }

    void ProcessEvents()
    {
        EnsureGlfw();
        ::glfwPollEvents();
    }

    void InstallGlfwErrorCallback(std::function<void(std::int32_t, std::string)> callback)
    {
        ErrorCallback() = std::move(callback);
        // NativeWindowSettings installs the callback before OpenTK initializes
        // GLFW, so failures while GLFW starts or queries monitors are reported
        // through the same handler. Do not initialize here: the context owner
        // may still need GLFW.InitHint before the first window is made.
        ::glfwSetErrorCallback(&ForwardGlfwError);
    }

    std::int32_t GlfwFeatureUnavailableCode()
    {
#if defined(GLFW_FEATURE_UNAVAILABLE)
        return static_cast<std::int32_t>(GLFW_FEATURE_UNAVAILABLE);
#else
        // GLFW before 3.4 has no such error and never reports it; this is the
        // value 3.4 gives it, which OpenTK's ErrorCode.FeatureUnavailable is.
        return 0x0001000C;
#endif
    }
}
