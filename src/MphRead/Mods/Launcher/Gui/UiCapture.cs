#if MPHREAD_AVALONIA
using System;
using System.Collections.Generic;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// Screenshots of the front screen, without a screen.
    ///
    /// The launcher is the one part of this program that could not be looked
    /// at from here: the game renders through GL and can be read back
    /// (ScreenCapture), but the launcher is Avalonia, and checking a change to
    /// it meant opening a window on a machine with a display and looking. On a
    /// headless box, or over SSH, or in CI, there was no way to see what a
    /// layout change had actually done -- which is how a control that moves
    /// under the pointer ships.
    ///
    /// Avalonia can measure, arrange and draw a control into a bitmap with no
    /// window involved, which is all a screenshot of a layout needs. So
    /// `-uishot DIR` builds each screen at a fixed size, renders it, and
    /// writes a PNG.
    ///
    /// What this does *not* prove: that a real window manager gives the window
    /// the size asked for, that the fonts on another machine are these ones,
    /// or that anything is clickable. It proves the layout -- which is what
    /// every report about this screen has been about.
    /// </summary>
    internal static class UiCapture
    {
        /// <summary>
        /// The size the screens are photographed at. Close to what the game
        /// window gives them at its own startup size, which is what they are
        /// laid out against (see UiSurface.Scale).
        /// </summary>
        private static readonly Size _windowSize = new Size(940, 560);

        public static int RunLobby(string directory)
        {
            if (!GuiLauncher.EnsureSetup()) return 1;
            Directory.CreateDirectory(directory);
            int written = 0;
            Dispatcher.UIThread.Invoke(() =>
            {
                List<string> rooms = RoomList();
                var state = new SessionStatePacket { Policy = ServerSessionPolicy.Lobby, Phase = SessionPhase.Lobby,
                    MaxPlayers = 8, OwnerSlot = 0, Revision = 7, RuleFlags = SessionRules.RequireReady | SessionRules.AllowJoinInProgress,
                    Match = new MatchDefinition { RoomKey = rooms[0], Mode = GameMode.BattleTeams, Format = MatchFormat.FourVsFour,
                        TimeLimitSeconds = 600, PointGoal = 20, ShadowFreeze = true } };
                NetSession.ApplySessionState(state);
                var roster = RosterPacket.Create(); roster.Count = 8; roster.Revision = 7;
                for (int i = 0; i < 8; i++)
                {
                    roster.Slots[i] = (byte)i; roster.Names[i] = i == 0 ? "Jarrett" : $"Player {i + 1}";
                    roster.Hunters[i] = (byte)(i % 7); roster.Colors[i] = (byte)(i % 4);
                    roster.Teams[i] = (sbyte)(i % 2); roster.LobbyReady[i] = i < 5; roster.Pings[i] = (ushort)(23 + 11 * i);
                }
                NetSession.ApplyRoster(roster);
                Chat.NetChat.Remember(new ChatPacket { Name = "Player 2", Text = "Ready for the next round.", Kind = ChatPacket.KindSay });
                foreach (var (name, size) in new[] { ("lobby-desktop", new Size(1280, 720)),
                    ("lobby-phone", new Size(960, 540)), ("lobby-short", new Size(800, 400)) })
                {
                    var lobby = new LobbyScreen(rooms); lobby.Suspend();
                    if (Capture(lobby, Path.Combine(directory, name + ".png"), size)) written++;
                }
                NetSession.Stop();
            });
            Console.WriteLine($"[lobbyshot] wrote {written} layouts to {directory}");
            return written == 3 ? 0 : 1;
        }

        public static int RunReplay(string directory, string replay)
        {
            if (!GuiLauncher.EnsureSetup()) return 1;
            Directory.CreateDirectory(directory);
            if (!DemoPlayback.Join(replay)) { Console.WriteLine(DemoPlayback.LastError); return 1; }
            int written = 0;
            try
            {
                Dispatcher.UIThread.Invoke(() =>
                {
                    foreach (var (name, size) in new[] { ("desktop", new Size(1280, 720)), ("phone", new Size(960, 540)), ("short", new Size(800, 400)) })
                    {
                        if (Capture(new PauseMenuView(offerWindowMode: true), Path.Combine(directory, "replay-controls-" + name + ".png"), size)) written++;
                        if (Capture(new PlayScreen(new MenuSettings(), RoomList(), PlayScreen.Face.Clips), Path.Combine(directory, "replay-library-" + name + ".png"), size)) written++;
                    }
                });
            }
            finally { DemoPlayback.Stop(); NetSession.Stop(); }
            Console.WriteLine($"[replayshot] {written} layouts written to {directory}");
            return written == 6 ? 0 : 1;
        }

        public static int Run(string directory)
        {
            if (!GuiLauncher.EnsureSetup())
            {
                Console.WriteLine("[uishot] no Avalonia backend on this machine; nothing captured");
                return 1;
            }
            Directory.CreateDirectory(directory);
            // The front screen's Share button only exists where something can
            // receive a file, which today is Android alone -- so without a
            // stand-in the one corner this tool was made to check could never
            // be photographed as a phone draws it. Same reason as SampleDemos
            // below, and it is still only offered when real logs exist.
            Mods.LogShare.Current ??= new CaptureLogShare();
            int written = 0;
            // On the toolkit's own thread, and drained afterwards: the views
            // post work to the dispatcher as they are built (the front screen
            // focuses its first control that way), and a render before that
            // has run is a picture of a half-built screen.
            Dispatcher.UIThread.Invoke(() =>
            {
                var settings = new MenuSettings();
                List<string> rooms = RoomList();
                foreach ((string name, Control view, Size size) in Screens(settings, rooms))
                {
                    string path = Path.Combine(directory, $"{name}.png");
                    if (Capture(view, path, size))
                    {
                        written++;
                        Console.WriteLine($"[uishot] {path}");
                    }
                }
            });
            Console.WriteLine($"[uishot] {written} screen(s) written to {directory}");
            return written > 0 ? 0 : 1;
        }

        private static List<string> RoomList()
        {
            var rooms = new List<string>();
            try
            {
                foreach (RoomMetadata meta in Metadata.RoomMetadata.Values)
                {
                    if (meta.Multiplayer)
                    {
                        rooms.Add(meta.Name);
                    }
                }
            }
            catch (Exception)
            {
                // No game files here. The screens still lay out; the map rows
                // are simply empty, which is itself worth being able to see.
            }
            rooms.Sort(StringComparer.OrdinalIgnoreCase);
            return rooms;
        }

        private static IEnumerable<(string, Control, Size)> Screens(MenuSettings settings,
            IReadOnlyList<string> rooms)
        {
            yield return ("start", new StartScreen(settings, rooms), _windowSize);
            // Every face of the one screen that replaced seven. They share a
            // layout and nothing else -- the list, the settings beside it and
            // the word on the tick are different on each -- so one picture of
            // it would prove nothing about the other three.
            yield return ("play-online",
                new PlayScreen(settings, rooms, PlayScreen.Face.Online), _windowSize);
            yield return ("play-offline",
                new PlayScreen(settings, rooms, PlayScreen.Face.Offline), _windowSize);
            yield return ("play-story",
                new PlayScreen(settings, rooms, PlayScreen.Face.Story), _windowSize);
            yield return ("play-clips",
                new PlayScreen(settings, rooms, PlayScreen.Face.Clips), _windowSize);
            yield return ("play-vote",
                new PlayScreen(settings, rooms, PlayScreen.Face.Vote, overGame: true),
                _windowSize);
            // Both faces of creating a server, and the map list it opens.
            // The dedicated one is a separate picture because the rows it
            // hides and the warning it raises are the whole difference between
            // the two, and neither shows on the other.
            yield return ("create-server", new CreateServerScreen(rooms), _windowSize);
            var dedicated = new CreateServerScreen(rooms);
            dedicated.ShowDedicated();
            yield return ("create-server-dedicated", dedicated, _windowSize);
            yield return ("create-server-maps",
                new MapRotationPicker(rooms, Array.Empty<string>()), _windowSize);
            yield return ("create-server-hosts", new HostPicker(Fleet(), asking: false),
                _windowSize);
            yield return ("settings", new SettingsView(settings), _windowSize);
            var credits = new SettingsView(settings);
            credits.ShowSection("Profile");
            yield return ("settings-player", credits, _windowSize);
            yield return ("setup", new SetupScreen(), _windowSize);
            yield return ("confirm",
                new ConfirmScreen($"Quit {Mods.Branding.Name}?"), _windowSize);
            yield return ("pausemenu", new PauseMenuView(offerWindowMode: true), _windowSize);
            // Deliberately shorter than the menu's own content, and shorter
            // than the game window is now allowed to be. The pause menu is
            // laid over the game window, so its host is whatever size the
            // player dragged that to, and entries drawn off the bottom edge
            // are a player who cannot leave the match. This is the check that
            // the column shrinks to carry them.
            yield return ("pausemenu-small", new PauseMenuView(offerWindowMode: true),
                new Size(560, 320));
            yield return ("serverbrowser", ServerList(), _windowSize);
        }

        /// <summary>
        /// The fleet as the host picker draws it, without asking the network:
        /// one that will run a match, one too old to say so, and one with no
        /// directory at all. The three states are the whole point of the
        /// screen, and a capture that queried the real directory would
        /// photograph whichever of them happened to be true that morning.
        /// </summary>
        private static List<HostCandidate> Fleet()
        {
            return new List<HostCandidate>
            {
                new() { Label = "net.livetek.fr", Host = "net.livetek.fr", Port = 27889,
                    Answered = true, CanHost = true, Latency = 3 },
                new() { Label = "Fruity Prime - West Europe", Host = "20.16.135.109",
                    Port = 27889, Answered = true, CanHost = null, Latency = 39 },
                new() { Label = "Fruity Prime - Japan", Host = "13.78.14.98", Port = 27889,
                    Answered = false, CanHost = null, Latency = -1 }
            };
        }

        /// <summary>
        /// Somewhere for the Share button to point while it is being
        /// photographed. Nothing is built and nothing is sent: a capture has
        /// nobody to press it.
        /// </summary>
        private sealed class CaptureLogShare : Mods.ILogShare
        {
            public string StagingPath(string fileName) =>
                Path.Combine(Path.GetTempPath(), fileName);

            public bool Share(string path, string subject, out string error)
            {
                error = "there is nothing to share to on this platform";
                return false;
            }
        }

        /// <summary>
        /// The browser's table, at the width the panel gives it, with rows
        /// standing in for servers that are not up.
        ///
        /// Built here rather than reached through the play screen because that
        /// one only fills in when a directory answers -- and the fault this is
        /// for (a map name wrapping onto the row below, headings running into
        /// each other) is a property of the columns and the width, not of any
        /// real server. Both widths are drawn, so a narrow row is checked too.
        /// </summary>
        private static Control ServerList()
        {
            var stack = new StackPanel { Spacing = 18, Margin = new Thickness(12) };
            foreach (double width in new[] { 600.0, 400.0 })
            {
                var list = new StackPanel { Spacing = 2, Width = width };
                list.Children.Add(new ServerHeader());
                foreach ((string name, string room, GameMode mode, int players, int ping) in _sampleServers)
                {
                    var row = new ServerRow(name, "203.0.113.7:27888");
                    row.SetStatus(new ServerStatus
                    {
                        Online = true,
                        RoomKey = room,
                        Mode = mode,
                        Players = players,
                        MaxPlayers = 8,
                        Latency = ping
                    });
                    list.Children.Add(row);
                }
                stack.Children.Add(list);
            }
            return stack;
        }

        private static readonly (string, string, GameMode, int, int)[] _sampleServers =
        {
            ("net.livetek.fr", "MP3 PROVING GROUND", GameMode.Battle, 3, 41),
            ("A very long server name indeed", "MP7 PROCESSOR CORE", GameMode.PrimeHunter, 8, 152),
            ("lan", "MP2 HARVESTER", GameMode.Bounty, 1, 2)
        };

        /// <summary>
        /// Render one screen.
        ///
        /// Through a real <see cref="Window"/>, not by laying the control out
        /// on its own. Avalonia resolves styles through the visual tree's
        /// style host, and a control with no window above it has none: it
        /// measures, arranges and renders perfectly happily and comes out a
        /// flat rectangle of the background colour, which is exactly what the
        /// first attempt at this produced. The window is what connects the
        /// tree to the Application's styles.
        ///
        /// It is shown, because a window that has never been shown has no
        /// layout pass behind it -- but shown *off the side of the display*
        /// and without taking focus, so a capture run does not steal the
        /// pointer or flash a window per screen.
        /// </summary>
        internal static bool Capture(Control view, string path, Size size)
        {
            Window? window = null;
            try
            {
                window = new Window
                {
                    Width = size.Width,
                    Height = size.Height,
                    Background = GuiTheme.PanelBrush,
                    RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark,
                    SystemDecorations = SystemDecorations.None,
                    ShowInTaskbar = false,
                    ShowActivated = false,
                    WindowStartupLocation = WindowStartupLocation.Manual,
                    Position = new PixelPoint(-4000, -4000),
                    Content = view
                };
                window.Show();
                // The views post work to the dispatcher as they are built --
                // the front screen focuses its first control that way, and the
                // map picker loads its pictures -- and a render before that has
                // run is a picture of a half-built screen. Several passes,
                // because one job can queue another.
                for (int i = 0; i < 8; i++)
                {
                    Dispatcher.UIThread.RunJobs();
                }
                window.Measure(size);
                window.Arrange(new Rect(size));
                Dispatcher.UIThread.RunJobs();
                var bitmap = new RenderTargetBitmap(
                    new PixelSize((int)size.Width, (int)size.Height),
                    new Vector(96, 96));
                bitmap.Render(window);
                bitmap.Save(path);
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[shot] {Path.GetFileName(path)} could not be rendered: {ex.Message}");
                return false;
            }
            finally
            {
                window?.Close();
            }
        }

    }
}
#endif
