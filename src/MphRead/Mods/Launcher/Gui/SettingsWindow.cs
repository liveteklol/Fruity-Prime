using Avalonia.Controls;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// A frame around <see cref="SettingsView"/>, and nothing else.
    ///
    /// Every setting, every row and every value lives in the view, which is
    /// what the Android head shows as a full-screen overlay instead. This is
    /// the size, the title and the two flags that only mean something over a
    /// match.
    /// </summary>
    internal sealed class SettingsWindow : Window
    {
        private readonly SettingsView _view;

        /// <summary>True when the user pressed save rather than closing.</summary>
        public bool Saved => _view.Saved;

        public SettingsWindow(MenuSettings settings, bool inGame = false)
            : this(new SettingsView(settings, inGame))
        {
        }

        public SettingsWindow(SettingsView view)
        {
            _view = view;
            _view.Closed += (_, _) => Close();

            Title = view.WindowTitle;
            Icon = GuiTheme.AppIcon.Value;
            Width = 980;
            Height = 660;
            MinWidth = 720;
            MinHeight = 460;
            Background = GuiTheme.InkBrush;
            RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            // Opened from the pause menu this has to clear a game window that
            // may be borderless fullscreen, which is a topmost window's job.
            // From the front screen it is an ordinary dialog and behaves like
            // one.
            Topmost = view.InGame;
            ShowInTaskbar = !view.InGame;
            Content = view;
        }
    }
}
