using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Avalonia.VisualTree;
using MphRead.Entities;
using MphRead.Mods;
using MphRead.Mods.Network;
using MphRead.Mods.Update;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// The front screen: a picture, the things you can do, and nothing to read
    /// before you can play. The Avalonia counterpart of <c>HomeForm</c>.
    ///
    /// Same five entries, same card-at-a-time shape, and -- the part that
    /// matters -- the same <see cref="LauncherPrefs"/>, <see cref="GameFiles"/>
    /// and <see cref="MatchStart"/> underneath, so the two screens cannot come
    /// to disagree about what "host a game" does. What is deliberately not the
    /// same is the window chrome: this one is an ordinary decorated window
    /// rather than the WinForms one's borderless panel, because a window with
    /// no frame that a Linux window manager will not let you move is a trap,
    /// and there are many window managers.
    /// </summary>
    internal sealed class HomeWindow : Window
    {
        private readonly MenuSettings _settings;
        private readonly IReadOnlyList<string> _rooms;
        private readonly List<string> _playable = new();
        private readonly SplashView _splash = new();
        private readonly Panel _cards = new();

        private readonly ProgressRow _setupProgress = new();
        private MenuEntry _setupBack = null!;
        private Control _homeCard = null!;
        private Control _setupCard = null!;
        private Control _onlineCard = null!;
        private Control _matchCard = null!;
        private Control _browseCard = null!;
        private Control _adventureCard = null!;
        private Control? _current;
        private Control? _browseReturn;

        private DispatcherTimer? _statusTimer;
        private CancellationTokenSource? _statusCancel;

        /// <summary>What the screen decided. Kind None means it was closed.</summary>
        public LaunchPlan Plan { get; private set; }

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
            Enumerable.Range(0, 7).Select(i => ((Hunter)i).ToString())
                .Append(Hunter.Random.ToString()).ToArray();

        public HomeWindow(MenuSettings settings, IReadOnlyList<string> rooms)
        {
            _settings = settings;
            _rooms = rooms;
            foreach (string room in rooms)
            {
                _playable.Add(room);
            }

            Title = Mods.Branding.Name;
            Icon = GuiTheme.AppIcon.Value;
            Width = 940;
            Height = 560;
            MinWidth = 780;
            MinHeight = 480;
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            Background = GuiTheme.PanelBrush;
            RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;

            // Small, dim, and in the corner: an acknowledgement, not a
            // feature. The full list is -credits.
            var credits = new TextBlock
            {
                Text = Mods.Credits.Compact,
                FontFamily = GuiTheme.Display,
                FontSize = 10,
                Foreground = new SolidColorBrush(Color.FromRgb(78, 86, 102)),
                TextWrapping = TextWrapping.Wrap,
                TextAlignment = TextAlignment.Right,
                HorizontalAlignment = HorizontalAlignment.Right,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(0, 14, 0, 0)
            };
            var stack = new Grid { RowDefinitions = new RowDefinitions("*,Auto") };
            Grid.SetRow(_cards, 0);
            Grid.SetRow(credits, 1);
            stack.Children.Add(_cards);
            stack.Children.Add(credits);
            var panel = new Border
            {
                Background = GuiTheme.PanelBrush,
                Padding = new Thickness(22, 20, 22, 16),
                Width = 400,
                Child = stack
            };
            var grid = new Grid
            {
                ColumnDefinitions = new ColumnDefinitions("*,Auto")
            };
            _updateBadge.Click += (_, _) => UpdateNow();
            _updateBadge.HorizontalAlignment = HorizontalAlignment.Left;
            _updateBadge.VerticalAlignment = VerticalAlignment.Bottom;
            _updateBadge.Margin = new Thickness(24, 0, 24, 22);
            Grid.SetColumn(_splash, 0);
            Grid.SetColumn(_updateBadge, 0);
            Grid.SetColumn(panel, 1);
            grid.Children.Add(_splash);
            // After the splash and in the same cell, so it draws on top of it.
            grid.Children.Add(_updateBadge);
            grid.Children.Add(panel);
            Content = grid;

            _homeCard = BuildHomeCard();
            _setupCard = BuildSetupCard();
            _onlineCard = BuildOnlineCard();
            _matchCard = BuildMatchCard();
            _browseCard = BuildBrowseCard();
            _adventureCard = BuildAdventureCard();

            _setupBack.IsVisible = GameFiles.Ready;
            ShowCard(GameFiles.Ready ? _homeCard : _setupCard);
            RefreshSplash();

            if (LauncherPrefs.AutoUpdate)
            {
                // In the background, and never blocking the window: a launcher
                // that will not draw until GitHub answers looks broken on a bad
                // connection.
                Update.Updater.CheckInBackground(update =>
                    Dispatcher.UIThread.Post(() => ShowUpdate(update)));
            }
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                // Back one card, then out -- the same as the WinForms screen.
                if (_current != _homeCard && GameFiles.Ready)
                {
                    ShowCard(_homeCard);
                }
                else
                {
                    Close();
                }
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
        }

        private void ShowCard(Control card)
        {
            StopStatusPolling();
            _cards.Children.Clear();
            _cards.Children.Add(card);
            _current = card;
            if (ReferenceEquals(card, _onlineCard))
            {
                StartStatusPolling();
            }
            // The first thing on the card takes the keyboard, so the screen can
            // be used without touching the mouse.
            Dispatcher.UIThread.Post(() => card.GetVisualDescendants()
                .OfType<Control>().FirstOrDefault(c => c.Focusable)?.Focus(),
                DispatcherPriority.Background);
        }

        private static StackPanel Card() => new() { Spacing = 2 };

        private static MenuEntry Back(Action go)
        {
            var entry = new MenuEntry("Back", titleSize: 13) { Accent = GuiTheme.TextDim };
            entry.Click += (_, _) => go();
            return entry;
        }

        // ---------------------------------------------------------------- home

        private readonly UpdateBadge _updateBadge = new();
        private MenuEntry _onlineEntry = null!;
        private MenuEntry _offlineEntry = null!;
        private MenuEntry _hostEntry = null!;
        private MenuEntry _filesEntry = null!;
        private MenuEntry _adventureEntry = null!;
        private ChoiceRow _adventureSlot = null!;
        private ChoiceRow _adventureHunter = null!;
        private Note _adventureNote = null!;
        private MenuEntry _adventureStart = null!;
        private MenuEntry _adventureNew = null!;

        private Control BuildHomeCard()
        {
            var card = Card();
            _adventureEntry = new MenuEntry("Adventure", "The story, from a save slot");
            _adventureEntry.Click += (_, _) => OpenAdventure();
            _onlineEntry = new MenuEntry("Play online", "Join a server");
            _onlineEntry.Click += (_, _) => ShowCard(_onlineCard);
            _offlineEntry = new MenuEntry("Play offline", "A match against bots");
            _offlineEntry.Click += (_, _) => OpenMatch(LaunchKind.Offline);
            _hostEntry = new MenuEntry("Host a game", "Run a server and play on it");
            _hostEntry.Click += (_, _) => OpenMatch(LaunchKind.Host);
            var settings = new MenuEntry("Settings",
                "Display, audio, controls, match rules, cheats");
            settings.Click += async (_, _) => await OpenSettings();
            _filesEntry = new MenuEntry("Game files", GameFiles.Describe());
            _filesEntry.Click += (_, _) => ShowCard(_setupCard);
            var quit = new MenuEntry("Quit");
            quit.Click += (_, _) => Close();

            card.Children.Add(new Caption(Mods.Branding.NameAndVersion));
            card.Children.Add(_adventureEntry);
            card.Children.Add(_onlineEntry);
            card.Children.Add(_offlineEntry);
            card.Children.Add(_hostEntry);
            card.Children.Add(settings);
            card.Children.Add(_filesEntry);
            card.Children.Add(quit);
            return card;
        }

        private void ShowUpdate(UpdateInfo update)
        {
            _updateBadge.Show(update.AssetName.Length > 0
                ? $"{update.Tag} is out -- get {update.AssetName}"
                : $"{update.Tag} is out");
            // The picture's own caption moves up out of the way.
            _splash.BottomInset = _updateBadge.DesiredSize.Height + 22;
        }

        /// <summary>
        /// Open the release page. The program does not install anything: this
        /// hands the person the page and they replace the files themselves.
        /// </summary>
        private void UpdateNow()
        {
            UpdateInfo? update = Update.Updater.Available;
            if (update == null)
            {
                return;
            }
            if (!Update.Updater.OpenPage(update.Value))
            {
                // No browser to open, or it refused. Putting the address on the
                // badge beats a button that appears to do nothing.
                _updateBadge.Say(update.Value.PageUrl);
            }
        }

        /// <summary>
        /// Everything but "game files" is unusable until there is something to
        /// load, and says so rather than failing when pressed.
        /// </summary>
        private void RefreshGameFilesState()
        {
            bool ready = GameFiles.Ready;
            _onlineEntry.IsEnabled = ready;
            _offlineEntry.IsEnabled = ready;
            _hostEntry.IsEnabled = ready;
            _adventureEntry.IsEnabled = ready;
            _filesEntry.Subtitle = GameFiles.Describe();
            _filesEntry.SubtitleColor = ready ? GuiTheme.Good : GuiTheme.Warm;
        }

        // --------------------------------------------------------------- setup

        private Control BuildSetupCard()
        {
            var card = Card();
            var log = new Note("");
            var scroll = new ScrollViewer
            {
                Height = 150,
                Content = log,
                HorizontalScrollBarVisibility = Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled
            };
            var choose = new MenuEntry("Choose your .nds file", "", titleSize: 15)
            {
                Primary = true,
                Height = 44
            };
            choose.Click += async (_, _) => await ChooseRom(choose, log);

            card.Children.Add(new Caption("Game files"));
            card.Children.Add(new Note(
                Mods.Branding.Name + " needs your own Metroid Prime Hunters cartridge dump. It "
                + "unpacks what it needs next to this program and leaves the file "
                + "alone. No game data is included in this download, and none is "
                + "downloaded."));
            card.Children.Add(choose);
            card.Children.Add(_setupProgress);
            card.Children.Add(scroll);
            // Hidden until there is something to go back to. Before the game
            // files are set up this card is the whole launcher: the menu behind
            // it has no map previews to show on the left and four of its five
            // entries refused, which is a worse first impression than one
            // screen asking for the one thing it needs.
            _setupBack = Back(() => ShowCard(_homeCard));
            _setupBack.IsVisible = false;
            card.Children.Add(_setupBack);
            return card;
        }

        private async Task ChooseRom(MenuEntry button, Note log)
        {
            IReadOnlyList<IStorageFile> picked = await StorageProvider.OpenFilePickerAsync(
                new FilePickerOpenOptions
                {
                    Title = "Your Metroid Prime Hunters cartridge dump",
                    AllowMultiple = false,
                    FileTypeFilter = new[]
                    {
                        new FilePickerFileType("Nintendo DS ROM") { Patterns = new[] { "*.nds" } },
                        new FilePickerFileType("Every file") { Patterns = new[] { "*" } }
                    }
                });
            if (picked.Count == 0)
            {
                return;
            }
            string? path = picked[0].TryGetLocalPath();
            if (path == null)
            {
                log.Text = "That file is not on this machine.";
                return;
            }
            button.IsEnabled = false;
            button.Title = "Working...";
            log.Text = "";
            var progress = new SetupProgress();
            _setupProgress.Set(0, "Starting");
            // Extraction is upstream's own, in a child process, and takes
            // minutes. Off the UI thread, or the window stops answering and
            // looks like it has crashed at the exact moment it is doing the one
            // thing a fresh install needs.
            bool ok = await Task.Run(() => GameFiles.RunSetup(path, line =>
                Dispatcher.UIThread.Post(() =>
                {
                    log.Text = Tail(log.Text, line);
                    if (progress.Observe(line))
                    {
                        _setupProgress.Set(progress.Fraction, progress.Stage);
                    }
                })));
            if (ok)
            {
                log.Text = Tail(log.Text, "Rendering map previews...");
                IReadOnlyList<string> missing = ThumbnailGenerator.MissingThumbnails();
                await Task.Run(() => ThumbnailBatch.Run(missing, ThumbnailBatch.DefaultParallelism,
                    ThumbnailGenerator.ThumbnailWidth, ThumbnailGenerator.ThumbnailHeight,
                    line => Dispatcher.UIThread.Post(() =>
                    {
                        log.Text = Tail(log.Text, line);
                    })));
            }
            progress.Finish(ok);
            _setupProgress.Set(progress.Fraction, progress.Stage);
            button.IsEnabled = true;
            button.Title = "Choose your .nds file";
            log.Text = Tail(log.Text, ok ? "Ready to play." : "Setup did not finish.");
            RefreshGameFilesState();
            if (ok)
            {
                RefreshRooms();
            }
            RefreshSplash();
            if (ok)
            {
                // The launcher proper, now that there is something behind it:
                // map previews to show on the left, and every entry usable.
                _setupBack.IsVisible = true;
                _setupProgress.IsVisible = false;
                ShowCard(_homeCard);
            }
        }

        /// <summary>Keep the last few lines; the extraction prints hundreds.</summary>
        private static string Tail(string? existing, string line)
        {
            string[] lines = ((existing ?? "") + line + "\n")
                .Split('\n', StringSplitOptions.RemoveEmptyEntries);
            return String.Join("\n", lines.Skip(Math.Max(0, lines.Length - 8)));
        }

        // -------------------------------------------------------------- online

        private FieldRow _onlineName = null!;
        private ChoiceRow _onlineHunter = null!;
        private FieldRow _onlineAddress = null!;
        private Note _onlineStatus = null!;
        private MenuEntry _connect = null!;

        private Control BuildOnlineCard()
        {
            var card = Card();
            _onlineName = new FieldRow("Your name", LauncherPrefs.PlayerName);
            _onlineHunter = new ChoiceRow("Hunter", _hunters,
                Array.IndexOf(_hunters, LauncherPrefs.LastHunter.ToString()));
            _onlineAddress = new FieldRow("Server",
                $"{LauncherPrefs.ServerAddress}:{LauncherPrefs.ServerPort}", boxWidth: 190);
            _onlineStatus = new Note("Checking...");
            _onlineAddress.Box.LostFocus += (_, _) => QueryStatusSoon();

            var find = new MenuEntry("Find a server", "See who is up right now", titleSize: 15);
            find.Click += (_, _) =>
            {
                _browseReturn = _onlineCard;
                ShowCard(_browseCard);
                ReloadServers();
            };
            _connect = new MenuEntry("Connect", titleSize: 16) { Primary = true, Height = 44 };
            _connect.Click += async (_, _) => await Connect();

            card.Children.Add(new Caption("Play online"));
            card.Children.Add(_onlineName);
            card.Children.Add(_onlineHunter);
            card.Children.Add(_onlineAddress);
            card.Children.Add(_onlineStatus);
            card.Children.Add(find);
            card.Children.Add(_connect);
            card.Children.Add(Back(() => ShowCard(_homeCard)));
            return card;
        }

        private (string Host, int Port) OnlineEndpoint()
        {
            string host = LauncherPrefs.ServerAddress;
            int port = LauncherPrefs.ServerPort;
            ParseEndpoint(_onlineAddress.Value, ref host, ref port);
            return (host, port);
        }

        /// <summary>
        /// Poll what the server is running while somebody is reading the card.
        ///
        /// StatusQuery answers without claiming a slot, which is what makes
        /// polling it reasonable; a server too old to know the packet is asked
        /// once with a join probe and then left alone.
        /// </summary>
        private void StartStatusPolling()
        {
            QueryStatusSoon();
            _statusTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(4) };
            _statusTimer.Tick += (_, _) => QueryStatusSoon();
            _statusTimer.Start();
        }

        private void StopStatusPolling()
        {
            _statusTimer?.Stop();
            _statusTimer = null;
            _statusCancel?.Cancel();
            _statusCancel = null;
        }

        private void QueryStatusSoon()
        {
            _statusCancel?.Cancel();
            var cancel = new CancellationTokenSource();
            _statusCancel = cancel;
            (string host, int port) = OnlineEndpoint();
            Task.Run(() =>
            {
                ServerStatus status = NetStatus.Query(host, port, allowJoinProbe: true);
                if (cancel.IsCancellationRequested)
                {
                    return;
                }
                Dispatcher.UIThread.Post(() =>
                {
                    if (cancel.IsCancellationRequested)
                    {
                        return;
                    }
                    if (status.Online)
                    {
                        _onlineStatus.Text = Describe(status);
                        _onlineStatus.Foreground = GuiTheme.GoodBrush;
                        _splash.ShowRoom(status.RoomKey, Describe(status));
                    }
                    else
                    {
                        _onlineStatus.Text = "No answer -- it may be off, or UDP may be blocked.";
                        _onlineStatus.Foreground = GuiTheme.WarmBrush;
                    }
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
            return $"{status.RoomKey} ({NetStatus.ModeName(status.Mode)}) "
                + $"{players} players, {ping}";
        }

        private async Task Connect()
        {
            (string host, int port) = OnlineEndpoint();
            string name = _onlineName.Value.Length > 0 ? _onlineName.Value : "Player";
            var hunter = (Hunter)Enum.Parse(typeof(Hunter), _onlineHunter.Value);
            StopStatusPolling();
            _connect.IsEnabled = false;
            _connect.Title = "Connecting";
            _onlineStatus.Text = $"Connecting to {host}:{port}...";
            _onlineStatus.Foreground = GuiTheme.TextDimBrush;

            LauncherPrefs.PlayerName = name;
            LauncherPrefs.LastHunter = hunter;
            LauncherPrefs.ServerAddress = host;
            LauncherPrefs.ServerPort = port;
            LauncherPrefs.LastKind = (int)LaunchKind.Online;
            LauncherPrefs.Save();

            // Joining blocks for up to eight seconds while it retries; on the
            // UI thread that is eight seconds of a window that does not redraw.
            bool joined = await Task.Run(() => NetLaunch.Join(host, port, name, hunter));
            _connect.IsEnabled = true;
            _connect.Title = "Connect";
            if (!joined)
            {
                NetSession.Stop();
                _onlineStatus.Text = "Could not join. It may be off, full, or UDP may be blocked.";
                _onlineStatus.Foreground = GuiTheme.BadBrush;
                StartStatusPolling();
                return;
            }
            Plan = new LaunchPlan
            {
                Kind = LaunchKind.Online,
                Hunter = hunter,
                PlayerName = name,
                RoomKey = "",
                Mode = GameMode.Battle,
                Port = port
            };
            Close();
        }

        // --------------------------------------------------------- offline/host

        private LaunchKind _matchKind = LaunchKind.Offline;
        private Caption _matchCaption = null!;
        private ChoiceRow _matchMap = null!;
        private ChoiceRow _matchMode = null!;
        private ChoiceRow _matchHunter = null!;
        private ChoiceRow _matchBots = null!;
        private ChoiceRow _matchSkill = null!;
        private FieldRow _matchName = null!;
        private FieldRow _matchPort = null!;
        private ToggleRow _matchOnMaster = null!;
        private ToggleRow _matchListed = null!;
        private MenuEntry _matchStart = null!;
        private Note _matchNote = null!;

        private Control BuildMatchCard()
        {
            var card = Card();
            _matchCaption = new Caption("Play offline");
            _matchMap = new ChoiceRow("Map", _playable,
                Math.Max(0, _playable.IndexOf(_settings.RoomKey)));
            _matchMap.Changed += (_, _) => RefreshSplash();
            // Stepping through maps one at a time is the right gesture while
            // the picture beside it changes as you step, and the wrong one when
            // the map you want is twenty steps away.
            var browseMaps = new MenuEntry("See every map", "", titleSize: 13)
            {
                Accent = GuiTheme.TextDim
            };
            browseMaps.Click += async (_, _) => await BrowseMaps();
            _matchMode = new ChoiceRow("Mode", _modes.Select(m => m.Label).ToArray());
            _matchHunter = new ChoiceRow("Hunter", _hunters,
                Array.IndexOf(_hunters, LauncherPrefs.LastHunter.ToString()));
            _matchBots = new ChoiceRow("Bots",
                Enumerable.Range(0, PlayerEntity.SlotCapacity)
                    .Select(i => i.ToString(CultureInfo.InvariantCulture)).ToArray(),
                LauncherPrefs.Bots);
            _matchSkill = new ChoiceRow("Bot skill", new[] { "Easy", "Normal", "Hard" },
                LauncherPrefs.BotLevel);
            _matchName = new FieldRow("Your name", LauncherPrefs.PlayerName);
            _matchPort = new FieldRow("Port",
                LauncherPrefs.HostPort.ToString(CultureInfo.InvariantCulture), boxWidth: 90);
            _matchOnMaster = new ToggleRow("Let the directory run it",
                LauncherPrefs.HostOnMaster);
            _matchOnMaster.Changed += (_, _) => RefreshMatchCard();
            _matchListed = new ToggleRow("List it so others can find it",
                LauncherPrefs.ListHostedGame);
            _matchNote = new Note("");
            _matchStart = new MenuEntry("Start", titleSize: 16) { Primary = true, Height = 44 };
            _matchStart.Click += async (_, _) => await StartMatch();

            card.Children.Add(_matchCaption);
            card.Children.Add(_matchMap);
            card.Children.Add(browseMaps);
            card.Children.Add(_matchMode);
            card.Children.Add(_matchHunter);
            card.Children.Add(_matchBots);
            card.Children.Add(_matchSkill);
            card.Children.Add(_matchName);
            card.Children.Add(_matchOnMaster);
            card.Children.Add(_matchPort);
            card.Children.Add(_matchListed);
            card.Children.Add(_matchNote);
            card.Children.Add(_matchStart);
            card.Children.Add(Back(() => ShowCard(_homeCard)));
            return card;
        }

        // ----------------------------------------------------------- adventure

        /// <summary>
        /// The story: a slot, then continue it or start it over.
        ///
        /// Picking the slot is what makes saving work at all -- see
        /// <see cref="AdventureSave"/>, and Menu.SaveSlot, which is 0 until
        /// something sets it and writes nothing while it is.
        /// </summary>
        private Control BuildAdventureCard()
        {
            var card = Card();
            var slots = new string[AdventureSave.SlotCount];
            for (int i = 0; i < slots.Length; i++)
            {
                slots[i] = $"Slot {i + 1}";
            }
            _adventureSlot = new ChoiceRow("Save slot", slots);
            _adventureSlot.Changed += (_, _) => RefreshAdventureCard();
            _adventureHunter = new ChoiceRow("Hunter", _hunters,
                Array.IndexOf(_hunters, LauncherPrefs.LastHunter.ToString()));
            _adventureNote = new Note("");
            _adventureStart = new MenuEntry("Continue", titleSize: 16)
            {
                Primary = true,
                Height = 44
            };
            _adventureStart.Click += (_, _) => StartAdventure(newGame: false);
            _adventureNew = new MenuEntry("New game");
            _adventureNew.Click += (_, _) => StartAdventure(newGame: true);

            card.Children.Add(new Caption("Adventure"));
            card.Children.Add(_adventureSlot);
            card.Children.Add(_adventureNote);
            card.Children.Add(_adventureHunter);
            card.Children.Add(_adventureStart);
            card.Children.Add(_adventureNew);
            card.Children.Add(Back(() => ShowCard(_homeCard)));
            return card;
        }

        private void OpenAdventure()
        {
            RefreshAdventureCard();
            ShowCard(_adventureCard);
        }

        private void RefreshAdventureCard()
        {
            AdventureSave.SlotInfo info = AdventureSave.Read(CurrentSlot());
            _adventureNote.Text = info.Describe();
            _adventureNote.IsVisible = true;
            // Nothing to continue in an empty slot, so the only button that
            // means anything there is the one that starts a game.
            _adventureStart.Title = info.Used ? "Continue" : "Start a new game";
            _adventureNew.IsVisible = info.Used;
        }

        private byte CurrentSlot()
        {
            return (byte)Math.Clamp(_adventureSlot.Index + 1, 1, AdventureSave.SlotCount);
        }

        private void StartAdventure(bool newGame)
        {
            byte slot = CurrentSlot();
            if (!AdventureSave.Read(slot).Used)
            {
                newGame = true;
            }
            var hunter = (Hunter)Enum.Parse(typeof(Hunter), _adventureHunter.Value);
            LauncherPrefs.LastHunter = hunter;
            LauncherPrefs.LastKind = (int)LaunchKind.Adventure;
            LauncherPrefs.Save();
            Plan = new LaunchPlan
            {
                Kind = LaunchKind.Adventure,
                Hunter = hunter,
                PlayerName = LauncherPrefs.PlayerName,
                RoomKey = "",
                SaveSlot = slot,
                NewGame = newGame
            };
            Close();
        }

        /// <summary>
        /// Re-read the rows that show a launcher preference. The settings
        /// window owns the same values, so anything it changed has to reach the
        /// cards that were built before it opened.
        /// </summary>
        private void RefreshPrefRows()
        {
            _onlineName.Value = LauncherPrefs.PlayerName;
            _matchName.Value = LauncherPrefs.PlayerName;
            _onlineAddress.Value =
                $"{LauncherPrefs.ServerAddress}:{LauncherPrefs.ServerPort}";
            int hunter = Array.IndexOf(_hunters, LauncherPrefs.LastHunter.ToString());
            if (hunter >= 0)
            {
                _onlineHunter.Index = hunter;
                _matchHunter.Index = hunter;
                _adventureHunter.Index = hunter;
            }
        }

        /// <summary>Every map at once, as pictures.</summary>
        private async Task BrowseMaps()
        {
            if (_playable.Count == 0)
            {
                return;
            }
            var picker = new MapPickerWindow(_playable, _matchMap.Value);
            await picker.ShowDialog(this);
            if (picker.RoomKey == null)
            {
                return;
            }
            int index = _playable.IndexOf(picker.RoomKey);
            if (index >= 0)
            {
                _matchMap.Index = index;
                RefreshSplash();
            }
        }

        private void OpenMatch(LaunchKind kind)
        {
            _matchKind = kind;
            RefreshMatchCard();
            ShowCard(_matchCard);
            RefreshSplash();
        }

        private void RefreshMatchCard()
        {
            bool host = _matchKind == LaunchKind.Host;
            _matchCaption.IsVisible = true;
            _matchBots.IsVisible = !host;
            _matchSkill.IsVisible = !host;
            _matchName.IsVisible = host;
            _matchOnMaster.IsVisible = host;
            // Only meaningful when this machine is the one running the server:
            // a match the directory runs is on the directory's port, on the
            // directory's machine, and neither is this screen's to choose.
            _matchPort.IsVisible = host && !_matchOnMaster.On;
            _matchListed.IsVisible = host && !_matchOnMaster.On;
            _matchNote.Text = host
                ? (_matchOnMaster.On
                    ? "The directory runs the match, so nothing here needs a "
                        + "forwarded port."
                    : "Runs on this machine. Friends can only reach it if UDP on "
                        + "this port is forwarded to you.")
                : "";
            _matchNote.IsVisible = _matchNote.Text.Length > 0;
        }

        private async Task StartMatch()
        {
            if (_playable.Count == 0)
            {
                _matchNote.Text = "No multiplayer rooms were found.";
                _matchNote.IsVisible = true;
                return;
            }
            string roomKey = _matchMap.Value;
            GameMode mode = _modes[_matchMode.Index].Mode;
            var hunter = (Hunter)Enum.Parse(typeof(Hunter), _matchHunter.Value);
            _settings.RoomKey = roomKey;
            LauncherPrefs.LastHunter = hunter;
            LauncherPrefs.LastKind = (int)_matchKind;

            if (_matchKind == LaunchKind.Offline)
            {
                LauncherPrefs.Bots = _matchBots.Index;
                LauncherPrefs.BotLevel = _matchSkill.Index;
                LauncherPrefs.Save();
                Plan = new LaunchPlan
                {
                    Kind = LaunchKind.Offline,
                    Hunter = hunter,
                    PlayerName = LauncherPrefs.PlayerName,
                    RoomKey = roomKey,
                    Mode = mode,
                    Bots = _matchBots.Index,
                    BotLevel = _matchSkill.Index
                };
                Close();
                return;
            }

            string name = _matchName.Value.Length > 0 ? _matchName.Value : "Player";
            LauncherPrefs.PlayerName = name;
            LauncherPrefs.HostOnMaster = _matchOnMaster.On;
            LauncherPrefs.ListHostedGame = _matchListed.On;
            if (Int32.TryParse(_matchPort.Value, NumberStyles.Integer,
                CultureInfo.InvariantCulture, out int port) && port > 0 && port <= 65535)
            {
                LauncherPrefs.HostPort = port;
            }
            LauncherPrefs.Save();

            _matchStart.IsEnabled = false;
            _matchStart.Title = "Starting";
            _matchNote.IsVisible = true;
            bool ok;
            if (_matchOnMaster.On)
            {
                _matchNote.Text = $"Asking {LauncherPrefs.MasterHost} to run {roomKey}...";
                ok = await Task.Run(() =>
                {
                    HostedGame game = NetMasterClient.RequestGame(LauncherPrefs.MasterHost,
                        LauncherPrefs.MasterPort, roomKey, mode, timeLimit: 7 * 60,
                        pointGoal: 7, maxPlayers: PlayerEntity.SlotCapacity,
                        serverName: $"{name}'s game");
                    return game.Started
                        && NetLaunch.Join(game.Host, game.Port, name, hunter);
                });
            }
            else
            {
                _matchNote.Text = $"Starting a server on port {LauncherPrefs.HostPort}...";
                ok = await Task.Run(() => NetHostSession.StartAndJoin(
                    LauncherPrefs.HostPort, name, hunter, roomKey, mode,
                    timeLimit: 7 * 60, pointGoal: 7,
                    listing: _matchListed.On
                        ? (LauncherPrefs.MasterHost, LauncherPrefs.MasterPort, $"{name}'s game")
                        : null));
            }
            _matchStart.IsEnabled = true;
            _matchStart.Title = "Start";
            if (!ok)
            {
                NetSession.Stop();
                NetHostSession.Stop();
                _matchNote.Text = NetHostSession.LastError
                    ?? "The game could not be started. The port may be in use, or the "
                        + "directory may be down.";
                _matchNote.Foreground = GuiTheme.BadBrush;
                return;
            }
            Plan = new LaunchPlan
            {
                Kind = LaunchKind.Host,
                Hunter = hunter,
                PlayerName = name,
                RoomKey = roomKey,
                Mode = mode,
                Port = LauncherPrefs.HostPort
            };
            Close();
        }

        // -------------------------------------------------------------- browse

        private StackPanel _browseList = null!;
        private Note _browseNote = null!;

        private Control BuildBrowseCard()
        {
            var card = Card();
            _browseList = new StackPanel { Spacing = 2 };
            _browseNote = new Note("");
            var refresh = new MenuEntry("Refresh", titleSize: 15);
            refresh.Click += (_, _) => ReloadServers();

            card.Children.Add(new Caption("Find a server"));
            card.Children.Add(_browseNote);
            card.Children.Add(new ScrollViewer
            {
                Height = 300,
                Content = _browseList,
                HorizontalScrollBarVisibility =
                    Avalonia.Controls.Primitives.ScrollBarVisibility.Disabled
            });
            card.Children.Add(refresh);
            card.Children.Add(Back(() => ShowCard(_browseReturn ?? _homeCard)));
            return card;
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
            _browseList.Children.Clear();
            _browseNote.Text = $"Asking {LauncherPrefs.MasterHost}...";
            _browseNote.Foreground = GuiTheme.TextDimBrush;
            Task.Run(() =>
            {
                MasterListResult result = NetMasterClient.Query(LauncherPrefs.MasterHost,
                    LauncherPrefs.MasterPort);
                Dispatcher.UIThread.Post(() =>
                {
                    if (!result.Answered)
                    {
                        _browseNote.Text = "The directory did not answer. It may be down, "
                            + "or UDP may not reach it.";
                        _browseNote.Foreground = GuiTheme.WarmBrush;
                        return;
                    }
                    if (result.Servers.Count == 0)
                    {
                        _browseNote.Text = "The directory is up and has nobody listed.";
                        _browseNote.Foreground = GuiTheme.WarmBrush;
                        return;
                    }
                    _browseNote.Text = $"{result.Servers.Count} listed.";
                    foreach (MasterListing listing in result.Servers)
                    {
                        AddServerRow(listing);
                    }
                });
            });
        }

        private void AddServerRow(MasterListing listing)
        {
            string name = listing.ServerName.Length > 0 ? listing.ServerName : listing.Endpoint;
            var entry = new MenuEntry(name, listing.Endpoint, titleSize: 15);
            entry.Click += (_, _) =>
            {
                _onlineAddress.Value = $"{listing.Address}:{listing.Port}";
                ShowCard(_onlineCard);
            };
            _browseList.Children.Add(entry);
            Task.Run(() =>
            {
                ServerStatus status = NetStatus.Query(listing.Address, listing.Port,
                    allowJoinProbe: false);
                Dispatcher.UIThread.Post(() =>
                {
                    entry.Subtitle = status.Online
                        ? $"{listing.Endpoint} -- {Describe(status)}"
                        : $"{listing.Endpoint} -- did not answer";
                    entry.SubtitleColor = status.Online ? GuiTheme.TextDim : GuiTheme.Warm;
                });
            });
        }

        // ------------------------------------------------------------ settings

        /// <summary>
        /// The settings window, over the launcher.
        ///
        /// A window rather than another card, and the same window the pause
        /// menu opens mid-match: it is a rail of seven sections and several
        /// dozen rows, which is not a thing to page through in a 400-pixel
        /// column beside a picture.
        /// </summary>
        private async Task OpenSettings()
        {
            try
            {
                var window = new SettingsWindow(_settings);
                await window.ShowDialog(this);
            }
            catch (Exception ex)
            {
                // A failure here used to be an unhandled exception over a
                // launcher, which is a front screen that simply vanishes.
                Console.WriteLine($"[launcher] the settings could not be opened: {ex.Message}");
                return;
            }
            // The settings window writes straight into the same MenuSettings
            // and the same LauncherPrefs, so the rows here have to be re-read
            // or they would write the old values back over what was just
            // chosen there -- a name typed in the settings would last exactly
            // until the online card saved its own copy on connect.
            int index = _playable.IndexOf(_settings.RoomKey);
            if (index >= 0 && _matchMap != null)
            {
                _matchMap.Index = index;
            }
            RefreshPrefRows();
            RefreshSplash();
        }

        // --------------------------------------------------------------- shared

        private void RefreshSplash()
        {
            RefreshGameFilesState();
            string? room = _playable.Count > 0 && _matchMap != null ? _matchMap.Value : null;
            _splash.ShowRoom(room, room != null ? "Ready" : "");
        }

        /// <summary>
        /// Pick up rooms that did not exist when this window was built -- a
        /// fresh install opens with no game files, so the room list came up
        /// empty. First-time setup is the only path that changes it while
        /// this window is open.
        /// </summary>
        private void RefreshRooms()
        {
            _playable.Clear();
            foreach (string room in ThumbnailGenerator.MultiplayerRooms())
            {
                _playable.Add(room);
            }
            if (_matchMap != null)
            {
                int index = Math.Max(0, _playable.IndexOf(_settings.RoomKey));
                _matchMap.SetItems(_playable, index);
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
