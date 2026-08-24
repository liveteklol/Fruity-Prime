using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using Avalonia.Threading;
using MphRead.Entities;
using MphRead.Mods;
using MphRead.Mods.Launcher;
using MphRead.Mods.Launcher.Gui;
using MphRead.Mods.Network;

namespace MphRead.Droid
{
    /// <summary>
    /// The screen on a phone: who you are, what to load, and which servers are
    /// up.
    ///
    /// Built out of the same painted controls as the desktop front screen
    /// (<see cref="MenuEntry"/>, <see cref="Caption"/>, the rows in Rows.cs) so
    /// that the two are one product rather than two that look alike, and over
    /// the same <see cref="LauncherPrefs"/> and the same directory client -- a
    /// server list here is the same query the desktop browser makes, answered
    /// by the same servers.
    ///
    /// The play button is real now: the engine draws through
    /// <see cref="MphRead.Mods.Render.GlEs"/> on OpenGL ES and takes its input
    /// from <see cref="TouchControls"/>. What it needs is the extracted game
    /// files, which are the player's own and have to be copied onto the device,
    /// so the card above it says where.
    /// </summary>
    internal sealed class AndroidHomeView : UserControl
    {
        private readonly StackPanel _servers = new() { Spacing = 4 };
        private readonly Note _serverNote = new("");
        private readonly FieldRow _name;
        private readonly ChoiceRow _hunter;
        private readonly FieldRow _master;
        private MenuEntry _refresh = null!;
        private ChoiceRow _room = null!;
        private ChoiceRow _mode = null!;
        private ChoiceRow _bots = null!;
        private ChoiceRow _botLevel = null!;
        private MenuEntry _play = null!;
        private MenuEntry _recheck = null!;
        private Note _filesNote = null!;

        // The modes a multiplayer match can be started in, in the order the
        // desktop front screen lists them. Not every GameMode value is one.
        private static readonly (string Name, GameMode Mode)[] _modes =
        {
            ("Battle", GameMode.Battle),
            ("Battle teams", GameMode.BattleTeams),
            ("Survival", GameMode.Survival),
            ("Survival teams", GameMode.SurvivalTeams),
            ("Capture", GameMode.Capture),
            ("Bounty", GameMode.Bounty),
            ("Bounty teams", GameMode.BountyTeams),
            ("Defender", GameMode.Defender),
            ("Defender teams", GameMode.DefenderTeams),
            ("Nodes", GameMode.Nodes),
            ("Nodes teams", GameMode.NodesTeams),
            ("Prime hunter", GameMode.PrimeHunter)
        };

        private static readonly string[] _botCounts =
            Enumerable.Range(0, 8).Select(i => i.ToString()).ToArray();
        private static readonly string[] _botLevels = { "Easy", "Normal", "Hard" };

        private static readonly string[] _hunters = Enumerable.Range(0, 7)
            .Select(i => ((Hunter)i).ToString())
            .Append(Hunter.Random.ToString()).ToArray();

