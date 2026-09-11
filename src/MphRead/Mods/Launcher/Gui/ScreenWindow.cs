using Avalonia.Controls;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// A frame around a screen that has to be a real window, and nothing else.
    ///
    /// Only the desktop ever needs one, and only over a running match: the
    /// launcher pushes everything onto one stack over its own picture, and
    /// Android has no windows at all. What is left for a window to carry here
    /// is the title, the icon and the rectangle -- and the rectangle is the
    /// game's own, which <see cref="PauseMenuWindow.CoverGameWindow"/> sets.
    ///
    /// Borderless and topmost for the pause menu's reason: the game behind may
    /// be borderless fullscreen, and a screen that disappears behind the window
    /// it belongs to is not a screen.
    /// </summary>
    internal sealed class ScreenWindow : Window
    {
        public ScreenWindow(Control view, string title)
        {
            Title = $"{Mods.Branding.Name} - {title}";
            Icon = GuiTheme.AppIcon.Value;
            CanResize = false;
            SystemDecorations = SystemDecorations.None;
            ShowInTaskbar = false;
            Topmost = true;
            Background = GuiTheme.InkBrush;
            RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            Content = view;
        }
    }
}
