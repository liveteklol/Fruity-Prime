using System;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using MphRead.Entities;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// What Escape shows during a match.
    ///
    /// The same shape as the front screen -- a column of words in the
    /// bottom-left corner over a dim line saying where you are -- because it
    /// is the front screen's job during a match, and a pause menu that looks
    /// like a different program is a pause menu that has to be read rather
    /// than glanced at. What differs is the backdrop: the scrim alone, so the
    /// match shows through. A networked match cannot be paused, and covering
    /// it with a photograph would be a lie about what the program is doing.
    ///
    /// The entries are not fewer than they were. Voting on a map, going
    /// fullscreen, spectating and recording are things you can only want
    /// *during* a match, so this is the one screen they can live on -- the
    /// list is shorter everywhere else precisely so it can be long here.
    ///
    /// A view rather than a window, because there is a platform with no
    /// windows to be one. <see cref="PauseMenuWindow"/> wraps this on the
    /// desktop; Android pushes it onto <see cref="StartScreen"/>'s own stack.
    /// One menu either way, so an entry added here turns up on both.
    ///
    /// It decides nothing itself. Every entry raises an event and the host
    /// acts on it: leaving a match is closing a window on one platform and
    /// swapping two views on the other, and neither belongs in a menu.
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
        public event EventHandler? VoteMapRequested;

        private readonly UiWord _resume;

        /// <param name="offerWindowMode">
        /// Show the fullscreen/windowed entry. False on a phone, which has one
        /// window, it is already the whole screen, and there is no F11.
        /// </param>
        public PauseMenuView(bool offerWindowMode)
        {
            Background = Brushes.Transparent;
            Focusable = true;

            StackPanel menu = UiLayout.Column(14);
            // Titles only. Every entry here used to say what it did twice --
            // "Quit", "Close FruityPrime" -- and the second saying is what
            // made a seven-line menu tall enough to be cut off by the window
            // it is drawn over.
            _resume = Add(menu, "Resume", () => Resumed?.Invoke(this, EventArgs.Empty));
            if (!DemoPlayback.IsActive && NetSession.Active)
            {
                // Offered whenever there is a server to ask, rather than only
                // when a vote could pass right now: the reasons it cannot --
                // somebody else's vote is running, the room is still cooling
                // down -- are things the player wants told to them, and an
                // entry that quietly disappears tells them nothing.
                Add(menu, "Vote map", () => VoteMapRequested?.Invoke(this, EventArgs.Empty));
            }
            if (!DemoPlayback.IsActive)
            {
                if (SpectatorMode.IsSpectating)
                {
                    Add(menu, "Rejoin match",
                        () => RejoinRequested?.Invoke(this, EventArgs.Empty));
                }
                else if (SpectatorMode.CanSpectate)
                {
                    Add(menu, "Spectate", () => SpectateRequested?.Invoke(this, EventArgs.Empty));
                }
            }
            if (offerWindowMode)
            {
                var window = new UiWord(WindowLabel());
                window.Click += (_, _) =>
                {
                    FullscreenRequested?.Invoke(this, EventArgs.Empty);
                    // The game thread does it on the next frame; reflect it
                    // here straight away so the label is not a lie for 16
                    // milliseconds.
                    window.Text = WindowMode.IsFullscreen ? "Windowed" : "Fullscreen";
                };
                menu.Children.Add(window);
            }
            if (!DemoPlayback.IsActive && NetSession.Active)
            {
                Add(menu, DemoRecorder.IsRecording ? "Stop recording" : "Record demo",
                    () => RecordToggleRequested?.Invoke(this, EventArgs.Empty));
            }
            Add(menu, "Settings", () => SettingsRequested?.Invoke(this, EventArgs.Empty));
            Add(menu, "Leave match", () => LeaveRequested?.Invoke(this, EventArgs.Empty));
            Add(menu, "Quit", () => QuitRequested?.Invoke(this, EventArgs.Empty));

            Panel root = UiLayout.Backdrop(overGame: true);
            // Shrunk to fit rather than scrolled. The host is the game window
            // and the game window is whatever size the player dragged it to;
            // a scrollbar's answer to that is a menu with its top and bottom
            // cut off, which is what "the menu is always bitten" was. There is
            // nothing here to reflow -- eight words in a column stay eight
            // words in a column, just smaller.
            _scaler = new LayoutTransformControl
            {
                Child = menu,
                HorizontalAlignment = HorizontalAlignment.Left,
                VerticalAlignment = VerticalAlignment.Bottom
            };
            root.Children.Add(_scaler);
            root.Children.Add(UiLayout.Footer(Where()));
            Content = root;
            SizeChanged += (_, e) => FitToHost(e.NewSize.Height);
        }

        private readonly LayoutTransformControl _scaler;

        /// <summary>
        /// What the column needs at full size: eight words, their spacing, and
        /// the corner it is anchored in.
        /// </summary>
        private const double NeededHeight = 8 * 26 + 7 * 14 + UiLayout.ColumnBottom + 20;

        /// <summary>
        /// Fit the column to the height it has been given, down to half size.
        /// Below that there is no menu either way, and a window that short is
        /// not one anybody is playing in.
        /// </summary>
        private void FitToHost(double height)
        {
            if (height <= 0)
            {
                return;
            }
            double scale = Math.Clamp(height / NeededHeight, 0.5, 1);
            if (_scaler.LayoutTransform is ScaleTransform current
                && Math.Abs(current.ScaleY - scale) < 0.001)
            {
                return;
            }
            _scaler.LayoutTransform = scale >= 1 ? null : new ScaleTransform(scale, scale);
        }

        /// <summary>
        /// The line under the column: which match this is. The front screen
        /// puts the build here, because that is what you want to know while
        /// looking at a menu with no match behind it; in one, you want the
        /// room.
        /// </summary>
        private static string Where()
        {
            if (DemoPlayback.IsActive)
            {
                return "watching a recording";
            }
            if (!NetSession.Active)
            {
                return "offline match";
            }
            MatchStatePacket? match = NetSession.ServerMatch;
            string room = match?.RoomKey ?? "";
            string players = $"{NetSession.ServerPlayerCount}/{PlayerEntity.SlotCapacity}";
            int ping = NetSession.SlotPing.Length > 0 ? NetSession.SlotPing[0] : -1;
            string latency = ping >= 0
                ? $"{ping.ToString(CultureInfo.InvariantCulture)}ms"
                : "--";
            return room.Length > 0
                ? $"{room}  \u2502  {players}  \u2502  {latency}"
                : $"online match  \u2502  {players}  \u2502  {latency}";
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

        private static UiWord Add(StackPanel menu, string text, Action action)
        {
            var word = new UiWord(text);
            word.Click += (_, _) => action();
            menu.Children.Add(word);
            return word;
        }
    }
}
