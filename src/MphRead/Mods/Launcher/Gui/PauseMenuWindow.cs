using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Threading;
using MphRead.Mods;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// What Escape shows during a match: resume, the window mode, the settings,
    /// and the two ways out.
    ///
    /// Sized to the game window and laid straight over it, so it reads as the
    /// game's own pause screen rather than as a dialog the game happens to
    /// have opened. It was a 340x392 box in the middle of the desktop before,
    /// which is what a settings prompt looks like and not what pressing
    /// Escape in a game does.
    ///
    /// The match is still running behind it, which is the point -- a networked
    /// match cannot be paused, and watching it carry on is more honest than
    /// pretending it stopped -- so the fill is a scrim rather than a wall. A
    /// compositor that will not give a window an alpha channel renders it
    /// opaque, which loses the view of the match and nothing else.
    ///
    /// Unlike the WinForms menu this replaces, it does not own a thread. The
    /// game's own thread is the one Avalonia was set up on, so the menu is a
    /// window on that thread and <see cref="PauseMenu.Poll"/> gives the toolkit
    /// a slice of every frame. That is what makes it work on every platform:
    /// macOS will not accept windows off the main thread at all, and a second
    /// UI thread is not something Avalonia offers anywhere.
    /// </summary>
    internal sealed class PauseMenuWindow : Window
    {
        private static PauseMenuWindow? _open;
        private readonly PauseMenuView _view;
        private bool _settingsOpen;

        /// <summary>Open the menu, or bring the one already up to the front.</summary>
        public static bool Open()
        {
            if (_open != null)
            {
                _open.Activate();
                return true;
            }
            var window = new PauseMenuWindow();
            _open = window;
            window.Show();
            window.Activate();
            return true;
        }

        /// <summary>Close it from the game's own code path.</summary>
        public static void CloseIfOpen()
        {
            PauseMenuWindow? window = _open;
            if (window == null)
            {
                return;
            }
            try
            {
                window.Close();
            }
            catch (Exception)
            {
                // A menu that will not close must not take the match with it.
            }
        }

        public static bool IsOpen => _open != null;

        private PauseMenuWindow()
        {
            // Not just the product name: the game window carries that, and two
            // windows with one title is what an alt-tab list cannot tell apart.
            Title = $"{Mods.Branding.Name} - paused";
            Icon = GuiTheme.AppIcon.Value;
            CanResize = false;
            SystemDecorations = SystemDecorations.None;
            TransparencyLevelHint = _scrimLevels;
            Background = GuiTheme.ScrimBrush;
            RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            // The game may be borderless fullscreen, and a menu that
            // disappears behind the window it belongs to is not a menu.
            Topmost = true;
            ShowInTaskbar = false;
            CoverGameWindow(this);

            // The entries themselves live in PauseMenuView, which Android
            // shows through an overlay because it has no windows to put this
            // in. What is left here is the window and what the entries mean on
            // a desktop.
            _view = new PauseMenuView(offerWindowMode: true);
            _view.Resumed += (_, _) => Close();
            _view.FullscreenRequested += (_, _) =>
            {
                PauseMenu.RequestFullscreenToggle();
                Close();
            };
            _view.SettingsRequested += (_, _) => OpenSettings();
            _view.SpectateRequested += (_, _) => { SpectatorMode.Start(); Close(); };
            _view.RejoinRequested += (_, _) => { SpectatorMode.Rejoin(); Close(); };
            _view.RecordToggleRequested += (_, _) =>
            {
                if (DemoRecorder.IsRecording)
                {
                    Console.WriteLine($"[demo] recording saved to {DemoRecorder.CurrentPath}");
                    DemoRecorder.Stop();
                }
                else
                {
                    DemoRecorder.Start();
                }
                Close();
            };
            _view.LeaveRequested += (_, _) => { PauseMenu.RequestLeave(); Close(); };
            _view.QuitRequested += (_, _) => { PauseMenu.RequestQuit(); Close(); };
            Content = _view;
        }

        /// <summary>
        /// Transparency worth asking for, best first. Avalonia walks the list
        /// and takes the first the platform will give; None is last, and is
        /// the honest fallback rather than a failure.
        /// </summary>
        private static readonly IReadOnlyList<WindowTransparencyLevel> _scrimLevels =
            new[] { WindowTransparencyLevel.Transparent, WindowTransparencyLevel.None };

        /// <summary>
        /// Put a window exactly over the game's client area, or over the
        /// screen when the game has not said where it is yet.
        ///
        /// Shared with the settings, which is opened from here and has to land
        /// on the same rectangle: two in-game screens of different sizes in
        /// different places is the "popup" shape all over again.
        ///
        /// Called twice, once in the constructor and again from OnOpened.
        /// RenderScaling is 1 until the window has been given a screen, so on
        /// a display running at anything but 100% the first call gets the
        /// conversion wrong and only the second one can be right -- but the
        /// first is still worth making, because it is what stops the window
        /// appearing in the middle of the desktop for a frame before moving.
        /// </summary>
        internal static void CoverGameWindow(Window window)
        {
            if (PauseMenu.WindowWidth <= 0 || PauseMenu.WindowHeight <= 0)
            {
                window.WindowStartupLocation = WindowStartupLocation.CenterScreen;
                return;
            }
            window.WindowStartupLocation = WindowStartupLocation.Manual;
            // Client pixels, which is what GLFW reports and what Avalonia's
            // Position is in. Width and Height are device-independent, so the
            // scaling the display is running at has to come back out of them
            // or the menu overhangs the game by that factor.
            double scale = window.RenderScaling > 0 ? window.RenderScaling : 1;
            window.Position = new PixelPoint(PauseMenu.WindowX, PauseMenu.WindowY);
            window.Width = PauseMenu.WindowWidth / scale;
            window.Height = PauseMenu.WindowHeight / scale;
        }

        private static string WindowLabel()
        {
            return WindowMode.IsFullscreen ? "Windowed" : "Fullscreen";
        }

        private static MenuEntry Add(StackPanel stack, string text, string note, Action action)
        {
            var entry = new MenuEntry(text, note, titleSize: 17);
            entry.Click += (_, _) => action();
            stack.Children.Add(entry);
            return entry;
        }

        /// <summary>
        /// Open the settings over the menu, not under it.
        ///
        /// Both windows are topmost, because the game behind them may be
        /// borderless fullscreen and a menu that disappears behind the window it
        /// belongs to is not a menu. This one steps out of the topmost band
        /// while the dialog is up so that the two are not left arguing about
        /// which of them is in front.
        /// </summary>
        private async void OpenSettings()
        {
            if (_settingsOpen)
            {
                return;
            }
            _settingsOpen = true;
            bool wasTopmost = Topmost;
            Topmost = false;
            try
            {
                MenuSettings settings = GameState.LoadSettings();
                var window = new SettingsWindow(settings, inGame: true);
                await window.ShowDialog(this);
            }
            catch (Exception ex)
            {
                // Anything the settings could not do -- an unreadable settings
                // file, a preview cache it cannot count -- would otherwise reach
                // the toolkit as an unhandled exception, over a match still
                // being played.
                Console.WriteLine($"[pause] the settings could not be opened: {ex.Message}");
            }
            finally
            {
                _settingsOpen = false;
                Topmost = wasTopmost;
                Activate();
            }
        }

        protected override void OnOpened(EventArgs e)
        {
            base.OnOpened(e);
            CoverGameWindow(this);
            _view.FocusResume();
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                Close();
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
        }

        protected override void OnClosed(EventArgs e)
        {
            if (ReferenceEquals(_open, this))
            {
                _open = null;
            }
            // Tell the game the cursor is its own again. Called straight out
            // rather than posted: this already runs on the game's own thread --
            // the toolkit is pumped from inside the render loop -- and what it
            // sets is a flag the next frame acts on.
            PauseMenu.MarkClosed();
            base.OnClosed(e);
        }
    }
}
