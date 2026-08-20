using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Reflection;
using System.Runtime.Versioning;
using System.Windows.Forms;
using MphRead.Mods;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// Settings, in the same language as the front screen: a rail of sections
    /// on the left, one page at a time on the right, everything painted here.
    ///
    /// It replaces a tabbed window whose first and largest page was a grid of
    /// maps. That page had a job when the launcher was the only way to pick
    /// one; the front screen does that now, with a picture of the map beside
    /// it, and a second map picker behind a tab was a second answer to a
    /// question already answered.
    ///
    /// The same window opens from the front screen and from the pause menu
    /// inside a match, which is why nothing here needs a restart to take
    /// effect except the window mode.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class SettingsForm : Form
    {
        private readonly LauncherTheme _theme;
        private readonly bool _ownsTheme;
        private readonly MenuSettings _settings;
        private readonly bool _inGame;
        private readonly Panel _rail = new();
        private readonly Panel _content = new();
        private readonly List<(MenuButton Button, FlowLayoutPanel Page)> _sections = new();

        private ChoiceRow _windowRow = null!;
        private ToggleRow _helmetRow = null!;
        private SliderRow _helmetOpacity = null!;
        private SliderRow _visorOpacity = null!;
        private SliderRow _hudOpacity = null!;
        private SliderRow _sfxVolume = null!;
        private SliderRow _musicVolume = null!;
        private ChoiceRow _languageRow = null!;
        private SliderRow _sensitivity = null!;
        private ToggleRow _invertY = null!;
        private ToggleRow _invertX = null!;
        private FieldBox _pointGoal = null!;
        private FieldBox _timeLimit = null!;
        private ChoiceRow _damageRow = null!;
        private ToggleRow _teamPlay = null!;
        private ToggleRow _friendlyFire = null!;
        private ToggleRow _radar = null!;
        private ToggleRow _affinity = null!;
        private readonly List<(PropertyInfo Property, ToggleRow Row)> _toggles = new();
        private Label _previewStatus = null!;
        private Label _saveError = null!;

        private const int _railWidth = 216;

        /// <summary>True when the user pressed save rather than closing.</summary>
        public bool Saved { get; private set; }

        public SettingsForm(MenuSettings settings, LauncherTheme? theme = null, bool inGame = false)
        {
            _settings = settings;
            _inGame = inGame;
            _theme = theme ?? new LauncherTheme(DeviceDpi);
            _ownsTheme = theme == null;

            Text = "MphRead settings";
            FormBorderStyle = FormBorderStyle.None;
            AutoScaleMode = AutoScaleMode.None;
            StartPosition = FormStartPosition.CenterParent;
            // Opened from the pause menu, this has to clear a game window that
            // may be borderless fullscreen -- which is a topmost window's job.
            // From the front screen it is an ordinary dialog and should behave
            // like one.
            TopMost = inGame;
            ShowInTaskbar = !inGame;
            BackColor = LauncherTheme.Ink;
            ForeColor = LauncherTheme.Text;
            KeyPreview = true;
            Rectangle work = Screen.PrimaryScreen?.WorkingArea ?? new Rectangle(0, 0, 1280, 800);
            ClientSize = new Size(
                Math.Min(_theme.S(980), work.Width - _theme.S(40)),
                Math.Min(_theme.S(660), work.Height - _theme.S(40)));

            _rail.BackColor = LauncherTheme.Panel;
            _content.BackColor = LauncherTheme.Ink;
            Controls.Add(_content);
            Controls.Add(_rail);

            BuildRail();
            BuildPages();
            Layout1();
            Resize += (_, _) => Layout1();
            // The pages were built before the content panel had its size, so
            // every row is still the fallback width until this runs.
            LayoutPages();
            Show(_sections[0].Page);
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            WindowChrome.RoundCorners(Handle);
        }

        private void Layout1()
        {
            int rail = _theme.S(_railWidth);
            _rail.SetBounds(0, 0, rail, ClientSize.Height);
            _content.SetBounds(rail, 0, ClientSize.Width - rail, ClientSize.Height);
            LayoutPages();
        }

        /// <summary>
        /// Give every row the width of the page it is on.
        ///
        /// A FlowLayoutPanel does not stretch its children, and these were
        /// created before the panel had a size: without this they keep the
        /// fallback width, which is how a settings page ends up as a narrow
        /// column with its explanations cut off.
        /// </summary>
        private void LayoutPages()
        {
            if (_sections.Count == 0)
            {
                return;
            }
            int width = Math.Max(_theme.S(320), Inner);
            foreach ((MenuButton _, FlowLayoutPanel page) in _sections)
            {
                foreach (Control control in page.Controls)
                {
                    control.Width = width;
                    if (control is Label label)
                    {
                        // Wrapped text needs the height the width implies.
                        Size measured = TextRenderer.MeasureText(label.Text, label.Font,
                            new Size(width, Int32.MaxValue), TextFormatFlags.WordBreak);
                        label.Height = measured.Height + _theme.S(6);
                    }
                }
            }
        }

        private void BuildRail()
        {
            var stack = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                BackColor = LauncherTheme.Panel,
                Padding = new Padding(_theme.S(18), _theme.S(20), _theme.S(14), _theme.S(14))
            };
            var title = new Caption(_theme, "Settings", 22, LauncherTheme.Text, 1, display: true)
            {
                Width = _theme.S(_railWidth - 32),
                Margin = new Padding(0, 0, 0, _theme.S(14))
            };
            stack.Controls.Add(title);
            _rail.Controls.Add(stack);
            _railStack = stack;
            // A drag anywhere on the rail moves the window; it has no frame.
            stack.MouseDown += (_, e) =>
            {
                if (e.Button == MouseButtons.Left)
                {
                    WindowChrome.DragFrom(this);
                }
            };
        }

        private FlowLayoutPanel _railStack = null!;

        private FlowLayoutPanel AddSection(string name)
        {
            var button = new MenuButton(_theme, name, titleSize: 15)
            {
                Width = _theme.S(_railWidth - 32),
                Height = _theme.S(32),
                Margin = new Padding(0, 0, 0, _theme.S(2))
            };
            var page = new FlowLayoutPanel
            {
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoScroll = true,
                BackColor = LauncherTheme.Ink,
                Padding = new Padding(_theme.S(26), _theme.S(22), _theme.S(26), _theme.S(22)),
                Visible = false,
                Dock = DockStyle.Fill
            };
            button.Click += (_, _) => Show(page);
            _railStack.Controls.Add(button);
            _content.Controls.Add(page);
            _sections.Add((button, page));
            return page;
        }

        private void Show(FlowLayoutPanel page)
        {
            foreach ((MenuButton button, FlowLayoutPanel candidate) in _sections)
            {
                candidate.Visible = candidate == page;
                button.Accent = candidate == page ? LauncherTheme.Accent : LauncherTheme.TextDim;
                button.Invalidate();
            }
            page.BringToFront();
        }

        private int Inner => _content.ClientSize.Width - _theme.S(64)
            - SystemInformation.VerticalScrollBarWidth;

        private T Add<T>(FlowLayoutPanel page, T control, int gap = 6) where T : Control
        {
            control.Width = Math.Max(_theme.S(260), Inner);
            control.Margin = new Padding(0, 0, 0, _theme.S(gap));
            page.Controls.Add(control);
            return control;
        }

        private Caption Heading(FlowLayoutPanel page, string text, int gap = 10)
        {
            return Add(page, new Caption(_theme, text, 20, LauncherTheme.Text, 1, display: true), gap);
        }

        private Label Note(FlowLayoutPanel page, string text, Color? color = null, int height = 40)
        {
            var label = new Label
            {
                AutoSize = false,
                Height = _theme.S(height),
                ForeColor = color ?? LauncherTheme.TextDim,
                BackColor = LauncherTheme.Ink,
                Font = _theme.Body(_theme.S(12)),
                Text = text
            };
            return Add(page, label, gap: 8);
        }

        private void BuildPages()
        {
            BuildDisplay();
            BuildAudio();
            BuildControls();
            BuildMatch();
            BuildToggles("Features", typeof(Features),
                "Behaviour changes and quality-of-life tweaks.");
            BuildToggles("Cheats", typeof(Cheats),
                "Applied when a session starts, and turned off outright while "
                + "you are connected to a server.");
            BuildToggles("Bugfixes", typeof(Bugfixes),
                "Corrections to original-game bugs. Off restores what the retail "
                + "game shipped with.");
            BuildPreviews();
            BuildFooter();
        }

        private void BuildDisplay()
        {
            FlowLayoutPanel page = AddSection("Display");
            Heading(page, "Window");
            _windowRow = Add(page, new ChoiceRow(_theme, "Mode", labelWidth: 120), gap: 4);
            _windowRow.SetItems(new[] { "Windowed", "Fullscreen (borderless)" },
                LauncherPrefs.WindowMode == WindowStartMode.BorderlessFullscreen ? 1 : 0);
            Note(page, "F11 switches at any time, and so does Alt+Enter. Escape opens the "
                + "pause menu, which can switch it too, and gives the mouse back so the "
                + "window can be moved or resized.", LauncherTheme.Accent, height: 52);

            Heading(page, "Helmet and HUD");
            _helmetRow = Add(page, new ToggleRow(_theme, "Draw the helmet",
                Features.HelmetOpacity > 0 || Features.VisorOpacity > 0,
                "All of it: the shell in front of and behind the readouts, and "
                + "the visor pane over the view."), gap: 4);
            _helmetRow.Toggled += (_, _) => UpdateHelmetRows();
            _helmetOpacity = Add(page, new SliderRow(_theme, "Helmet",
                (int)Math.Round(Features.HelmetOpacity * 100)), gap: 2);
            _visorOpacity = Add(page, new SliderRow(_theme, "Visor",
                (int)Math.Round(Features.VisorOpacity * 100)), gap: 4);
            Note(page, "The helmet is three layers: the shell behind the readouts, the "
                + "shell in front, and the visor pane over the view. Clearing only the "
                + "shell is what leaves a tinted pane with nothing behind it.", height: 44);
            _hudOpacity = Add(page, new SliderRow(_theme, "HUD readouts",
                (int)Math.Round(Features.HudOpacity * 100)), gap: 4);
            Note(page, "Energy, ammo, the radar and the rest. Separate from the helmet.",
                height: 26);
            UpdateHelmetRows();
        }

        private void UpdateHelmetRows()
        {
            bool on = _helmetRow.On;
            _helmetOpacity.Enabled = on;
            _visorOpacity.Enabled = on;
            if (on && _helmetOpacity.Value == 0)
            {
                // On with the shell at zero is the state that started this:
                // the visor frame still drawn, with nothing behind it.
                _helmetOpacity.Value = 100;
                if (_visorOpacity.Value == 0)
                {
                    _visorOpacity.Value = 50;
                }
            }
            _helmetOpacity.Invalidate();
            _visorOpacity.Invalidate();
        }

        private void BuildAudio()
        {
            FlowLayoutPanel page = AddSection("Audio");
            Heading(page, "Volume");
            _sfxVolume = Add(page, new SliderRow(_theme, "Sound effects",
                Percent(_settings.SfxVolume, 35)), gap: 2);
            _musicVolume = Add(page, new SliderRow(_theme, "Music",
                Percent(_settings.MusicVolume, 50)), gap: 10);
            Heading(page, "Language");
            _languageRow = Add(page, new ChoiceRow(_theme, "Text", labelWidth: 120), gap: 4);
            string[] languages = Enum.GetNames<Language>();
            int index = Math.Max(0, Array.IndexOf(languages, _settings.Language));
            _languageRow.SetItems(languages, index);
            Note(page, "The game's own text, from your own files.", height: 26);
        }

        private static int Percent(string stored, int fallback)
        {
            return Single.TryParse(stored, NumberStyles.Float, CultureInfo.InvariantCulture,
                out float parsed)
                ? Math.Clamp((int)Math.Round(parsed * 100), 0, 100)
                : fallback;
        }

        private void BuildControls()
        {
            FlowLayoutPanel page = AddSection("Controls");
            Heading(page, "Mouse");
            _sensitivity = Add(page, new SliderRow(_theme, "Sensitivity",
                SensitivityToSlider(InputSettings.MouseSensitivity),
                v => $"{SliderToSensitivity(v):0.00}x"), gap: 2);
            _invertY = Add(page, new ToggleRow(_theme, "Invert vertical aim",
                InputSettings.InvertMouseY), gap: 2);
            _invertX = Add(page, new ToggleRow(_theme, "Invert horizontal aim",
                InputSettings.InvertMouseX), gap: 10);

            Heading(page, "Keys");
            Note(page, "Click a binding and press a key, a mouse button or the wheel. "
                + "Backspace clears it, Escape leaves it alone.", height: 34);
            foreach (PropertyInfo property in InputSettings.Bindings)
            {
                Add(page, new KeyRow(_theme, property), gap: 2);
            }
            var reset = new MenuButton(_theme, "Reset to defaults", titleSize: 13)
            {
                Height = _theme.S(30)
            };
            reset.Accent = LauncherTheme.Warm;
            reset.Click += (_, _) =>
            {
                InputSettings.Reset();
                _sensitivity.Value = SensitivityToSlider(InputSettings.MouseSensitivity);
                _invertY.On = InputSettings.InvertMouseY;
                _invertX.On = InputSettings.InvertMouseX;
                foreach (Control control in page.Controls)
                {
                    control.Invalidate();
                }
            };
            Add(page, reset, gap: 10);
        }

        private static int SensitivityToSlider(float sensitivity)
        {
            return Math.Clamp((int)Math.Round((sensitivity - 0.1f) / 2.9f * 100), 0, 100);
        }

        private static float SliderToSensitivity(int value)
        {
            return 0.1f + value / 100f * 2.9f;
        }

        private void BuildMatch()
        {
            FlowLayoutPanel page = AddSection("Match rules");
            Heading(page, "Match rules");
            Note(page, "Used by the matches you start yourself. A server you join brings "
                + "its own.", height: 34);
            _pointGoal = Add(page, new FieldBox(_theme, "Point goal", labelWidth: 120), gap: 4);
            _pointGoal.Value = _settings.PointGoal;
            _timeLimit = Add(page, new FieldBox(_theme, "Time limit", labelWidth: 120), gap: 4);
            _timeLimit.Value = _settings.TimeLimit;
            _timeLimit.Placeholder = "m:ss";
            _damageRow = Add(page, new ChoiceRow(_theme, "Damage", labelWidth: 120), gap: 10);
            string[] damage = { "low", "medium", "high" };
            _damageRow.SetItems(damage, Math.Max(0, Array.IndexOf(damage, _settings.DamageLevel)));
            _teamPlay = Add(page, new ToggleRow(_theme, "Team play",
                _settings.TeamPlay == "on"), gap: 2);
            _friendlyFire = Add(page, new ToggleRow(_theme, "Friendly fire",
                _settings.FriendlyFire == "on"), gap: 2);
            _radar = Add(page, new ToggleRow(_theme, "Hunter radar",
                _settings.HunterRadar == "on"), gap: 2);
            _affinity = Add(page, new ToggleRow(_theme, "Affinity weapons",
                _settings.AffinityWeapons == "on",
                "Each hunter's own weapon does more, and is the only way to freeze, "
                + "burn or disrupt anybody."), gap: 2);
        }

        private void BuildToggles(string name, Type type, string blurb)
        {
            FlowLayoutPanel page = AddSection(name);
            Heading(page, name);
            Note(page, blurb, height: 34);
            foreach (PropertyInfo property in type.GetProperties(
                BindingFlags.Public | BindingFlags.Static))
            {
                if (property.PropertyType != typeof(bool) || !property.CanRead || !property.CanWrite)
                {
                    continue;
                }
                var row = Add(page, new ToggleRow(_theme, Humanize(property.Name),
                    (bool)(property.GetValue(null) ?? false)), gap: 1);
                _toggles.Add((property, row));
            }
        }

        private void BuildPreviews()
        {
            FlowLayoutPanel page = AddSection("Map previews");
            Heading(page, "Map previews");
            Note(page, "The pictures beside the map on the front screen. They are rendered "
                + "here, from your own files -- nothing is downloaded, and none of it is "
                + "part of this program.", height: 52);
            _previewStatus = Note(page, PreviewStatus(), height: 30);
            var generate = new MenuButton(_theme, "Generate the missing ones", titleSize: 14)
            {
                Height = _theme.S(38)
            };
            generate.Click += (_, _) => GeneratePreviews(generate);
            Add(page, generate, gap: 8);
        }

        private static string PreviewStatus()
        {
            int missing = ThumbnailGenerator.MissingThumbnails().Count;
            return missing == 0
                ? "Every map has one."
                : $"{missing} map(s) have none yet.";
        }

        private void GeneratePreviews(MenuButton button)
        {
            // The batch spawns its own processes and takes a while; a player
            // who pressed this has nothing else to do in this window.
            button.Enabled = false;
            button.Text = "Generating";
            _previewStatus.Text = "Rendering... the game will open and close once per map.";
            Application.DoEvents();
            try
            {
                IReadOnlyList<string> missing = ThumbnailGenerator.MissingThumbnails();
                if (missing.Count > 0)
                {
                    ThumbnailBatch.Run(missing, ThumbnailBatch.DefaultParallelism,
                        ThumbnailGenerator.ThumbnailWidth, ThumbnailGenerator.ThumbnailHeight);
                }
                _previewStatus.Text = missing.Count == 0
                    ? "Every map already had one."
                    : $"Rendered {missing.Count}. Reopen the launcher to see them.";
            }
            finally
            {
                button.Enabled = true;
                button.Text = "Generate the missing ones";
            }
        }

        private void BuildFooter()
        {
            var footer = new FlowLayoutPanel
            {
                Dock = DockStyle.Bottom,
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoSize = true,
                BackColor = LauncherTheme.Panel,
                Padding = new Padding(_theme.S(18), _theme.S(10), _theme.S(14), _theme.S(14))
            };
            var save = new MenuButton(_theme, _inGame ? "Apply" : "Save and close", titleSize: 15)
            {
                Primary = true,
                Width = _theme.S(_railWidth - 32),
                Height = _theme.S(40),
                Margin = new Padding(0, 0, 0, _theme.S(6))
            };
            save.Click += (_, _) => TryCommit();
            _saveError = new Label
            {
                AutoSize = false,
                Height = 0,
                Visible = false,
                ForeColor = LauncherTheme.Warm,
                BackColor = LauncherTheme.Panel,
                Font = _theme.Body(_theme.S(11)),
                Width = _theme.S(_railWidth - 32)
            };
            var cancel = new MenuButton(_theme, "Cancel", titleSize: 13)
            {
                Width = _theme.S(_railWidth - 32),
                Height = _theme.S(26)
            };
            cancel.Accent = LauncherTheme.TextDim;
            cancel.Click += (_, _) => Close();
            footer.Controls.Add(save);
            footer.Controls.Add(cancel);
            footer.Controls.Add(_saveError);
            _rail.Controls.Add(footer);
            footer.BringToFront();
        }

        private static string Humanize(string name)
        {
            var builder = new System.Text.StringBuilder(name.Length + 8);
            for (int i = 0; i < name.Length; i++)
            {
                char c = name[i];
                if (i > 0 && Char.IsUpper(c) && !Char.IsUpper(name[i - 1]))
                {
                    builder.Append(' ');
                    builder.Append(Char.ToLowerInvariant(c));
                }
                else
                {
                    builder.Append(i == 0 ? Char.ToUpperInvariant(c) : c);
                }
            }
            return builder.ToString();
        }

        /// <summary>
        /// Save, and say so on the window if it does not work.
        ///
        /// The window used to let whatever went wrong out into the process.
        /// From the front screen that is a crash dialog over a launcher; from
        /// the pause menu it is worse, because the settings window runs on the
        /// pause menu's own thread and taking that thread out leaves a match
        /// running behind a menu that no longer answers. Either way the person
        /// at the keyboard is told nothing except that they have to press
        /// Cancel.
        ///
        /// Writing settings.json touches the disk, and the disk is allowed to
        /// say no -- a read-only folder, a file open elsewhere, a full drive.
        /// That is worth a line on the screen, not a stack trace.
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
                _saveError.Height = TextRenderer.MeasureText(_saveError.Text, _saveError.Font,
                    new Size(_saveError.Width, Int32.MaxValue), TextFormatFlags.WordBreak).Height
                    + _theme.S(8);
                _saveError.Visible = true;
            }
        }

        private void Commit()
        {
            // Display
            LauncherPrefs.WindowMode = _windowRow.Index == 1
                ? WindowStartMode.BorderlessFullscreen
                : WindowStartMode.Windowed;
            WindowMode.Startup = LauncherPrefs.WindowMode;
            Features.HelmetOpacity = _helmetRow.On ? _helmetOpacity.Value / 100f : 0;
            Features.VisorOpacity = _helmetRow.On ? _visorOpacity.Value / 100f : 0;
            Features.HudOpacity = _hudOpacity.Value / 100f;
            // Audio
            _settings.SfxVolume = (_sfxVolume.Value / 100f).ToString(CultureInfo.InvariantCulture);
            _settings.MusicVolume = (_musicVolume.Value / 100f).ToString(CultureInfo.InvariantCulture);
            _settings.Language = _languageRow.Value;
            // Controls
            InputSettings.MouseSensitivity = SliderToSensitivity(_sensitivity.Value);
            InputSettings.InvertMouseY = _invertY.On;
            InputSettings.InvertMouseX = _invertX.On;
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
            // Features, cheats, bugfixes
            foreach ((PropertyInfo property, ToggleRow row) in _toggles)
            {
                property.SetValue(null, row.On);
            }
            GameState.CommitSettings(_settings);
            LauncherPrefs.Save();
            // Written and *applied*. The volumes, the language and the match
            // rules were only ever put in the file: nothing on this path read
            // them back, so the music slider moved a number in settings.json
            // and left the music exactly where it was -- during a match as
            // well as before one, since this same window opens from the pause
            // menu.
            Mods.GameSettings.Apply(_settings);
            Saved = true;
            Close();
        }

        protected override bool ProcessCmdKey(ref System.Windows.Forms.Message message, Keys keyData)
        {
            if (keyData == Keys.Escape)
            {
                Close();
                return true;
            }
            return base.ProcessCmdKey(ref message, keyData);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing && _ownsTheme)
            {
                _theme.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
