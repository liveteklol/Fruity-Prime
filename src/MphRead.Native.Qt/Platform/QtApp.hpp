#pragma once

// The process's QGuiApplication. It is made on first use rather than in main()
// so the headless paths (server, master server, tools) never load a Qt
// platform plugin or need a display.

namespace MphRead::Qt
{
    void EnsureApplication();

    // Ends Qt while the process still can; the launcher calls it when done.
    void ShutdownApplication();
}

class QEvent;
class QWindow;

namespace MphRead::Qt
{
    // The game's QWindow (null before the RenderWindow exists).
    [[nodiscard]] QWindow* GameWindow() noexcept;

    // The Qt event the game window is dispatching right now, so the menus can
    // take it whole (text, modifiers, touch points) instead of its GLFW-shaped
    // summary. Null outside that dispatch.
    [[nodiscard]] QEvent* CurrentEvent() noexcept;
}
