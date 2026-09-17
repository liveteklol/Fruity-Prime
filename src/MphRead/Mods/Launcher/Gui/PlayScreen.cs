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
            Clips,
            Vote
        }

        /// <summary>Raised when the player backed out without choosing anything.</summary>
        public event EventHandler? Closed;

        /// <summary>Raised with a match to start. The screen is finished with by then.</summary>
        public event EventHandler<LaunchPlan>? Launched;

        /// <summary>Vote mode only: the map to put to the room.</summary>
        public event EventHandler<string>? Voted;

        /// <summary>
        /// Online only: run a server rather than join one.
        ///
        /// Raised rather than handled here because the screen that answers it
        /// is a screen, not a panel: it is pushed onto the same stack this one
        /// is on, so it gets the window, the keyboard and the back mark that
        /// every other screen has, and this one is not holding a second layout
        /// it shows a tenth of the time.
        /// </summary>
        public event EventHandler? CreateRequested;

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

        /// <summary>The three things the well holds, kept so the shape can change.</summary>
        private readonly Grid _body;
        private readonly ScrollViewer _side;
        private bool _compact;

        /// <summary>
        /// Whether this face has a picture to show, as against whether it is
        /// being drawn. Two questions since the compact arrangement exists:
        /// every face still says what it wants, and the box says whether
        /// there is room for it.
        /// </summary>
        private bool _previewWanted;
        private readonly Note _note = new("");
        private readonly UiMark _go;
        private readonly UiMark _back;
        private readonly UiMark _create;

        private DispatcherTimer? _statusTimer;
        private CancellationTokenSource? _statusCancel;
        private bool _finished;

        private ChoiceRow? _hunter;
        private ChoiceRow? _mode;
        private ChoiceRow? _bots;
        private ChoiceRow? _skill;
        private ChoiceRow? _resume;
        private FieldRow? _name;
        private FieldRow? _address;

        /// <summary>
        /// Picture above the list, or options beside it.
        ///
        /// The stacked arrangement -- what you are choosing at the top, with
        /// its picture, and everything else underneath -- wants about 190
        /// points before the list gets any, and it is the right shape
        /// wherever there is height for it. A phone in landscape has 390
        /// points in total (see <c>UiScaleHost</c>), so the top block and the
        /// well's own margins took all of it and the list was arranged one
        /// row tall behind the footer: a server browser with no servers in
        /// it, again, for a different reason than the last one.
        ///
        /// So on a short box the two swap axes. The options go down the right
        /// at the width they were drawn for, the list takes the whole height
        /// on the left, and the picture goes under the options in the space
        /// they do not use -- which is the arrangement this screen had before
        /// the picture was made big, with the picture kept. It is a band
        /// rather than a landscape box there, and that is enough: the
        /// question it answers is "which map is that". Keyed on the height
        /// actually handed over rather than on the platform, so a tablet in
        /// landscape keeps the desktop shape.
        /// </summary>
        private void SetCompact(bool compact)
        {
            if (compact == _compact)
            {
                return;
            }
            _compact = compact;
            _previewBox.IsVisible = _previewWanted;
            if (compact)
            {
                // The list down the left, the options and the picture down the
                // right. The column is already as wide as the options asked
                // for and they do not fill the height, so the picture takes
                // what is under them: a band rather than the landscape box it
                // is on a desktop, which is what UniformToFill and a clipping
                // border were already there for.
                _side.MaxHeight = Double.PositiveInfinity;
                Grid.SetRow(_side, 0);
                Grid.SetRowSpan(_side, 1);
                _side.Margin = new Thickness(18, 0, 0, 0);
                Grid.SetColumn(_previewBox, 1);
                Grid.SetRow(_previewBox, 1);
                // Stretched into what is left rather than given a height:
                // whatever that is, it is the room there is, and a fixed
                // number here would either overlap the line under the list on
                // the shortest screens or leave a gap on the tallest. Capped
                // so that a tall-enough box does not hand a 300-point-wide
                // thumbnail half the screen.
                _previewBox.Height = Double.NaN;
                _previewBox.MaxHeight = 150;
                _previewBox.VerticalAlignment = VerticalAlignment.Stretch;
                _previewBox.Margin = new Thickness(18, 12, 0, 0);
                Grid.SetRow(_list, 0);
                Grid.SetRowSpan(_list, 2);
                Grid.SetColumnSpan(_list, 1);
            }
            else
            {
                _side.MaxHeight = 190;
                Grid.SetRow(_side, 0);
                Grid.SetRowSpan(_side, 1);
                _side.Margin = new Thickness(0);
                Grid.SetColumn(_previewBox, 0);
                Grid.SetRow(_previewBox, 0);
                _previewBox.Height = 172;
                _previewBox.MaxHeight = Double.PositiveInfinity;
                _previewBox.VerticalAlignment = VerticalAlignment.Stretch;
                _previewBox.Margin = new Thickness(0, 0, 24, 14);
                Grid.SetRow(_list, 1);
                Grid.SetRowSpan(_list, 1);
                Grid.SetColumnSpan(_list, 2);
            }
        }

        /// <summary>This face wants the picture; whether it gets it is the box's.</summary>
        private void WantPreview(bool wanted)
        {
            _previewWanted = wanted;
            _previewBox.IsVisible = wanted;
        }

        protected override Size MeasureOverride(Size availableSize)
        {
            if (!Double.IsInfinity(availableSize.Height) && availableSize.Height > 0)
            {
                SetCompact(availableSize.Height < UiLayout.ShortBox);
            }
            return base.MeasureOverride(availableSize);
        }

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
                IsVisible = false,
                Child = _preview
            };

            // The picture above the list rather than beside it, and the
            // settings about it to its right.
            //
            // A centred well has no side to hang a column off without
            // stopping being centred, so the three things this screen holds
            // are stacked instead: what you are choosing at the top -- its
            // picture, and the settings that belong to it -- and the list of
            // everything you could choose instead underneath, running the full
            // width of the well. The old arrangement put the picture in the
            // corner of the options column, a third of the size, and the
            // picture is the answer to "which map is that".
            //
            // The preview stretches and the options do not: a room's picture
            // is better bigger, and a row of settings at 320 is the width it
            // was drawn for.
            var body = new Grid
            {
                ColumnDefinitions = new ColumnDefinitions("*,Auto"),
                RowDefinitions = new RowDefinitions("Auto,*,Auto")
            };
            _body = body;
            _previewBox.Margin = new Thickness(0, 0, 24, 14);
            Grid.SetColumn(_previewBox, 0);
            Grid.SetRow(_previewBox, 0);
            body.Children.Add(_previewBox);
            // Scrolled, because a short window gives this row less height than
            // the options asked for -- and a StackPanel given less height than
            // it wants does not shrink and is not clipped, it simply draws
            // past the bottom of its row. That was the tick being covered by
            // the rows above it, which is a layout fault rather than a
            // z-order one.
            var side = new ScrollViewer
            {
                Content = _options,
                MaxHeight = 190,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto
            };
            _side = side;
            Grid.SetColumn(side, 1);
            Grid.SetRow(side, 0);
            body.Children.Add(side);
            Grid.SetColumn(_list, 0);
            Grid.SetColumnSpan(_list, 2);
            Grid.SetRow(_list, 1);
            body.Children.Add(_list);
            Grid.SetColumn(_note, 0);
            Grid.SetColumnSpan(_note, 2);
            Grid.SetRow(_note, 2);
            body.Children.Add(_note);

            _back = new UiMark(UiMark.Shape.Cancel, "back");
            _back.Click += (_, _) => Leave();
            _go = new UiMark(UiMark.Shape.Accept, "play");
            _go.Click += (_, _) => Go();
            // Between the two because that is what it is: running a server is
            // neither leaving the browser nor joining a row on it, and the one
            // place on the browser it belongs is beside the act it is an
            // alternative to. Drawn only on Online -- there is nothing to
            // create on any other face.
            _create = new UiMark(UiMark.Shape.Add, "create server")
            {
                IsVisible = false
            };
            _create.Click += (_, _) => CreateRequested?.Invoke(this, EventArgs.Empty);

            if (face != Face.Vote)
            {
                _tabs = new UiTabs(new[] { "Online", "Offline", "Story", "Clips" },
                    (int)face);
                _tabs.Changed += (_, _) => Rebuild();
            }
            Content = UiLayout.Page(overGame, UiLayout.WellPlay,
                face == Face.Vote ? "vote" : "play", _tabs, body, _back, _go,
                extra: _create);

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
            // What the tick does, in the word for this face. It is the only
            // thing that starts anything now -- a click on a row selects it
            // and nothing more -- so it has to say which of the four it is
            // about to do.
            // Joining somebody else's server is a different act from starting
            // a match, and is the only one that keeps its own word. Offline
            // and Story both start something of your own, so they say the same
            // thing -- "play" on one and "start" on the other was two words
            // for one action, which is the habit this screen exists to break.
            _go.Label = Current switch
            {
                Face.Online => "join",
                Face.Clips => "watch",
                Face.Vote => "vote",
                _ => "start"
            };
            _options.Children.Clear();
            _note.Text = "";
            _note.Foreground = GuiTheme.TextDimBrush;
            _hunter = _mode = _bots = _skill = _resume = null;
            _name = _address = null;
            WantPreview(false);
            _create.IsVisible = Current == Face.Online;

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
            case Face.Clips:
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
            // The same picture the map list shows. A server row says which map
            // it is running and the map is most of what decides whether to
            // join, so the one screen that had the name without the picture
            // was the one where the picture would have answered the question.
            WantPreview(true);
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
            // Pressing a server fills the address box in and selects it;
            // JOIN in the corner (or a second click, or Enter) is what
            // actually connects. One press used to do both, which is a player
            // dropped into a match they had only meant to read the ping of.
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
                Dispatcher.UIThread.Post(() =>
                {
                    row.SetStatus(status);
                    // The answers come back in whatever order the servers
                    // reply in, and the selected row is usually the first one
                    // -- which means its map arrives after the preview was
                    // last refreshed. Refresh again for the row that is
                    // selected, and for no other.
                    if (ReferenceEquals(_list.Selected, row))
                    {
                        RefreshPreview();
                    }
                });
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
            // No "Where" row. Offline is a match on this machine and nothing
            // else: the row's other answer -- have the directory run it --
            // was a *server*, sitting on the one face of this screen that is
            // about not being online, offering a choice whose two halves have
            // different settings under them. Running a server is its own act
            // and it moved to Online, beside the servers it joins.
            _mode = new ChoiceRow("Match type", _modes.Select(m => m.Label).ToArray());
            _options.Children.Add(_mode);
            _hunter = AddHunter();
            _bots = new ChoiceRow("Bots",
                Enumerable.Range(0, PlayerEntity.SlotCapacity)
                    .Select(i => i.ToString(CultureInfo.InvariantCulture)).ToArray(),
                LauncherPrefs.Bots);
            _options.Children.Add(_bots);
            _skill = new ChoiceRow("Bot skill", new[] { "Easy", "Normal", "Hard", "Insane" },
                LauncherPrefs.BotLevel);
            _options.Children.Add(_skill);
            WantPreview(true);
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

        private void StartMatch()
        {
            if (SelectedRoom() is not string roomKey)
            {
                _note.Text = "Pick a map first.";
                _note.Foreground = GuiTheme.WarmBrush;
                return;
            }
            GameMode mode = _modes[_mode!.Index].Mode;
            var custom=MapGen.CustomRooms.Definitions.FirstOrDefault(d=>d.Name==roomKey);
            string? unsupported=custom==null?null:MapGen.MapModeValidator.WhyUnsupported(custom,mode,1+_bots!.Index);
            if(unsupported!=null){_note.Text=unsupported;_note.Foreground=GuiTheme.WarmBrush;return;}
            var hunter = (Hunter)Enum.Parse(typeof(Hunter), _hunter!.Value);
            _settings.RoomKey = roomKey;
            LauncherPrefs.LastHunter = hunter;
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
        }

        private string? SelectedRoom()
        {
            return (_list.Selected as UiListRow)?.Choice as string;
        }

        /// <summary>
        /// Which map the picture beside the list should be of.
        ///
        /// A map row stands for its own room; a server row stands for whatever
        /// that server said it was running, which is a thing it only knows
        /// once the server has answered. Nothing is drawn until it has.
        /// </summary>
        private string? PreviewRoom()
        {
            if (_list.Selected is ServerRow server)
            {
                return server.RoomKey.Length > 0 ? server.RoomKey : null;
            }
            return SelectedRoom();
        }

        // --------------------------------------------------------------- story

        private void BuildStory()
        {
            _go.Label = "start";
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
                _note.Text = "Nothing recorded yet. Clips are made from the pause menu "
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
            string? demoDir = null;
            try
            {
                demoDir = DemoLibrary.Directory;
            }
            catch (IOException)
            {
                // No default folder is a worse first run than one with a clean
                // slate, not a reason to refuse the picker outright.
            }
            IStorageFile? file = null;
            string? path = null;
            // Headless has no chooser and answers with an empty list rather
            // than a failure, which is a button that does nothing at all.
            if (!top.StorageProvider.CanOpen)
            {
                NativeFileDialog.FilePick pick = await NativeFileDialog.OpenAsync(
                    "Clips", $"{Branding.Name} demo", DemoFile.Extension, demoDir);
                if (pick.Problem != null)
                {
                    _note.Text = pick.Problem;
                    _note.Foreground = GuiTheme.BadBrush;
                    return;
                }
                if (pick.Path == null)
                {
                    return;
                }
                await Watch(pick.Path);
                return;
            }
            var options = new FilePickerOpenOptions { Title = "Clips", AllowMultiple = false };
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
            file = picked[0];
            path = file.TryGetLocalPath();
            if (path == null)
            {
                // Android hands back a content:// document with no file behind
                // it, and the demo reader takes a path. Kept afterwards rather
                // than deleted: the reader holds the file open for the whole
                // session.
                try
                {
                    string scratch = Path.Combine(GameFiles.Root, $"picked{DemoFile.Extension}");
                    await using (Stream source = await file.OpenReadAsync())
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
            WantPreview(true);
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
                StartMatch();
                break;
            case Face.Story:
                StartAdventure();
                break;
            case Face.Clips:
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
            if (PreviewRoom() is not string room)
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
