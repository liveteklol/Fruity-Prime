using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.Versioning;
using System.Text;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// First-time setup, done from a button instead of by dragging a ROM onto
    /// the executable.
    ///
    /// The extraction itself is upstream's (`Extract.Setup`, the same code the
    /// drag-and-drop path runs) and it is run in a child process rather than
    /// called directly: it asks its questions and reports its errors on the
    /// console, with `Console.ReadKey` waits that would hang a window with no
    /// console attached. A child process gets the prompts answered on stdin
    /// and its output back as text to put on screen.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public static class GameFiles
    {
        private static string PathsFile => Path.Combine(AppContext.BaseDirectory, "paths.txt");

        /// <summary>
        /// The oldest paths.txt this build can read. Upstream keeps the same
        /// number in Program; it is repeated here because the launcher runs
        /// before the check that uses it, and a launcher that opened onto a
        /// stale paths.txt would fail later with no explanation.
        /// </summary>
        private static readonly Version _minExtractVersion = new Version(0, 19, 0, 0);

        /// <summary>True when a match could be loaded right now.</summary>
        public static bool Ready => Problem() == null;

        /// <summary>What is wrong with the setup, or null when nothing is.</summary>
        public static string? Problem()
        {
            if (!File.Exists(PathsFile))
            {
                return "No game files yet";
            }
            try
            {
                string first = File.ReadAllText(PathsFile).Split('\n')[0].Trim();
                if (!Version.TryParse(first, out Version? extracted)
                    || extracted < _minExtractVersion)
                {
                    return "The extracted files are from an older version -- set up again";
                }
            }
            catch (IOException)
            {
                return "paths.txt could not be read";
            }
            try
            {
                // ChooseMphPath as well as UpdatePaths: FileSystem reads the
                // entry for whichever version is current, and the default is
                // the one an EU or JP dump does not fill in.
                ApplyPaths();
                string root = Paths.FileSystem;
                if (String.IsNullOrWhiteSpace(root) || !Directory.Exists(root))
                {
                    return "The extracted files are missing -- set up again";
                }
            }
            catch (Exception)
            {
                return "No Metroid Prime Hunters files are configured";
            }
            return null;
        }

        /// <summary>"Ready -- AMHE1" for the button's second line.</summary>
        public static string Describe()
        {
            string? problem = Problem();
            if (problem != null)
            {
                return problem;
            }
            try
            {
                return $"Ready -- {Paths.MphKey}";
            }
            catch (Exception)
            {
                return "Ready";
            }
        }

        /// <summary>
        /// Extract a .nds ROM into files this build can load.
        ///
        /// Returns true when paths.txt exists and points somewhere real
        /// afterwards -- the child's exit code says nothing useful, because
        /// upstream's setup reports a bad ROM by printing and waiting for a
        /// key rather than by failing.
        /// </summary>
        public static bool RunSetup(string romPath, Action<string> report)
        {
            string? exe = Environment.ProcessPath;
            if (exe == null)
            {
                report("Could not find the MphRead executable.");
                return false;
            }
            var info = new ProcessStartInfo(exe)
            {
                WorkingDirectory = AppContext.BaseDirectory,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };
            // One argument, no switches: that is the form upstream's setup
            // recognises, and it is what dragging a ROM onto the exe produces.
            info.ArgumentList.Add(romPath);
            try
            {
                using Process? child = Process.Start(info);
                if (child == null)
                {
                    report("Could not start the extraction.");
                    return false;
                }
                var output = new StringBuilder();
                child.OutputDataReceived += (_, e) =>
                {
                    if (e.Data != null)
                    {
                        output.AppendLine(e.Data);
                        report(e.Data);
                    }
                };
                child.ErrorDataReceived += (_, e) =>
                {
                    if (e.Data != null)
                    {
                        output.AppendLine(e.Data);
                        report(e.Data);
                    }
                };
                child.BeginOutputReadLine();
                child.BeginErrorReadLine();
                // Two answers cover both questions setup can ask: "a path is
                // already set, update it?" and "press any key to exit" after
                // a failure. Sending them up front means neither can hang.
                try
                {
                    child.StandardInput.WriteLine("y");
                    child.StandardInput.WriteLine();
                    child.StandardInput.Flush();
                }
                catch (IOException)
                {
                    // The child may have exited before reading; not an error.
                }
                if (!child.WaitForExit(10 * 60 * 1000))
                {
                    child.Kill(entireProcessTree: true);
                    report("The extraction took too long and was stopped.");
                    return false;
                }
            }
            catch (Exception ex)
            {
                report($"The extraction failed: {ex.Message}");
                return false;
            }
            string? problem = Problem();
            if (problem != null)
            {
                report(problem);
                return false;
            }
            return true;
        }

        /// <summary>
        /// Finish what upstream's CheckSetup would have done. The launcher
        /// runs before that check -- it has to, or a fresh install could never
        /// reach the screen that fixes it -- so it does this part itself.
        /// </summary>
        public static void ApplyPaths()
        {
            Paths.UpdatePaths();
            Paths.ChooseMphPath();
            Paths.ChooseFhPath();
        }
    }
}
