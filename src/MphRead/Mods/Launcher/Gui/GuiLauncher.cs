using System;
using System.Collections.Generic;
using System.Threading;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Themes.Fluent;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// Entry point for the graphical launcher on the platforms WinForms cannot
    /// reach.
    ///
    /// The loop is <c>LauncherEntry</c>'s: one launcher, then a match, then the
    /// launcher again. What differs is that the window has to be raised and
    /// torn down around each match rather than shown as a dialog -- Avalonia
    /// owns a real application lifetime and does not offer a nested one, so the
    /// screen is a full app run per visit, and the answer comes back through a
    /// field the way the WinForms version passes it back off its STA thread.
    /// </summary>
    public static class GuiLauncher
    {
        /// <summary>
        /// Show the launcher, or say why it could not be shown.
        ///
        /// Returns false when there is no usable display -- a machine with no X
        /// or Wayland session, an SSH login without forwarding, a container, or
        /// a system missing the client libraries Avalonia binds. That is not an
        /// error worth stopping for: the text launcher does the same job, and
        /// falling back to it is the difference between "this build has no
        /// launcher on my machine" and "this build does not start".
        /// </summary>
        public static bool TryRun()
        {
            if (!Probe())
            {
                return false;
            }
            try
            {
                Run();
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[launcher] the window could not be opened: {ex.Message}");
                Console.WriteLine("[launcher] falling back to the text launcher");
                return false;
            }
        }

        /// <summary>
        /// Is there a display at all? Checked before Avalonia is initialised
        /// rather than by catching its failure, because the failure is a native
        /// abort in some configurations and there is nothing to catch.
        /// </summary>
        private static bool Probe()
        {
            if (OperatingSystem.IsWindows() || OperatingSystem.IsMacOS())
            {
                return true;
            }
            string? display = Environment.GetEnvironmentVariable("DISPLAY");
            string? wayland = Environment.GetEnvironmentVariable("WAYLAND_DISPLAY");
            if (String.IsNullOrEmpty(display) && String.IsNullOrEmpty(wayland))
            {
                Console.WriteLine("[launcher] no DISPLAY or WAYLAND_DISPLAY; "
                    + "using the text launcher");
                return false;
            }
            return true;
        }

        private static void Run()
        {
            if (GameFiles.Ready)
            {
                // Upstream's CheckSetup does this before anything runs; the
                // launcher is dispatched before that check, so it does it here
                // -- and tolerates the files being absent, which is the whole
                // reason it goes first.
                GameFiles.ApplyPaths();
            }
            IReadOnlyList<string> rooms = Array.Empty<string>();

            while (true)
            {
                MenuSettings settings = GameState.LoadSettings();
                // LoadSettings only fills in Features/Cheats/Bugfixes; the rest
                // of the file reaches the engine through Mods.GameSettings.
                Mods.GameSettings.Apply(settings);
                LauncherPrefs.Load();
                Mods.WindowMode.Startup = LauncherPrefs.WindowMode;
                if (rooms.Count == 0 && GameFiles.Ready)
                {
                    // Needs the game files: the room list is read out of them.
                    rooms = ThumbnailGenerator.MultiplayerRooms();
                }

                LaunchPlan plan = Ask(settings, rooms);
                if (plan.Kind == LaunchKind.None)
                {
                    return;
                }
                try
                {
                    MatchStart.Launch(settings, plan);
                }
                catch (Exception ex)
                {
                    Console.WriteLine();
                    Console.WriteLine($"The game could not start: {ex.Message}");
                    Console.WriteLine(ex.StackTrace);
                    return;
                }
                finally
                {
                    // Both own a worker thread and a bound socket; a crash in
                    // the game must not leave either behind.
                    NetSession.Stop();
                    NetHostSession.Stop();
                }
            }
        }

        /// <summary>
        /// Show the front screen and wait for an answer.
        ///
        /// On its own thread, and a fresh Avalonia lifetime each time. The GL
        /// context the game creates afterwards belongs to the thread that
        /// creates it, and a toolkit that has installed itself on that thread
        /// is a toolkit pumping messages underneath the render loop.
        /// </summary>
        private static LaunchPlan Ask(MenuSettings settings, IReadOnlyList<string> rooms)
        {
            LaunchPlan plan = default;
            Exception? failure = null;
            var thread = new Thread(() =>
            {
                try
                {
                    plan = ShowOnce(settings, rooms);
                }
                catch (Exception ex)
                {
                    failure = ex;
                }
            })
            {
                Name = "MphRead launcher",
                IsBackground = false
            };
            thread.Start();
            thread.Join();
            if (failure != null)
            {
                throw failure;
            }
            return plan;
        }

        private static LaunchPlan ShowOnce(MenuSettings settings, IReadOnlyList<string> rooms)
        {
            LaunchPlan plan = default;
            var lifetime = new ClassicDesktopStyleApplicationLifetime
            {
                ShutdownMode = Avalonia.Controls.ShutdownMode.OnMainWindowClose
            };
            AppBuilder.Configure<LauncherApp>()
                .UsePlatformDetect()
                .WithInterFont()
                .SetupWithLifetime(lifetime);
            var window = new HomeWindow(settings, rooms);
            lifetime.MainWindow = window;
            window.Show();
            lifetime.Start(Array.Empty<string>());
            plan = window.Plan;
            return plan;
        }
    }

    /// <summary>
    /// The Avalonia application object. Fluent is here for the handful of stock
    /// controls the screen uses -- the text boxes and the scroll bars; every
    /// other control on it is drawn by this code, for the same reason the
    /// WinForms screen draws its own.
    /// </summary>
    internal sealed class LauncherApp : Application
    {
        /// <summary>
        /// Dark, because two stock controls survive on this screen -- the text
        /// boxes and the scroll bars -- and they have to match the rest of it.
        /// Everything else is drawn by this code, for the same reason the
        /// WinForms screen draws its own.
        ///
        /// In Initialize rather than the constructor only because that is
        /// where Avalonia expects styles to be registered; both work.
        /// </summary>
        public override void Initialize()
        {
            Styles.Add(new FluentTheme());
            RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            base.Initialize();
        }
    }
}
