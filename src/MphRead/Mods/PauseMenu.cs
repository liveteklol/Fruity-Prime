using System;
using System.Threading;
using OpenTK.Windowing.Desktop;

namespace MphRead.Mods
{
    /// <summary>
    /// The menu Escape opens during a match: let go of the mouse, offer the
    /// way out, and put the settings within reach without leaving the game.
    ///
    /// It runs on its own thread with its own message loop, and talks to the
    /// game through the flags below. That is not gold-plating: GLFW window
    /// calls -- closing it, changing its border -- belong to the thread that
    /// created it, and a WinForms message loop pumped from inside the render
    /// loop would tie the menu's responsiveness to the frame rate. So the menu
    /// asks, and <see cref="Poll"/> does it on the game's own thread.
    /// </summary>
    public static class PauseMenu
    {
        private static volatile bool _open;
        private static volatile bool _leave;
        private static volatile bool _quit;
        private static volatile bool _toggleFullscreen;
        private static volatile bool _refocus;
#if MPHREAD_LAUNCHER
        // The menu's own message loop. Windows-only, like the menu itself.
        private static Thread? _thread;
#endif

        /// <summary>Open right now: the cursor is free and the player is not driving.</summary>
        public static bool Open => _open;

        /// <summary>The player asked to leave the match but not the program.</summary>
        public static bool LeftMatch { get; private set; }

        /// <summary>The player asked to close the program outright.</summary>
        public static bool QuitProgram { get; private set; }

        /// <summary>
        /// Escape, from the game window. True when the menu took it, so the
        /// caller's own Escape handling -- which quits -- must not run.
        /// </summary>
        /// <summary>Where the game window is, so the menu can open over it.</summary>
        public static int WindowX { get; private set; }
        public static int WindowY { get; private set; }
        public static int WindowWidth { get; private set; }
        public static int WindowHeight { get; private set; }

        public static bool HandleEscape(NativeWindow window)
        {
            WindowX = window.ClientLocation.X;
            WindowY = window.ClientLocation.Y;
            WindowWidth = window.ClientSize.X;
            WindowHeight = window.ClientSize.Y;
            if (!OperatingSystem.IsWindows())
            {
                return false;
            }
            if (_open)
            {
                Close();
                return true;
            }
            OpenMenu();
            return true;
        }

        /// <summary>Called once a frame by the game window.</summary>
        public static void Poll(GameWindow window)
        {
            if (_refocus)
            {
                _refocus = false;
                // Give the keyboard back to the game.
                //
                // The menu is a WinForms window on its own thread. Closing it
                // does not reliably make the game window the foreground one
                // again -- Windows restricts which thread may hand focus
                // around -- and an unfocused GLFW window receives no keys and
                // cannot grab the pointer. The game goes on simulating, so
                // nothing looks crashed: the mouse still moves, the picture
                // still draws, and nothing the player presses arrives. That
                // reads as a freeze.
                try
                {
                    window.Focus();
                }
                catch (Exception)
                {
                    // Not worth losing the match over.
                }
            }
            if (_toggleFullscreen)
            {
                _toggleFullscreen = false;
                WindowMode.Toggle(window);
            }
            if (_quit)
            {
                _quit = false;
                QuitProgram = true;
                Close();
                window.Close();
            }
            else if (_leave)
            {
                _leave = false;
                LeftMatch = true;
                Close();
                window.Close();
            }
        }

        /// <summary>Forget what the last match asked for.</summary>
        public static void Reset()
        {
            LeftMatch = false;
            QuitProgram = false;
            _leave = false;
            _quit = false;
        }

        internal static void RequestLeave() => _leave = true;

        internal static void RequestQuit() => _quit = true;

        internal static void RequestFullscreenToggle() => _toggleFullscreen = true;

        internal static void MarkClosed()
        {
            _open = false;
            // Asked for here, done in Poll: the window belongs to the game's
            // thread, and this runs on the menu's.
            _refocus = true;
        }

        private static void OpenMenu()
        {

#if MPHREAD_LAUNCHER
            if (_thread != null && _thread.IsAlive)
            {
                return;
            }
            _open = true;
            _thread = new Thread(Launcher.PauseMenuForm.RunLoop)
            {
                IsBackground = true,
                Name = "MphRead pause menu"
            };
            // Single-threaded apartment for the same reason the launcher's is:
            // file dialogs and image handling go through OLE.
            _thread.SetApartmentState(ApartmentState.STA);
            _thread.Start();
#endif
        }

        private static void Close()
        {
#if MPHREAD_LAUNCHER
            Launcher.PauseMenuForm.CloseIfOpen();
#endif
            _open = false;
        }
    }
}