        public AndroidHomeView()
        {
            LauncherPrefs.Load();
            Background = GuiTheme.PanelBrush;

            _name = new FieldRow("Your name", LauncherPrefs.PlayerName, boxWidth: 180);
            _hunter = new ChoiceRow("Hunter", _hunters,
                Math.Max(0, Array.IndexOf(_hunters, LauncherPrefs.LastHunter.ToString())));
            _master = new FieldRow("Server directory",
                $"{LauncherPrefs.MasterHost}:{LauncherPrefs.MasterPort}", boxWidth: 200);

            var body = new StackPanel { Spacing = 4, Margin = new Thickness(18, 14, 18, 24) };
            body.Children.Add(Wordmark());
            body.Children.Add(new Note($"{Mods.Branding.NameAndVersion} on Android",
                GuiTheme.TextDim));

            body.Children.Add(new Caption("You") { Height = 30 });
            body.Children.Add(_name);
            body.Children.Add(_hunter);
            var save = new MenuEntry("Save", titleSize: 15) { Primary = true, Height = 42 };
            save.Click += (_, _) => SavePrefs();
            body.Children.Add(save);

            body.Children.Add(new Caption("Servers") { Height = 30 });
            body.Children.Add(_master);
            _refresh = new MenuEntry("Find servers", "Ask the directory who is up",
                titleSize: 15);
            _refresh.Click += async (_, _) => await ReloadServers();
            body.Children.Add(_refresh);
            body.Children.Add(_serverNote);
            body.Children.Add(_servers);

            body.Children.Add(new Caption("Play") { Height = 30 });
            _filesNote = new Note("", GuiTheme.TextDim);
            body.Children.Add(_filesNote);
            // Files arrive on the device while this screen is already up --
            // over USB, from another app -- so there has to be a way to look
            // again that is not "kill the app and start it".
            _recheck = new MenuEntry("Check for game files", "", titleSize: 15);
            _recheck.Click += (_, _) => RefreshGameFiles();
            body.Children.Add(_recheck);
            _room = new ChoiceRow("Map", new[] { "none" });
            _mode = new ChoiceRow("Mode", _modes.Select(m => m.Name).ToArray());
            _bots = new ChoiceRow("Bots", _botCounts, Math.Clamp(LauncherPrefs.Bots, 0, 7));
            _botLevel = new ChoiceRow("Bot skill", _botLevels,
                Math.Clamp(LauncherPrefs.BotLevel, 0, 2));
            body.Children.Add(_room);
            body.Children.Add(_mode);
            body.Children.Add(_bots);
            body.Children.Add(_botLevel);
            _play = new MenuEntry("Play offline", "Load the map with bots", titleSize: 17)
            {
                Primary = true,
                Height = 52
            };
            _play.Click += (_, _) => StartMatch();
            body.Children.Add(_play);
            RefreshGameFiles();

            Content = new ScrollViewer
            {
                Content = body,
                Background = GuiTheme.PanelBrush,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
            };
        }

        private static Control Wordmark()
        {
            try
            {
                using Stream stream = AssetLoader.Open(
                    new Uri("avares://FruityPrime/Assets/fruity-prime-logo.png"));
                return new Image
                {
                    Source = new Bitmap(stream),
                    Height = 90,
                    HorizontalAlignment = HorizontalAlignment.Left,
                    Margin = new Thickness(0, 8, 0, 8)
                };
            }
            catch (Exception)
            {
                // A build without the asset gets the name, not a crash on the
                // first screen.
                return new TextBlock
                {
                    Text = Mods.Branding.Name,
                    FontFamily = GuiTheme.Display,
                    FontSize = 28,
                    Foreground = GuiTheme.TextBrush
                };
            }
        }

        /// <summary>
        /// Whether a match could be loaded right now, and what to do about it
        /// if not. The check is <see cref="GameFiles"/>'s, the same one the
        /// desktop front screen greys its entries out on; what differs is the
        /// answer to "so where do I put them", which on Android is one
        /// directory a player can reach over USB.
        /// </summary>
        private void RefreshGameFiles()
        {
            string? problem = GameFiles.Problem();
            if (problem == null)
            {
                _filesNote.Text = GameFiles.Describe();
                _filesNote.Foreground = GuiTheme.GoodBrush;
                _room.SetItems(ThumbnailGenerator.MultiplayerRooms());
                _play.IsEnabled = true;
                return;
            }
            _filesNote.Text = $"{problem}. Extract the game on a desktop, then copy "
                + $"paths.txt and the extracted folders into {GameFiles.Root} -- the "
                + "app's own directory on this device, which shows up over USB under "
                + "Android/data. Nothing is downloaded and no Nintendo file is shipped.";
            _filesNote.Foreground = GuiTheme.WarmBrush;
            _play.IsEnabled = false;
        }

