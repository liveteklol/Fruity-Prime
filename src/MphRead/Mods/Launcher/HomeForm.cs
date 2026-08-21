using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.Versioning;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using MphRead.Entities;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// The front screen: a picture, four things you can do, and nothing to
    /// read before you can play.
    ///
    /// The settings window that used to open first is still here, one button
    /// away, and still owns everything this screen does not: cheats, audio,
    /// bugfix toggles, point goals. What moved out of it is the handful of
    /// choices somebody actually makes each time they play -- online or
    /// offline, which hunter, which map -- because putting those on the same
    /// screen as forty checkboxes is what made the old launcher look like a
    /// configuration dialog rather than a game.
    ///
    /// Everything here is custom-painted. WinForms will not draw a dark combo
    /// box or a dark tab strip, and a half-dark window looks broken in a way
    /// that a consistently plain one does not.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class HomeForm : Form
    {
        private readonly LauncherTheme _theme;
        private readonly MenuSettings _settings;
        private readonly IReadOnlyList<string> _rooms;
        private readonly List<string> _playable = new();
        private readonly SplashPanel _splash;
        private readonly Panel _panel = new();

        private FlowLayoutPanel _homeCard = null!;
        private FlowLayoutPanel _browseCard = null!;
        private FlowLayoutPanel _onlineCard = null!;
        private FlowLayoutPanel _matchCard = null!;
        private FlowLayoutPanel _setupCard = null!;
        private Panel _homeSpacer = null!;
        private Panel _onlineSpacer = null!;
        private Panel _matchSpacer = null!;
        /// <summary>
        /// Rows this screen has deliberately taken out of a card. Not read
        /// from Control.Visible: that reports the whole parent chain, so
        /// while the window is still being built -- which is when the first
        /// layout runs -- every control on it claims to be invisible and the
        /// spacers grow to fill a card that measured as empty.
        /// </summary>
        private readonly HashSet<Control> _collapsed = new();

        private MenuButton _onlineButton = null!;
        private MenuButton _offlineButton = null!;
        private MenuButton _hostButton = null!;
        private MenuButton _filesButton = null!;
        private MenuButton _setupButton = null!;
        private Label _setupStatus = null!;
        private Label _setupLog = null!;
        private bool _settingUp;
        private FieldBox _nameField = null!;
        private FieldBox _serverField = null!;
        private HunterPicker _onlineHunter = null!;
        private Label _statusLabel = null!;
        private MenuButton _connectButton = null!;

        private Caption _matchTitle = null!;
        private ChoiceRow _mapRow = null!;
        private ChoiceRow _modeRow = null!;
        private ChoiceRow _botsRow = null!;
        private ChoiceRow _skillRow = null!;
        private FieldBox _portField = null!;
        private ChoiceRow _hostWhereRow = null!;
        private ToggleRow _listPublicly = null!;
        private Label _hostNote = null!;
        private Label _matchStatus = null!;
        private HunterPicker _matchHunter = null!;
        private MenuButton _startButton = null!;

        /// <summary>One server on the browse card, and what this machine found out about it.</summary>
        private sealed class ServerRow
        {
            public MasterListing Listing;
            public ServerStatus Status;
            public bool Probed;
        }

        private FlowLayoutPanel _serverList = null!;
        private Label _browseStatus = null!;
        private MenuButton _browseRefresh = null!;
        private FieldBox _browseAddress = null!;
        private MenuButton _browseJoin = null!;
        private readonly List<ServerRow> _rows = new();
        private CancellationTokenSource? _browseWork;
        /// <summary>Where Back goes from the list: the front screen, or the card that opened it.</summary>
        private Control _browseReturn = null!;

        private readonly System.Windows.Forms.Timer _statusTimer = new();
        private ServerStatus _status;
        private bool _statusBusy;
        private bool _statusQueryWorks;
        private int _statusPolls;
        private bool _connecting;
        private bool _hostMode;
        /// <summary>Set once the control tree exists: setting ClientSize in the
        /// constructor raises Resize before there is anything to lay out.</summary>
        private bool _built;

        public LaunchPlan Plan { get; private set; }

        private const int _panelWidth = 392;

        private static readonly (string Label, GameMode Mode)[] _modes =
        {
            // "Recommended" is auto-select: several modes only work on maps
            // whose entity layout was drawn for them, and picking one on the
            // wrong map produces a match with no objectives in it.
            ("Recommended", GameMode.None),
            ("Battle", GameMode.Battle),
            ("Battle Teams", GameMode.BattleTeams),
            ("Survival", GameMode.Survival),
            ("Survival Teams", GameMode.SurvivalTeams),
            ("Prime Hunter", GameMode.PrimeHunter),
            ("Capture", GameMode.Capture),
            ("Bounty", GameMode.Bounty),
            ("Bounty Teams", GameMode.BountyTeams),
            ("Nodes", GameMode.Nodes),
            ("Nodes Teams", GameMode.NodesTeams),
            ("Defender", GameMode.Defender),
            ("Defender Teams", GameMode.DefenderTeams)
        };

        public HomeForm(MenuSettings settings, IReadOnlyList<string> rooms)
        {
            _settings = settings;
            _rooms = rooms;
            _theme = new LauncherTheme(DeviceDpi);
            foreach (string room in rooms)
            {
                if (Playable(room))
                {
                    _playable.Add(room);
                }
            }

            Text = "MphRead";
            FormBorderStyle = FormBorderStyle.None;
            // Sizes here are already multiplied by this monitor's scaling, so
            // WinForms must not scale them a second time.
            AutoScaleMode = AutoScaleMode.None;
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = LauncherTheme.Ink;
            ForeColor = LauncherTheme.Text;
            KeyPreview = true;
            Rectangle work = Screen.PrimaryScreen?.WorkingArea ?? new Rectangle(0, 0, 1280, 800);
            ClientSize = new Size(
                Math.Min(_theme.S(1040), work.Width - _theme.S(40)),
                Math.Min(_theme.S(600), work.Height - _theme.S(40)));

            _splash = new SplashPanel(_theme, _playable);
            _splash.MouseDown += (_, e) =>
            {
                // No title bar, so a drag on the picture moves the window --
                // the same gesture the frame would have offered.
                if (e.Button == MouseButtons.Left)
                {
                    WindowChrome.DragFrom(this);
                }
            };
            Controls.Add(_splash);

            _panel.BackColor = LauncherTheme.Panel;
            Controls.Add(_panel);

            BuildHomeCard();
            BuildBrowseCard();
            BuildOnlineCard();
            BuildMatchCard();
            BuildSetupCard();
            BuildWindowButtons();
            _built = true;
            LayoutChildren();

            _statusTimer.Interval = 5000;
            _statusTimer.Tick += (_, _) => PollStatus();
            _statusTimer.Start();
            PollStatus();
            RefreshGameFiles();
            // Nothing can be played without the game's own files, so a fresh
            // install opens on the one screen that can fix that rather than on
            // a menu of things that would all fail.
            ShowCard(GameFiles.Ready ? _homeCard : _setupCard);
        }

        /// <summary>
        /// Rooms a match can actually be played in.
        ///
        /// The six First Hunt "biodefense chamber" rooms are listed as
        /// multiplayer and carry no player spawn points at all, so a match
        /// there places nobody -- the map audit reports 0/8 for every one of
        /// them. They are survival rooms; offering them on a screen aimed at
        /// somebody who has never played this would be a trap.
        /// </summary>
        private static bool Playable(string roomKey)
        {
            return !roomKey.StartsWith("biodefense", StringComparison.OrdinalIgnoreCase);
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            WindowChrome.RoundCorners(Handle);
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            // The first layout ran before the window existed, when the fonts
            // had not yet been asked how tall they are.
            LayoutCards();
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            LayoutChildren();
        }

        private void LayoutChildren()
        {
            if (!_built)
            {
                return;
            }
            int panel = Math.Min(_theme.S(_panelWidth), ClientSize.Width / 2);
            _splash.SetBounds(0, 0, ClientSize.Width - panel, ClientSize.Height);
            _panel.SetBounds(ClientSize.Width - panel, 0, panel, ClientSize.Height);
            LayoutCards();
        }

        private FlowLayoutPanel NewCard()
        {
            var card = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoScroll = false,
                BackColor = LauncherTheme.Panel,
                // Top padding clears the minimise and close buttons, which
                // float above every card.
                Padding = new Padding(_theme.S(24), _theme.S(44), _theme.S(24), _theme.S(18)),
                Visible = false
            };
            _panel.Controls.Add(card);
            return card;
        }

        private int Inner => _theme.S(_panelWidth) - _theme.S(48);

        private T Add<T>(FlowLayoutPanel card, T control, int gap = 8) where T : Control
        {
            control.Width = Inner;
            control.Margin = new Padding(0, 0, 0, _theme.S(gap));
            card.Controls.Add(control);
            return control;
        }

        private void BuildWindowButtons()
        {
            var close = new GlyphButton(_theme, GlyphButton.Glyph.Close);
            close.Click += (_, _) => Close();
            var minimise = new GlyphButton(_theme, GlyphButton.Glyph.Minimise);
            minimise.Click += (_, _) => WindowState = FormWindowState.Minimized;
            Controls.Add(close);
            Controls.Add(minimise);
            close.BringToFront();
            minimise.BringToFront();
            void Place()
            {
                int right = ClientSize.Width - _theme.S(10);
                close.Location = new Point(right - close.Width, _theme.S(10));
                minimise.Location = new Point(close.Left - minimise.Width, _theme.S(10));
            }
            Place();
            Resize += (_, _) => Place();
        }

        private void BuildHomeCard()
        {
            _homeCard = NewCard();
            _homeSpacer = new Panel { Height = _theme.S(10), Margin = Padding.Empty };
            Add(_homeCard, _homeSpacer, gap: 0);

            _onlineButton = Add(_homeCard, new MenuButton(_theme, "Play online",
                "Looking for the server..."));
            _onlineButton.Click += (_, _) => ShowOnline();

            _offlineButton = Add(_homeCard, new MenuButton(_theme, "Play offline",
                "A match against bots, on any map"));
            _offlineButton.Click += (_, _) => ShowMatch(host: false);

            _hostButton = Add(_homeCard, new MenuButton(_theme, "Host a game",
                "Friends join this machine"));
            _hostButton.Click += (_, _) => ShowMatch(host: true);

            MenuButton settings = Add(_homeCard, new MenuButton(_theme, "Settings",
                "Match rules, audio, cheats, previews"));
            settings.Click += (_, _) => OpenSettings();

            _filesButton = Add(_homeCard, new MenuButton(_theme, "Game files",
                GameFiles.Describe()));
            _filesButton.Click += (_, _) => ShowCard(_setupCard);

            MenuButton quit = Add(_homeCard, new MenuButton(_theme, "Quit"));
            quit.Click += (_, _) => Close();
        }

        /// <summary>
        /// Give a card's flexible spacer whatever height is left over.
        ///
        /// A FlowLayoutPanel stacks from the top and stops; without this the
        /// home menu would sit against the window's top edge and each card's
        /// main action would land at a different height depending on how many
        /// rows that card happens to show.
        /// </summary>
        private void LayoutSpacer(FlowLayoutPanel? card, Panel? spacer, float share)
        {
            if (card == null || spacer == null)
            {
                return;
            }
            int content = 0;
            foreach (Control control in card.Controls)
            {
                if (control != spacer && !_collapsed.Contains(control))
                {
                    content += control.Height + control.Margin.Vertical;
                }
            }
            int free = _panel.ClientSize.Height - card.Padding.Vertical - content;
            spacer.Height = Math.Max(_theme.S(4), (int)(free * share));
        }

        private void LayoutCards()
        {
            LayoutSpacer(_homeCard, _homeSpacer, 0.5f);
            LayoutSpacer(_onlineCard, _onlineSpacer, 1f);
            LayoutSpacer(_matchCard, _matchSpacer, 1f);
            // The list takes whatever the fixed rows around it leave, which is
            // the same arithmetic the spacers do -- a scrolling list is a
            // spacer that happens to have things in it.
            LayoutSpacer(_browseCard, _serverList, 1f);
        }

        private void BuildSetupCard()
        {
            _setupCard = NewCard();
            AddBack(_setupCard);
            Add(_setupCard, new Caption(_theme, "Game files", 26,
                LauncherTheme.Text, 1, display: true), gap: 10);

            var note = new Label
            {
                AutoSize = false,
                Height = _theme.S(84),
                ForeColor = LauncherTheme.TextDim,
                BackColor = LauncherTheme.Panel,
                Font = _theme.Body(_theme.S(12)),
                Text = "MphRead plays the maps, models and sounds out of your own "
                    + "copy of Metroid Prime Hunters. Point it at the .nds file once "
                    + "and it unpacks what it needs next to this program. Nothing is "
                    + "downloaded, and the file itself is left alone."
            };
            Add(_setupCard, note, gap: 12);

            _setupStatus = new Label
            {
                AutoSize = false,
                Height = _theme.S(34),
                ForeColor = LauncherTheme.TextDim,
                BackColor = LauncherTheme.Panel,
                Font = _theme.Body(_theme.S(12), FontStyle.Bold),
                Text = GameFiles.Describe()
            };
            Add(_setupCard, _setupStatus, gap: 6);

            _setupLog = new Label
            {
                AutoSize = false,
                Height = _theme.S(60),
                ForeColor = Color.FromArgb(110, 120, 140),
                BackColor = LauncherTheme.Panel,
                Font = _theme.Body(_theme.S(11)),
                Text = ""
            };
            Add(_setupCard, _setupLog, gap: 6);

            Add(_setupCard, new Panel { Height = _theme.S(4) }, gap: 0);
            _setupButton = Add(_setupCard, new MenuButton(_theme, "Choose your .nds file",
                titleSize: 17));
            _setupButton.Primary = true;
            _setupButton.Height = _theme.S(46);
            _setupButton.Click += (_, _) => ChooseRom();
        }

        /// <summary>
        /// Pick a ROM and unpack it, with the output on screen.
        ///
        /// Upstream's setup runs in a child process (see GameFiles): it talks
        /// to a console this window does not have, and it waits for a keypress
        /// when it does not like the file it was given.
        /// </summary>
        private void ChooseRom()
        {
            if (_settingUp)
            {
                return;
            }
            string rom;
            using (var dialog = new OpenFileDialog
            {
                Title = "Choose your Metroid Prime Hunters .nds file",
                Filter = "Nintendo DS ROM (*.nds)|*.nds|All files (*.*)|*.*",
                CheckFileExists = true
            })
            {
                if (dialog.ShowDialog(this) != DialogResult.OK)
                {
                    return;
                }
                rom = dialog.FileName;
            }
            _settingUp = true;
            _setupButton.Enabled = false;
            _setupButton.Text = "Unpacking";
            _setupStatus.ForeColor = LauncherTheme.Accent;
            _setupStatus.Text = $"Unpacking {Path.GetFileName(rom)}...";
            _setupLog.Text = "";

            Task.Run(() => GameFiles.RunSetup(rom, line => ApplyOnUi(() =>
            {
                // The last line only: this is a progress indicator, not a log
                // viewer, and the whole of it is on the console anyway.
                _setupLog.Text = line.Length > 120 ? line[..120] : line;
            }))).ContinueWith(task =>
            {
                bool ok = task.IsCompletedSuccessfully && task.Result;
                ApplyOnUi(() =>
                {
                    _settingUp = false;
                    _setupButton.Enabled = true;
                    _setupButton.Text = ok ? "Choose a different file" : "Choose your .nds file";
                    if (ok)
                    {
                        GameFiles.ApplyPaths();
                        _setupLog.Text = "";
                    }
                    RefreshGameFiles();
                    _setupStatus.ForeColor = ok ? LauncherTheme.Good : LauncherTheme.Bad;
                    _setupStatus.Text = ok
                        ? $"{GameFiles.Describe()} -- you can play now."
                        : "That file could not be used. It has to be a Metroid Prime "
                            + "Hunters or First Hunt cartridge dump.";
                    if (ok)
                    {
                        ShowCard(_homeCard);
                    }
                });
            }, TaskScheduler.Default);
        }

        /// <summary>Enable or grey out everything that needs the game's files.</summary>
        private void RefreshGameFiles()
        {
            bool ready = GameFiles.Ready;
            _onlineButton.Enabled = ready;
            _offlineButton.Enabled = ready;
            _hostButton.Enabled = ready;
            _filesButton.Subtitle = GameFiles.Describe();
            _filesButton.SubtitleColor = ready ? LauncherTheme.TextDim : LauncherTheme.Warm;
            if (!ready)
            {
                _onlineButton.Subtitle = "Set up your game files first";
                _offlineButton.Subtitle = "Set up your game files first";
                _hostButton.Subtitle = "Set up your game files first";
            }
            else
            {
                _offlineButton.Subtitle = "A match against bots, on any map";
                _hostButton.Subtitle = "Friends join this machine";
            }
        }

        /// <summary>
        /// The server list, as a card on the front screen rather than a window
        /// over it.
        ///
        /// It is the first thing Play online shows, because the address is the
        /// one thing a new player cannot invent. The rows come from the
        /// directory; what each one says about itself is asked of the server
        /// directly by this machine, which is also the only way to get a
        /// latency worth showing and the only proof that this player can reach
        /// it at all.
        /// </summary>
        private void BuildBrowseCard()
        {
            _browseCard = NewCard();
            var back = new MenuButton(_theme, "Back", titleSize: 13)
            {
                Height = _theme.S(26)
            };
            back.Accent = LauncherTheme.TextDim;
            back.Click += (_, _) => ShowCard(_browseReturn ?? _homeCard);
            Add(_browseCard, back, gap: 10);
            Add(_browseCard, new Caption(_theme, "Servers", 26,
                LauncherTheme.Text, 1, display: true), gap: 8);

            _browseStatus = new Label
            {
                AutoSize = false,
                Height = _theme.S(34),
                ForeColor = LauncherTheme.TextDim,
                BackColor = LauncherTheme.Panel,
                Font = _theme.Body(_theme.S(12)),
                Text = ""
            };
            Add(_browseCard, _browseStatus, gap: 6);

            // Its own scroll area inside the card: the card itself does not
            // scroll, and the address field below has to stay reachable however
            // many servers are up.
            _serverList = new FlowLayoutPanel
            {
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoScroll = true,
                BackColor = LauncherTheme.Panel,
                Height = _theme.S(240)
            };
            Add(_browseCard, _serverList, gap: 8);

            _browseRefresh = Add(_browseCard, new MenuButton(_theme, "Refresh",
                titleSize: 13), gap: 10);
            _browseRefresh.Height = _theme.S(28);
            _browseRefresh.Accent = LauncherTheme.TextDim;
            _browseRefresh.Click += (_, _) => ReloadServers();

            // Not every server is on the list: one can be handed over by a
            // friend, kept unlisted on purpose, or sitting behind a directory
            // this machine cannot reach. In all three the address is the only
            // thing the player has, so it belongs on this screen.
            Add(_browseCard, new Caption(_theme, "Or type an address", 11,
                LauncherTheme.TextDim, 1, display: false), gap: 4);
            _browseAddress = Add(_browseCard, new FieldBox(_theme, "Address"), gap: 4);
            _browseAddress.Placeholder = "address or address:port";
            _browseAddress.ValueChanged += (_, _) =>
            {
                _browseJoin.Enabled = _browseAddress.Value.Length > 0;
                _browseJoin.Invalidate();
            };
            _browseJoin = Add(_browseCard, new MenuButton(_theme, "Use this address",
                titleSize: 14), gap: 0);
            _browseJoin.Height = _theme.S(36);
            _browseJoin.Enabled = false;
            _browseJoin.Click += (_, _) => UseTypedAddress();
        }

        /// <summary>Open the list, remembering what to go back to.</summary>
        private void ShowBrowse(Control returnTo)
        {
            _browseReturn = returnTo;
            ShowCard(_browseCard);
            ReloadServers();
        }

        private void UseTypedAddress()
        {
            string typed = _browseAddress.Value.Trim();
            if (typed.Length > 0)
            {
                ChooseServer(typed);
            }
        }

        /// <summary>
        /// Take an address and move on to the name-and-hunter half.
        ///
        /// The choice is not joined outright: the hunter and the name are part
        /// of joining too, and a list that connected the moment somebody
        /// clicked a row would skip both.
        /// </summary>
        private void ChooseServer(string endpoint)
        {
            _browseWork?.Cancel();
            _serverField.Value = endpoint;
            (string address, int port) = ParseServer(endpoint);
            LauncherPrefs.ServerAddress = address;
            LauncherPrefs.ServerPort = port;
            _status = ServerStatus.Offline("Checking...");
            ShowOnlineCard();
        }

        private void ReloadServers()
        {
            _browseWork?.Cancel();
            _browseWork = new CancellationTokenSource();
            CancellationToken token = _browseWork.Token;
            _rows.Clear();
            RebuildServerList();
            _browseRefresh.Enabled = false;
            _browseStatus.Text = $"Asking {LauncherPrefs.MasterHost} who is up...";
            string host = LauncherPrefs.MasterHost;
            int port = LauncherPrefs.MasterPort;
            Task.Run(() =>
            {
                try
                {
                    BrowseWork(host, port, token);
                }
                catch (OperationCanceledException)
                {
                    // Refresh pressed again, or the card left, mid-probe.
                }
                catch (Exception ex)
                {
                    ApplyOnUi(() =>
                    {
                        _browseStatus.Text = $"Could not read the list: {ex.Message}";
                        _browseRefresh.Enabled = true;
                    });
                }
            }, token);
        }

        /// <summary>
        /// Ask the directory, then ask each server it named. Off the UI
        /// thread, reporting back at every step -- a list that shows nothing
        /// for two seconds and then everything reads as broken.
        /// </summary>
        private void BrowseWork(string host, int port, CancellationToken token)
        {
            MasterListResult result = NetMasterClient.Query(host, port);
            if (token.IsCancellationRequested)
            {
                return;
            }
            IReadOnlyList<MasterListing> listings = result.Servers;
            ApplyOnUi(() =>
            {
                foreach (MasterListing listing in listings)
                {
                    _rows.Add(new ServerRow { Listing = listing });
                }
                if (listings.Count > 0)
                {
                    _browseStatus.Text = $"{listings.Count} server(s) listed \u00B7 checking each one...";
                }
                else if (result.Answered)
                {
                    _browseStatus.Text = "Nobody is listed right now. "
                        + "Type an address below to join one anyway.";
                }
                else
                {
                    // Worth the distinction: "nobody is playing" and "this
                    // launcher cannot reach the directory" are the same empty
                    // list and completely different problems.
                    _browseStatus.Text = $"No answer from {host}. "
                        + "Type an address below, or check that UDP reaches it.";
                }
                RebuildServerList();
            });
            if (listings.Count == 0)
            {
                ApplyOnUi(() =>
                {
                    _browseRefresh.Enabled = true;
                    _browseAddress.SelectField();
                });
                return;
            }
            // In parallel: the whole point of measuring the round trip here is
            // that some of these are far away, and a serial sweep at a second
            // each is not a screen anybody waits for.
            Parallel.ForEach(listings, new ParallelOptions
            {
                MaxDegreeOfParallelism = 8,
                CancellationToken = token
            }, listing =>
            {
                ServerStatus status = NetStatus.Query(listing.Address, listing.Port,
                    allowJoinProbe: false, timeoutMs: 1200);
                if (token.IsCancellationRequested)
                {
                    return;
                }
                ApplyOnUi(() =>
                {
                    ServerRow? row = _rows.Find(r => r.Listing.Address == listing.Address
                        && r.Listing.Port == listing.Port);
                    if (row == null)
                    {
                        return;
                    }
                    row.Status = status;
                    row.Probed = true;
                    RebuildServerList();
                });
            });
            if (token.IsCancellationRequested)
            {
                return;
            }
            ApplyOnUi(() =>
            {
                int reachable = _rows.Count(r => r.Status.Online);
                _browseStatus.Text = $"{reachable} of {_rows.Count} reachable from here.";
                _browseRefresh.Enabled = true;
            });
        }

        /// <summary>
        /// Redraw the list, best server first: people, then latency. An empty
        /// server with a perfect ping is not what somebody opening a server
        /// browser is looking for, and a busy one at 200 ms usually is.
        /// Servers that did not answer sink rather than disappear -- "listed
        /// but unreachable from here" is information, usually a firewall.
        /// </summary>
        private void RebuildServerList()
        {
            _serverList.SuspendLayout();
            _serverList.Controls.Clear();
            int width = Math.Max(_theme.S(200),
                _serverList.ClientSize.Width - SystemInformation.VerticalScrollBarWidth - _theme.S(4));
            foreach (ServerRow row in _rows
                .OrderByDescending(r => !r.Probed || r.Status.Online)
                .ThenByDescending(r => r.Status.Players)
                .ThenBy(r => r.Status.Latency < 0 ? Int32.MaxValue : r.Status.Latency))
            {
                string name = row.Probed && row.Status.ServerName.Length > 0
                    ? row.Status.ServerName
                    : row.Listing.ServerName.Length > 0
                        ? row.Listing.ServerName
                        : row.Listing.Endpoint;
                var button = new MenuButton(_theme, name, DescribeRow(row), titleSize: 14)
                {
                    Width = width,
                    Height = _theme.S(50),
                    Margin = new Padding(0, 0, 0, _theme.S(4)),
                    Accent = TintRow(row)
                };
                string endpoint = row.Listing.Endpoint;
                button.Click += (_, _) => ChooseServer(endpoint);
                _serverList.Controls.Add(button);
            }
            if (_rows.Count == 0)
            {
                _serverList.Controls.Add(new Label
                {
                    Text = "No servers on the list.",
                    AutoSize = false,
                    Width = width,
                    Height = _theme.S(22),
                    ForeColor = LauncherTheme.TextDim,
                    BackColor = LauncherTheme.Panel,
                    Font = _theme.Body(_theme.S(12))
                });
            }
            _serverList.ResumeLayout();
        }

        private static Color TintRow(ServerRow row)
        {
            if (!row.Probed)
            {
                return LauncherTheme.TextDim;
            }
            if (!row.Status.Online)
            {
                return LauncherTheme.Bad;
            }
            // The scoreboard's ping thresholds, so a colour means the same
            // thing before a match as during one.
            if (row.Status.Latency < 0 || row.Status.Latency >= 160)
            {
                return LauncherTheme.Warm;
            }
            return row.Status.Latency < 80 ? LauncherTheme.Good : LauncherTheme.Warm;
        }

        private static string DescribeRow(ServerRow row)
        {
            if (!row.Probed)
            {
                return $"{row.Listing.Endpoint} \u00B7 checking...";
            }
            if (!row.Status.Online)
            {
                return $"{row.Listing.Endpoint} \u00B7 did not answer";
            }
            string room = row.Status.RoomKey;
            if (room.Length > 0)
            {
                (RoomMetadata? meta, _) = Metadata.GetRoomByName(room);
                room = meta?.InGameName ?? room;
            }
            string players = row.Status.MaxPlayers > 0
                ? $"{row.Status.Players}/{row.Status.MaxPlayers}"
                : row.Status.Players.ToString();
            string ping = row.Status.Latency >= 0 ? $"{row.Status.Latency} ms" : "-- ms";
            return $"{room} \u00B7 {players} \u00B7 {ping}";
        }

        private void BuildOnlineCard()
        {
            _onlineCard = NewCard();
            AddBack(_onlineCard);
            Add(_onlineCard, new Caption(_theme, "Play online", 26,
                LauncherTheme.Text, 1, display: true), gap: 14);

            _nameField = Add(_onlineCard, new FieldBox(_theme, "Name"));
            _nameField.MaxLength = RosterPacket.MaxNameBytes;
            _nameField.Value = LauncherPrefs.PlayerName;
            _nameField.Placeholder = "Player";

            _serverField = Add(_onlineCard, new FieldBox(_theme, "Server"), gap: 4);
            _serverField.Value = LauncherPrefs.ServerPort == NetConfig.DefaultPort
                ? LauncherPrefs.ServerAddress
                : $"{LauncherPrefs.ServerAddress}:{LauncherPrefs.ServerPort}";
            _serverField.Placeholder = "address or address:port";
            _serverField.ValueChanged += (_, _) =>
            {
                // Typing invalidates whatever the last poll said, and the next
                // tick will fill it in for the address now on screen.
                _status = ServerStatus.Offline("Checking...");
                UpdateStatusLabel();
            };

            // The list is where this card is reached from, so from here it is
            // a way back to it rather than a discovery.
            var browse = Add(_onlineCard, new MenuButton(_theme, "Change server",
                "Back to the list of servers", titleSize: 14), gap: 6);
            browse.Height = _theme.S(44);
            browse.Accent = LauncherTheme.Accent;
            browse.Click += (_, _) => BrowseServers();

            _statusLabel = new Label
            {
                AutoSize = false,
                Height = _theme.S(34),
                ForeColor = LauncherTheme.TextDim,
                BackColor = LauncherTheme.Panel,
                Font = _theme.Body(_theme.S(12)),
                Text = "Checking..."
            };
            Add(_onlineCard, _statusLabel, gap: 12);

            Add(_onlineCard, new Caption(_theme, "Choose your hunter", 11,
                LauncherTheme.TextDim, 1, display: false), gap: 6);
            _onlineHunter = Add(_onlineCard, new HunterPicker(_theme), gap: 4);
            _onlineHunter.Selected = LauncherPrefs.LastHunter;

            _onlineSpacer = Add(_onlineCard, new Panel { Height = _theme.S(10) }, gap: 0);
            _connectButton = Add(_onlineCard, new MenuButton(_theme, "Connect",
                titleSize: 18));
            _connectButton.Primary = true;
            _connectButton.Height = _theme.S(46);
            _connectButton.Click += (_, _) => Connect();
        }

        private void BuildMatchCard()
        {
            _matchCard = NewCard();
            AddBack(_matchCard);
            _matchTitle = Add(_matchCard, new Caption(_theme, "Play offline", 26,
                LauncherTheme.Text, 1, display: true), gap: 14);

            _mapRow = Add(_matchCard, new ChoiceRow(_theme, "Map"), gap: 6);
            var mapNames = new List<string>();
            foreach (string room in _playable)
            {
                (RoomMetadata? meta, _) = Metadata.GetRoomByName(room);
                mapNames.Add(meta?.InGameName ?? room);
            }
            int startIndex = Math.Max(0, _playable.IndexOf(_settings.RoomKey));
            _mapRow.SetItems(mapNames, startIndex);
            _mapRow.ValueClickable = true;
            _mapRow.Changed += (_, _) => UpdateSplashForMatch();
            _mapRow.Activated += (_, _) => BrowseMaps();

            _modeRow = Add(_matchCard, new ChoiceRow(_theme, "Mode"), gap: 6);
            var modeNames = new List<string>();
            foreach ((string label, GameMode _) in _modes)
            {
                modeNames.Add(label);
            }
            _modeRow.SetItems(modeNames, ModeIndexFromSettings());
            _modeRow.Changed += (_, _) => UpdateSplashForMatch();

            _botsRow = Add(_matchCard, new ChoiceRow(_theme, "Opponents"), gap: 6);
            var bots = new List<string> { "None" };
            for (int i = 1; i <= PlayerEntity.SlotCapacity - 1; i++)
            {
                bots.Add(i == 1 ? "1 bot" : $"{i} bots");
            }
            _botsRow.SetItems(bots, Math.Clamp(LauncherPrefs.Bots, 0, bots.Count - 1));

            _skillRow = Add(_matchCard, new ChoiceRow(_theme, "Bot skill"), gap: 6);
            _skillRow.SetItems(new[] { "Easy", "Normal", "Hard" },
                Math.Clamp(LauncherPrefs.BotLevel, 0, 2));

            // Where the server runs, which decides whether anybody has to
            // touch a router. See StartHosting.
            _hostWhereRow = Add(_matchCard, new ChoiceRow(_theme, "Run it"), gap: 6);
            _hostWhereRow.SetItems(new[] { "Online, no setup", "On this PC" },
                LauncherPrefs.HostOnMaster ? 0 : 1);
            _hostWhereRow.Changed += (_, _) => UpdateHostRows();

            _portField = Add(_matchCard, new FieldBox(_theme, "Port"), gap: 4);
            _portField.Value = LauncherPrefs.HostPort.ToString(CultureInfo.InvariantCulture);

            _listPublicly = Add(_matchCard, new ToggleRow(_theme, "Show on the server list",
                LauncherPrefs.ListHostedGame,
                "Puts this game in everyone's browser. It publishes this machine's address."),
                gap: 4);

            _hostNote = new Label
            {
                AutoSize = false,
                Height = _theme.S(58),
                ForeColor = LauncherTheme.TextDim,
                BackColor = LauncherTheme.Panel,
                Font = _theme.Body(_theme.S(11)),
                Text = "Over the internet, UDP on this port has to be forwarded to this "
                    + "machine, whether or not the game is listed \u2014 the list only says "
                    + "where a server is, it cannot reach through a router for you."
            };
            Add(_matchCard, _hostNote, gap: 4);

            _matchStatus = new Label
            {
                AutoSize = false,
                Height = _theme.S(30),
                ForeColor = LauncherTheme.TextDim,
                BackColor = LauncherTheme.Panel,
                Font = _theme.Body(_theme.S(12)),
                Text = ""
            };
            Add(_matchCard, _matchStatus, gap: 6);

            Add(_matchCard, new Caption(_theme, "Choose your hunter", 11,
                LauncherTheme.TextDim, 1, display: false), gap: 6);
            _matchHunter = Add(_matchCard, new HunterPicker(_theme), gap: 4);
            _matchHunter.Selected = LauncherPrefs.LastHunter;

            _matchSpacer = Add(_matchCard, new Panel { Height = _theme.S(10) }, gap: 0);
            _startButton = Add(_matchCard, new MenuButton(_theme, "Start match",
                titleSize: 18));
            _startButton.Primary = true;
            _startButton.Height = _theme.S(46);
            _startButton.Click += (_, _) => StartMatch();
            // Offline is the shape this card takes by default; ShowMatch puts
            // the hosting rows back when they are wanted.
            Collapse(_portField, true);
            Collapse(_hostNote, true);
            Collapse(_matchStatus, true);
        }

        private void AddBack(FlowLayoutPanel card)
        {
            var back = new MenuButton(_theme, "Back", titleSize: 13)
            {
                Height = _theme.S(26)
            };
            back.Accent = LauncherTheme.TextDim;
            back.Click += (_, _) => ShowCard(_homeCard);
            Add(card, back, gap: 10);
        }

        private int ModeIndexFromSettings()
        {
            string configured = _settings.Mode.Replace(" ", "");
            for (int i = 0; i < _modes.Length; i++)
            {
                if (String.Equals(_modes[i].Mode.ToString(), configured,
                    StringComparison.OrdinalIgnoreCase))
                {
                    return i;
                }
            }
            return 0;
        }

        private string SelectedRoomKey => _playable.Count == 0
            ? _settings.RoomKey
            : _playable[Math.Clamp(_mapRow.Index, 0, _playable.Count - 1)];

        private GameMode SelectedMode => _modes[Math.Clamp(_modeRow.Index, 0, _modes.Length - 1)].Mode;

        private void ShowCard(Control card)
        {
            _homeCard.Visible = card == _homeCard;
            _browseCard.Visible = card == _browseCard;
            _onlineCard.Visible = card == _onlineCard;
            _matchCard.Visible = card == _matchCard;
            _setupCard.Visible = card == _setupCard;
            if (card == _homeCard || card == _browseCard)
            {
                _splash.ShowRoom(null);
            }
            LayoutCards();
            card.BringToFront();
        }

        /// <summary>
        /// Playing online starts with the question "which server", so that is
        /// the screen it opens on.
        ///
        /// It used to open on a card holding a name, a hunter and an address
        /// field pre-filled with whatever was last used -- which is the right
        /// screen for somebody who already knows where they are going and the
        /// wrong one for everybody else, since the address is the one thing a
        /// new player cannot invent. The list comes first, with a field on it
        /// for an address somebody was given directly; the hunter and the name
        /// come after, on the card behind it.
        ///
        /// Cancelling the list goes back to the front screen rather than on to
        /// the card: backing out of "which server" is backing out of playing
        /// online.
        /// </summary>
        private void ShowOnline()
        {
            ShowBrowse(_homeCard);
        }

        /// <summary>The name-and-hunter half, once a server has been chosen.</summary>
        private void ShowOnlineCard()
        {
            ShowCard(_onlineCard);
            UpdateStatusLabel();
            UpdateSplashForOnline();
            PollStatus(force: true);
            _nameField.SelectField();
        }

        private void ShowMatch(bool host)
        {
            _hostMode = host;
            _matchTitle.SetText(host ? "Host a game" : "Play offline");
            _startButton.Text = host ? "Start hosting" : "Start match";
            // Hosting relays every player's input, and a bot would have its
            // Controls overwritten by whoever holds that slot; a networked
            // match has no AI in it at all.
            Collapse(_botsRow, host);
            Collapse(_skillRow, host);
            Collapse(_hostWhereRow, !host);
            UpdateHostRows();
            Collapse(_matchStatus, !host);
            _matchStatus.Text = "";
            ShowCard(_matchCard);
            UpdateSplashForMatch();
            LayoutCards();
        }

        /// <summary>
        /// Show only the rows that mean something for where the game will run.
        ///
        /// A game the directory runs has no port to choose -- it picks one --
        /// and no decision about being listed, because being findable is the
        /// entire reason it is there.
        /// </summary>
        private void UpdateHostRows()
        {
            bool onThisPc = !_hostMode || _hostWhereRow.Index == 1;
            Collapse(_portField, !_hostMode || !onThisPc);
            Collapse(_listPublicly, !_hostMode || !onThisPc);
            Collapse(_hostNote, !_hostMode);
            if (_hostMode)
            {
                _hostNote.Text = onThisPc
                    ? "The server runs here, so UDP on this port has to be forwarded from "
                        + "your router to this machine \u2014 otherwise nobody outside your "
                        + "network can join, listed or not."
                    : $"The match runs on {LauncherPrefs.MasterHost} and you join it like "
                        + "everyone else, so nothing has to reach into your network. "
                        + "No port to open, no router to touch.";
            }
            LayoutCards();
        }

        /// <summary>Show or take away a row, keeping the spacer arithmetic honest.</summary>
        private void Collapse(Control control, bool collapsed)
        {
            control.Visible = !collapsed;
            if (collapsed)
            {
                _collapsed.Add(control);
            }
            else
            {
                _collapsed.Remove(control);
            }
        }

        private void UpdateSplashForMatch()
        {
            _splash.ShowRoom(SelectedRoomKey, _hostMode ? "hosting" : "offline match");
        }

        private void UpdateSplashForOnline()
        {
            if (_status.Online && !String.IsNullOrEmpty(_status.RoomKey))
            {
                _splash.ShowRoom(_status.RoomKey, "now playing");
            }
            else
            {
                _splash.ShowRoom(null);
            }
        }

        /// <summary>
        /// Open the server browser and take the address it comes back with.
        ///
        /// The choice is written straight into the field rather than joined
        /// outright: the hunter and the name on this card are part of joining
        /// too, and a list that connected the moment somebody clicked a row
        /// would skip both.
        /// </summary>
        /// <summary>Reopen the list from the online card, and come back to it.</summary>
        private void BrowseServers()
        {
            ShowBrowse(_onlineCard);
        }

        private void BrowseMaps()
        {
            using var picker = new MapPickerForm(_theme, _playable, SelectedRoomKey);
            if (picker.ShowDialog(this) == DialogResult.OK && picker.RoomKey != null)
            {
                int index = _playable.IndexOf(picker.RoomKey);
                if (index >= 0)
                {
                    _mapRow.Index = index;
                }
            }
        }

        private void OpenSettings()
        {
            Hide();
            try
            {
                using var form = new SettingsForm(_settings, _theme);
                form.ShowDialog(this);
            }
            catch (Exception ex)
            {
                // Same reason as the pause menu's: a failure here used to be
                // an unhandled-exception dialog over a hidden launcher, which
                // is a front screen that has simply vanished.
                MessageBox.Show(this, $"The settings could not be opened:\n\n{ex.Message}",
                    "MphRead", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
            Show();
            // The settings window writes straight into the same MenuSettings,
            // so the rows here have to be re-read or they would overwrite what
            // was just chosen there.
            int index = _playable.IndexOf(_settings.RoomKey);
            if (index >= 0)
            {
                _mapRow.SetIndexQuiet(index);
            }
            _modeRow.SetIndexQuiet(ModeIndexFromSettings());
        }

        private void PollStatus(bool force = false)
        {
            if (_statusBusy || _connecting || IsDisposed)
            {
                return;
            }
            (string address, int port) = ParseServer(_serverField.Value);
            if (address.Length == 0)
            {
                return;
            }
            // A server that only answers the join probe is asked rarely: the
            // probe holds a slot for the length of the exchange, and doing
            // that every five seconds would churn the roster of a match in
            // progress. Between those asks the last answer stands rather than
            // being replaced by "no answer", which is what made a running
            // server show as offline five seconds after it had shown as up.
            bool legacyUp = _status.Online && _status.Legacy;
            if (legacyUp && !force && _statusPolls % 4 != 0)
            {
                _statusPolls++;
                return;
            }
            _statusBusy = true;
            bool allowProbe = !_statusQueryWorks;
            _statusPolls++;
            Task.Run(() => NetStatus.Query(address, port, allowProbe))
                .ContinueWith(task =>
                {
                    ServerStatus status = task.IsCompletedSuccessfully
                        ? task.Result
                        : ServerStatus.Offline("Could not reach the server.");
                    ApplyOnUi(() =>
                    {
                        _statusBusy = false;
                        if (status.Online && !status.Legacy)
                        {
                            _statusQueryWorks = true;
                        }
                        _status = status;
                        UpdateStatusLabel();
                        if (_onlineCard.Visible)
                        {
                            UpdateSplashForOnline();
                        }
                    });
                }, TaskScheduler.Default);
        }

        /// <summary>Run on the UI thread, unless the window has gone away first.</summary>
        private void ApplyOnUi(Action action)
        {
            try
            {
                if (IsDisposed || !IsHandleCreated)
                {
                    return;
                }
                BeginInvoke(action);
            }
            catch (ObjectDisposedException)
            {
                // The form closed between the check and the call; there is
                // nothing left to update.
            }
            catch (InvalidOperationException)
            {
            }
        }

        private void UpdateStatusLabel()
        {
            string text = _connecting ? _statusLabel.Text : _status.Message;
            text ??= "";
            if (text.Length == 0)
            {
                text = "Checking...";
            }
            _statusLabel.Text = text;
            // Nothing is wrong yet while the first answer is still on its way,
            // so it is not shown in the colour that means something is.
            bool waiting = _statusBusy || _statusPolls <= 1;
            _statusLabel.ForeColor = _connecting
                ? LauncherTheme.Accent
                : _status.Online ? LauncherTheme.Good
                : waiting ? LauncherTheme.TextDim : LauncherTheme.Bad;

            _onlineButton.Subtitle = _status.Online
                ? _status.Message
                : waiting ? "Looking for the server..." : "No server at this address";
            _onlineButton.SubtitleColor = _status.Online
                ? LauncherTheme.Good
                : LauncherTheme.TextDim;
        }

        private static (string Address, int Port) ParseServer(string text)
        {
            string value = text.Trim();
            int port = LauncherPrefs.ServerPort;
            int colon = value.LastIndexOf(':');
            if (colon > 0 && Int32.TryParse(value[(colon + 1)..], NumberStyles.Integer,
                CultureInfo.InvariantCulture, out int parsed))
            {
                port = Math.Clamp(parsed, 1, 65535);
                value = value[..colon].Trim();
            }
            return (value, port);
        }

        private Hunter Resolve(Hunter hunter)
        {
            return hunter == Hunter.Random ? (Hunter)Random.Shared.Next(7) : hunter;
        }

        private void Connect()
        {
            if (_connecting)
            {
                return;
            }
            (string address, int port) = ParseServer(_serverField.Value);
            if (address.Length == 0)
            {
                _statusLabel.Text = "Type a server address first.";
                _statusLabel.ForeColor = LauncherTheme.Bad;
                return;
            }
            string name = _nameField.Value.Length > 0 ? _nameField.Value : "Player";
            Hunter hunter = Resolve(_onlineHunter.Selected);

            _connecting = true;
            _connectButton.Enabled = false;
            _connectButton.Text = "Connecting";
            _statusLabel.Text = $"Connecting to {address}...";
            _statusLabel.ForeColor = LauncherTheme.Accent;

            Task.Run(() =>
            {
                try
                {
                    return NetLaunch.Join(address, port, name, hunter);
                }
                catch (Exception)
                {
                    // A refused or unroutable address throws out of the socket
                    // layer; the screen should say so rather than take the
                    // launcher down with it.
                    return false;
                }
            }).ContinueWith(task =>
            {
                bool joined = task.IsCompletedSuccessfully && task.Result;
                ApplyOnUi(() =>
                {
                    _connecting = false;
                    _connectButton.Enabled = true;
                    _connectButton.Text = "Connect";
                    if (joined)
                    {
                        LauncherPrefs.ServerAddress = address;
                        LauncherPrefs.ServerPort = port;
                        LauncherPrefs.PlayerName = name;
                        LauncherPrefs.LastHunter = _onlineHunter.Selected;
                        LauncherPrefs.LastKind = (int)LaunchKind.Online;
                        Plan = new LaunchPlan
                        {
                            Kind = LaunchKind.Online,
                            Hunter = hunter,
                            PlayerName = name,
                            Port = port
                        };
                        Finish();
                        return;
                    }
                    // Half a session is worse than none: the socket and its
                    // worker thread have to go before the next attempt.
                    NetSession.Stop();
                    _statusLabel.Text = $"Could not join {address}. It may be off, "
                        + "or UDP may be blocked.";
                    _statusLabel.ForeColor = LauncherTheme.Bad;
                });
            }, TaskScheduler.Default);
        }

        /// <summary>
        /// Start a server in this process and join it over the loopback.
        ///
        /// Hosting used to be its own half of the network layer -- no roster,
        /// so no names, hunters or pings, and no clock, so no rotation. This
        /// runs the same server the Pi runs and joins it locally, which makes
        /// a hosted match identical to a dedicated one for everybody in it.
        /// </summary>
        private void StartHosting()
        {
            if (_connecting)
            {
                return;
            }
            int port = ParsePort();
            Hunter hunter = Resolve(_matchHunter.Selected);
            string name = LauncherPrefs.PlayerName.Length > 0 ? LauncherPrefs.PlayerName : "Player";
            string roomKey = SelectedRoomKey;
            GameMode mode = SelectedMode;
            (float timeLimit, int pointGoal) = MatchRules();

            _connecting = true;
            _startButton.Enabled = false;
            _startButton.Text = "Starting";
            _matchStatus.ForeColor = LauncherTheme.Accent;
            int hostedPort = 0;

            bool onThisPc = _hostWhereRow.Index == 1;
            LauncherPrefs.HostOnMaster = !onThisPc;
            bool list = _listPublicly.On;
            LauncherPrefs.ListHostedGame = list;
            // The host's own name, because that is what their friends are
            // looking for on a list -- not the machine name, which on a
            // Windows PC is usually something nobody has ever read.
            string serverName = $"{name}'s game";
            (string, int, string)? listing = list
                ? (LauncherPrefs.MasterHost, LauncherPrefs.MasterPort, serverName)
                : null;
            string masterHost = LauncherPrefs.MasterHost;
            int masterPort = LauncherPrefs.MasterPort;
            _matchStatus.Text = onThisPc
                ? $"Starting a server on port {port}..."
                : $"Asking {masterHost} to start the match...";
            string failure = "";

            Task.Run(() =>
            {
                try
                {
                    if (onThisPc)
                    {
                        return NetHostSession.StartAndJoin(port, name, hunter, roomKey, mode,
                            timeLimit, pointGoal, PlayerEntity.SlotCapacity, listing);
                    }
                    // Somebody else's machine runs it and this one joins by
                    // connecting out, which is the whole reason no port has to
                    // be opened here.
                    HostedGame game = NetMasterClient.RequestGame(masterHost, masterPort,
                        roomKey, mode, timeLimit, pointGoal, PlayerEntity.SlotCapacity,
                        serverName);
                    if (!game.Started)
                    {
                        failure = game.Reason.Length > 0
                            ? game.Reason
                            : $"{masterHost} would not start a game";
                        return false;
                    }
                    hostedPort = game.Port;
                    return NetLaunch.Join(game.Host, game.Port, name, hunter);
                }
                catch (Exception ex)
                {
                    failure = ex.Message;
                    return false;
                }
            }).ContinueWith(task =>
            {
                bool ok = task.IsCompletedSuccessfully && task.Result;
                ApplyOnUi(() =>
                {
                    _connecting = false;
                    _startButton.Enabled = true;
                    _startButton.Text = "Start hosting";
                    if (ok)
                    {
                        SaveMatchChoices(port);
                        LauncherPrefs.LastKind = (int)LaunchKind.Host;
                        Plan = new LaunchPlan
                        {
                            Kind = LaunchKind.Host,
                            Hunter = hunter,
                            RoomKey = roomKey,
                            Mode = mode,
                            Port = onThisPc ? port : hostedPort,
                            PlayerName = name
                        };
                        Finish();
                        return;
                    }
                    NetHostSession.Stop();
                    NetSession.Stop();
                    _matchStatus.ForeColor = LauncherTheme.Bad;
                    if (failure.Length > 0)
                    {
                        _matchStatus.Text = $"Could not start the match: {failure}";
                    }
                    else if (!onThisPc)
                    {
                        _matchStatus.Text = $"{masterHost} started the match but it could "
                            + "not be joined.";
                    }
                    else
                    {
                        _matchStatus.Text = NetHostSession.LastError != null
                            ? $"Could not start the server: {NetHostSession.LastError}"
                            : $"Could not start a server on port {port}. "
                                + "Another program may be using it.";
                    }
                });
            }, TaskScheduler.Default);
        }

        private int ParsePort()
        {
            return Int32.TryParse(_portField.Value, NumberStyles.Integer,
                CultureInfo.InvariantCulture, out int parsed)
                ? Math.Clamp(parsed, 1, 65535)
                : NetConfig.DefaultPort;
        }

        /// <summary>Time limit in seconds and point goal, from the settings window.</summary>
        private (float TimeLimit, int PointGoal) MatchRules()
        {
            float seconds = 7 * 60;
            string[] parts = _settings.TimeLimit.Split(':');
            if (parts.Length == 2 && Int32.TryParse(parts[0], out int minutes)
                && Int32.TryParse(parts[1], out int secs))
            {
                seconds = minutes * 60 + secs;
            }
            int goal = Int32.TryParse(_settings.PointGoal, out int parsedGoal) ? parsedGoal : 7;
            return (seconds, goal);
        }

        private void SaveMatchChoices(int port)
        {
            LauncherPrefs.LastHunter = _matchHunter.Selected;
            LauncherPrefs.Bots = _botsRow.Index;
            LauncherPrefs.BotLevel = _skillRow.Index;
            LauncherPrefs.HostPort = port;
        }

        private void StartMatch()
        {
            if (_hostMode)
            {
                StartHosting();
                return;
            }
            HunterPicker picker = _matchHunter;
            int port = ParsePort();
            SaveMatchChoices(port);
            LauncherPrefs.PlayerName = _nameField.Value.Length > 0
                ? _nameField.Value
                : LauncherPrefs.PlayerName;
            LauncherPrefs.LastKind = (int)LaunchKind.Offline;
            Plan = new LaunchPlan
            {
                Kind = LaunchKind.Offline,
                Hunter = Resolve(picker.Selected),
                RoomKey = SelectedRoomKey,
                Mode = SelectedMode,
                Bots = _botsRow.Index,
                BotLevel = _skillRow.Index,
                Port = port,
                PlayerName = LauncherPrefs.PlayerName
            };
            Finish();
        }

        /// <summary>Write the choices down and hand back to LauncherEntry.</summary>
        private void Finish()
        {
            _statusTimer.Stop();
            if (Plan.Kind != LaunchKind.Online)
            {
                _settings.RoomKey = Plan.RoomKey;
                _settings.Mode = Plan.Mode == GameMode.None
                    ? "auto-select"
                    : NetStatus.ModeName(Plan.Mode);
                if (Plan.Mode.ToString().EndsWith("Teams", StringComparison.Ordinal))
                {
                    _settings.TeamPlay = "on";
                }
            }
            _settings.Player1 = $"{Plan.Hunter} 0";
            GameState.CommitSettings(_settings);
            LauncherPrefs.Save();
            DialogResult = DialogResult.OK;
            Close();
        }

        protected override bool ProcessCmdKey(ref System.Windows.Forms.Message message, Keys keyData)
        {
            if (keyData == Keys.Escape)
            {
                if (_homeCard.Visible)
                {
                    Close();
                }
                else if (_setupCard.Visible && !GameFiles.Ready)
                {
                    // Nothing to go back to until the files are there.
                    Close();
                }
                else
                {
                    ShowCard(_homeCard);
                }
                return true;
            }
            return base.ProcessCmdKey(ref message, keyData);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _statusTimer.Stop();
                _statusTimer.Dispose();
                _browseWork?.Cancel();
                _browseWork?.Dispose();
                _browseWork = null;
                _theme.Dispose();
            }
            base.Dispose(disposing);
        }

    }
}
