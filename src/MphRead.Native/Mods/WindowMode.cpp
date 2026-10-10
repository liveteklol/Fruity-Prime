#include "WindowMode.hpp"

#include "WindowGeometry.hpp"

#include "../Renderer.hpp"

#include "Chat/ChatBox.hpp"
#include "../NativeRuntime/System/Globalization.hpp"
#include "../NativeRuntime/System/Managed.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

using ::MphRead::NativeRuntime::StringEqualsOrdinalIgnoreCase;
using ::MphRead::NativeRuntime::StringTrimView;
using ::MphRead::NativeRuntime::UncheckedDecrement;

namespace MphRead::Mods::Detail
{
    // PauseMenu owns its Open state and publishes only this narrow observation.
    [[nodiscard]] bool WindowModePauseMenuOpen();
}

namespace
{
    using MphRead::Mods::WindowStartMode;
    using OpenTK::Mathematics::Vector2i;

    constexpr std::int32_t ResizableWindowBorder = 0;
    constexpr std::int32_t HiddenWindowBorder = 2;
    constexpr std::int32_t EnterKey = 257;
    constexpr std::int32_t F11Key = 300;

    WindowStartMode startupState = WindowStartMode::Windowed;
    bool startupForcedState = false;
    bool fullscreenState = false;
    WindowStartMode activeState = WindowStartMode::Windowed;
    std::int32_t savedBorderState = ResizableWindowBorder;
    OpenTK::Mathematics::Vector2i savedLocationState{};
    OpenTK::Mathematics::Vector2i savedSizeState{};
    bool savedState = false;
    bool topmostState = false;


}

namespace MphRead::Mods
{
    WindowStartMode WindowMode::Startup() noexcept
    {
        return startupState;
    }

    void WindowMode::Startup(WindowStartMode value) noexcept
    {
        startupState = value;
    }

    bool WindowMode::StartupForced() noexcept
    {
        return startupForcedState;
    }

    void WindowMode::ForceStartup(WindowStartMode mode) noexcept
    {
        startupState = mode;
        startupForcedState = true;
    }

    bool WindowMode::IsFullscreen() noexcept
    {
        return fullscreenState;
    }

    WindowStartMode WindowMode::Active() noexcept
    {
        return fullscreenState ? activeState : WindowStartMode::Windowed;
    }

    WindowStartMode WindowMode::PreferredFullscreen() noexcept
    {
        return startupState == WindowStartMode::ExclusiveFullscreen
            ? WindowStartMode::ExclusiveFullscreen
            : WindowStartMode::BorderlessFullscreen;
    }

    OpenTK::Mathematics::Vector2i WindowMode::WindowedSize() noexcept
    {
        return savedState ? savedSizeState : OpenTK::Mathematics::Vector2i();
    }

    OpenTK::Mathematics::Vector2i WindowMode::WindowedLocation() noexcept
    {
        return savedLocationState;
    }

    void WindowMode::ApplyStartup(MphRead::RenderWindow& window)
    {
        if (startupState != WindowStartMode::Windowed && !fullscreenState)
        {
            Enter(window, startupState);
        }
    }

    bool WindowMode::HandleKey(
        MphRead::RenderWindow& window,
        const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e)
    {
        const std::int32_t key = static_cast<std::int32_t>(e.Key);
        if (key == F11Key || (key == EnterKey && e.Alt))
        {
            Toggle(window);
            return true;
        }
        return false;
    }

    void WindowMode::Toggle(MphRead::RenderWindow& window)
    {
        if (fullscreenState)
        {
            Leave(window);
        }
        else
        {
            Enter(window);
        }
    }

    void WindowMode::Apply(MphRead::RenderWindow& window, WindowStartMode mode)
    {
        if (Active() == mode)
        {
            return;
        }
        Leave(window);
        if (mode != WindowStartMode::Windowed)
        {
            Enter(window, mode);
        }
    }

    void WindowMode::Enter(MphRead::RenderWindow& window)
    {
        Enter(window, PreferredFullscreen());
    }