        private void StartMatch()
        {
            if (MainActivity.Instance == null || GameFiles.Problem() != null)
            {
                return;
            }
            SavePrefs();
            int modeIndex = Math.Clamp(Array.FindIndex(_modes, m => m.Name == _mode.Value), 0,
                _modes.Length - 1);
            int bots = Math.Clamp(Array.IndexOf(_botCounts, _bots.Value), 0, 7);
            int level = Math.Clamp(Array.IndexOf(_botLevels, _botLevel.Value), 0, 2);
            LauncherPrefs.Bots = bots;
            LauncherPrefs.BotLevel = level;
            LauncherPrefs.LastKind = (int)LaunchKind.Offline;
            LauncherPrefs.Save();
            var plan = new LaunchPlan
            {
                Kind = LaunchKind.Offline,
                Hunter = Enum.Parse<Hunter>(_hunter.Value),
                PlayerName = LauncherPrefs.PlayerName,
                RoomKey = _room.Value,
                Mode = _modes[modeIndex].Mode,
                Bots = bots,
                BotLevel = level
            };
            MainActivity.Instance.StartMatch(plan);
        }

        private void SavePrefs()
        {
            if (_name.Value.Trim().Length > 0)
            {
                LauncherPrefs.PlayerName = _name.Value.Trim();
            }
            LauncherPrefs.LastHunter = Enum.Parse<Hunter>(_hunter.Value);
            LauncherPrefs.Save();
        }

        /// <summary>
        /// Ask the directory who is up, then ask each of them directly -- the
        /// same two calls the desktop browser makes, and for the same reason:
        /// an answer proves the server is reachable from *this* device rather
        /// than only from the directory.
        /// </summary>
        private async Task ReloadServers()
        {
            _servers.Children.Clear();
            _refresh.IsEnabled = false;
            string host = LauncherPrefs.MasterHost;
            int port = LauncherPrefs.MasterPort;
            ParseEndpoint(_master.Value, ref host, ref port);
            _serverNote.Text = $"Asking {host}...";
            _serverNote.Foreground = GuiTheme.TextDimBrush;

            MasterListResult result = await Task.Run(() => NetMasterClient.Query(host, port));
            _refresh.IsEnabled = true;
            if (!result.Answered)
            {
                _serverNote.Text = "The directory did not answer. It may be down, or UDP "
                    + "may not reach it from this network.";
                _serverNote.Foreground = GuiTheme.WarmBrush;
                return;
            }
            if (result.Servers.Count == 0)
            {
                _serverNote.Text = "The directory is up and has nobody listed.";
                _serverNote.Foreground = GuiTheme.WarmBrush;
                return;
            }
            _serverNote.Text = $"{result.Servers.Count} listed.";
            _serverNote.Foreground = GuiTheme.TextDimBrush;
            foreach (MasterListing listing in result.Servers)
            {
                AddServerRow(listing);
            }
        }

        private void AddServerRow(MasterListing listing)
        {
            string name = listing.ServerName.Length > 0 ? listing.ServerName : listing.Endpoint;
            var entry = new MenuEntry(name, listing.Endpoint, titleSize: 15);
            _servers.Children.Add(entry);
            Task.Run(() =>
            {
                ServerStatus status = NetStatus.Query(listing.Address, listing.Port,
                    allowJoinProbe: false);
                Dispatcher.UIThread.Post(() =>
                {
                    entry.Subtitle = status.Online
                        ? $"{listing.Endpoint} -- {status.RoomKey} "
                            + $"{NetStatus.ModeName(status.Mode)} {status.Players} players"
                        : $"{listing.Endpoint} -- did not answer";
                    entry.SubtitleColor = status.Online ? GuiTheme.TextDim : GuiTheme.Warm;
                });
            });
        }

        /// <summary>host, or host:port. Leaves both alone on anything else.</summary>
        private static void ParseEndpoint(string text, ref string host, ref int port)
        {
            text = text.Trim();
            if (text.Length == 0)
            {
                return;
            }
            int colon = text.LastIndexOf(':');
            if (colon <= 0)
            {
                host = text;
                return;
            }
            if (Int32.TryParse(text[(colon + 1)..], out int parsed)
                && parsed >= 1 && parsed <= 65535)
            {
                host = text[..colon];
                port = parsed;
            }
        }
    }
}
