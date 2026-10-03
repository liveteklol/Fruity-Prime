#include "GuiLauncher.hpp"

#include "Shell.hpp"
#include "../../Branding.hpp"
#include "../../../NativeRuntime/Rhi/SceneBackend.hpp"
#include "../../../NativeRuntime/System/ErrorDialog.hpp"
#include "UiTopLevel.hpp"
#include "../../Diagnostics/PlatformDiagnostics.hpp"
#include "../../../NativeRuntime/System/Console.hpp"
#include "../../../NativeRuntime/System/ExceptionText.hpp"
#include "../../../NativeRuntime/System/Runtime.hpp"

#include <exception>
#include <string>

namespace MphRead::Mods::Launcher::Gui
{
    namespace Runtime = ::MphRead::NativeRuntime;

    std::atomic_bool GuiLauncher::_setUp{false};
    std::atomic_bool GuiLauncher::_failed{false};

    namespace
    {
        void WriteLauncherOpenFailure(std::exception_ptr exception)
        {
            Runtime::ConsoleWriteLine("[launcher] the window could not be opened: "
                + Runtime::ExceptionMessage(exception));
            Runtime::ConsoleWriteLine("[launcher] falling back to the text launcher");
            ::MphRead::Mods::Diagnostics::PlatformDiagnostics::Report(
                "libglfw.3.dylib", exception);
        }

        void WriteToolkitFailure(std::exception_ptr exception)
        {
            Runtime::ConsoleWriteLine("[launcher] the window toolkit could not start: "
                + Runtime::ExceptionMessage(exception));
            ::MphRead::Mods::Diagnostics::PlatformDiagnostics::Report(
                "libSkiaSharp.dylib", exception);
        }
    }

    bool GuiLauncher::TryRun()
    {
        if (!EnsureSetup())
        {
            return false;
        }
        try
        {
#if defined(ANDROID) || defined(__ANDROID__)
            // Android enters through its activity and hosts the same screens
            // over the GL surface; the desktop launcher has no role there.
            return false;
#else
            return Shell::Run();
#endif
        }
        catch (const ::MphRead::NativeRuntime::Rhi::SceneBackendUnavailable& unavailable)
        {
            // A renderer asked for by name that cannot start is the person's
            // to hear about, not a reason to open a different launcher.
            Runtime::ConsoleWriteLine(std::string("[launcher] ") + unavailable.what());
            Runtime::ShowErrorDialog(std::string(::MphRead::Mods::Branding::Name),
                std::string(unavailable.what())
                    + "\n\nChoose OpenGL or Auto under Settings > Game > Renderer, or start with -rhi opengl.");
            return true;
        }
        catch (...)
        {
            WriteLauncherOpenFailure(std::current_exception());
            return false;
        }
    }

    bool GuiLauncher::EnsureSetup(bool requireDisplay)
    {
        if (_setUp.load(std::memory_order_relaxed))
        {
            return true;
        }
        if (_failed.load(std::memory_order_relaxed) || (requireDisplay && !Probe()))
        {
            return false;
        }
        try
        {
#if defined(ANDROID) || defined(__ANDROID__)
            _setUp.store(false, std::memory_order_relaxed);
            return false;
#else
            // NativeRuntime uses the in-tree Avalonia/Skia implementation, so
            // there is no second platform window or AppBuilder to initialise.
            // Installing the frame pump is the one process-wide setup step.
            UiRenderTimer::Install();
            _setUp.store(true, std::memory_order_relaxed);
            return true;
#endif
        }
        catch (...)
        {
            _failed.store(true, std::memory_order_relaxed);
            WriteToolkitFailure(std::current_exception());
            SayWhyOnLinux();
            return false;
        }
    }

    void GuiLauncher::SayWhyOnLinux()
    {
        if (Runtime::IsWindows() || Runtime::IsMacOS() || Runtime::IsAndroid())
        {
            return;
        }
        Runtime::ConsoleWriteLine("[launcher] the game itself is unaffected -- the text launcher "
            "below starts the same matches.");
        Runtime::ConsoleWriteLine("[launcher] the screens need fontconfig, which a minimal install "
            "sometimes lacks:");
        Runtime::ConsoleWriteLine("[launcher]   Debian/Ubuntu: sudo apt install libfontconfig1");
        Runtime::ConsoleWriteLine("[launcher]   Fedora: sudo dnf install fontconfig");
        Runtime::ConsoleWriteLine("[launcher]   NixOS/Guix: run it inside an FHS environment, "
            "e.g. steam-run ./FruityPrime -launcher");
    }

    bool GuiLauncher::Probe()
    {
        if (Runtime::IsWindows() || Runtime::IsMacOS())
        {
            return true;
        }
        const std::string display = Runtime::EnvironmentGetVariable("DISPLAY").value_or("");
        const std::string wayland = Runtime::EnvironmentGetVariable("WAYLAND_DISPLAY").value_or("");
        if (display.empty() && wayland.empty())
        {
            Runtime::ConsoleWriteLine("[launcher] no DISPLAY or WAYLAND_DISPLAY; "
                "using the text launcher");
            return false;
        }
        return true;
    }
}
