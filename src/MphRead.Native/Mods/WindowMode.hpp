#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace OpenTK::Windowing::Common
{
    struct KeyboardKeyEventArgs;
}

namespace OpenTK::Mathematics
{
    struct Vector2i;
}

namespace MphRead
{
    class RenderWindow;
}

namespace MphRead::Mods
{
    enum class WindowStartMode : std::int32_t
    {
        Windowed,
        BorderlessFullscreen,
        // The toolkit's real fullscreen state at the monitor's exact size,
        // which the driver may turn into exclusive ownership of the display
        // (flip without the compositor). Borderless deliberately stays one
        // pixel short of the monitor so that it never does.
        ExclusiveFullscreen
    };

    class WindowMode final
    {
    public:
        WindowMode() = delete;

        [[nodiscard]] static WindowStartMode Startup() noexcept;
        static void Startup(WindowStartMode value) noexcept;

        // Whether Startup came from the command line, and so must not be
        // overwritten by the saved preference.
        //
        // The launcher reads the preference as it opens its window, and it
        // does that *after* the flags have been parsed: without this,
        // `-launcher -fullscreen` opened windowed, because the preference
        // landed on top of the flag. The flag is the more specific
        // instruction -- somebody typed it for this run.
        //
        // What happens to the preference afterwards is not this flag's
        // business: the window goes fullscreen, and WindowGeometry::NoteMode
        // writes down where the window ended up, the same as it would for F11.
        // Somebody who starts fullscreen and quits from fullscreen was last in
        // fullscreen.
        [[nodiscard]] static bool StartupForced() noexcept;

        // The command line asking for a mode, once.
        static void ForceStartup(WindowStartMode mode) noexcept;

        [[nodiscard]] static bool IsFullscreen() noexcept;

        // Which mode the window is in now: Windowed, or the fullscreen kind
        // Enter actually managed (exclusive falls back to borderless on a
        // window that cannot do it).
        [[nodiscard]] static WindowStartMode Active() noexcept;

        // The fullscreen kind F11 and the pause menu enter: the one the window
        // started in when it started fullscreen, else FullscreenKind's.
        [[nodiscard]] static WindowStartMode PreferredFullscreen() noexcept;
        // What fullscreen means for a window that starts windowed: exclusive
        // unless the player chose the borderless kind. A fullscreen Startup
        // sets it too.
        static void FullscreenKind(WindowStartMode kind) noexcept;

        // The shape the window had before fullscreen took it, for whoever
        // needs the *windowed* geometry while the window is reporting the
        // monitor's.
        //
        // WindowGeometry is the caller: a player who quits from fullscreen
        // must not have the monitor's rectangle saved as their window size, or
        // their next windowed session opens the size of the screen with a
        // title bar pushing it off the bottom.
        [[nodiscard]] static OpenTK::Mathematics::Vector2i WindowedSize() noexcept;
        [[nodiscard]] static OpenTK::Mathematics::Vector2i WindowedLocation() noexcept;

        static void ApplyStartup(MphRead::RenderWindow& window);
        [[nodiscard]] static bool HandleKey(
            MphRead::RenderWindow& window,
            const OpenTK::Windowing::Common::KeyboardKeyEventArgs& e);
        static void Toggle(MphRead::RenderWindow& window);
        static void Enter(MphRead::RenderWindow& window);
        static void Enter(MphRead::RenderWindow& window, WindowStartMode mode);
        // Bring the window to `mode`, switching between the two fullscreen
        // kinds through windowed when it has to.
        static void Apply(MphRead::RenderWindow& window, WindowStartMode mode);
        static void Leave(MphRead::RenderWindow& window);

        [[nodiscard]] static bool IsTopmost() noexcept;
        static void SetTopmost(MphRead::RenderWindow& window, bool topmost);
        static void SyncTopmost(MphRead::RenderWindow& window);

        [[nodiscard]] static WindowStartMode Parse(
            std::optional<std::string_view> value,
            WindowStartMode fallback);
    };
}