    void WindowMode::Enter(MphRead::RenderWindow& window, WindowStartMode mode)
    {
        if (fullscreenState || mode == WindowStartMode::Windowed)
        {
            return;
        }
        if (!savedState)
        {
            savedBorderState = window.WindowBorder();
            savedLocationState = window.Location();
            savedSizeState = window.ClientSize();
            savedState = true;
        }

        // Before the window is touched, not after the geometry is set --
        // and after the monitor lookup, which is the one line above that
        // can fail and leave this method without a fullscreen window to
        // describe.
        //
        // Windows dispatches WM_SIZE from inside SetWindowPos, so the
        // resize callback for the lines below runs *during* them, and
        // WindowGeometry::Capture reads this flag to decide whether the
        // rectangle it is being handed is the player's window or the
        // monitor. Set at the end instead, the one callback that matters
        // arrived while it still said "windowed", and the monitor's
        // rectangle went into the remembered window size.
        const RendererPlatform::MonitorArea monitor
            = window.CurrentMonitorClientArea();
        fullscreenState = true;

        // The toolkit's own fullscreen: no border to hide and no geometry to
        // set -- the window takes the monitor it is on, at its exact size.
        // Leave's showNormal hands the old geometry back.
        if (mode == WindowStartMode::ExclusiveFullscreen)
        {
            activeState = WindowStartMode::ExclusiveFullscreen;
            if (window.WindowStateFullscreen())
            {
                SetTopmost(window, true);
                WindowGeometry::NoteMode();
                return;
            }
        }
        activeState = WindowStartMode::BorderlessFullscreen;

        window.WindowStateNormal();
        window.WindowBorder(HiddenWindowBorder);
        RendererPlatform::ProcessEvents();
        window.Location(monitor.Min);

        // The monitor is in physical pixels, the client size in the
        // toolkit's own units: logical ones under Qt. At 150 % scaling the
        // physical size given as is made a 3840x2399 window on a 2560x1600
        // screen, and only its top-left two thirds were visible.
        const OpenTK::Mathematics::Vector2i client = window.ClientSize();
        const OpenTK::Mathematics::Vector2i framebuffer = window.FramebufferSize();
        // The width is rounded to the nearest unit; the height is rounded
        // down so the window stays shorter than the monitor, as it must for
        // borderless rather than exclusive.
        const auto logical = [](std::int32_t physical, std::int32_t clientUnits, std::int32_t pixels, bool down)
        {
            if (clientUnits <= 0 || pixels <= 0)
            {
                return physical;
            }
            const double units = static_cast<double>(physical) * clientUnits / pixels;
            return std::max<std::int32_t>(1, static_cast<std::int32_t>(down ? std::floor(units) : std::round(units)));
        };
        window.ClientSize(OpenTK::Mathematics::Vector2i{
            logical(monitor.Size.X, client.X, framebuffer.X, false),
            logical(UncheckedDecrement(monitor.Size.Y), client.Y, framebuffer.Y, true)});

        SetTopmost(window, true);
        // And written down, so the next session opens this way. See
        // WindowGeometry::NoteMode: F11 used to be a decision the program
        // forgot on exit.
        WindowGeometry::NoteMode();
    }

    void WindowMode::Leave(MphRead::RenderWindow& window)
    {
        if (!fullscreenState)
        {
            return;
        }

        window.WindowStateNormal();
        
            window.WindowBorder(savedState ? savedBorderState : ResizableWindowBorder);
        RendererPlatform::ProcessEvents();

        if (savedState)
        {
            window.ClientSize(savedSizeState);
            window.Location(savedLocationState);
        }

        fullscreenState = false;
        // Forget it, so the *next* Enter captures where the window is then.
        // Without this the saved rectangle is whatever the window was the
        // first time fullscreen was ever used: go fullscreen, come back, drag
        // the window somewhere else, go fullscreen again, and leaving put it
        // back at the first size rather than the one it was just at.
        savedState = false;
        SetTopmost(window, false);
        WindowGeometry::NoteMode();
    }

    bool WindowMode::IsTopmost() noexcept
    {
        return topmostState;
    }

    void WindowMode::SetTopmost(
        MphRead::RenderWindow& window, bool topmost)
    {
        if (topmostState == topmost)
        {
            return;
        }

        topmostState = topmost;
        try
        {
            window.Floating(topmost);
        }
        catch (...)
        {
        }
    }

    void WindowMode::SyncTopmost(MphRead::RenderWindow& window)
    {
        // Not "unless the pause menu is up" any more. That exception was for
        // a menu that was its *own* window and had to be allowed above this
        // one; the menu is drawn inside this window now, so dropping out of
        // the band while it is open only lets the taskbar cover the bottom of
        // our own screen -- which is where Save and Cancel are, and they
        // became unclickable the moment Escape was pressed in fullscreen.
        SetTopmost(window, fullscreenState && window.IsFocused());
    }

    WindowStartMode WindowMode::Parse(
        std::optional<std::string_view> value,
        WindowStartMode fallback)
    {
        if (!value.has_value())
        {
            return fallback;
        }

        const std::string_view text = StringTrimView(*value);
        if (StringEqualsOrdinalIgnoreCase(text, "exclusive")
            || StringEqualsOrdinalIgnoreCase(text, "exclusive fullscreen")
            || text == "2")
        {
            return WindowStartMode::ExclusiveFullscreen;
        }
        if (StringEqualsOrdinalIgnoreCase(text, "borderless")
            || StringEqualsOrdinalIgnoreCase(text, "fullscreen")
            || StringEqualsOrdinalIgnoreCase(text, "borderless fullscreen")
            || text == "1"
            || StringEqualsOrdinalIgnoreCase(text, "true"))
        {
            return WindowStartMode::BorderlessFullscreen;
        }
        if (StringEqualsOrdinalIgnoreCase(text, "windowed")
            || StringEqualsOrdinalIgnoreCase(text, "window")
            || text == "0"
            || StringEqualsOrdinalIgnoreCase(text, "false"))
        {
            return WindowStartMode::Windowed;
        }
        return fallback;
    }
}

namespace MphRead::Mods::Launcher::Detail
{
    void TextLauncherSetWindowStartup(MphRead::Mods::WindowStartMode value)
    {
        MphRead::Mods::WindowMode::Startup(value);
    }
}
