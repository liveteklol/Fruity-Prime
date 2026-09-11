using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using MphRead.Mods.Network;
using MphRead.Mods.Update;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// The front screen: a picture, three words, and nothing to read before
    /// you can play.
    ///
    /// The layout is id-Tech3's and OpenQuake3/defrag's: the menu anchored to
    /// the bottom-left corner growing upward from the one word that matters,
    /// the build under it, the wordmark in the opposite corner. Every other
    /// screen in the launcher is now built from the same three anchors --
    /// <see cref="UiLayout"/> holds them -- so that Play, Settings and the
    /// pause menu read as the same program rather than as four that happen to
    /// ship together.
    ///
    /// It is a <see cref="UserControl"/> rather than a <see cref="Window"/>
    /// for one reason: Android has no windows. The desktop puts it in
    /// <see cref="HomeWindow"/>, which is a frame and nothing else; the
    /// Android head hands this same object to Avalonia as its single view.
    /// There is no second front screen to keep in step, which is the point --
    /// a phone-shaped copy was the previous arrangement and it drifted within
    /// a release.
    ///
    /// Everything it opens is pushed onto one stack over the picture, on every
    /// platform. There used to be three windows on the desktop and overlays on
    /// the phone, which is two arrangements of the same four screens.
    /// </summary>
    internal sealed class StartScreen : UserControl
    {
        private readonly MenuSettings _settings;
        private readonly List<string> _rooms;
        private readonly Panel _overlay;
        private readonly List<Control> _stack = new();
        private readonly TextBlock _version;
        private readonly Border _versionBox;
        private readonly StackPanel _menu;

        private bool _finished;
        private bool _updatable;
        private bool _updating;

        /// <summary>What the screen decided. Kind None means it was closed.</summary>
        public LaunchPlan Plan { get; private set; }

        /// <summary>Raised once, when the screen is done with.</summary>
        public event EventHandler<LaunchPlan>? Done;

        public StartScreen(MenuSettings settings, IReadOnlyList<string> rooms)
        {
            _settings = settings;
            _rooms = new List<string>(rooms);
            Focusable = true;

            Panel root = UiLayout.Backdrop();

            _menu = UiLayout.Column(26);
            _menu.Children.Add(Word("Play", GuiTheme.Title, 60, GuiTheme.Warm,
                () => _ = OpenPlay()));
            var rest = new StackPanel { Spacing = 16 };
            rest.Children.Add(Word("Settings", GuiTheme.Display, UiLayout.WordSize,
                GuiTheme.Text, () => _ = OpenSettings()));
            rest.Children.Add(Word("Quit", GuiTheme.Display, UiLayout.WordSize,
                GuiTheme.Text, AskToQuit));
            _menu.Children.Add(rest);
            root.Children.Add(_menu);

            _version = new TextBlock
            {
                Text = VersionNumber(),
                FontFamily = GuiTheme.Display,
                FontSize = 12,
                Foreground = GuiTheme.TextDimBrush
            };
            // The whole line is the button, rather than a label with one beside
            // it: the state and the action are the same fact here -- dim is
            // "nothing to do" and amber is "press this" -- and two controls
            // saying that would be one of them redundant in either state.
            _versionBox = new Border
            {
                Background = Brushes.Transparent,
                HorizontalAlignment = HorizontalAlignment.Left,
                VerticalAlignment = VerticalAlignment.Bottom,
                Margin = new Thickness(22, 0, 0, UiLayout.FooterBottom),
                Child = _version
            };
            _versionBox.PointerPressed += (_, e) =>
            {
                if (_updatable)
                {
                    e.Handled = true;
                    UpdateNow();
                }
            };
            root.Children.Add(_versionBox);
            root.Children.Add(UiLayout.Wordmark());

            _overlay = new Panel { Background = Brushes.Transparent, IsVisible = false };
            root.Children.Add(_overlay);
            Content = root;

            if (LauncherPrefs.AutoUpdate)
            {
                // In the background, and never blocking the window: a launcher
                // that will not draw until GitHub answers looks broken on a bad
                // connection.
                Updater.CheckInBackground(
                    _ => Dispatcher.UIThread.Post(RefreshVersionLine),
                    () => Dispatcher.UIThread.Post(RefreshVersionLine));
            }
            RefreshVersionLine();
            _ = CatchUpPreviews();
        }

        private static UiWord Word(string text, FontFamily font, double size,
            Color colour, Action go)
        {
            var word = new UiWord(text, size, font, colour);
            word.Click += (_, _) => go();
            return word;
        }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            // A fresh install has nothing to play, so the one thing it needs is
            // the whole screen rather than one refused entry among three.
            if (!GameFiles.Ready && _stack.Count == 0)
            {
                OpenSetup();
            }
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape && _stack.Count == 0)
            {
                AskToQuit();
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
        }

        /// <summary>
        /// Come back from a match and be usable again. Android keeps this view
        /// alive across a match, where the desktop builds a new one each time
        /// round <see cref="GuiLauncher"/>'s loop.
        /// </summary>
        public void Reset()
        {
            _finished = false;
            Plan = default;
            while (_stack.Count > 0)
            {
                Pop();
            }
            // Android keeps one of these for the life of the app, so this is
            // where a launch begins there -- the roll behind "Random" is held
            // for exactly one launch.
            Hunters.Reroll();
            LauncherPrefs.Load();
            RefreshRooms();
            RefreshVersionLine();
            if (!GameFiles.Ready)
            {
                OpenSetup();
            }
        }

        /// <summary>
        /// Answer a back gesture: Escape and the phone's back button are the
        /// same question. True when this screen dealt with it.
        /// </summary>
        public bool GoBack()
        {
            if (_stack.Count == 0)
            {
                return false;
            }
            Pop();
            return true;
        }

        // -------------------------------------------------------------- stack

        private void Push(Control view)
        {
            _stack.Add(view);
            _overlay.Children.Clear();
            _overlay.Children.Add(view);
            _overlay.IsVisible = true;
            _menu.IsVisible = false;
            _versionBox.IsVisible = false;
            Dispatcher.UIThread.Post(() => view.Focus(), DispatcherPriority.Background);
        }

        private void Pop()
        {
            if (_stack.Count > 0)
            {
                _stack.RemoveAt(_stack.Count - 1);
            }
            _overlay.Children.Clear();
            if (_stack.Count > 0)
            {
                Control top = _stack[^1];
                _overlay.Children.Add(top);
                Dispatcher.UIThread.Post(() => top.Focus(), DispatcherPriority.Background);
                return;
            }
            _overlay.IsVisible = false;
            _menu.IsVisible = true;
            _versionBox.IsVisible = true;
            RefreshVersionLine();
            Dispatcher.UIThread.Post(() => Focus(), DispatcherPriority.Background);
        }

        /// <summary>Hand the answer back, once.</summary>
        private void Finish(LaunchPlan plan)
        {
            if (_finished)
            {
                return;
            }
            _finished = true;
            Plan = plan;
            Done?.Invoke(this, plan);
        }

        // ------------------------------------------------------------- screens

        private Task OpenPlay()
        {
            if (!GameFiles.Ready)
            {
                OpenSetup();
                return Task.CompletedTask;
            }
            var view = new PlayScreen(_settings, _rooms);
            view.Closed += (_, _) => Pop();
            view.Launched += (_, plan) => Finish(plan);
            Push(view);
            return Task.CompletedTask;
        }

        private Task OpenSettings()
        {
            var view = new SettingsView(_settings);
            view.Closed += (_, _) => Pop();
            view.GameFilesRequested += (_, _) =>
            {
                Pop();
                OpenSetup();
            };
            Push(view);
            return Task.CompletedTask;
        }

        private void OpenSetup()
        {
            var view = new SetupScreen();
            view.Closed += (_, _) =>
            {
                Pop();
                RefreshRooms();
            };
            Push(view);
        }

        private void AskToQuit()
        {
            var view = new ConfirmScreen($"Quit {Mods.Branding.Name}?");
            view.Answered += (_, yes) =>
            {
                Pop();
                if (yes)
                {
                    Finish(default);
                }
            };
            Push(view);
        }

        /// <summary>
        /// The pause menu, over a running match.
        ///
        /// The platform with no windows takes this path; the desktop opens
        /// <see cref="PauseMenuWindow"/>, which is a real window over the game
        /// window. Both show the same <see cref="PauseMenuView"/>, so the menu
        /// cannot drift into being two menus.
        /// </summary>
        public void ShowPauseMenu(Action onResume, Action onLeave, Action onQuit)
        {
            var view = new PauseMenuView(offerWindowMode: false);
            view.Resumed += (_, _) => { Pop(); onResume(); };
            view.LeaveRequested += (_, _) => { Pop(); onLeave(); };
            view.QuitRequested += (_, _) => { Pop(); onQuit(); };
            view.SpectateRequested += (_, _) =>
            {
                Pop();
                SpectatorMode.Start();
                onResume();
            };
            view.RejoinRequested += (_, _) =>
            {
                Pop();
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
                Pop();
                onResume();
            };
            view.VoteMapRequested += (_, _) => OpenVote();
            view.SettingsRequested += (_, _) =>
            {
                var settings = new SettingsView(_settings, inGame: true);
                settings.Closed += (_, _) => Pop();
                Push(settings);
            };
            Push(view);
            view.FocusResume();
        }

        /// <summary>
        /// Pick a map and put it to the room -- the same screen a match is
        /// chosen from, with the strip of sources taken away.
        /// </summary>
        private void OpenVote()
        {
            string why = MapVote.WhyNotProposing();
            if (why.Length > 0)
            {
                // Said in the game's own chat rather than in a box here: it is
                // one sentence, the player is about to go back to the match,
                // and a dialog for it is a second thing to dismiss.
                Chat.ChatBox.System(why);
                return;
            }
            var view = new PlayScreen(_settings, _rooms, PlayScreen.Face.Vote,
                overGame: true);
            view.Closed += (_, _) => Pop();
            view.Voted += (_, room) =>
            {
                MapVote.Propose(room);
                // Both this and the pause menu under it: the answer arrives as
                // a prompt over the match, which is not a thing to read through
                // a menu.
                Pop();
                Pop();
            };
            Push(view);
        }

        // -------------------------------------------------------------- version

        /// <summary>
        /// Render the pictures of any map that does not have one yet, without
        /// being asked. Nothing happens in the ordinary case, which is every
        /// map already having one.
        /// </summary>
        private async Task CatchUpPreviews()
        {
            if (!GameFiles.Ready || !ThumbnailHost.CanRender
                || ThumbnailGenerator.MissingThumbnails().Count == 0)
            {
                return;
            }
            await ThumbnailHost.RenderMissingAsync(_ => { });
        }

        private void RefreshRooms()
        {
            if (!GameFiles.Ready)
            {
                return;
            }
            _rooms.Clear();
            foreach (string room in ThumbnailGenerator.MultiplayerRooms())
            {
                _rooms.Add(room);
            }
        }

        /// <summary>
        /// "1.2.3", or what to say instead when this build is not a release.
        /// Not <c>BuildVersion.Display</c>, which puts a v in front: this is a
        /// corner of a picture rather than a sentence.
        /// </summary>
        private static string VersionNumber()
        {
            Version? current = BuildVersion.Current;
            return current == null ? "a local build" : current.ToString(3);
        }

        private void Say(string text, Color colour, bool pressable = false)
        {
            _version.Text = text;
            _version.Foreground = new SolidColorBrush(colour);
            _updatable = pressable;
            _versionBox.Cursor = new Cursor(
                pressable ? StandardCursorType.Hand : StandardCursorType.Arrow);
        }

        /// <summary>
        /// Three states, not two. Amber is "there is a newer build, press
        /// this"; green is "this is the published one"; dim is everything else
        /// -- a local build, or a check that has not answered. Painting "no
        /// answer" green would be the one wrong thing this can do: a server
        /// refuses a client on a different build at Hello, so being told you
        /// are current when nobody has checked is worse than being told
        /// nothing.
        /// </summary>
        private void RefreshVersionLine()
        {
            if (_updating)
            {
                return;
            }
            string number = VersionNumber();
            if (Updater.Available != null)
            {
                Say($"{number} -- update available, click here", GuiTheme.Warm,
                    pressable: true);
                return;
            }
            Say(number, BuildVersion.IsRelease && Updater.Checked
                ? GuiTheme.Good : GuiTheme.TextDim);
        }

        /// <summary>
        /// Take the update.
        ///
        /// On the desktop that still means opening the release page: a release
        /// is an archive somebody unpacks over their own copy, and a program
        /// that rewrote its own files while running would have to solve
        /// restarting itself on three operating systems to save one unzip.
        /// Where the platform can install for itself -- a phone, today -- it
        /// fetches the file and hands it to the system installer instead.
        /// </summary>
        private void UpdateNow()
        {
            UpdateInfo? found = Updater.Available;
            if (found == null)
            {
                return;
            }
            UpdateInfo update = found.Value;
            if (UpdateInstall.CanInstall(update))
            {
                _ = FetchAndInstall(update, UpdateInstall.Current!);
                return;
            }
            if (!Updater.OpenPage(update))
            {
                // No browser to open, or it refused. Putting the address on the
                // line beats a button that appears to do nothing.
                Say(update.PageUrl, GuiTheme.Warm);
            }
        }

        private async Task FetchAndInstall(UpdateInfo update, IUpdateInstaller installer)
        {
            if (_updating)
            {
                return;
            }
            string number = VersionNumber();
            if (!installer.Allowed)
            {
                // This stage has to leave the line pressable: on a phone,
                // allowing this app as an install source is a Settings screen,
                // nothing here can wait for it, and the player comes back and
                // presses again.
                Say($"{number} -- allow installs from this app, then press again",
                    GuiTheme.Warm, pressable: true);
                installer.RequestPermission();
                return;
            }
            _updating = true;
            installer.Finished = (ok, message) => Dispatcher.UIThread.Post(() =>
            {
                _updating = false;
                Say(ok ? number : $"{number} -- {message}",
                    ok ? GuiTheme.TextDim : GuiTheme.Warm, pressable: !ok);
            });
            string label = update.AssetName.Length > 0 ? update.AssetName : update.Tag;
            Say($"{number} -- downloading {label}...", GuiTheme.Warm);
            var reported = new object();
            int shown = -1;
            void Progress(float fraction)
            {
                // Whole percents only, and only when one changes: this is
                // called for every 64 KB and each post crosses to the UI thread.
                int percent = fraction < 0 ? -1 : (int)(fraction * 100);
                lock (reported)
                {
                    if (percent == shown)
                    {
                        return;
                    }
                    shown = percent;
                }
                Dispatcher.UIThread.Post(() => Say(percent < 0
                    ? $"{number} -- downloading {label}..."
                    : $"{number} -- downloading {label}... {percent}%", GuiTheme.Warm));
            }
            string error = "";
            bool ready = await Task.Run(() => installer.Prepare(update, Progress, out error));
            if (!ready)
            {
                _updating = false;
                Say($"{number} -- {(error.Length > 0 ? error : "the download failed")}",
                    GuiTheme.Warm, pressable: true);
                return;
            }
            Say(installer.ExitAfterInstall
                ? $"{number} -- restarting to finish..."
                : $"{number} -- waiting for the system installer...", GuiTheme.Warm);
            if (!installer.Install(out error))
            {
                _updating = false;
                Say($"{number} -- {(error.Length > 0 ? error : "the install could not be started")}",
                    GuiTheme.Warm, pressable: true);
                return;
            }
            if (installer.ExitAfterInstall)
            {
                // The copying process is already running and waiting for this
                // one to be gone before it touches a single file. Staying open
                // would leave it waiting until its own deadline.
                Finish(default);
            }
        }
    }
}
