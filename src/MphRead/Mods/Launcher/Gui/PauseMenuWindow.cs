using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Threading;
using MphRead.Mods;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// What Escape shows during a match: resume, the window mode, the settings,
    /// and the two ways out.
    ///
    /// Deliberately small and centred rather than full-screen: the match is
    /// still running behind it, which is the point -- a networked match cannot
    /// be paused, and watching it carry on is more honest than pretending it
    /// stopped.
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
        private readonly MenuEntry _windowEntry;
        private readonly MenuEntry _resume;
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
            Width = 340;
            Height = 392;
            CanResize = false;
            SystemDecorations = SystemDecorations.BorderOnly;
            Background = GuiTheme.PanelBrush;
            RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            // Over the game window rather than the middle of the desktop: the
            // match is still running behind it and that is where the player is
            // looking. The game may also be borderless fullscreen, which is
            // what Topmost is for.
            Topmost = true;
            ShowInTaskbar = false;
            if (PauseMenu.WindowWidth > 0 && PauseMenu.WindowHeight > 0)
            {
                WindowStartupLocation = WindowStartupLocation.Manual;
                Position = new PixelPoint(
                    PauseMenu.WindowX + (PauseMenu.WindowWidth - (int)Width) / 2,
                    PauseMenu.WindowY + (PauseMenu.WindowHeight - (int)Height) / 2);
            }
            else
            {
                WindowStartupLocation = WindowStartupLocation.CenterScreen;
            }

            var stack = new StackPanel { Spacing = 4 };
            stack.Children.Add(new Caption("Paused") { Height = 34 });
            _resume = Add(stack, "Resume", "Escape", Close);
            _windowEntry = new MenuEntry(WindowLabel(), "F11 or Alt+Enter", titleSize: 17);
            _windowEntry.Click += (_, _) =>
            {
                PauseMenu.RequestFullscreenToggle();
                // The game thread does it on the next frame; reflect it here
                // straight away so the label is not a lie for 16 milliseconds.
                _windowEntry.Title = WindowMode.IsFullscreen ? "Windowed" : "Fullscreen";
                Close();
            };
            stack.Children.Add(_windowEntry);
            Add(stack, "Settings", "Controls, display, audio", OpenSettings);
            Add(stack, "Leave match", "Back to the launcher",
                () => { PauseMenu.RequestLeave(); Close(); });
            Add(stack, "Quit", $"Close {Mods.Branding.Name}",
                () => { PauseMenu.RequestQuit(); Close(); });

            Content = new Border
            {
                Background = GuiTheme.PanelBrush,
                Padding = new Thickness(22, 18, 22, 18),
                Child = stack
            };
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
            // Resume takes the keyboard: somebody who just pressed Escape in a
            // match is looking at five entries and expects Enter to mean the
            // top one.
            Dispatcher.UIThread.Post(() => _resume.Focus(), DispatcherPriority.Background);
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
