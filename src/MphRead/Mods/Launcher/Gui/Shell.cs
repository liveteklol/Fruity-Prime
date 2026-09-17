using System;
using System.Collections.Generic;
using Avalonia.Controls;
using Avalonia.VisualTree;
using MphRead.Mods.Network;
using MphRead.Mods.Render;
using OpenTK.Mathematics;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// One window, for the whole program.
    ///
    /// The loop this replaces was: open an Avalonia window, wait for it to
    /// close, open a GL window, run the match in it, destroy it, open another
    /// Avalonia window. Two toolkits, two windows, and a visible seam at every
    /// boundary -- the launcher vanished from the taskbar when a match
    /// started, the match vanished when it ended, and anything that went wrong
    /// in between left the player looking at an empty desktop.
    ///
    /// Now the GL window is opened once and never closed until the player
    /// leaves. The launcher is drawn inside it (<see cref="UiSurface"/>,
    /// <see cref="UiOverlay"/>), a match is a <see cref="Scene"/> built into
    /// that same window, and leaving a match unloads the scene and puts the
    /// front screen back up. The pause menu and the settings are screens in
    /// the same surface rather than borderless windows chasing the game's
    /// rectangle.
    ///
    /// This is the shape the Android head has always had -- one surface, one
    /// view stack over it, matches loaded and unloaded underneath -- which is
    /// why the screens needed no changes to be drawn here.
    ///
    /// Everything below runs on the game's own thread, between frames. The
    /// toolkit is set up on that thread and the screens post their work to its
    /// dispatcher, which <see cref="UiSurface.Tick"/> drains once a frame; a
    /// decision a screen makes is therefore acted on at the top of the next
    /// frame rather than in the middle of the one that is being drawn.
    /// </summary>
    internal static class Shell
    {
        /// <summary>True while the shell window is the one running.</summary>
        public static bool Active { get; private set; }

        /// <summary>Is a screen up and taking the input?</summary>
        public static bool UiVisible => UiSurface.Current?.Visible == true;

        private static RenderWindow? _window;
        private static StartScreen? _front;
        private static InGameMenu? _menu;
        private static MenuSettings _settings = new MenuSettings();
        private static IReadOnlyList<string> _rooms = Array.Empty<string>();
        private static LaunchPlan? _pending;
        private static bool _endMatch;
        private static bool _quit;

        /// <summary>
        /// Open the window and run until the player quits.
        ///
        /// False means there is nothing to run in: no display, no GL, or a
        /// toolkit that will not start on this machine. The text launcher
        /// plays the same matches and is what the caller falls back to.
        /// </summary>
        public static bool Run()
        {
            if (UiSurface.Ensure() == null)
            {
                return false;
            }
            LauncherPrefs.Load();
            // The backdrop is GL's from here on: this is the one head with a
            // window under the screens, and the photograph is worth the
            // window's own pixels rather than the screens' capped ones. Said
            // before the first screen is built, because it decides what goes
            // into the bake. See Mods.Render.LauncherPhoto.
            Mods.Render.LauncherPhoto.Enabled = true;
            if (GameFiles.Ready)
            {
                // Upstream's CheckSetup does this before anything runs; the
                // launcher is dispatched before that check, so it does it here
                // -- and tolerates the files being absent, which is the whole
                // reason it goes first.
                GameFiles.ApplyPaths();
                // A map added after the install was set up has no picture and
                // no sweep coming to give it one.
                ThumbnailGenerator.EnsureCustomPreviews();
            }
            // How the window opens: the way this one was left, unless the
            // command line said otherwise for this run.
            if (!Mods.WindowMode.StartupForced)
            {
                Mods.WindowMode.Startup = LauncherPrefs.WindowMode;
            }
            RenderWindow.LogCreatingWindow();
            RenderWindow? window = null;
            try
            {
                window = new RenderWindow(shell: true);
                _window = window;
                Active = true;
                ShowFrontScreen();
                window.Run();
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"The window could not be opened: {ex.Message}");
                Mods.DebugLog.Exception("launcher", ex);
                return false;
            }
            finally
            {
                Active = false;
                _window = null;
                _pending = null;
                _endMatch = false;
                _quit = false;
                // Both own a worker thread and a bound socket; leaving the
                // program must not leave either behind.
                NetSession.Stop();
                NetHostSession.Stop();
                window?.Dispose();
            }
        }

        // --------------------------------------------------------- the frame

        /// <summary>
        /// Act on what the screens decided since the last frame: start a
        /// match, end one, or leave.
        /// </summary>
        internal static void BeforeFrame(RenderWindow window)
        {
            if (!Active)
            {
                return;
            }
            if (_quit)
            {
                _quit = false;
                window.Close();
                return;
            }
            if (window.HasScene && NetSession.PersistentLobby && NetSession.IsInLobby && !_endMatch)
                EndNetworkMatchToLobby(window);
            if (window.HasScene && (NetSession.Refused || NetSession.SessionTimedOut)) _endMatch = true;
            if (_endMatch)
            {
                _endMatch = false;
                EndMatch(window);
            }
            if (_pending is LaunchPlan plan)
            {
                _pending = null;
                StartMatch(window, plan);
            }
        }

        /// <summary>
        /// Draw whatever screen is up into the overlay texture.
        ///
        /// Not only in the shell: the pause menu is the same surface, and a
        /// window opened by one of the harness commands has one too.
        /// </summary>
        internal static void TickUi(RenderWindow window)
        {
            UiSurface? surface = UiSurface.Current;
            if (surface == null)
            {
                UiOverlay.Visible = false;
                return;
            }
            // The window's size, every frame, whether or not a screen is up.
            // It used to be read only while one was, so a window made bigger
            // during a match told the surface nothing: Escape then built the
            // pause menu against the size the window had when the match
            // started.
            surface.Resize(window.FramebufferSize.X, window.FramebufferSize.Y);
            if (!surface.Visible)
            {
                UiOverlay.Visible = false;
                return;
            }
            surface.Tick();
        }

        // -------------------------------------------------------- the screens

        private static void ShowFrontScreen()
        {
            UiSurface? surface = UiSurface.Current;
            if (surface == null)
            {
                return;
            }
            PauseMenu.Reset();
            // Read again rather than reusing the object from the last time
            // round: the settings page opened from the pause menu loads and
            // commits its own copy, so after a match this one is stale and
            // would write the old values back over it.
            _settings = GameState.LoadSettings();
            // LoadSettings only fills in Features; the rest of the file
            // reaches the engine through Mods.GameSettings.
            Mods.GameSettings.Apply(_settings);
            LauncherPrefs.Load();
            if (_rooms.Count == 0 && GameFiles.Ready)
            {
                // Needs the game files: the room list is read out of them.
                _rooms = ThumbnailGenerator.MultiplayerRooms();
            }
            if (_window != null)
            {
                _window.Title = Mods.Branding.Name;
            }
            if (_front == null)
            {
                // Before the screen that offers "Random" as a hunter: the roll
                // is held for one launch so the joined server and the loaded
                // player agree, and this is where a launch begins.
                Hunters.Reroll();
                _front = new StartScreen(_settings, _rooms);
                _front.Done += (_, plan) => Decided(plan);
                _front.MatchRequested += (_, plan) => Decided(plan);
            }
            else
            {
                // One front screen for the life of the process, the way the
                // Android head has always kept one: Reset is what makes it
                // usable again -- the stack emptied, the hunter rerolled, the
                // room list and the version line read afresh.
                _front.Reset();
            }
            surface.Show(_front);
        }

        private static void Decided(LaunchPlan plan)
        {
            if (plan.Kind == LaunchKind.None)
            {
                RequestQuit();
                return;
            }
            _pending = plan;
        }

        // -------------------------------------------------------- the match

        /// <summary>
        /// What the running match was started from, so another can be started
        /// like it. See <see cref="PlayAnother"/>.
        /// </summary>
        private static LaunchPlan? _played;

        /// <summary>
        /// Whether "play the map the results screen picked" means anything
        /// here: an offline match of one's own, started from this shell.
        ///
        /// Online there is a server with a rotation and this is not its
        /// business; a story match and a demo have no next map to pick.
        /// </summary>
        public static bool CanPlayAnother => Active && _pending == null
            && _played is LaunchPlan plan && plan.Kind == LaunchKind.Offline;

        /// <summary>
        /// Load another map, exactly as this match was loaded, with the room
        /// the results screen chose.
        ///
        /// The same two requests the pause menu's "Leave match" and the front
        /// screen's "Start" already use, sent together: the frame that ends the
        /// match starts the next one, so the launcher is built and hidden
        /// again without ever being drawn. Offline that is the whole of what a
        /// rotation is.
        /// </summary>
        public static void PlayAnother(string roomKey)
        {
            if (!CanPlayAnother || String.IsNullOrWhiteSpace(roomKey)
                || _played is not LaunchPlan plan)
            {
                return;
            }
            _endMatch = true;
            _pending = plan with { RoomKey = roomKey };
        }

        private static void StartMatch(RenderWindow window, LaunchPlan plan)
        {
            _played = plan;
            _front?.SuspendLobby();
            UiSurface.Current?.Hide();
            try
            {
                if (!MatchStart.Begin(window, _settings, plan))
                {
                    NetSession.ReportMatchLoadFailed("The map could not be loaded.");
                    EndMatch(window);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine();
                Console.WriteLine($"The game could not start: {ex.Message}");
                Console.WriteLine(ex.StackTrace);
                // The Windows build is a GUI binary with no console behind it,
                // so the two lines above reach nobody: from the player's side
                // the game simply disappears while a map is loading. This is
                // the one place that can still be read afterwards -- and the
                // whole reason the switch in the settings exists.
                Mods.DebugLog.Line("crash", "the match could not start");
                Mods.DebugLog.Exception("crash", ex);
                // Back to the front screen rather than out of the program: a
                // map that will not load is a reason to pick another one.
                NetSession.ReportMatchLoadFailed(ex.Message);
                EndMatch(window);
            }
        }

        private static void EndNetworkMatchToLobby(RenderWindow window)
        {
            CloseMenu(); window.EndScene(); MatchStart.AfterMatch();
            NetSession.ResetMatchState(); PauseMenu.Reset();
            if (_front != null) { UiSurface.Current?.Show(_front); _front.ResumeLobby(); }
        }

        private static void EndMatch(RenderWindow window)
        {
            CloseMenu();
            window.EndScene();
            // Both own a worker thread and a bound socket; a match that ends
            // any way at all must not leave either behind.
            NetSession.Stop();
            NetHostSession.Stop();
            MatchStart.AfterMatch();
            ShowFrontScreen();
        }

        /// <summary>The match is over; the launcher comes back.</summary>
        public static void RequestEndMatch()
        {
            if (!Active)
            {
                return;
            }
            _endMatch = true;
        }

        /// <summary>Leave the program.</summary>
        public static void RequestQuit()
        {
            _quit = true;
        }

        /// <summary>
        /// What the pause menu's "Leave match" and "Quit" mean to a window.
        ///
        /// In the shell they are the two requests above; in one of the harness
        /// windows, where there is no launcher to come back to, both still
        /// mean what they always meant -- close the window and end the run.
        /// </summary>
        internal static void LeaveMatch(OpenTK.Windowing.Desktop.GameWindow window)
        {
            if (Active)
            {
                RequestEndMatch();
                return;
            }
            window.Close();
        }

        internal static void Quit(OpenTK.Windowing.Desktop.GameWindow window)
        {
            if (Active)
            {
                RequestQuit();
                return;
            }
            window.Close();
        }

        // ---------------------------------------------------- the pause menu

        /// <summary>
        /// Open the in-game menu. False when there is no surface to draw it on
        /// -- Escape then keeps its old meaning rather than doing nothing.
        /// </summary>
        internal static bool OpenPauseMenu()
        {
            UiSurface? surface = UiSurface.Ensure();
            if (surface == null)
            {
                return false;
            }
            if (_menu != null)
            {
                return true;
            }
            // The settings page in the menu commits its own copy of the file,
            // so the one the menu is built with is read now rather than kept
            // from whenever the match started.
            var menu = new InGameMenu(GameState.LoadSettings());
            menu.Emptied += (_, _) => CloseMenu();
            _menu = menu;
            surface.Show(menu);
            return true;
        }

        /// <summary>Take it down and give the match its input back.</summary>
        internal static void CloseMenu()
        {
            if (_menu == null)
            {
                return;
            }
            _menu = null;
            UiSurface.Current?.Hide();
            PauseMenu.MarkClosed();
        }

        // ------------------------------------------------------------ capture

        private static string? _shotDirectory;
        private static int _shotStep;
        private static int _shotWait;

        /// <summary>
        /// Photograph the shell rather than play in it: `-shellshot DIR`.
        ///
        /// <c>-uishot</c> renders the screens on their own and proves the
        /// layout; this proves what replaced the windows. It walks the whole
        /// loop the change is about -- front screen, a match loaded into the
        /// same window, the pause menu over it, leaving the match, the front
        /// screen again -- and photographs the *window* at each stop, so a
        /// picture that is black or a step that never arrives is a failure
        /// with a name rather than a report that "the launcher looks wrong".
        ///
        /// Escape is pressed on the front screen before anything else: on that
        /// screen it is the quit prompt, so a second picture that differs from
        /// the first is the keyboard reaching a screen that is no longer a
        /// window.
        /// </summary>
        public static void RequestShots(string directory)
        {
            _shotDirectory = directory;
            _shotStep = 0;
            _shotWait = 0;
            // The window is the capture's for the duration, not the player's:
            // the script maximizes it half way through, and a screenshot run
            // must not be how somebody's window size changes. See
            // WindowGeometry.Enabled.
            Mods.WindowGeometry.Enabled = false;
        }

        /// <summary>
        /// Between the draw and the buffer swap, which is the only moment the
        /// window's back buffer holds this frame. Called from both halves of
        /// the frame -- with a match and without one.
        ///
        /// A list of steps rather than a switch on a frame number: the script
        /// is a sequence, and every insertion into a numbered one renumbered
        /// the rest.
        /// </summary>
        internal static void AfterDraw(RenderWindow window)
        {
            if (_shotDirectory == null)
            {
                return;
            }
            if (_shotWait > 0)
            {
                _shotWait--;
                return;
            }
            Action<RenderWindow>[] script = Script;
            if (_shotStep >= script.Length)
            {
                _shotDirectory = null;
                window.Close();
                return;
            }
            script[_shotStep++](window);
        }

        /// <summary>
        /// What the capture does, in order. Each step ends by saying how many
        /// frames to leave before the next one -- a window is not on the
        /// screen the moment it is created (a buffer read before the window
        /// manager has mapped it comes back black under Mesa), a room loads
        /// into a fade, and a window mode takes a few frames to settle.
        /// </summary>
        private static Action<RenderWindow>[] Script => new Action<RenderWindow>[]
        {
            _ => Wait(20),
            w => { Shot(w, "shell-start"); Escape(); Wait(15); },
            // Escape on the front screen is the quit prompt, so a second
            // picture that differs from the first is the keyboard reaching a
            // screen that is no longer a window.
            w => { Shot(w, "shell-escape"); Escape(); Wait(10); },
            // The pointer resting on a word, pressing nothing. Every word
            // glides towards the accent and a little to the right over about
            // 140 ms, and this is the one picture that says so -- fifteen
            // frames is a quarter of a second, so the glide is over by the
            // time it is taken. Settings rather than Play, because Play starts
            // amber and was the only word whose reaction was ever visible.
            _ => { Hover(c => c is UiWord word && word.Text == "Settings"); Wait(15); },
            w => { Shot(w, "shell-hover"); Wait(2); },
            // A click on a word rather than a key: the pointer has its own
            // arithmetic between GLFW and a screen drawn into the frame, and a
            // picture of the settings page is that arithmetic being right.
            _ => { ClickSettings(); Wait(15); },
            w => { Shot(w, "shell-click"); Escape(); Wait(10); },
            // Resized, maximised and fullscreen, all on the front screen: a
            // screen that does not follow the window is the fault this
            // sequence exists for.
            w => { w.ClientSize = new Vector2i(1000, 620); Wait(20); },
            w => { Shot(w, "shell-resized"); w.WindowState = OpenTK.Windowing.Common.WindowState.Maximized; Wait(20); },
            w => { Shot(w, "shell-maximized"); w.WindowState = OpenTK.Windowing.Common.WindowState.Normal; Wait(20); },
            w => { Mods.WindowMode.Toggle(w); Wait(25); },
            // The click that matters: at 1.5x, where a pointer conversion off
            // by the scale factor puts every press in the corner of the screen
            // and nothing can be pressed at all. The windowed click above
            // cannot catch that -- the scale there is 1.
            w => { Shot(w, "shell-fullscreen"); ClickSettings(); Wait(15); },
            w =>
            {
                Shot(w, "shell-fullscreen-click");
                Escape();
                Mods.WindowMode.Toggle(w);
                Wait(25);
            },
            w => { Shot(w, "shell-windowed"); Wait(5); },
            // Through the screens rather than by handing the shell a plan:
            // picking a map is two presses now -- one to select the row, one
            // on the tick -- and the picture after the first of them has to be
            // the play screen and not a match.
            _ => { Click(c => c is UiWord word && word.Text == "Play"); Wait(20); },
            w => { Shot(w, "shell-play"); Key(Keys.Right); Wait(15); },
            w => { Shot(w, "shell-play-offline"); Click(c => c is UiListRow); Wait(15); },
            w =>
            {
                // Still the play screen: a click on a row selects it and
                // starts nothing. A picture of a room here is the regression.
                Shot(w, "shell-play-selected");
                Click(c => c is UiMark mark && mark.Label == "start");
                Wait(40);
            },
            w =>
            {
                if (w.HasScene)
                {
                    Wait(0);
                    return;
                }
                // The screens could not start one -- no game files, or a map
                // list that came up empty. Ask the shell directly so the rest
                // of the sequence still runs.
                Console.WriteLine("[shellshot] the play screen started nothing; asking directly");
                if (!StartShotMatch())
                {
                    Console.WriteLine("[shellshot] no room to load; stopping after the screens");
                    _shotDirectory = null;
                    w.Close();
                    return;
                }
                Wait(40);
            },
            // Made bigger *during* the match, with no screen up: the surface
            // hears nothing about the window until a screen is shown again,
            // which is the state Escape then has to size itself against.
            w => { Shot(w, "shell-match"); w.WindowState = OpenTK.Windowing.Common.WindowState.Maximized; Wait(30); },
            w => { PauseMenu.HandleEscape(w); Wait(20); },
            w =>
            {
                Shot(w, "shell-pause-maximized");
                PauseMenu.HandleEscape(w);
                w.WindowState = OpenTK.Windowing.Common.WindowState.Normal;
                Wait(30);
            },
            w => { Mods.WindowMode.Toggle(w); Wait(30); },
            // The pause menu over a fullscreen match, which is where a menu
            // drawn into the wrong viewport shows up worst.
            w => { Shot(w, "shell-match-fullscreen"); PauseMenu.HandleEscape(w); Wait(20); },
            w => { Shot(w, "shell-pause-fullscreen"); Mods.WindowMode.Toggle(w); Wait(30); },
            // The settings, in a match and in the smallest window the
            // sequence uses: the page that has to fit is this one, and the
            // in-game screens are drawn larger than the launcher's.
            w => { Shot(w, "shell-pause"); Click(c => c is UiWord word && word.Text == "Settings"); Wait(20); },
            w => { Shot(w, "shell-settings-ingame"); Escape(); Wait(15); },
            // The match *ending*, in fullscreen, rather than being left: a
            // different path out (the engine fades and quits the scene itself)
            // and the one the front screen comes back from. "The text is small
            // again in the launcher when the game finishes" was about this
            // frame and not about the one Leave match produces.
            w => { Escape(); Mods.WindowMode.Toggle(w); Wait(30); },
            _ => { EndShotMatch(); Wait(90); },
            w => { Shot(w, "shell-back-fullscreen"); Mods.WindowMode.Toggle(w); Wait(25); },
            w => { Shot(w, "shell-back"); Wait(5); }
        };

        private static void Wait(int frames)
        {
            _shotWait = frames;
        }

        private static void ClickSettings()
        {
            Click(c => c is UiWord word && word.Text == "Settings");
        }

        private static void Hover(Func<Control, bool> match)
        {
            if (!(UiSurface.Current?.HoverOn(match) ?? false))
            {
                Console.WriteLine("[shellshot] nothing on screen matched the hover");
            }
        }

        private static void Click(Func<Control, bool> match)
        {
            if (!(UiSurface.Current?.ClickOn(match) ?? false))
            {
                Console.WriteLine("[shellshot] nothing on screen matched the click");
            }
        }

        private static void Key(Keys key)
        {
            UiSurface? surface = UiSurface.Current;
            surface?.KeyDown(key, Avalonia.Input.RawInputModifiers.None);
            surface?.KeyUp(key, Avalonia.Input.RawInputModifiers.None);
        }

        private static void Escape()
        {
            UiSurface? surface = UiSurface.Current;
            surface?.KeyDown(OpenTK.Windowing.GraphicsLibraryFramework.Keys.Escape,
                Avalonia.Input.RawInputModifiers.None);
            surface?.KeyUp(OpenTK.Windowing.GraphicsLibraryFramework.Keys.Escape,
                Avalonia.Input.RawInputModifiers.None);
        }

        /// <summary>An offline match on the first room the build knows.</summary>
        private static bool StartShotMatch()
        {
            if (_rooms.Count == 0)
            {
                return false;
            }
            Decided(new LaunchPlan
            {
                Kind = LaunchKind.Offline,
                RoomKey = _rooms[0],
                Mode = GameMode.Battle,
                Hunter = Hunter.Samus,
                Bots = 1,
                BotLevel = 1
            });
            return true;
        }

        /// <summary>
        /// End the match the way running out of time does: the engine plays
        /// its own end sequence, fades and quits the scene. Shortened to a
        /// fifth of a second of results, since the capture is about what comes
        /// after it.
        /// </summary>
        private static void EndShotMatch()
        {
            GameState.MatchState = MatchState.Ending;
            GameState.MatchTime = 0.2f;
        }

        private static void Shot(RenderWindow window, string name)
        {
            string path = System.IO.Path.Combine(_shotDirectory!, $"{name}.png");
            System.IO.Directory.CreateDirectory(_shotDirectory!);
            Console.WriteLine($"[shellshot] {name}: pixels={window.FramebufferSize} "
                + $"scene={(window.HasScene ? window.Scene.Size.ToString() : "no match")} "
                + $"{UiSurface.Current?.Describe()}");
            bool saved = Mods.ScreenCapture.SaveWindow(
                window.FramebufferSize.X, window.FramebufferSize.Y, path);
            Console.WriteLine(saved
                ? $"[shellshot] {path}"
                : $"[shellshot] {name} could not be read from the window");
        }

        // -------------------------------------------------------------- input

        public static void PointerMoved(double x, double y)
        {
            UiSurface.Current?.PointerMoved(x, y);
        }

        public static void PointerButton(MouseButton button, double x, double y, bool down)
        {
            UiSurface? surface = UiSurface.Current;
            if (surface == null)
            {
                return;
            }
            // Where the click landed, before the click itself: the pointer is
            // the system's while a screen is up, so this is the first the
            // toolkit hears of it if the player moved and clicked between two
            // frames.
            surface.PointerMoved(x, y);
            surface.PointerButton(Translate(button), down);
        }

        public static void PointerWheel(double deltaX, double deltaY)
        {
            UiSurface.Current?.PointerWheel(deltaX, deltaY);
        }

        public static void KeyDown(KeyboardKeyEventArgs e)
        {
            UiSurface.Current?.KeyDown(e.Key, Modifiers(e));
        }

        public static void KeyUp(KeyboardKeyEventArgs e)
        {
            UiSurface.Current?.KeyUp(e.Key, Modifiers(e));
        }

        public static void TextInput(string text)
        {
            UiSurface.Current?.TextInput(text);
        }

        private static Avalonia.Input.MouseButton Translate(MouseButton button)
        {
            return button switch
            {
                MouseButton.Button2 => Avalonia.Input.MouseButton.Right,
                MouseButton.Button3 => Avalonia.Input.MouseButton.Middle,
                _ => Avalonia.Input.MouseButton.Left
            };
        }

        private static Avalonia.Input.RawInputModifiers Modifiers(KeyboardKeyEventArgs e)
        {
            Avalonia.Input.RawInputModifiers modifiers = Avalonia.Input.RawInputModifiers.None;
            if (e.Shift)
            {
                modifiers |= Avalonia.Input.RawInputModifiers.Shift;
            }
            if (e.Control)
            {
                modifiers |= Avalonia.Input.RawInputModifiers.Control;
            }
            if (e.Alt)
            {
                modifiers |= Avalonia.Input.RawInputModifiers.Alt;
            }
            return modifiers;
        }
    }
}
