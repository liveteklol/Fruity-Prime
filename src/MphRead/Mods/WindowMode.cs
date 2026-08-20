using System;
using OpenTK.Mathematics;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.Desktop;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace MphRead.Mods
{
    public enum WindowStartMode
    {
        Windowed,
        BorderlessFullscreen
    }

    /// <summary>
    /// Windowed or borderless fullscreen, and the two ways in and out of it
    /// that people expect: F11 and Alt+Enter. Escape belongs to the pause
    /// menu now, which is where the same switch also sits as an entry.
    ///
    /// Borderless rather than exclusive fullscreen: it alt-tabs instantly,
    /// keeps the desktop resolution, and does not black the screen while the
    /// display mode changes -- which matters most for the one thing this
    /// build is for, a match somebody is trying to join while talking to
    /// their friends on something else.
    ///
    /// The window's own state is the truth. Nothing here caches "am I
    /// fullscreen" beyond what it needs to put the window back where it was.
    /// </summary>
    public static class WindowMode
    {
        /// <summary>
        /// How the next window should open. Set by the launcher from its
        /// saved preference, or by -fullscreen on the command line.
        /// </summary>
        public static WindowStartMode Startup { get; set; } = WindowStartMode.Windowed;

        public static bool IsFullscreen { get; private set; }

        private static WindowBorder _savedBorder = WindowBorder.Resizable;
        private static Vector2i _savedLocation;
        private static Vector2i _savedSize;
        private static bool _saved;

        /// <summary>Called once, after the window is first shown.</summary>
        public static void ApplyStartup(NativeWindow window)
        {
            if (Startup == WindowStartMode.BorderlessFullscreen && !IsFullscreen)
            {
                Enter(window);
            }
        }

        /// <summary>
        /// The keys that change the mode: F11 and Alt+Enter. Returns true when
        /// the key was one of them, so the caller can stop.
        ///
        /// Escape is not one of them any more: it opens the pause menu, which
        /// is where leaving fullscreen now lives along with everything else
        /// somebody presses Escape looking for.
        /// </summary>
        public static bool HandleKey(NativeWindow window, KeyboardKeyEventArgs e)
        {
            if (e.Key == Keys.F11 || (e.Key == Keys.Enter && e.Alt))
            {
                Toggle(window);
                return true;
            }
            return false;
        }

        public static void Toggle(NativeWindow window)
        {
            if (IsFullscreen)
            {
                Leave(window);
            }
            else
            {
                Enter(window);
            }
        }

        public static void Enter(NativeWindow window)
        {
            if (IsFullscreen)
            {
                return;
            }
            if (!_saved)
            {
                _savedBorder = window.WindowBorder;
                _savedLocation = window.Location;
                _savedSize = window.ClientSize;
                _saved = true;
            }
            MonitorInfo monitor = Monitors.GetMonitorFromWindow(window);
            // Border first: resizing before the frame is gone leaves the
            // window an inset rectangle with a title bar off the top of the
            // screen on some window managers.
            window.WindowBorder = WindowBorder.Hidden;
            window.WindowState = WindowState.Normal;
            window.Location = monitor.ClientArea.Min;
            window.ClientSize = new Vector2i(monitor.ClientArea.Size.X, monitor.ClientArea.Size.Y);
            IsFullscreen = true;
        }

        public static void Leave(NativeWindow window)
        {
            if (!IsFullscreen)
            {
                return;
            }
            window.WindowBorder = _saved ? _savedBorder : WindowBorder.Resizable;
            window.WindowState = WindowState.Normal;
            if (_saved)
            {
                window.ClientSize = _savedSize;
                window.Location = _savedLocation;
            }
            IsFullscreen = false;
        }

        /// <summary>"borderless"/"fullscreen"/"windowed" from a settings file or a flag.</summary>
        public static WindowStartMode Parse(string? value, WindowStartMode fallback)
        {
            if (value == null)
            {
                return fallback;
            }
            string text = value.Trim().ToLowerInvariant();
            if (text is "borderless" or "fullscreen" or "borderless fullscreen" or "1" or "true")
            {
                return WindowStartMode.BorderlessFullscreen;
            }
            if (text is "windowed" or "window" or "0" or "false")
            {
                return WindowStartMode.Windowed;
            }
            return fallback;
        }
    }
}
