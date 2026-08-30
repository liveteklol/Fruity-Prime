using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using MphRead.Entities;
using MphRead.Mods;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// Settings, in the same language as the front screen: a rail of sections,
    /// one page at a time beside it, everything painted here.
    ///
    /// The same view opens from the front screen, from the pause menu inside a
    /// match, and from the Android head -- which is why nothing here needs a
    /// restart to take effect except the window mode, and why it is a
    /// <see cref="UserControl"/> rather than a <see cref="Window"/>: a phone has
    /// no second window to open it in. <see cref="SettingsWindow"/> is the frame
    /// the desktop puts around it.
    ///
    /// Below <see cref="_narrowWidth"/> the rail turns into a strip across the
    /// top and the footer drops to the bottom. The sections, the rows and every
    /// value they read and write are the same objects either way.
    /// </summary>
    internal sealed class SettingsView : UserControl
    {
        private readonly MenuSettings _settings;
        private readonly bool _inGame;
        private readonly StackPanel _rail = new() { Spacing = 2 };

        /// <summary>
        /// The same section buttons, wrapped over as many lines as they need.
        ///
        /// A narrow screen used to put them in a row inside a horizontal
        /// scroller, and the row could not be scrolled with a finger: every
        /// button in it takes the pointer for itself, so the drag never
        /// reached the scroller and the sections past the fourth were simply
        /// unreachable on a phone. Wrapping needs no gesture at all.
        /// </summary>
        private readonly WrapPanel _railWrap = new();
        private readonly Panel _pages = new();
        private readonly List<(MenuEntry Button, Control Page)> _sections = new();
        private readonly Grid _grid = new();
        private readonly Border _railPanel;
        private Caption? _heading;
        private readonly Border _footerPanel;
        private readonly ScrollViewer _railScroll;
        private bool _narrow;
        private bool _laidOut;

        /// <summary>Below this width the rail goes across the top.</summary>
        private const double _narrowWidth = 720;

        /// <summary>Raised when this view is finished with, saved or not.</summary>
        public event EventHandler? Closed;

        /// <summary>
        /// The player asked for the game-files screen, which lives on the
        /// front screen because extracting a ROM is a thing you do before
        /// there is anything to configure. Raised, not acted on: this view
        /// does not know what is behind it.
        /// </summary>
        public event EventHandler? GameFilesRequested;

        private ChoiceRow? _windowRow;
        private SliderRow _resolutionScale = null!;
        private ToggleRow _lightingRow = null!;
        private ToggleRow _fogRow = null!;
        private ToggleRow _filteringRow = null!;
        private ToggleRow _celRow = null!;
        private ToggleRow _fpsRow = null!;
        private ToggleRow _helmetRow = null!;
        private SliderRow _helmetOpacity = null!;
        private SliderRow _visorOpacity = null!;
        private SliderRow _hudOpacity = null!;
        private SliderRow _weaponListSize = null!;
        private ToggleRow _fixedCrosshair = null!;
        private ToggleRow _customCrosshair = null!;
        private ToggleRow _modernHud = null!;
        private ToggleRow _fixedWeapon = null!;
        private SliderRow _sfxVolume = null!;
        private SliderRow _musicVolume = null!;
        private ChoiceRow _languageRow = null!;
        private SliderRow _sensitivity = null!;
        private ToggleRow _invertY = null!;
        private ToggleRow _invertX = null!;
        private ToggleRow _scrollAllWeapons = null!;
        private FieldRow _pointGoal = null!;
        private FieldRow _timeLimit = null!;
        private ChoiceRow _damageRow = null!;
        private ToggleRow _teamPlay = null!;
        private ToggleRow _friendlyFire = null!;
        private ToggleRow _radar = null!;
        private ToggleRow _affinity = null!;
        private FieldRow _playerName = null!;
        private ChoiceRow _hunterRow = null!;
        private FieldRow _serverRow = null!;
        private FieldRow _masterRow = null!;
        private ToggleRow _autoUpdate = null!;
        private Note _saveError = null!;

        private const double _railWidth = 216;

        /// <summary>True when the user pressed save rather than closing.</summary>
        public bool Saved { get; private set; }

        /// <summary>What a frame around this should be titled.</summary>
        public string WindowTitle => $"{Mods.Branding.Name} settings";

        /// <summary>True when this was opened over a match rather than the launcher.</summary>
        public bool InGame => _inGame;

        public SettingsView(MenuSettings settings, bool inGame = false)
        {
            _settings = settings;
            _inGame = inGame;

            Background = GuiTheme.InkBrush;
            Focusable = true;

            _railScroll = new ScrollViewer
            {
                Content = _rail,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
            };
            Control footer = BuildFooter();
            _railPanel = new Border
            {
                Background = GuiTheme.PanelBrush,
                Child = _railScroll
            };
            _footerPanel = new Border
            {
                Background = GuiTheme.PanelBrush,
                Child = footer
            };
            // A grid rather than a docked panel so that the sections come
            // before the footer in the tab order: a DockPanel fills with its
            // *last* child, which would have put Save and Cancel first and made
            // the first Tab in the window a press away from closing it.
            _grid.Children.Add(_railPanel);
            _grid.Children.Add(_pages);
            _grid.Children.Add(_footerPanel);
            ApplyLayout(narrow: false);
            Content = _grid;
            SizeChanged += (_, e) => ApplyLayout(e.NewSize.Width < _narrowWidth);

            _heading = new Caption("Settings") { Height = 34 };
            _rail.Children.Add(_heading);
            BuildPages();
            // The sections did not exist when the first layout ran, so the one
            // that is wanted is chosen again now that they do.
            _laidOut = false;
            ApplyLayout(_narrow);
            ShowPage(_sections[0].Page);
        }

        /// <summary>
        /// A rail beside the pages, or a strip of sections above them.
        ///
        /// One tree moved between cells rather than two trees: a section added
        /// to <see cref="BuildPages"/> appears on a phone without anybody
        /// having to remember it twice.
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
                _grid.ColumnDefinitions = new ColumnDefinitions("*");
                _grid.RowDefinitions = new RowDefinitions("Auto,*,Auto");
                MoveSections(_railWrap);
                _railScroll.Content = _railWrap;
                _railScroll.HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled;
                _railScroll.VerticalScrollBarVisibility = ScrollBarVisibility.Disabled;
                _railPanel.Width = Double.NaN;
                _railPanel.Padding = new Thickness(12, 8, 12, 6);
                _footerPanel.Width = Double.NaN;
                _footerPanel.Padding = new Thickness(12, 6, 12, 10);
                Place(_railPanel, 0, 0, rowSpan: 1);
                Place(_pages, 1, 0, rowSpan: 1);
                Place(_footerPanel, 2, 0, rowSpan: 1);
                return;
            }
            _grid.ColumnDefinitions = new ColumnDefinitions("Auto,*");
            _grid.RowDefinitions = new RowDefinitions("*,Auto");
            MoveSections(_rail);
            _railScroll.Content = _rail;
            _railScroll.HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled;
            _railScroll.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
            _railPanel.Width = _railWidth;
            _railPanel.Padding = new Thickness(18, 20, 14, 4);
            _footerPanel.Width = _railWidth;
            _footerPanel.Padding = new Thickness(18, 4, 14, 14);
            Place(_railPanel, 0, 0, rowSpan: 1);
            Place(_footerPanel, 1, 0, rowSpan: 1);
            Place(_pages, 0, 1, rowSpan: 2);
        }

        /// <summary>
        /// Put the section buttons in one panel or the other.
        ///
        /// One set of buttons moved between two panels rather than two sets
        /// kept in step: a section added to <see cref="BuildPages"/> turns up
        /// in both shapes without anybody having to remember it twice, which
        /// is the same reason the pages themselves are shared.
        ///
        /// The heading goes with them only down the column. Across the top it
        /// would be a fifth thing on the first line that is not a section.
        /// </summary>
        private void MoveSections(Panel target)
        {
            if (_sections.Count == 0 || ReferenceEquals(_sections[0].Button.Parent, target))
            {
                return;
            }
            _rail.Children.Clear();
            _railWrap.Children.Clear();
            if (ReferenceEquals(target, _rail) && _heading != null)
            {
                _rail.Children.Add(_heading);
            }
            foreach ((MenuEntry button, Control _) in _sections)
            {
                target.Children.Add(button);
            }
        }

        private static void Place(Control control, int row, int column, int rowSpan)
        {
            Grid.SetRow(control, row);
            Grid.SetColumn(control, column);
            Grid.SetRowSpan(control, rowSpan);
        }

        /// <summary>
        /// Put the keyboard on the first section as the view appears.
        ///
        /// Without it the window opens with nothing focused, and the first Tab
        /// goes to whatever the tree happens to offer first rather than to the
        /// rail -- which is the difference between a window that can be driven
        /// from the keyboard and one that can be driven from the keyboard once
        /// you have found out how.
        /// </summary>
        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            Dispatcher.UIThread.Post(() => _sections[0].Button.Focus(),
                DispatcherPriority.Background);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                Close();
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
        }

        private void Close() => Closed?.Invoke(this, EventArgs.Empty);

        // ----------------------------------------------------------- structure

        private StackPanel AddSection(string name)
        {
            var page = new StackPanel { Spacing = 2 };
            // The inset is the page's margin rather than the scroll viewer's
            // padding: padding is not taken off the width the content is
            // measured with, so every wrapped note ran off the right edge of
            // the window by exactly that much.
            page.Margin = new Thickness(26, 22, 26, 22);
            var scroll = new ScrollViewer
            {
                Content = page,
                IsVisible = false,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
            };
            var button = new MenuEntry(name, titleSize: 15) { Height = 32 };
            button.Click += (_, _) => ShowPage(scroll);
            _rail.Children.Add(button);
            _pages.Children.Add(scroll);
            _sections.Add((button, scroll));
            return page;
        }

        private void ShowPage(Control page)
        {
            foreach ((MenuEntry button, Control candidate) in _sections)
            {
                candidate.IsVisible = ReferenceEquals(candidate, page);
                button.Selected = ReferenceEquals(candidate, page);
            }
        }

        private static Caption Heading(StackPanel page, string text)
        {
            var caption = new Caption(text) { Height = 30, Margin = new Thickness(0, 8, 0, 4) };
            page.Children.Add(caption);
            return caption;
        }

        private static Note Explain(StackPanel page, string text, Color? color = null)
        {
            var note = new Note(text, color);
            page.Children.Add(note);
            return note;
        }

        private static T Add<T>(StackPanel page, T control) where T : Control
        {
            page.Children.Add(control);
            return control;
        }

        private void BuildPages()
        {
            BuildDisplay();
            BuildAudio();
            BuildControls();
            BuildMatch();
            BuildLauncher();
            BuildCredits();
        }

        /// <summary>
        /// Who this is built on, in full.
        ///
        /// It used to be four dim lines in the corner of the front screen,
        /// where it was the first thing the eye landed on and the last thing
        /// anybody needed while choosing a match. Here it is out of the way
        /// and, being a page rather than a corner, it can say what each person
        /// actually did.
        /// </summary>
        private void BuildCredits()
        {
            StackPanel page = AddSection("Credits");
            Heading(page, "Credits");
            Explain(page, Mods.Credits.Summary);
            foreach (Mods.Credits.Entry entry in Mods.Credits.Entries)
            {
                page.Children.Add(new Caption(entry.Who));
                string what = entry.What;
                if (entry.Where.Length > 0)
                {
                    what += "\n" + entry.Where;
                }
                page.Children.Add(new Note(what));
            }
        }

        // ------------------------------------------------------------- display

        private void BuildDisplay()
        {
            StackPanel page = AddSection("Display");
            // A phone has one window, it is already the whole screen, and it
            // has no F11. Everything in this group is about a desktop window.
            if (!OperatingSystem.IsAndroid())
            {
                Heading(page, "Window");
                _windowRow = Add(page, new ChoiceRow("Mode",
                    new[] { "Windowed", "Fullscreen (borderless)" },
                    LauncherPrefs.WindowMode == WindowStartMode.BorderlessFullscreen ? 1 : 0));
                Explain(page, "F11 switches at any time, and so does Alt+Enter. Escape opens the "
                    + "pause menu, which can switch it too, and gives the mouse back so the "
                    + "window can be moved or resized.", GuiTheme.Accent);
            }

            Heading(page, "Performance");
            _resolutionScale = Add(page, new SliderRow("Render scale",
                RenderOptions.ResolutionScale,
                v => $"{Math.Max(RenderOptions.MinScale, v)}%"));
            Explain(page, "The 3D scene is drawn at this fraction of the window and stretched "
                + "back up; halving it quarters the pixels. The HUD, the helmet and the text "
                + "are drawn afterwards at full size, so they stay sharp. The one to reach for "
                + "first on a device that cannot keep up.");
            _lightingRow = Add(page, new ToggleRow("Lighting", RenderOptions.Lighting));
            _fogRow = Add(page, new ToggleRow("Fog", RenderOptions.Fog));
            _filteringRow = Add(page, new ToggleRow("Texture filtering", RenderOptions.TextureFiltering));
            _fpsRow = Add(page, new ToggleRow("FPS counter", RenderOptions.ShowFps));
            Explain(page, "The DS had no filtering, so off is both faster and what the game "
                + "looked like. Lighting and fog take effect on the next room; the render "
                + "scale takes effect at once. Room and player detail are in Features. The "
                + "FPS counter draws in the top left and takes effect at once, this match "
                + "included.");

            Heading(page, "Cel shading");
            _celRow = Add(page, new ToggleRow("Cel shading", RenderOptions.CelShading));
            Explain(page, "Every surface is painted in one flat colour -- the one its "
                + "texture averages to -- and the shapes in the room are drawn around in "
                + "ink. Not the same as turning lighting off, which only flattens "
                + "everything: a wall keeps its own colour and it is the shading across it "
                + "that goes to steps.");

            Heading(page, "Helmet and HUD");
            _helmetRow = Add(page, new ToggleRow("Draw the helmet",
                Features.HelmetOpacity > 0 || Features.VisorOpacity > 0));
            Explain(page, "All of it: the shell in front of and behind the readouts, and "
                + "the visor pane over the view.");
            _helmetRow.Changed += (_, _) => UpdateHelmetRows();
            _helmetOpacity = Add(page, new SliderRow("Helmet",
                (int)Math.Round(Features.HelmetOpacity * 100)));
            _visorOpacity = Add(page, new SliderRow("Visor",
                (int)Math.Round(Features.VisorOpacity * 100)));
            Explain(page, "The helmet is three layers: the shell behind the readouts, the "
                + "shell in front, and the visor pane over the view. Clearing only the "
                + "shell is what leaves a tinted pane with nothing behind it.");
            _hudOpacity = Add(page, new SliderRow("HUD readouts",
                (int)Math.Round(Features.HudOpacity * 100)));
            Explain(page, "Energy, ammo, the radar and the rest. Separate from the helmet.");
            UpdateHelmetRows();

            Heading(page, "Crosshair");
            _fixedCrosshair = Add(page, new ToggleRow("Fixed crosshair", Features.FixedCrosshair));
            Explain(page, "Stops the reticle shrinking when you fire -- it stays one size, "
                + "like Quake's.");
            _customCrosshair = Add(page, new ToggleRow("Custom crosshair", Features.CustomCrosshair));
            Explain(page, "Replaces the reticle with a plain cross at the centre of the "
                + "screen, coloured by your current HP: green above 60, orange down to 33, "
                + "red below that. Also shown with the modern HUD, below.");

            Heading(page, "Modern HUD");
            _modernHud = Add(page, new ToggleRow("Enable modern HUD", Features.ModernHud));
            Explain(page, "A column down the left listing every weapon you are carrying "
                + "and its ammo, each in that weapon's own colour, with the equipped one "
                + "marked. Only weapons you have actually picked up appear.");
            _weaponListSize = Add(page, new SliderRow("Weapon list size",
                ScaleToSlider(Features.WeaponListScale), v => $"{SliderToScale(v) * 100:0}%"));
            Explain(page, "How big that column is drawn, from 60% to 200%.");

            Heading(page, "Weapon");
            _fixedWeapon = Add(page, new ToggleRow("Fixed weapon", Features.FixedWeapon));
            Explain(page, "Stops the gun and the HUD drifting when you move the mouse -- "
                + "the gun rides square with the camera instead of lagging behind it, "
                + "like Quake's weapon.");
        }

        private void UpdateHelmetRows()
        {
            bool on = _helmetRow.On;
            _helmetOpacity.IsEnabled = on;
            _visorOpacity.IsEnabled = on;
            if (on && _helmetOpacity.Value == 0)
            {
                // On with the shell at zero is the state that started this: the
                // visor frame still drawn, with nothing behind it.
                _helmetOpacity.Value = 100;
                if (_visorOpacity.Value == 0)
                {
                    _visorOpacity.Value = 50;
                }
            }
            _helmetOpacity.InvalidateVisual();
            _visorOpacity.InvalidateVisual();
        }

        // --------------------------------------------------------------- audio

        private void BuildAudio()
        {
            StackPanel page = AddSection("Audio");
            Heading(page, "Volume");
            _sfxVolume = Add(page, new SliderRow("Sound effects",
                Percent(_settings.SfxVolume, 35)));
            _musicVolume = Add(page, new SliderRow("Music", Percent(_settings.MusicVolume, 50)));
            Heading(page, "Language");
            string[] languages = Enum.GetNames<Language>();
            _languageRow = Add(page, new ChoiceRow("Text", languages,
                Math.Max(0, Array.IndexOf(languages, _settings.Language))));
            Explain(page, "The game's own text, from your own files.");
        }

        private static int Percent(string stored, int fallback)
        {
            return Single.TryParse(stored, NumberStyles.Float, CultureInfo.InvariantCulture,
                out float parsed)
                ? Math.Clamp((int)Math.Round(parsed * 100), 0, 100)
                : fallback;
        }

        // ------------------------------------------------------------ controls

        private void BuildControls()
        {
            StackPanel page = AddSection("Controls");
            Heading(page, "Mouse");
            _sensitivity = Add(page, new SliderRow("Sensitivity",
                SensitivityToSlider(InputSettings.MouseSensitivity),
                v => $"{SliderToSensitivity(v).ToString("0.00", CultureInfo.InvariantCulture)}x"));
            _invertY = Add(page, new ToggleRow("Invert vertical aim", InputSettings.InvertMouseY));
            _invertX = Add(page, new ToggleRow("Invert horizontal aim", InputSettings.InvertMouseX));
            _scrollAllWeapons = Add(page, new ToggleRow("Wheel cycles every weapon",
                InputSettings.ScrollAllWeapons));

            Heading(page, "Keys");
            Explain(page, "Click a binding and press a key, a mouse button or the wheel. "
                + "Backspace clears it, Escape leaves it alone.");
            var rows = new List<KeyRow>();
            foreach (PropertyInfo property in InputSettings.Bindings)
            {
                rows.Add(Add(page, new KeyRow(property)));
            }
            var reset = new MenuEntry("Reset to defaults", titleSize: 13)
            {
                Height = 30,
                Accent = GuiTheme.Warm,
                Margin = new Thickness(0, 8, 0, 0)
            };
            reset.Click += (_, _) =>
            {
                InputSettings.Reset();
                _sensitivity.Value = SensitivityToSlider(InputSettings.MouseSensitivity);
                _invertY.On = InputSettings.InvertMouseY;
                _invertX.On = InputSettings.InvertMouseX;
                _scrollAllWeapons.On = InputSettings.ScrollAllWeapons;
                foreach (KeyRow row in rows)
                {
                    row.InvalidateVisual();
                }
            };
            page.Children.Add(reset);
        }

        private static int SensitivityToSlider(float sensitivity)
        {
            return Math.Clamp((int)Math.Round((sensitivity - 0.1f) / 2.9f * 100), 0, 100);
        }

        private static float SliderToSensitivity(int value)
        {
            return 0.1f + value / 100f * 2.9f;
        }

        // The weapon list runs 60%-200%, so the slider's 0-100 is that range
        // rather than a percentage of its own.
        private static int ScaleToSlider(float scale)
        {
            return Math.Clamp((int)Math.Round((scale - 0.6f) / 1.4f * 100), 0, 100);
        }

        private static float SliderToScale(int value)
        {
            return 0.6f + value / 100f * 1.4f;
        }

        // ---------------------------------------------------------- match rules

        private void BuildMatch()
        {
            StackPanel page = AddSection("Match rules");
            Heading(page, "Match rules");
            Explain(page, "Used by the matches you start yourself. A server you join brings "
                + "its own.");
            _pointGoal = Add(page, new FieldRow("Point goal", _settings.PointGoal, boxWidth: 120));
            _timeLimit = Add(page, new FieldRow("Time limit", _settings.TimeLimit, boxWidth: 120));
            _timeLimit.Box.Watermark = "m:ss";
            string[] damage = { "low", "medium", "high" };
            _damageRow = Add(page, new ChoiceRow("Damage", damage,
                Math.Max(0, Array.IndexOf(damage, _settings.DamageLevel))));
            _teamPlay = Add(page, new ToggleRow("Team play", _settings.TeamPlay == "on"));
            _friendlyFire = Add(page, new ToggleRow("Friendly fire", _settings.FriendlyFire == "on"));
            _radar = Add(page, new ToggleRow("Hunter radar", _settings.HunterRadar == "on"));
            _affinity = Add(page, new ToggleRow("Affinity weapons",
                _settings.AffinityWeapons == "on"));
            Explain(page, "Each hunter's own weapon does more, and is the only way to freeze, "
                + "burn or disrupt anybody.");
        }

        /// <summary>
        /// Who you are and where you play: the launcher's own preferences,
        /// which live in launcher.txt rather than in the game's settings.json.
        ///
        /// They were on a card of the front screen while this window was
        /// Windows-only and the other platforms had nothing else. They belong
        /// here: the front screen asks the questions a session needs answering
        /// now, and a default server address is not one of them.
        /// </summary>
        private void BuildLauncher()
        {
            StackPanel page = AddSection("Profile");
            Heading(page, "You");
            _playerName = Add(page, new FieldRow("Your name", LauncherPrefs.PlayerName,
                boxWidth: 200));
            // The seven playable hunters and Random, the same list the front
            // screen offers -- not every name in the enum, which also holds the
            // Guardian and the enemies' entries.
            string[] hunters = Enumerable.Range(0, 7)
                .Select(i => ((Hunter)i).ToString())
                .Append(Hunter.Random.ToString()).ToArray();
            _hunterRow = Add(page, new ChoiceRow("Hunter", hunters,
                Math.Max(0, Array.IndexOf(hunters, LauncherPrefs.LastHunter.ToString()))));
            Explain(page, "What the online and offline cards start out on. Either card can "
                + "still choose something else for one session.");

            Heading(page, "Servers");
            _serverRow = Add(page, new FieldRow("Default server",
                $"{LauncherPrefs.ServerAddress}:{LauncherPrefs.ServerPort}", boxWidth: 220));
            _masterRow = Add(page, new FieldRow("Server directory",
                $"{LauncherPrefs.MasterHost}:{LauncherPrefs.MasterPort}", boxWidth: 220));
            Explain(page, "host, or host:port. The directory is what Join lists, and what "
                + "runs an online match for you: this build never opens a port on your "
                + "own machine. To run a server yourself, use the dedicated server.");
            _autoUpdate = Add(page, new ToggleRow("Check for updates on startup",
                LauncherPrefs.AutoUpdate));
            Explain(page, "Asks GitHub whether there is a newer release and says so. It "
                + "downloads and installs nothing.");

            Heading(page, "Game files");
            var files = new MenuEntry("Game files", GameFiles.Describe(), titleSize: 15);
            files.SubtitleColor = GameFiles.Ready ? GuiTheme.Good : GuiTheme.Warm;
            files.Click += (_, _) =>
            {
                GameFilesRequested?.Invoke(this, EventArgs.Empty);
                Close();
            };
            page.Children.Add(files);
            Explain(page, "Where the extracted ROM lives, and how to point the game at a "
                + "different one. Nothing else here works without it.");
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

        // -------------------------------------------------------------- footer

        private Control BuildFooter()
        {
            var save = new MenuEntry(_inGame ? "Apply" : "Save and close", titleSize: 15)
            {
                Primary = true,
                Height = 40
            };
            save.Click += (_, _) => TryCommit();
            var cancel = new MenuEntry("Cancel", titleSize: 13)
            {
                Height = 26,
                Accent = GuiTheme.TextDim
            };
            cancel.Click += (_, _) => Close();
            _saveError = new Note("", GuiTheme.Warm) { IsVisible = false };
            var footer = new StackPanel
            {
                Spacing = 6,
                Margin = new Thickness(0, 12, 0, 0),
                VerticalAlignment = VerticalAlignment.Bottom
            };
            footer.Children.Add(save);
            footer.Children.Add(cancel);
            footer.Children.Add(_saveError);
            return footer;
        }

        /// <summary>
        /// Save, and say so on the window if it does not work.
        ///
        /// Writing settings.json touches the disk, and the disk is allowed to
        /// say no -- a read-only folder, a file open elsewhere, a full drive.
        /// That is worth a line on the screen, not an exception out of a window
        /// that may be sitting over a match still being played.
        /// </summary>
        private void TryCommit()
        {
            try
            {
                Commit();
            }
            catch (Exception ex)
            {
                _saveError.Text = $"Could not save: {ex.Message}";
                _saveError.IsVisible = true;
            }
        }

        private void Commit()
        {
            // Display
            if (_windowRow != null)
            {
                LauncherPrefs.WindowMode = _windowRow.Index == 1
                    ? WindowStartMode.BorderlessFullscreen
                    : WindowStartMode.Windowed;
                WindowMode.Startup = LauncherPrefs.WindowMode;
            }
            _settings.ResolutionScale = Math.Max(RenderOptions.MinScale, _resolutionScale.Value)
                .ToString(CultureInfo.InvariantCulture);
            _settings.Lighting = RenderOptions.OnOff(_lightingRow.On);
            _settings.Fog = RenderOptions.OnOff(_fogRow.On);
            _settings.TextureFiltering = RenderOptions.OnOff(_filteringRow.On);
            _settings.ShowFps = RenderOptions.OnOff(_fpsRow.On);
            _settings.CelShading = RenderOptions.OnOff(_celRow.On);
            _settings.CelBands = "8";
            _settings.CelEdge = "50";
            Features.HelmetOpacity = _helmetRow.On ? _helmetOpacity.Value / 100f : 0;
            Features.VisorOpacity = _helmetRow.On ? _visorOpacity.Value / 100f : 0;
            Features.HudOpacity = _hudOpacity.Value / 100f;
            Features.WeaponListScale = SliderToScale(_weaponListSize.Value);
            Features.FixedCrosshair = _fixedCrosshair.On;
            Features.CustomCrosshair = _customCrosshair.On;
            Features.ModernHud = _modernHud.On;
            Features.FixedWeapon = _fixedWeapon.On;
            // Audio
            _settings.SfxVolume = (_sfxVolume.Value / 100f).ToString(CultureInfo.InvariantCulture);
            _settings.MusicVolume = (_musicVolume.Value / 100f).ToString(CultureInfo.InvariantCulture);
            _settings.Language = _languageRow.Value;
            // Controls
            InputSettings.MouseSensitivity = SliderToSensitivity(_sensitivity.Value);
            InputSettings.InvertMouseY = _invertY.On;
            InputSettings.InvertMouseX = _invertX.On;
            InputSettings.ScrollAllWeapons = _scrollAllWeapons.On;
            InputSettings.Save();
            // The players in the match already have their own copies of these.
            InputSettings.ApplyToPlayers();
            // Match rules
            _settings.PointGoal = _pointGoal.Value;
            _settings.TimeLimit = _timeLimit.Value;
            _settings.DamageLevel = _damageRow.Value;
            _settings.TeamPlay = _teamPlay.On ? "on" : "off";
            _settings.FriendlyFire = _friendlyFire.On ? "on" : "off";
            _settings.HunterRadar = _radar.On ? "on" : "off";
            _settings.AffinityWeapons = _affinity.On ? "on" : "off";
            // Launcher preferences
            if (_playerName.Value.Trim().Length > 0)
            {
                LauncherPrefs.PlayerName = _playerName.Value.Trim();
            }
            LauncherPrefs.LastHunter = Enum.Parse<Hunter>(_hunterRow.Value);
            string host = LauncherPrefs.ServerAddress;
            int port = LauncherPrefs.ServerPort;
            if (ParseEndpoint(_serverRow.Value, ref host, ref port))
            {
                LauncherPrefs.ServerAddress = host;
                LauncherPrefs.ServerPort = port;
            }
            string masterHost = LauncherPrefs.MasterHost;
            int masterPort = LauncherPrefs.MasterPort;
            if (ParseEndpoint(_masterRow.Value, ref masterHost, ref masterPort))
            {
                LauncherPrefs.MasterHost = masterHost;
                LauncherPrefs.MasterPort = masterPort;
            }
            LauncherPrefs.AutoUpdate = _autoUpdate.On;
            GameState.CommitSettings(_settings);
            LauncherPrefs.Save();
            // Written and *applied*: the volumes, the language and the match
            // rules were only ever put in the file, so a music slider moved
            // here would otherwise leave the music exactly where it was --
            // during a match as well as before one, since this same window
            // opens from the pause menu.
            Mods.GameSettings.Apply(_settings);
            Saved = true;
            Close();
        }
    }
}
