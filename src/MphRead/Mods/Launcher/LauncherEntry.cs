using System;
using System.Collections.Generic;
using System.Runtime.Versioning;
using System.Threading;
using System.Windows.Forms;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// Entry point for the graphical launcher (-launcher). Windows-only
    /// because it uses WinForms; other platforms keep the console prompts,
    /// which remain the full-featured path.
    ///
    /// The screen the user sees is <see cref="HomeForm"/>; the old settings
    /// window is still reachable from it and still owns everything that is
    /// not a per-session choice.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public static class LauncherEntry
    {
        /// <param name="keepConsole">
        /// Leave the console window on screen (-console). Off by default: a
        /// player double-clicking the exe should get a launcher, not a
        /// launcher and a black terminal.
        /// </param>
        public static void Run(bool keepConsole = false)
        {
            // Upstream's CheckSetup does this before anything runs; the
            // launcher is dispatched before that check, so it does it here --
            // and tolerates the files being absent, which is the whole reason
            // it goes first.
            if (GameFiles.Ready)
            {
                GameFiles.ApplyPaths();
            }
            MenuSettings settings = GameState.LoadSettings();
            // LoadSettings only fills in Features/Cheats/Bugfixes; the rest of
            // the file -- volumes, language, match rules -- was read into a
            // MenuSettings object and never reached the engine on this path.
            // See Mods.GameSettings.
            Mods.GameSettings.Apply(settings);
            LauncherPrefs.Load();
            // The window belongs to the game, not to this form: hand the
            // start mode over before anything opens one. The helmet is not
            // here on purpose -- it is Features.HelmetOpacity, which
            // GameState.LoadSettings has just read from settings.json, and a
            // second copy of it in launcher.txt would overwrite whatever the
            // console menu had set.
            Mods.WindowMode.Startup = LauncherPrefs.WindowMode;
            IReadOnlyList<string> rooms = ThumbnailGenerator.MultiplayerRooms();

            // One launcher, then a match, then the launcher again -- the way
            // every game with a front screen behaves. "Leave match" in the
            // pause menu comes back here; "Quit" is the only thing that ends
            // the program, along with closing the launcher itself.
            while (true)
            {
                PauseMenu.Reset();
                // Read again rather than reusing the object loaded above: the
                // pause menu's settings window loads and commits its own copy,
                // so after a match this one is stale and would write the old
                // values back over it.
                settings = GameState.LoadSettings();
                Mods.GameSettings.Apply(settings);
                LauncherPrefs.Load();
                LaunchPlan plan = AskWhatToPlay(settings, rooms);
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
                    // With no console this would be a window that never
                    // appears and no explanation anywhere.
                    Mods.ConsoleWindow.Show();
                    Console.WriteLine();
                    Console.WriteLine($"The game could not start: {ex.Message}");
                    Console.WriteLine(ex.StackTrace);
                    MessageBox.Show($"The game could not start:\n\n{ex.Message}",
                        Mods.Branding.Name, MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }
                finally
                {
                    // Both own a worker thread and a bound socket; a crash in
                    // the game must not leave either behind.
                    NetSession.Stop();
                    NetHostSession.Stop();
                }
                if (PauseMenu.QuitProgram)
                {
                    return;
                }
            }
        }

        /// <summary>
        /// Show the front screen and wait for an answer.
        ///
        /// WinForms needs a single-threaded apartment (file dialogs and image
        /// handling go through OLE), but Main is not [STAThread] and marking it
        /// would change the apartment the game itself runs in. Showing the
        /// window on its own STA thread keeps that requirement local, and
        /// leaves the game on the thread its GL context expects.
        /// </summary>
        private static LaunchPlan AskWhatToPlay(MenuSettings settings,
            IReadOnlyList<string> rooms)
        {
            LaunchPlan plan = default;
            var thread = new Thread(() =>
            {
                ApplicationConfiguration.Initialize();
                using var form = new HomeForm(settings, rooms);
                if (form.ShowDialog() == DialogResult.OK)
                {
                    plan = form.Plan;
                }
            });
            thread.SetApartmentState(ApartmentState.STA);
            thread.Start();
            thread.Join();
            return plan;
        }
    }
}
