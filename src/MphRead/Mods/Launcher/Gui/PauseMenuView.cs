using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Threading;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// What Escape shows during a match: resume, the settings, and the two ways
    /// out.
    ///
    /// A view rather than a window, because there is a platform with no windows
    /// on it to be. <see cref="PauseMenuWindow"/> wraps this on the desktop,
    /// where a small window over a still-running match is the right shape;
    /// Android shows the same object through the launcher's full-screen
    /// overlay, which is what <see cref="HomeView"/> already does with the
    /// settings. One menu either way, so an entry added here turns up on both
    /// rather than on whichever was remembered.
    ///
    /// It decides nothing itself. Every entry raises an event and the host acts
    /// on it: leaving a match is closing a window on one platform and swapping
    /// two views on the other, and neither of those belongs in a menu.
    /// </summary>
    internal sealed class PauseMenuView : UserControl
    {
        public event EventHandler? Resumed;
        public event EventHandler? SettingsRequested;
        public event EventHandler? LeaveRequested;
        public event EventHandler? QuitRequested;
        public event EventHandler? FullscreenRequested;
        public event EventHandler? SpectateRequested;
        public event EventHandler? RejoinRequested;
        public event EventHandler? RecordToggleRequested;

        private readonly MenuEntry _resume;

        /// <param name="offerWindowMode">
        /// Show the fullscreen/windowed entry. False on a phone, which has one
        /// window, it is already the whole screen, and there is no F11.
        /// </param>
        public PauseMenuView(bool offerWindowMode)
        {
            var stack = new StackPanel { Spacing = 4 };
            stack.Children.Add(new Caption("Paused") { Height = 34 });
            _resume = Add(stack, "Resume", offerWindowMode ? "Escape" : "Back",
                () => Resumed?.Invoke(this, EventArgs.Empty));
            if (offerWindowMode)
            {
                var windowEntry = new MenuEntry(WindowLabel(), "F11 or Alt+Enter", titleSize: 17);
                windowEntry.Click += (_, _) =>
                {
                    FullscreenRequested?.Invoke(this, EventArgs.Empty);
                    // The game thread does it on the next frame; reflect it
                    // here straight away so the label is not a lie for 16
                    // milliseconds.
                    windowEntry.Title = WindowMode.IsFullscreen ? "Windowed" : "Fullscreen";
                };
                stack.Children.Add(windowEntry);
            }
            Add(stack, "Settings", "Controls, display, audio",
                () => SettingsRequested?.Invoke(this, EventArgs.Empty));
            if (!DemoPlayback.IsActive)
            {
                if (SpectatorMode.IsSpectating)
                {
                    Add(stack, "Rejoin match", "Score resets to 0",
                        () => RejoinRequested?.Invoke(this, EventArgs.Empty));
                }
                else if (SpectatorMode.CanSpectate)
                {
                    Add(stack, "Spectate", "Watch another player",
                        () => SpectateRequested?.Invoke(this, EventArgs.Empty));
                }
                if (NetSession.Active)
                {
                    Add(stack, DemoRecorder.IsRecording ? "Stop recording" : "Record demo",
                        DemoRecorder.IsRecording ? "Saving to a file" : "Watch it back later, like Quake",
                        () => RecordToggleRequested?.Invoke(this, EventArgs.Empty));
                }
            }
            Add(stack, "Leave match", "Back to the launcher",
                () => LeaveRequested?.Invoke(this, EventArgs.Empty));
            Add(stack, "Quit", $"Close {Branding.Name}",
                () => QuitRequested?.Invoke(this, EventArgs.Empty));

            var panel = new Border
            {
                Background = GuiTheme.PanelBrush,
                Padding = new Thickness(22, 18, 22, 18),
                Child = stack
            };
            if (!offerWindowMode)
            {
                // Full-screen overlay: a column of entries spread across a 20:9
                // phone is a menu you have to hunt across, so it is a panel of
                // a stated width in the middle. The desktop is left stretching,
                // because there the window is already this size and a panel
                // floating inside it would just be a border around a border.
                panel.Width = 420;
                panel.HorizontalAlignment = HorizontalAlignment.Center;
                panel.VerticalAlignment = VerticalAlignment.Center;
            }
            Content = panel;
        }

        /// <summary>
        /// Somebody who just asked for this is looking at a short list and
        /// expects the top entry to be the one already chosen.
        /// </summary>
        public void FocusResume()
        {
            Dispatcher.UIThread.Post(() => _resume.Focus(), DispatcherPriority.Background);
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
    }
}
