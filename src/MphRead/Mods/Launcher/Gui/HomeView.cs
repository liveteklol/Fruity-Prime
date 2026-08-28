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
    /// before you can play.
    ///
    /// **This is the whole front screen on every platform.** It is a
    /// <see cref="UserControl"/> rather than a <see cref="Window"/> for one
    /// reason: Android has no windows. The desktop heads put it in
    /// <see cref="HomeWindow"/>, which is a frame and nothing else; the Android
    /// head hands the same object to Avalonia as its single view. There is no
    /// second screen to keep in step, which is the point -- a phone-shaped copy
    /// of this file was the previous arrangement and it drifted within a
    /// release.
    ///
    /// What differs between the two is layout and not content: below
    /// <see cref="_narrowWidth"/> the picture becomes a band across the top and
    /// the panel takes the full width, because a 400-pixel column beside a
    /// photograph does not fit on a phone. And the two windows this screen
    /// opens -- settings and the map grid -- are shown as an overlay where
    /// there is no second window to open, which is what
    /// <see cref="ShowOverlay"/> is.
    /// </summary>
    internal sealed class HomeView : UserControl
    {
        private readonly MenuSettings _settings;
        private readonly List<string> _playable = new();
        private readonly SplashView _splash = new();
        private readonly Panel _cards = new();
        private readonly Grid _layout = new();
        private readonly Panel _overlay;
        private readonly Border _panel;

        private readonly ProgressRow _setupProgress = new();
        private MenuEntry _setupBack = null!;
        private MenuEntry? _previewEntry;
        private MenuEntry? _previewProgress;
        private Control _homeCard = null!;
        private Control _setupCard = null!;
        private Control _onlineCard = null!;
        private Control _hostCard = null!;
        private StackPanel _hostAdventure = null!;
        private StackPanel _hostBattle = null!;
        private ChoiceRow _hostMode = null!;
        private ChoiceRow _hostWhere = null!;
        private ToggleRow _hostCoop = null!;
        private Control _browseCard = null!;
        private Control? _current;
        private Control? _browseReturn;

        private DispatcherTimer? _statusTimer;
        private CancellationTokenSource? _statusCancel;
        private bool _finished;

        /// <summary>Below this width the screen folds into one column.</summary>
        private const double _narrowWidth = 720;

        /// <summary>What the screen decided. Kind None means it was closed.</summary>
        public LaunchPlan Plan { get; private set; }

        /// <summary>
        /// Raised once, when the screen is done with: with a plan to start, or
        /// with <see cref="LaunchKind.None"/> when the answer was "quit".
        /// </summary>
        public event EventHandler<LaunchPlan>? Done;

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

        public HomeView(MenuSettings settings, IReadOnlyList<string> rooms)
        {
            _settings = settings;
            foreach (string room in rooms)
            {
                _playable.Add(room);
            }

            Background = GuiTheme.PanelBrush;

            // The cards scroll: on a phone a card of a dozen rows is taller
            // than the screen, and on the desktop this costs nothing because
            // nothing overflows.
            var scroll = new ScrollViewer
            {
                Content = _cards,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
            };
            // The credits are a section of the settings now. A wall of names
            // under every card was the first thing the eye landed on and the
            // last thing anybody needed while choosing a match.
            var stack = new Grid { RowDefinitions = new RowDefinitions("*") };
            Grid.SetRow(scroll, 0);
            stack.Children.Add(scroll);
            _panel = new Border
            {
                Background = GuiTheme.PanelBrush,
                Padding = new Thickness(22, 20, 22, 16),
                Child = stack
            };
            _updateBadge.Click += (_, _) => UpdateNow();
            _updateBadge.HorizontalAlignment = HorizontalAlignment.Left;
            _updateBadge.VerticalAlignment = VerticalAlignment.Bottom;
            _updateBadge.Margin = new Thickness(24, 0, 24, 22);
            _layout.Children.Add(_splash);
            // After the splash and in the same cell, so it draws on top of it.
            _layout.Children.Add(_updateBadge);
            _layout.Children.Add(_panel);
            ApplyLayout(narrow: false);

            _overlay = new Panel
            {
                Background = GuiTheme.InkBrush,
                IsVisible = false
            };
            var root = new Panel();
            root.Children.Add(_layout);
            root.Children.Add(_overlay);
            Content = root;
            SizeChanged += (_, e) => ApplyLayout(e.NewSize.Width < _narrowWidth);

            _homeCard = BuildHomeCard();
            _setupCard = BuildSetupCard();
            _onlineCard = BuildOnlineCard();
            _hostCard = BuildHostCard();
            _browseCard = BuildBrowseCard();

            _setupBack.IsVisible = GameFiles.Ready;
            ShowCard(GameFiles.Ready ? _homeCard : _setupCard);
            RefreshSplash();
            RefreshPreviewEntry();

            if (LauncherPrefs.AutoUpdate)
            {
                // In the background, and never blocking the window: a launcher
                // that will not draw until GitHub answers looks broken on a bad
                // connection.
                Update.Updater.CheckInBackground(update =>
                    Dispatcher.UIThread.Post(() => ShowUpdate(update)));
            }
        }

        /// <summary>
        /// Beside the picture, or under a band of it.
        ///
        /// The same controls either way -- this moves them between cells rather
        /// than building a second tree, so a card added to the screen appears
        /// on a phone without anybody having to remember to add it twice.
        /// </summary>
        private void ApplyLayout(bool narrow)
        {
            if (_laidOut && narrow == _narrow)
            {
                return;
            }
            _narrow = narrow;
            _laidOut = true;
            if (narrow)
            {
                _layout.ColumnDefinitions = new ColumnDefinitions("*");
                _layout.RowDefinitions = new RowDefinitions("Auto,*");
                _splash.Height = 150;
                _panel.Width = Double.NaN;
                Grid.SetColumn(_splash, 0);
                Grid.SetRow(_splash, 0);
                Grid.SetColumn(_updateBadge, 0);
                Grid.SetRow(_updateBadge, 0);
                Grid.SetColumn(_panel, 0);
                Grid.SetRow(_panel, 1);
                return;
            }
            _layout.ColumnDefinitions = new ColumnDefinitions("*,Auto");
            _layout.RowDefinitions = new RowDefinitions("*");
            _splash.Height = Double.NaN;
            _panel.Width = 400;
            Grid.SetColumn(_splash, 0);
            Grid.SetRow(_splash, 0);
            Grid.SetColumn(_updateBadge, 0);
            Grid.SetRow(_updateBadge, 0);
            Grid.SetColumn(_panel, 1);
            Grid.SetRow(_panel, 0);
        }

        private bool _narrow;
        private bool _laidOut;

        /// <summary>
        /// Answer a "back" gesture: the pointer's Escape and the phone's back
        /// button are the same question. True when this screen dealt with it.
        /// </summary>
        public bool GoBack()
        {
            if (_overlay.IsVisible)
            {
                // The overlay's own view owns its Escape; reaching here means
                // it declined, so take the whole thing down.
                CloseOverlay();
                return true;
            }
            if (_current != _homeCard && GameFiles.Ready)
            {
                ShowCard(_homeCard);
                return true;
            }
            return false;
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                if (!GoBack())
                {
                    Finish(default);
                }
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
        }

        /// <summary>Hand the answer back, once.</summary>
        private void Finish(LaunchPlan plan)
        {
            if (_finished)
            {
                return;
            }
            _finished = true;
            StopStatusPolling();
            Plan = plan;
            Done?.Invoke(this, plan);
        }

        /// <summary>
        /// Come back from a match and be usable again. Android keeps this view
        /// alive across a match, where the desktop builds a new one each time
        /// round <c>GuiLauncher</c>'s loop.
        /// </summary>
        public void Reset()
        {
            _finished = false;
            Plan = default;
            LauncherPrefs.Load();
            RefreshPrefRows();
            RefreshRooms();
            ShowCard(GameFiles.Ready ? _homeCard : _setupCard);
            RefreshSplash();
            RefreshPreviewEntry();
        }

        // ------------------------------------------------------------ overlays

        /// <summary>
        /// Show a view over the whole screen and wait for it to say it is done.
        ///
        /// What a modal dialog is where there are no windows to be modal to.
        /// The desktop takes the dialog path instead -- see
        /// <see cref="OpenSettings"/> -- because a real window can be moved,
        /// resized and put beside the launcher, and that is worth having where
        /// it exists.
        /// </summary>
        private Task ShowOverlay(Control view, Action<EventHandler> subscribeClosed)
        {
            var done = new TaskCompletionSource();
            // Held so that *any* way the overlay comes down finishes the wait,
            // not only the view raising Closed. GoBack takes it down directly
            // when the view declines Escape, and whoever is awaiting this would
            // otherwise wait for ever -- which on this path means the settings
            // never run the code after them (the jump to the game-files card),
            // and the continuation is never collected.
            _overlayDone = done;
            subscribeClosed((_, _) =>
            {
                CloseOverlay();
            });
            _overlay.Children.Clear();
            _overlay.Children.Add(view);
            _overlay.IsVisible = true;
            Dispatcher.UIThread.Post(() => view.Focus(), DispatcherPriority.Background);
            return done.Task;
        }

        private TaskCompletionSource? _overlayDone;

        /// <summary>
        /// The pause menu, over a running match.
        ///
        /// The platform with no windows takes this path; the desktop opens
        /// <see cref="PauseMenuWindow"/>, which is a real window over the game
        /// window. Both show the same <see cref="PauseMenuView"/>, so the menu
        /// cannot drift into being two menus.
        ///
        /// The settings come back to this rather than dropping the player into
        /// the match: they were reached from the pause menu and that is where
        /// closing them should land.
        /// </summary>
        public void ShowPauseMenu(Action onResume, Action onLeave, Action onQuit)
        {
            var view = new PauseMenuView(offerWindowMode: false);
            EventHandler? closed = null;
            void Close()
            {
                closed?.Invoke(view, EventArgs.Empty);
            }
            view.Resumed += (_, _) => { Close(); onResume(); };
            view.LeaveRequested += (_, _) => { Close(); onLeave(); };
            view.QuitRequested += (_, _) => { Close(); onQuit(); };
            // Spectating and demo recording, which this screen offers and
            // nothing was listening for.
            //
            // PauseMenuView builds the same entries on every platform -- it is
            // the desktop's pause menu, shown as an overlay here because a
            // phone has no second window to put it in -- so "Spectate",
            // "Rejoin match" and "Record demo" were all drawn, all pressable,
            // and all did nothing but close the menu, because only Resume,
            // Leave, Quit and Settings were wired up. Same handlers as
            // PauseMenuWindow's.
            view.SpectateRequested += (_, _) =>
            {
                Close();
                SpectatorMode.Start();
                onResume();
            };
            view.RejoinRequested += (_, _) =>
            {
                Close();
                SpectatorMode.Rejoin();
                onResume();
            };
            view.RecordToggleRequested += (_, _) =>
            {
                if (DemoRecorder.IsRecording)
                {
                    Console.WriteLine($"[demo] recording saved to {DemoRecorder.CurrentPath}");
                    DemoRecorder.Stop();
                }
                else
                {
                    DemoRecorder.Start();
                }
                Close();
                onResume();
            };
            view.SettingsRequested += async (_, _) =>
            {
                await OpenSettings();
                ShowPauseMenu(onResume, onLeave, onQuit);
            };
            _ = ShowOverlay(view, handler => closed += handler);
            view.FocusResume();
        }

        private void CloseOverlay()
        {
            _overlay.IsVisible = false;
            _overlay.Children.Clear();
            TaskCompletionSource? done = _overlayDone;
            _overlayDone = null;
            done?.TrySetResult();
        }

        /// <summary>True when this screen is inside a real window.</summary>
        private Window? OwnerWindow => TopLevel.GetTopLevel(this) as Window;

        private void ShowCard(Control card)
        {
            StopStatusPolling();
            _cards.Children.Clear();
            _cards.Children.Add(card);
            _current = card;
            // The splash is a map preview everywhere but the setup card, so it
            // has to be told which one is up.
            if (_matchMap != null)
            {
                RefreshSplash();
            }
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
        private MenuEntry _hostEntry = null!;
        private MenuEntry _demoEntry = null!;
        private const string _hostBlurb = "The story, or a match you run";
        private ChoiceRow _adventureSlot = null!;
        private ChoiceRow _adventureHunter = null!;
        private Note _adventureNote = null!;
        private MenuEntry _adventureStart = null!;
        private MenuEntry _adventureNew = null!;

        private Control BuildHomeCard()
        {
            var card = Card();
            _hostEntry = new MenuEntry("Host", _hostBlurb);
            _hostEntry.Click += (_, _) => OpenHost();
            _onlineEntry = new MenuEntry("Join", "Play on a server somebody else is running");
            _onlineEntry.Click += (_, _) => OpenJoin();
            _demoEntry = new MenuEntry("Watch a demo", "Replay a recorded match");
            _demoEntry.Click += async (_, _) => await ChooseDemo();
            var settings = new MenuEntry("Settings",
                "Display, audio, controls, game files, credits");
            settings.Click += async (_, _) => await OpenSettings();
            var quit = new MenuEntry("Quit");
            quit.Click += (_, _) => Finish(default);

            card.Children.Add(_hostEntry);
            card.Children.Add(_onlineEntry);
            card.Children.Add(_demoEntry);
            card.Children.Add(settings);
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
            _hostEntry.IsEnabled = ready;
            _demoEntry.IsEnabled = ready;
            // Game files moved into the settings, so there is no row here to
            // colour any more. What the front screen can still say about a
            // missing extract is that the two entries needing one are dead and
            // why -- and the setup card is what it shows instead of this one
            // until there is an extract at all.
            _hostEntry.Subtitle = ready ? _hostBlurb : GameFiles.Describe();
            _hostEntry.SubtitleColor = ready ? GuiTheme.TextDim : GuiTheme.Warm;
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
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
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
            if (GameFiles.InProcessSetup)
            {
                card.Children.Add(new Note("The unpacked files land in " + GameFiles.Root
                    + " -- this device's own folder for the app, which shows up over USB "
                    + "under Android/data. Files already copied there are found without "
                    + "picking anything.", GuiTheme.TextDim));
            }
            card.Children.Add(choose);
            // Previews are rendered here from the files that are here. A run
            // can be interrupted, and files can arrive after one, so asking
            // for the missing ones has to be possible without setting up again.
            _previewEntry = new MenuEntry("Render map previews", "", titleSize: 13)
            {
                Accent = GuiTheme.TextDim
            };
            _previewEntry.Click += async (_, _) =>
            {
                _previewEntry.IsEnabled = false;
                _previewEntry.Title = "Rendering...";
                // The subtitle as well as the log: on Android the run is
                // offscreen and in other processes, so this row is the only
                // thing on the screen that says it is happening.
                _previewProgress = _previewEntry;
                await RenderPreviews(log);
                _previewProgress = null;
                _previewEntry.IsEnabled = true;
                _previewEntry.Title = "Render map previews";
                RefreshPreviewEntry();
            };
            card.Children.Add(_previewEntry);
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
            TopLevel? top = TopLevel.GetTopLevel(this);
            if (top == null)
            {
                return;
            }
            var options = new FilePickerOpenOptions
            {
                Title = "Your Metroid Prime Hunters cartridge dump",
                AllowMultiple = false
            };
            if (!OperatingSystem.IsAndroid())
            {
                // Patterns are what Windows, Linux and the browser filter on.
                // Android filters by MIME type, and .nds has none -- inventing
                // one there produces a picker in which every file is refused.
                options.FileTypeFilter = new[]
                {
                    new FilePickerFileType("Nintendo DS ROM")
                    {
                        Patterns = new[] { "*.nds" }
                    },
                    new FilePickerFileType("Every file") { Patterns = new[] { "*" } }
                };
            }
            IReadOnlyList<IStorageFile> picked =
                await top.StorageProvider.OpenFilePickerAsync(options);
            if (picked.Count == 0)
            {
                return;
            }
            button.IsEnabled = false;
            button.Title = "Working...";
            log.Text = "";
            var progress = new SetupProgress();
            _setupProgress.IsVisible = true;
            _setupProgress.Set(0, "Starting");
            string? path = picked[0].TryGetLocalPath();
            string? scratch = null;
            if (path == null)
            {
                // Android hands back a content:// document with no path behind
                // it. Copying is the only way to give the extractor a file, and
                // it is the player's own cartridge dump, so it is copied into
                // the app's directory and deleted afterwards.
                log.Text = "Copying the file onto this device...";
                try
                {
                    scratch = Path.Combine(GameFiles.Root, "picked.nds");
                    await using (Stream source = await picked[0].OpenReadAsync())
                    await using (var target = File.Create(scratch))
                    {
                        await source.CopyToAsync(target);
                    }
                    path = scratch;
                }
                catch (Exception ex)
                {
                    log.Text = $"The file could not be read: {ex.Message}";
                    button.IsEnabled = true;
                    button.Title = "Choose your .nds file";
                    return;
                }
            }
            // Extraction takes minutes. Off the UI thread, or the screen stops
            // answering at the exact moment it is doing the one thing a fresh
            // install needs.
            bool ok = await Task.Run(() => GameFiles.RunSetup(path, line =>
                Dispatcher.UIThread.Post(() =>
                {
                    log.Text = Tail(log.Text, line);
                    if (progress.Observe(line))
                    {
                        _setupProgress.Set(progress.Fraction, progress.Stage);
                    }
                })));
            if (scratch != null)
            {
                try
                {
                    File.Delete(scratch);
                }
                catch (IOException)
                {
                    // A copy left behind is untidy, not a failure worth saying.
                }
            }
            if (ok)
            {
                await RenderPreviews(log, progress);
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

        /// <summary>Pick a recorded demo file and hand it to <see cref="MatchStart"/> as a launch plan.</summary>
        private async Task ChooseDemo()
        {
            TopLevel? top = TopLevel.GetTopLevel(this);
            if (top == null)
            {
                return;
            }
            var options = new FilePickerOpenOptions
            {
                Title = "Watch a demo",
                AllowMultiple = false,
                FileTypeFilter = new[]
                {
                    new FilePickerFileType($"{Branding.Name} demo") { Patterns = new[] { $"*{DemoFile.Extension}" } },
                    new FilePickerFileType("Every file") { Patterns = new[] { "*" } }
                }
            };
            try
            {
                string demoDir = Paths.Combine(Paths.Export, "_demos");
                if (Directory.Exists(demoDir))
                {
                    options.SuggestedStartLocation =
                        await top.StorageProvider.TryGetFolderFromPathAsync(demoDir);
                }
            }
            catch (IOException)
            {
                // No default folder is a worse first run than one with a
                // clean slate, not a reason to refuse the picker outright.
            }
            IReadOnlyList<IStorageFile> picked = await top.StorageProvider.OpenFilePickerAsync(options);
            if (picked.Count == 0)
            {
                return;
            }
            string? path = picked[0].TryGetLocalPath();
            if (path == null)
            {
                return;
            }
            // Joined here, not inside MatchStart: a failure has to land back
            // on a screen that is still open to show it on. Console.WriteLine
            // is where DemoPlayback.Join otherwise says why -- invisible on
            // the Windows build, which has no console for anything the
            // launcher starts (only a typed command gets one). Silently
            // returning to the menu with the real reason nowhere the player
            // could see it is the bug this replaces.
            _demoEntry.IsEnabled = false;
            _demoEntry.Title = "Loading...";
            bool joined = await Task.Run(() => DemoPlayback.Join(path));
            if (!joined)
            {
                _demoEntry.IsEnabled = true;
                _demoEntry.Title = "Watch a demo";
                _demoEntry.Subtitle = DemoPlayback.LastError ?? "That file could not be read as a demo.";
                _demoEntry.SubtitleColor = GuiTheme.Warm;
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

        /// <summary>
        /// Render the map previews that are missing.
        ///
        /// The pictures are made here, on this machine, from the files that
        /// were just unpacked -- worker processes on the desktop, the phone's
        /// own GL thread on Android. Nothing is downloaded and no picture ships
        /// with the program.
        /// </summary>
        private async Task RenderPreviews(Note log, SetupProgress? progress = null)
        {
            if (!ThumbnailHost.CanRender)
            {
                return;
            }
            log.Text = Tail(log.Text, "Rendering map previews...");
            await ThumbnailHost.RenderMissingAsync(line => Dispatcher.UIThread.Post(() =>
            {
                log.Text = Tail(log.Text, line);
                if (_previewProgress != null)
                {
                    _previewProgress.Subtitle = line;
                }
                if (progress != null && progress.Observe(line))
                {
                    _setupProgress.Set(progress.Fraction, progress.Stage);
                }
            }));
            RefreshSplash();
        }

        /// <summary>How many previews are still to render, or nothing to say.</summary>
        private void RefreshPreviewEntry()
        {
            if (_previewEntry == null)
            {
                return;
            }
            if (!GameFiles.Ready || !ThumbnailHost.CanRender)
            {
                _previewEntry.IsVisible = false;
                return;
            }
            int missing = ThumbnailGenerator.MissingThumbnails().Count;
            _previewEntry.IsVisible = true;
            _previewEntry.Subtitle = missing == 0
                ? "Every map has one"
                : $"{missing} still to render, from your own files";
            _previewEntry.IsEnabled = missing > 0;
        }

        /// <summary>Keep the last few lines; the extraction prints hundreds.</summary>
        private static string Tail(string? existing, string line)
        {
            // The separator matters: without it each new line was glued onto
            // the end of the previous one and the whole log read as one
            // unbroken paragraph.
            string[] lines = ((existing ?? "") + "\n" + line)
                .Split('\n', StringSplitOptions.RemoveEmptyEntries);
            return String.Join("\n", lines.Skip(Math.Max(0, lines.Length - 8)));
        }

        // -------------------------------------------------------------- online

        private ChoiceRow _onlineHunter = null!;
        private FieldRow _onlineAddress = null!;
        private Note _onlineStatus = null!;
        private MenuEntry _connect = null!;

        private Control BuildOnlineCard()
        {
            var card = Card();
            _onlineHunter = new ChoiceRow("Hunter", _hunters,
                Array.IndexOf(_hunters, LauncherPrefs.LastHunter.ToString()));
            _onlineAddress = new FieldRow("Server",
                $"{LauncherPrefs.ServerAddress}:{LauncherPrefs.ServerPort}", boxWidth: 190);
            _onlineStatus = new Note("Checking...");
            _onlineAddress.Box.LostFocus += (_, _) => QueryStatusSoon();

            _connect = new MenuEntry("Connect", titleSize: 16) { Primary = true, Height = 44 };
            _connect.Click += async (_, _) => await Connect();

            // There is no "find a server" entry any more: Join opens the list
            // itself, so this is the page a server has already been picked on
            // and all that is left to choose is the hunter. Back goes to the
            // list rather than to the front screen, because somebody who
            // picked the wrong server is trying to reach the list.
            card.Children.Add(new Caption("Join"));
            card.Children.Add(_onlineHunter);
            card.Children.Add(_onlineAddress);
            card.Children.Add(_onlineStatus);
            card.Children.Add(_connect);
            card.Children.Add(Back(OpenJoin));
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
                        _splash.ShowRoom(status.RoomKey);
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
            string name = PlayerName();
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

        // --------------------------------------------------------- offline/host

        private LaunchKind _matchKind = LaunchKind.Offline;
        private ChoiceRow _matchMap = null!;
        private ChoiceRow _matchMode = null!;
        private ChoiceRow _matchHunter = null!;
        private ChoiceRow _matchBots = null!;
        private ChoiceRow _matchSkill = null!;
        private FieldRow _matchPort = null!;
        private ToggleRow _matchOnMaster = null!;
        private ToggleRow _matchListed = null!;
        private MenuEntry _matchStart = null!;
        private Note _matchNote = null!;

        /// <summary>
        /// The battle half of the host card: everything a match of your own
        /// needs, whether it runs here or on a server this machine puts up.
        ///
        /// A group rather than a card of its own, because choosing between the
        /// story and a match is one decision and the things it decides between
        /// belong under it.
        /// </summary>
        private StackPanel BuildBattleGroup()
        {
            var card = Card();
            // Local or online is what used to be two separate entries on the
            // front screen -- "play offline" and "host a game" -- which is the
            // same match with a server in front of it. One row says which.
            _hostWhere = new ChoiceRow("Where", new[] { "Local", "Online" }, 0);
            _hostWhere.Changed += (_, _) =>
            {
                _matchKind = _hostWhere.Index == 1 ? LaunchKind.Host : LaunchKind.Offline;
                RefreshMatchCard();
            };
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
            // Not "Mode": the row above it already says Adventure or Battle, and
            // two rows called Mode under each other is a card nobody can read.
            _matchMode = new ChoiceRow("Match type", _modes.Select(m => m.Label).ToArray());
            _matchHunter = new ChoiceRow("Hunter", _hunters,
                Array.IndexOf(_hunters, LauncherPrefs.LastHunter.ToString()));
            _matchBots = new ChoiceRow("Bots",
                Enumerable.Range(0, PlayerEntity.SlotCapacity)
                    .Select(i => i.ToString(CultureInfo.InvariantCulture)).ToArray(),
                LauncherPrefs.Bots);
            _matchSkill = new ChoiceRow("Bot skill", new[] { "Easy", "Normal", "Hard" },
                LauncherPrefs.BotLevel);
            // An online match is always run by the directory, and this build
            // never opens a port on the player's own machine.
            //
            // It used to offer the choice: a toggle for who runs it, a port
            // box, and a "list it" switch. Every one of them is a question
            // about the player's router, asked of somebody who wanted to play
            // a game -- and answered wrongly it produces a server nobody can
            // reach and no way of telling why from in here. Running one on
            // your own machine is a real thing to want, and it has its own
            // program: the dedicated server, which can be pointed at a port
            // and left running, rather than a phone that goes in a pocket.
            //
            // The rows are kept and set rather than deleted because the code
            // that starts a match reads them, and one place deciding this
            // beats the same constant written in four.
            _matchPort = new FieldRow("Port",
                LauncherPrefs.HostPort.ToString(CultureInfo.InvariantCulture), boxWidth: 90);
            _matchOnMaster = new ToggleRow("Let the directory run it", on: true);
            _matchListed = new ToggleRow("List it so others can find it", on: true);
            _matchNote = new Note("");
            _matchStart = new MenuEntry("Start", titleSize: 16) { Primary = true, Height = 44 };
            _matchStart.Click += async (_, _) => await StartMatch();

            card.Children.Add(_hostWhere);
            card.Children.Add(_matchMap);
            card.Children.Add(browseMaps);
            card.Children.Add(_matchMode);
            card.Children.Add(_matchHunter);
            card.Children.Add(_matchBots);
            card.Children.Add(_matchSkill);
            card.Children.Add(_matchNote);
            card.Children.Add(_matchStart);
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
        private StackPanel BuildAdventureGroup()
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

            card.Children.Add(_adventureSlot);
            card.Children.Add(_adventureNote);
            card.Children.Add(_adventureHunter);
            card.Children.Add(_adventureStart);
            card.Children.Add(_adventureNew);
            return card;
        }

        /// <summary>
        /// The whole of Host: the story or a match, and whichever one is
        /// chosen showing its own options underneath.
        /// </summary>
        private Control BuildHostCard()
        {
            var card = Card();
            _hostMode = new ChoiceRow("Mode", new[] { "Adventure", "Battle" }, 0);
            _hostMode.Changed += (_, _) => RefreshHostCard();
            // Announced rather than hidden. The work is not done, and a menu
            // that simply does not mention co-op tells somebody looking for it
            // that it was never considered; this says it is coming and refuses
            // to pretend it works.
            _hostCoop = new ToggleRow("Online co-op (coming soon!)", false);
            _hostCoop.Changed += (_, _) => RefreshAdventureCard();
            _hostAdventure = BuildAdventureGroup();
            _hostAdventure.Children.Insert(0, _hostCoop);
            _hostBattle = BuildBattleGroup();
            card.Children.Add(new Caption("Host"));
            card.Children.Add(_hostMode);
            card.Children.Add(_hostAdventure);
            card.Children.Add(_hostBattle);
            card.Children.Add(Back(() => ShowCard(_homeCard)));
            return card;
        }

        /// <summary>Open Host on the story, which is what it defaults to.</summary>
        private void OpenHost()
        {
            RefreshHostCard();
            ShowCard(_hostCard);
            RefreshSplash();
        }

        private void RefreshHostCard()
        {
            bool adventure = _hostMode.Index == 0;
            _hostAdventure.IsVisible = adventure;
            _hostBattle.IsVisible = !adventure;
            if (adventure)
            {
                RefreshAdventureCard();
            }
            else
            {
                _matchKind = _hostWhere.Index == 1 ? LaunchKind.Host : LaunchKind.Offline;
                RefreshMatchCard();
            }
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
            // Nothing starts while co-op is ticked: the mode does not exist
            // yet, and a button that starts a single-player game after being
            // asked for a co-op one is worse than a button that will not go.
            bool ready = !_hostCoop.On;
            _adventureStart.IsEnabled = ready;
            _adventureNew.IsEnabled = ready;
            if (!ready)
            {
                _adventureNote.Text = "Online co-op is not built yet. Untick it to play.";
            }
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

        /// <summary>
        /// Re-read the rows that show a launcher preference. The settings
        /// window owns the same values, so anything it changed has to reach the
        /// cards that were built before it opened.
        /// </summary>
        private void RefreshPrefRows()
        {
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
            // Over this screen, not in a window of its own.
            //
            // It used to open a dialog on the desktop and overlay only on
            // Android. A second window to pick a map from is a window to find,
            // move out of the way and close again, for a choice that belongs
            // to the card it came from -- and it covered the map picture the
            // front screen exists to show. The overlay fills the launcher
            // until the choice is made and then it is gone.
            var view = new MapPickerView(_playable, _matchMap.Value);
            await ShowOverlay(view, handler => view.Closed += handler);
            if (view.RoomKey == null)
            {
                return;
            }
            int index = _playable.IndexOf(view.RoomKey);
            if (index >= 0)
            {
                _matchMap.Index = index;
                RefreshSplash();
            }
        }

        /// <summary>
        /// The name to play under, which the settings own now.
        ///
        /// It used to be a row on the online card and another on the match
        /// card, which is the same answer asked for twice and two places for
        /// it to disagree.
        /// </summary>
        private static string PlayerName()
        {
            string name = LauncherPrefs.PlayerName.Trim();
            return name.Length > 0 ? name : "Player";
        }

        /// <summary>
        /// Join: the list of servers, straight away.
        ///
        /// It used to be a page with an address box and a "find a server"
        /// entry under it, which is one press between the player and the only
        /// answer most people have. The list is the page now; the address box
        /// is on the page a server has been chosen on, for anybody typing one
        /// in by hand.
        /// </summary>
        private void OpenJoin()
        {
            _browseReturn = _homeCard;
            ShowCard(_browseCard);
            ReloadServers();
        }

        private void RefreshMatchCard()
        {
            bool host = _matchKind == LaunchKind.Host;
            _matchBots.IsVisible = !host;
            _matchSkill.IsVisible = !host;
            _matchOnMaster.On = true;
            // Only meaningful when this machine is the one running the server:
            // a match the directory runs is on the directory's port, on the
            // directory's machine, and neither is this screen's to choose.
            _matchListed.On = true;
            _matchNote.Text = host
                ? "The directory runs the match, so nothing here needs a "
                    + "forwarded port. To run one on your own machine, use the "
                    + "dedicated server."
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
                Finish(new LaunchPlan
                {
                    Kind = LaunchKind.Offline,
                    Hunter = hunter,
                    PlayerName = LauncherPrefs.PlayerName,
                    RoomKey = roomKey,
                    Mode = mode,
                    Bots = _matchBots.Index,
                    BotLevel = _matchSkill.Index
                });
                return;
            }

            string name = PlayerName();
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

            card.Children.Add(new Caption("Join"));
            card.Children.Add(_browseNote);
            // Headings over the list, outside the scroll viewer so they stay
            // put while it scrolls -- which is the whole point of having them.
            card.Children.Add(new ServerHeader());
            card.Children.Add(new ScrollViewer
            {
                Height = 300,
                Content = _browseList,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
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
            var row = new ServerRow(name, listing.Endpoint);
            ToolTip.SetTip(row, listing.Endpoint);
            row.Clicked += (_, _) =>
            {
                _onlineAddress.Value = $"{listing.Address}:{listing.Port}";
                ShowCard(_onlineCard);
                QueryStatusSoon();
            };
            _browseList.Children.Add(row);
            Task.Run(() =>
            {
                ServerStatus status = NetStatus.Query(listing.Address, listing.Port,
                    allowJoinProbe: false);
                Dispatcher.UIThread.Post(() => row.SetStatus(status));
            });
        }

        // ------------------------------------------------------------ settings

        /// <summary>
        /// The settings, over the launcher.
        ///
        /// A window where there are windows -- the same one the pause menu
        /// opens mid-match -- and the same view as a full-screen overlay where
        /// there are not. Either way it is <see cref="SettingsView"/>: a rail
        /// of eight sections and several dozen rows, which is not a thing to
        /// page through in a 400-pixel column beside a picture.
        /// </summary>
        private async Task OpenSettings()
        {
            try
            {
                var view = new SettingsView(_settings);
                // Noted while the settings are up and acted on once they are
                // down: showing a card behind a window that is still open is
                // how you end up on a screen you cannot see.
                bool gameFiles = false;
                view.GameFilesRequested += (_, _) => gameFiles = true;
                // The same overlay the map picker uses, for the same reason.
                // The pause menu still opens SettingsWindow: it is over a game
                // window, and there is no front screen there to lay this on.
                await ShowOverlay(view, handler => view.Closed += handler);
                if (gameFiles)
                {
                    ShowCard(_setupCard);
                    return;
                }
            }
            catch (Exception ex)
            {
                // A failure here used to be an unhandled exception over a
                // launcher, which is a front screen that simply vanishes.
                Console.WriteLine($"[launcher] the settings could not be opened: {ex.Message}");
                return;
            }
            // The settings view writes straight into the same MenuSettings
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
            // Nothing from the game while the game is still being unpacked.
            // The setup card is up for the whole of the first run -- the
            // extraction and then the preview rendering -- and map pictures
            // appearing one at a time beside it is the generation being
            // watched, which is the thing it was moved offscreen to avoid.
            if (ReferenceEquals(_current, _setupCard))
            {
                room = null;
            }
            _splash.ShowRoom(room);
        }

        /// <summary>
        /// Pick up rooms that did not exist when this screen was built -- a
        /// fresh install opens with no game files, so the room list came up
        /// empty. First-time setup is the only path that changes it while
        /// this screen is up.
        /// </summary>
        private void RefreshRooms()
        {
            if (!GameFiles.Ready)
            {
                return;
            }
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
