using System;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// The two or three window-manager favours a borderless front screen
    /// needs: rounded corners, and letting a drag on the artwork move the
    /// window the way a title bar would.
    ///
    /// Every call is best-effort. The attributes used here arrived in
    /// Windows 11 and are simply ignored by older versions, which is why
    /// nothing checks a build number: a square-cornered launcher on Windows
    /// 10 is fine, a crash is not.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public static class WindowChrome
    {
        private const int _dwmwaWindowCornerPreference = 33;
        private const int _dwmwaUseImmersiveDarkMode = 20;
        private const int _cornerPreferenceRound = 2;
        private const int _wmNcLeftButtonDown = 0xA1;
        private const int _htCaption = 0x2;

        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(IntPtr window, int attribute,
            ref int value, int size);

        [DllImport("kernel32.dll")]
        private static extern IntPtr GetConsoleWindow();

        [DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr window, int command);

        [DllImport("user32.dll")]
        private static extern bool ReleaseCapture();

        [DllImport("user32.dll")]
        private static extern IntPtr SendMessage(IntPtr window, int message, IntPtr wParam,
            IntPtr lParam);

        /// <summary>
        /// Hide the console this process was born with.
        ///
        /// MphRead is a console application -- it has to be, since most of
        /// what it does is command line -- so double-clicking it opens a black
        /// window behind the launcher. Nobody outside this project wants to
        /// see it. It is hidden rather than never created, so `-server`,
        /// `-netcheck` and the rest keep their output exactly as before, and
        /// so the launcher can put it back if the game fails to start.
        /// </summary>
        public static void HideConsole()
        {
            IntPtr console = GetConsoleWindow();
            if (console != IntPtr.Zero)
            {
                ShowWindow(console, 0); // SW_HIDE
            }
        }

        public static void ShowConsole()
        {
            IntPtr console = GetConsoleWindow();
            if (console != IntPtr.Zero)
            {
                ShowWindow(console, 5); // SW_SHOW
            }
        }

        public static void RoundCorners(IntPtr window)
        {
            try
            {
                int preference = _cornerPreferenceRound;
                DwmSetWindowAttribute(window, _dwmwaWindowCornerPreference,
                    ref preference, sizeof(int));
            }
            catch (Exception)
            {
                // dwmapi is present everywhere this runs, but a themed
                // desktop is not something to bet the launcher on.
            }
        }

        /// <summary>Dark title bar, for the windows that still have one.</summary>
        public static void DarkTitleBar(IntPtr window)
        {
            try
            {
                int enabled = 1;
                DwmSetWindowAttribute(window, _dwmwaUseImmersiveDarkMode,
                    ref enabled, sizeof(int));
            }
            catch (Exception)
            {
            }
        }

        /// <summary>
        /// Continue this mouse press as a title-bar drag.
        ///
        /// Releasing capture first is what makes it work: the control that
        /// received the click still owns the mouse, and the window manager
        /// will not start a move until it lets go.
        /// </summary>
        public static void DragFrom(Form form)
        {
            ReleaseCapture();
            SendMessage(form.Handle, _wmNcLeftButtonDown, _htCaption, IntPtr.Zero);
        }
    }
}
