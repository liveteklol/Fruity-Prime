using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using MphRead.Entities;
using MphRead.Mods;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// Everything that answers "what are we playing", on one screen.
    ///
    /// It replaces seven: a host card, a join card, a server browser, an
    /// adventure card, a demo list, a map grid and a vote picker. All seven
    /// were a list of things to pick from and a handful of settings about the
    /// pick, and each had invented its own layout for that -- which is why
    /// joining a server took five presses and starting an offline match took
    /// six.
    ///
    /// So there is one list, one column of settings beside it, and a strip
    /// over the top saying which of the four sources the list is showing.
    /// Changing the strip rebuilds both, because the settings that mean
    /// anything are different for each: bots and a match type belong to a
    /// local game and nothing else, and a greyed-out row that can never be
    /// used is a row that should not be drawn.
    ///
    /// The fifth face, <see cref="Face.Vote"/>, is the same screen opened from
    /// the pause menu with the strip taken away: calling a vote is picking a
    /// map, and picking a map is what this screen does.
    /// </summary>
    internal sealed class PlayScreen : UserControl
    {
        public enum Face
        {
            Online,
            Offline,
            Story,
            Demo,
            Vote
        }

        /// <summary>Raised when the player backed out without choosing anything.</summary>
        public event EventHandler? Closed;

        /// <summary>Raised with a match to start. The screen is finished with by then.</summary>
        public event EventHandler<LaunchPlan>? Launched;

        /// <summary>Vote mode only: the map to put to the room.</summary>
        public event EventHandler<string>? Voted;

        private static readonly (string Label, GameMode Mode)[] _modes =
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

        private static readonly string[] _hunters =
            Enumerable.Range(0, Hunters.Playable).Select(i => ((Hunter)i).ToString())
                .Append(Hunter.Random.ToString()).ToArray();

        private readonly MenuSettings _settings;
        private readonly List<string> _rooms;
        private readonly bool _overGame;
        private readonly Face _only;

        private readonly UiTabs? _tabs;
        private readonly UiList _list = new();
        private readonly StackPanel _options = new() { Spacing = 2, Width = 300 };
        private readonly Image _preview = new() { Stretch = Stretch.UniformToFill };
        private readonly Border _previewBox;
        private readonly Note _note = new("");
        private readonly UiMark _go;
        private readonly UiMark _back;

        private DispatcherTimer? _statusTimer;
        private CancellationTokenSource? _statusCancel;
        private bool _finished;

        private ChoiceRow? _hunter;
        private ChoiceRow? _mode;
        private ChoiceRow? _bots;
        private ChoiceRow? _skill;
        private ChoiceRow? _where;
        private ChoiceRow? _resume;
        private FieldRow? _name;
        private FieldRow? _address;

        public PlayScreen(MenuSettings settings, IReadOnlyList<string> rooms,
            Face face = Face.Online, bool overGame = false)
        {
            _settings = settings;
            _rooms = new List<string>(rooms);
            _overGame = overGame;
            _only = face;
            Background = Brushes.Transparent;
            Focusable = true;

            _previewBox = new Border
            {
                Height = 172,
                CornerRadius = new CornerRadius(3),
                ClipToBounds = true,
                Margin = new Thickness(0, 0, 0, 10),
                IsVisible = false,
                Child = _preview
            };

            var body = new Grid
            {
                ColumnDefinitions = new ColumnDefinitions("*,Auto"),
                RowDefinitions = new RowDefinitions("*,Auto"),
                Margin = UiLayout.BodyMargin
            };
            Grid.SetColumn(_list, 0);
            Grid.SetRow(_list, 0);
            var side = new StackPanel { Spacing = 2, Margin = new Thickness(30, 0, 0, 0) };
            side.Children.Add(_previewBox);
            side.Children.Add(_options);
            Grid.SetColumn(side, 1);
            Grid.SetRow(side, 0);
            Grid.SetColumn(_note, 0);
            Grid.SetColumnSpan(_note, 2);
            Grid.SetRow(_note, 1);
            body.Children.Add(_list);
            body.Children.Add(side);
            body.Children.Add(_note);

            _back = new UiMark(UiMark.Shape.Cancel, "back")
            {
                HorizontalAlignment = HorizontalAlignment.Left,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(UiLayout.CornerX, 0, 0, UiLayout.CornerY)
            };
            _back.Click += (_, _) => Leave();
            _go = new UiMark(UiMark.Shape.Accept, "play")
            {
                HorizontalAlignment = HorizontalAlignment.Right,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(0, 0, UiLayout.CornerX, UiLayout.CornerY)
            };
            _go.Click += (_, _) => Go();

            Panel root = UiLayout.Backdrop(overGame);
            root.Children.Add(body);
            root.Children.Add(UiLayout.Heading(face == Face.Vote ? "vote" : "play"));
            if (face != Face.Vote)
            {
                _tabs = new UiTabs(new[] { "Online", "Offline", "Story", "Demo" },
                    (int)face)
                {
                    HorizontalAlignment = HorizontalAlignment.Left,
                    VerticalAlignment = VerticalAlignment.Top,
                    Margin = UiLayout.TabMargin
                };
                _tabs.Changed += (_, _) => Rebuild();
                root.Children.Add(_tabs);
            }
            root.Children.Add(_back);
            root.Children.Add(_go);
            Content = root;

            _list.Activated += (_, _) => Go();
            // Subscribed once, not per face: the list outlives every rebuild,
            // and a handler added each time round is a handler added twenty
            // times by the time somebody has looked at each face five times.
            _list.SelectionChanged += (_, _) =>
            {
                RefreshPreview();
                RefreshStory();
            };
            Rebuild();
        }

        private Face Current => _tabs == null ? _only : (Face)_tabs.Index;

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            _list.FocusFirst();
        }

        protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
        {
            StopPolling();
            base.OnDetachedFromVisualTree(e);
        }

        /// <summary>
        /// The keys the screen owns rather than any one control on it.
        ///
        /// Reached only when nothing under them wanted the key: a row does not
        /// handle up and down, so those arrive here and walk the list; a
        /// setting beside the list *does* handle left and right, so those
        /// never reach here while the keyboard is on one. Which is the
        /// behaviour wanted in both cases and not a special case anywhere.
        /// </summary>
        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                Leave();
                e.Handled = true;
                return;
            }
            if (_list.HandleKey(e.Key))
            {
                e.Handled = true;
                return;
            }
            if (_tabs != null && _tabs.HandleKey(e.Key))
            {
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
        }

        private void Leave()
        {
            if (_finished)
            {
                return;
            }
            _finished = true;
            StopPolling();
            Closed?.Invoke(this, EventArgs.Empty);
        }

        private void Finish(LaunchPlan plan)
        {
            if (_finished)
            {
                return;
            }
            _finished = true;
            StopPolling();
            Launched?.Invoke(this, plan);
        }

        // ------------------------------------------------------------- shaping

        /// <summary>
        /// Throw the list and the settings away and build the face that is up.
        ///
        /// Rebuilt rather than hidden and shown: the four faces share no row
        /// at all -- even "Hunter" means a different preference on each -- and
        /// a screen holding four sets of controls, three of them invisible, is
        /// how the card it replaces grew to two thousand lines.
        /// </summary>
        private void Rebuild()
        {
            StopPolling();
            _list.Clear();
            _list.SetHeader(null);
            _options.Children.Clear();
            _note.Text = "";
            _note.Foreground = GuiTheme.TextDimBrush;
            _hunter = _mode = _bots = _skill = _where = _resume = null;
            _name = _address = null;
            _previewBox.IsVisible = false;

            switch (Current)
            {
            case Face.Online:
                BuildOnline();
                break;
            case Face.Offline:
                BuildOffline();
                break;
            case Face.Story:
                BuildStory();
                break;
            case Face.Demo:
                BuildDemo();
                break;
            case Face.Vote:
                BuildVote();
                break;
            }
            RefreshPreview();
            _list.FocusFirst();
        }

        private ChoiceRow AddHunter()
        {
            var row = new ChoiceRow("Hunter", _hunters,
                Math.Max(0, Array.IndexOf(_hunters, LauncherPrefs.LastHunter.ToString())));
            _options.Children.Add(row);
            return row;
        }

        private static string PlayerName()
        {
            string name = LauncherPrefs.PlayerName.Trim();
            return name.Length > 0 ? name : "Player";
        }

        // -------------------------------------------------------------- online

        private void BuildOnline()
        {
            _go.Label = "join";
            _list.SetHeader(new ServerHeader());
            _name = new FieldRow("Name", PlayerName(), boxWidth: 150);
            _hunter = AddHunter();
            _options.Children.Insert(0, _name);
            _address = new FieldRow("Address",
                $"{LauncherPrefs.ServerAddress}:{LauncherPrefs.ServerPort}", boxWidth: 170);
            _address.Box.LostFocus += (_, _) => QueryStatusSoon();
            _options.Children.Add(_address);
            var refresh = new UiWord("Refresh", 15, colour: GuiTheme.TextDim)
            {
                Margin = new Thickness(4, 10, 0, 0)
            };
            refresh.Click += (_, _) => ReloadServers();
            _options.Children.Add(refresh);
            ReloadServers();
            StartPolling();
        }

        /// <summary>
        /// Ask the directory who is up, then ask each of them directly.
        ///
        /// Directly, not through the directory: the round trip that matters is
        /// this machine's, and an answer also proves the server is reachable
        /// from here rather than only from there.
        /// </summary>
        private void ReloadServers()
        {
            _list.Clear();
            _note.Text = $"Asking {LauncherPrefs.MasterHost}...";
            _note.Foreground = GuiTheme.TextDimBrush;
            Task.Run(() =>
            {
                MasterListResult result = NetMasterClient.Query(LauncherPrefs.MasterHost,
                    LauncherPrefs.MasterPort);
                Dispatcher.UIThread.Post(() =>
                {
                    if (_finished || Current != Face.Online)
                    {
                        return;
                    }
                    if (!result.Answered)
                    {
                        _note.Text = "The directory did not answer. It may be down, or UDP "
                            + "may not reach it. The address beside the list still works.";
                        _note.Foreground = GuiTheme.WarmBrush;
                        return;
                    }
                    if (result.Servers.Count == 0)
                    {
                        _note.Text = "The directory is up and has nobody listed.";
                        _note.Foreground = GuiTheme.WarmBrush;
                        return;
                    }
                    _note.Text = $"{result.Servers.Count} listed.";
                    foreach (MasterListing listing in result.Servers)
                    {
                        AddServerRow(listing);
                    }
                    _list.FocusFirst();
                });
            });
        }

        private void AddServerRow(MasterListing listing)
        {
            string name = listing.ServerName.Length > 0 ? listing.ServerName : listing.Endpoint;
            var row = new ServerRow(name, listing.Endpoint);
            ToolTip.SetTip(row, listing.Endpoint);
            // Pressing a server fills the address in and joins it in one go:
            // the row is the choice, and a second press somewhere else to
            // confirm it is the press this screen exists to remove.
            _list.Add(row, _ =>
            {
                if (_address != null)
                {
                    _address.Value = $"{listing.Address}:{listing.Port}";
                }
            });
            Task.Run(() =>
            {
                ServerStatus status = NetStatus.Query(listing.Address, listing.Port,
                    allowJoinProbe: false);
                Dispatcher.UIThread.Post(() => row.SetStatus(status));
            });
        }

        private (string Host, int Port) Endpoint()
        {
            string host = LauncherPrefs.ServerAddress;
            int port = LauncherPrefs.ServerPort;
            if (_address != null)
            {
                ParseEndpoint(_address.Value, ref host, ref port);
            }
            return (host, port);
        }

        /// <summary>
        /// Poll what the typed-in server is running while somebody is reading
        /// the screen. StatusQuery answers without claiming a slot, which is
        /// what makes polling it reasonable.
        /// </summary>
        private void StartPolling()
        {
            QueryStatusSoon();
            _statusTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(4) };
            _statusTimer.Tick += (_, _) => QueryStatusSoon();
            _statusTimer.Start();
        }

        private void StopPolling()
        {
            _statusTimer?.Stop();
            _statusTimer = null;
            _statusCancel?.Cancel();
            _statusCancel = null;
        }

        private void QueryStatusSoon()
        {
            if (Current != Face.Online)
            {
                return;
            }
            _statusCancel?.Cancel();
            var cancel = new CancellationTokenSource();
            _statusCancel = cancel;
            (string host, int port) = Endpoint();
            Task.Run(() =>
            {
                ServerStatus status = NetStatus.Query(host, port, allowJoinProbe: true);
                if (cancel.IsCancellationRequested)
                {
                    return;
                }
                Dispatcher.UIThread.Post(() =>
                {
                    if (cancel.IsCancellationRequested || _finished
                        || Current != Face.Online)
                    {
                        return;
                    }
                    _note.Text = status.Online
                        ? $"{host}:{port} -- {Describe(status)}"
                        : $"{host}:{port} -- no answer. It may be off, or UDP may be blocked.";
                    _note.Foreground = status.Online ? GuiTheme.GoodBrush : GuiTheme.WarmBrush;
                });
            });
        }

        private static string Describe(ServerStatus status)
        {
            string players = status.MaxPlayers > 0
                ? $"{status.Players}/{status.MaxPlayers}"
                : status.Players.ToString(CultureInfo.InvariantCulture);
            string ping = status.Latency >= 0
                ? $"{status.Latency.ToString(CultureInfo.InvariantCulture)} ms"
                : "-- ms";
            return $"{status.RoomKey} ({NetStatus.ModeName(status.Mode)}) {players} players, {ping}";
        }

        private async Task Join()
        {
            (string host, int port) = Endpoint();
            string name = _name != null && _name.Value.Trim().Length > 0
                ? _name.Value.Trim() : PlayerName();
            var hunter = (Hunter)Enum.Parse(typeof(Hunter), _hunter!.Value);
            StopPolling();
            _go.IsEnabled = false;
            _go.Label = "joining";
            _note.Text = $"Connecting to {host}:{port}...";
            _note.Foreground = GuiTheme.TextDimBrush;

            LauncherPrefs.PlayerName = name;
            LauncherPrefs.LastHunter = hunter;
            LauncherPrefs.ServerAddress = host;
            LauncherPrefs.ServerPort = port;
            LauncherPrefs.LastKind = (int)LaunchKind.Online;
            LauncherPrefs.Save();

            // Joining blocks for up to eight seconds while it retries; on the
            // UI thread that is eight seconds of a screen that does not redraw.
            bool joined = await Task.Run(() => NetLaunch.Join(host, port, name, hunter));
            _go.IsEnabled = true;
            _go.Label = "join";
            if (!joined)
            {
                NetSession.Stop();
                _note.Text = NetLaunch.LastJoinError;
                _note.Foreground = GuiTheme.BadBrush;
                StartPolling();
                return;
            }
            Finish(new LaunchPlan
            {
                Kind = LaunchKind.Online,
                Hunter = hunter,
                PlayerName = name,
                RoomKey = "",
                Mode = GameMode.Battle,
                Port = port
            });
        }

        // ------------------------------------------------------------- offline

        private void BuildOffline()
        {
            _go.Label = "start";
            FillRooms(_settings.RoomKey);
            // Local or over the directory. One row, because it is the same
            // match either way -- and the only difference it makes to this
            // screen is that a server fills its own slots, so bots are not a
            // question there.
            _where = new ChoiceRow("Where", new[] { "Local", "Online" }, 0);
            _where.Changed += (_, _) => RefreshOffline();
            _options.Children.Add(_where);
            _mode = new ChoiceRow("Match type", _modes.Select(m => m.Label).ToArray());
            _options.Children.Add(_mode);
            _hunter = AddHunter();
            _bots = new ChoiceRow("Bots",
                Enumerable.Range(0, PlayerEntity.SlotCapacity)
                    .Select(i => i.ToString(CultureInfo.InvariantCulture)).ToArray(),
                LauncherPrefs.Bots);
            _options.Children.Add(_bots);
            _skill = new ChoiceRow("Bot skill", new[] { "Easy", "Normal", "Hard" },
                LauncherPrefs.BotLevel);
            _options.Children.Add(_skill);
            _previewBox.IsVisible = true;
            RefreshOffline();
        }

        private void RefreshOffline()
        {
            bool host = _where!.Index == 1;
            _bots!.IsVisible = !host;
            _skill!.IsVisible = !host;
            _note.Text = host
                ? "The directory runs the match, so nothing here needs a forwarded port. "
                    + "To run one on your own machine, use the dedicated server."
                : "";
            _note.Foreground = GuiTheme.TextDimBrush;
        }

        private void FillRooms(string? current)
        {
            if (_rooms.Count == 0)
            {
                _list.AddNote("No multiplayer rooms were found. Set the game files up "
                    + "from Settings.", GuiTheme.Warm);
                return;
            }
            foreach (string room in _rooms)
            {
                (RoomMetadata? meta, _) = Metadata.GetRoomByName(room);
                _list.Add(new UiListRow(meta?.InGameName ?? room, room) { Choice = room });
            }
            _list.SelectTag(current);
        }

        private async Task StartMatch()
        {
            if (SelectedRoom() is not string roomKey)
            {
                _note.Text = "Pick a map first.";
                _note.Foreground = GuiTheme.WarmBrush;
                return;
            }
            GameMode mode = _modes[_mode!.Index].Mode;
            var hunter = (Hunter)Enum.Parse(typeof(Hunter), _hunter!.Value);
            _settings.RoomKey = roomKey;
            LauncherPrefs.LastHunter = hunter;

            if (_where!.Index == 0)
            {
                LauncherPrefs.Bots = _bots!.Index;
                LauncherPrefs.BotLevel = _skill!.Index;
                LauncherPrefs.LastKind = (int)LaunchKind.Offline;
                LauncherPrefs.Save();
                Finish(new LaunchPlan
                {
                    Kind = LaunchKind.Offline,
                    Hunter = hunter,
                    PlayerName = LauncherPrefs.PlayerName,
                    RoomKey = roomKey,
                    Mode = mode,
                    Bots = _bots.Index,
                    BotLevel = _skill.Index
                });
                return;
            }

            string name = PlayerName();
            LauncherPrefs.PlayerName = name;
            LauncherPrefs.LastKind = (int)LaunchKind.Host;
            LauncherPrefs.Save();
            _go.IsEnabled = false;
            _go.Label = "starting";
            _note.Text = $"Asking {LauncherPrefs.MasterHost} to run {roomKey}...";
            _note.Foreground = GuiTheme.TextDimBrush;
            bool ok = await Task.Run(() =>
            {
                HostedGame game = NetMasterClient.RequestGame(LauncherPrefs.MasterHost,
                    LauncherPrefs.MasterPort, roomKey, mode, timeLimit: 7 * 60,
                    pointGoal: 7, maxPlayers: PlayerEntity.SlotCapacity,
                    serverName: $"{name}'s game");
                return game.Started && NetLaunch.Join(game.Host, game.Port, name, hunter);
            });
            _go.IsEnabled = true;
            _go.Label = "start";
            if (!ok)
            {
                NetSession.Stop();
                NetHostSession.Stop();
                _note.Text = NetHostSession.LastError
                    ?? "The game could not be started. The directory may be down.";
                _note.Foreground = GuiTheme.BadBrush;
                return;
            }
            Finish(new LaunchPlan
            {
                Kind = LaunchKind.Host,
                Hunter = hunter,
                PlayerName = name,
                RoomKey = roomKey,
                Mode = mode,
                Port = LauncherPrefs.HostPort
            });
        }

        private string? SelectedRoom()
        {
            return (_list.Selected as UiListRow)?.Choice as string;
        }

        // --------------------------------------------------------------- story

        private void BuildStory()
        {
            _go.Label = "play";
            for (byte slot = 1; slot <= AdventureSave.SlotCount; slot++)
            {
                AdventureSave.SlotInfo info = AdventureSave.Read(slot);
                _list.Add(new UiListRow($"Slot {slot}", info.Describe()) { Choice = slot });
            }
            _hunter = AddHunter();
            // Continue or start over, rather than two buttons: an empty slot
            // has nothing to continue, and the row says so by being fixed on
            // the only answer it has.
            _resume = new ChoiceRow("Start", new[] { "Continue", "New game" }, 0);
            _options.Children.Add(_resume);
            RefreshStory();
        }

        private void RefreshStory()
        {
            if (_resume == null || Current != Face.Story)
            {
                return;
            }
            bool used = AdventureSave.Read(SelectedSlot()).Used;
            _resume.SetItems(used ? new[] { "Continue", "New game" } : new[] { "New game" });
        }

        private byte SelectedSlot()
        {
            if ((_list.Selected as UiListRow)?.Choice is byte slot)
            {
                return slot;
            }
            return 1;
        }

        private void StartAdventure()
        {
            byte slot = SelectedSlot();
            bool used = AdventureSave.Read(slot).Used;
            bool newGame = !used || _resume!.Value == "New game";
            var hunter = (Hunter)Enum.Parse(typeof(Hunter), _hunter!.Value);
            LauncherPrefs.LastHunter = hunter;
            LauncherPrefs.LastKind = (int)LaunchKind.Adventure;
            LauncherPrefs.Save();
            Finish(new LaunchPlan
            {
                Kind = LaunchKind.Adventure,
                Hunter = hunter,
                PlayerName = LauncherPrefs.PlayerName,
                RoomKey = "",
                SaveSlot = slot,
                NewGame = newGame
            });
        }

        // ---------------------------------------------------------------- demo

        private void BuildDemo()
        {
            _go.Label = "watch";
            IReadOnlyList<DemoRecording> demos = DemoLibrary.List();
            foreach (DemoRecording demo in demos)
            {
                _list.Add(new UiListRow(demo.Room.Length > 0 ? demo.Room : demo.FileName,
                    DemoLibrary.Describe(demo))
                { Choice = demo.Path });
            }
            if (demos.Count == 0)
            {
                // The folder, spelled out. It is the app's own directory and
                // no file manager on a modern Android can open it, so a player
                // who wants to copy a recording off the device needs the path
                // itself -- and this is the only place it is ever written down.
                _note.Text = "Nothing recorded yet. Recordings are made from the pause menu "
                    + $"during an online match, and are written to:\n{DemoLibrary.Directory}";
            }
            // The system picker last rather than first: on Android it cannot
            // reach the folder the recordings are in at all.
            _list.Add(new UiListRow("Open a file...",
                "a demo from somewhere else on this device")
            { Choice = _import });
        }

        /// <summary>The stand-in choice that means "ask the system picker instead".</summary>
        private static readonly object _import = new();

        private async Task PlayDemo()
        {
            if ((_list.Selected as UiListRow)?.Choice is not object choice)
            {
                return;
            }
            if (ReferenceEquals(choice, _import))
            {
                await ImportDemo();
                return;
            }
            await Watch((string)choice);
        }

        /// <summary>Load a demo file and, if it reads, start playing it.</summary>
        private async Task Watch(string path)
        {
            // Joined here, not inside MatchStart: a failure has to land back on
            // a screen that is still open to show it on. The Windows build has
            // no console for anything the launcher starts, so the alternative
            // is a menu that silently does nothing.
            _go.IsEnabled = false;
            _go.Label = "loading";
            bool joined = await Task.Run(() => DemoPlayback.Join(path));
            _go.IsEnabled = true;
            _go.Label = "watch";
            if (!joined)
            {
                _note.Text = DemoPlayback.LastError
                    ?? "That file could not be read as a demo.";
                _note.Foreground = GuiTheme.BadBrush;
                return;
            }
            Finish(new LaunchPlan
            {
                Kind = LaunchKind.Demo,
                DemoPath = path,
                Hunter = Hunter.Samus,
                PlayerName = "",
                RoomKey = ""
            });
        }

        private async Task ImportDemo()
        {
            TopLevel? top = TopLevel.GetTopLevel(this);
            if (top == null)
            {
                return;
            }
            var options = new FilePickerOpenOptions { Title = "Demos", AllowMultiple = false };
            if (!OperatingSystem.IsAndroid())
            {
                // Android filters by MIME type and a demo file has none; a
                // pattern there produces a picker in which every file is
                // refused.
                options.FileTypeFilter = new[]
                {
                    new FilePickerFileType($"{Branding.Name} demo")
                    {
                        Patterns = new[] { $"*{DemoFile.Extension}" }
                    },
                    new FilePickerFileType("Every file") { Patterns = new[] { "*" } }
                };
            }
            try
            {
                string demoDir = DemoLibrary.Directory;
                if (Directory.Exists(demoDir))
                {
                    options.SuggestedStartLocation =
                        await top.StorageProvider.TryGetFolderFromPathAsync(demoDir);
                }
            }
            catch (IOException)
            {
                // No default folder is a worse first run than one with a clean
                // slate, not a reason to refuse the picker outright.
            }
            IReadOnlyList<IStorageFile> picked =
                await top.StorageProvider.OpenFilePickerAsync(options);
            if (picked.Count == 0)
            {
                return;
            }
            string? path = picked[0].TryGetLocalPath();
            if (path == null)
            {
                // Android hands back a content:// document with no file behind
                // it, and the demo reader takes a path. Kept afterwards rather
                // than deleted: the reader holds the file open for the whole
                // session.
                try
                {
                    string scratch = Path.Combine(GameFiles.Root, $"picked{DemoFile.Extension}");
                    await using (Stream source = await picked[0].OpenReadAsync())
                    await using (var target = File.Create(scratch))
                    {
                        await source.CopyToAsync(target);
                    }
                    path = scratch;
                }
                catch (Exception ex)
                {
                    _note.Text = $"That file could not be read: {ex.Message}";
                    _note.Foreground = GuiTheme.BadBrush;
                    return;
                }
            }
            await Watch(path);
        }

        // ---------------------------------------------------------------- vote

        private void BuildVote()
        {
            _go.Label = "call the vote";
            FillRooms(NetSession.ServerMatch?.RoomKey);
            _previewBox.IsVisible = true;
        }

        // -------------------------------------------------------------- shared

        private void Go()
        {
            if (_finished)
            {
                return;
            }
            switch (Current)
            {
            case Face.Online:
                _ = Join();
                break;
            case Face.Offline:
                _ = StartMatch();
                break;
            case Face.Story:
                StartAdventure();
                break;
            case Face.Demo:
                _ = PlayDemo();
                break;
            case Face.Vote:
                if (SelectedRoom() is string room)
                {
                    _finished = true;
                    Voted?.Invoke(this, room);
                }
                break;
            }
        }

        /// <summary>
        /// The picture of the chosen map, beside the list.
        ///
        /// The grid of previews is gone -- twenty-seven pictures is a page to
        /// scroll where a column of names is a glance -- but the picture
        /// itself is the answer to "which map is that", so it follows the
        /// selection instead of being a screen of its own.
        /// </summary>
        private void RefreshPreview()
        {
            if (!_previewBox.IsVisible)
            {
                return;
            }
            _preview.Source = null;
            if (SelectedRoom() is not string room)
            {
                return;
            }
            try
            {
                string path = ThumbnailGenerator.PathFor(room);
                if (!File.Exists(path))
                {
                    return;
                }
                // Through a MemoryStream so the file is not held open: the
                // preview generator rewrites these while the launcher is up.
                using var stream = new MemoryStream(File.ReadAllBytes(path));
                _preview.Source = new Bitmap(stream);
            }
            catch (Exception)
            {
                // A truncated PNG from an interrupted batch should show an
                // empty frame, not take the screen down.
            }
        }

        /// <summary>host, or host:port. Leaves both alone on anything else, so a
        /// typo does not silently change the address.</summary>
        private static bool ParseEndpoint(string text, ref string host, ref int port)
        {
            text = text.Trim();
            if (text.Length == 0)
            {
                return false;
            }
            int colon = text.LastIndexOf(':');
            if (colon <= 0)
            {
                host = text;
                return true;
            }
            if (!Int32.TryParse(text[(colon + 1)..], NumberStyles.Integer,
                CultureInfo.InvariantCulture, out int parsed)
                || parsed < 1 || parsed > 65535)
            {
                return false;
            }
            host = text[..colon];
            port = parsed;
            return true;
        }
    }
}
