using System;
using System.Diagnostics;
using System.Formats.Tar;
using System.IO;
using System.IO.Compression;
using System.Threading;

namespace MphRead.Mods.Update
{
    /// <summary>
    /// Replace this installation with the release's own package, without
    /// anybody opening a browser.
    ///
    /// The awkward part is that a program cannot overwrite the file it is
    /// running from -- Windows refuses outright, and on Unix it works in a way
    /// that is worse than refusing. So the swap is done by a **second copy of
    /// the new build**: the archive is unpacked beside the installation, the
    /// unpacked binary is started with <c>-applyupdate</c>, this process
    /// exits, and that copy waits for it to be gone, copies itself and
    /// everything beside it over the installation, and starts it again.
    ///
    /// Running the *new* binary as the one doing the copying, rather than the
    /// old one, is what makes it a single mechanism: the old build never has
    /// to know how a future release wants to be laid out, and the file doing
    /// the work is never one of the files being replaced.
    ///
    /// Nothing is deleted from the installation. The copy is a copy over the
    /// top, which is exactly what the instructions on the release page have
    /// always said to do by hand -- so a player's <c>paths.txt</c>, their
    /// <c>controls.txt</c>, their saves and their extracted game files are
    /// where they were.
    /// </summary>
    public static class DesktopUpdate
    {
        /// <summary>The argument that turns a launch into the copying half.</summary>
        public const string ApplyFlag = "applyupdate";

        /// <summary>Where the download and the unpacked build wait.</summary>
        private static string Staging => Path.Combine(AppContext.BaseDirectory, ".update");

        private static string StagedBuild => Path.Combine(Staging, "staged");

        /// <summary>Why the last attempt produced nothing.</summary>
        public static string? LastError { get; private set; }

        /// <summary>
        /// Whether this installation can be replaced in place: a published
        /// build, in a directory this user may write to.
        ///
        /// A read-only directory is the ordinary case for a system-wide
        /// install, and the answer there is the release page, not a failure
        /// half way through a copy.
        /// </summary>
        public static bool Supported
        {
            get
            {
                if (OperatingSystem.IsAndroid() || !BuildVersion.IsRelease)
                {
                    return false;
                }
                try
                {
                    string probe = Path.Combine(AppContext.BaseDirectory, ".update-probe");
                    File.WriteAllBytes(probe, Array.Empty<byte>());
                    File.Delete(probe);
                    return true;
                }
                catch (Exception)
                {
                    return false;
                }
            }
        }

        /// <summary>
        /// Fetch and unpack, and report whether the swap can now be started.
        ///
        /// Everything that can fail happens here, while the program is still
        /// running and can say so on screen. By the time <see cref="Launch"/>
        /// is called there is a complete, unpacked build on disk and the only
        /// work left is copying it.
        /// </summary>
        public static bool Stage(UpdateInfo update, Action<float>? progress = null,
            CancellationToken cancel = default)
        {
            LastError = null;
            if (update.AssetUrl.Length == 0)
            {
                LastError = "this release has no package for this platform";
                return false;
            }
            try
            {
                Clean();
                Directory.CreateDirectory(Staging);
                bool zip = update.AssetName.EndsWith(".zip", StringComparison.OrdinalIgnoreCase);
                string archive = Path.Combine(Staging, zip ? "package.zip" : "package.tar.gz");
                if (!UpdateDownload.Fetch(update.AssetUrl, archive, update.AssetSize,
                    progress, cancel))
                {
                    LastError = UpdateDownload.LastError ?? "the download failed";
                    return false;
                }
                Directory.CreateDirectory(StagedBuild);
                if (zip)
                {
                    ZipFile.ExtractToDirectory(archive, StagedBuild, overwriteFiles: true);
                }
                else
                {
                    using FileStream compressed = File.OpenRead(archive);
                    using var plain = new GZipStream(compressed, CompressionMode.Decompress);
                    // The tar reader is what carries the executable bit across;
                    // a zip has none to carry, which is why Windows ships one.
                    TarFile.ExtractToDirectory(plain, StagedBuild, overwriteFiles: true);
                }
                File.Delete(archive);
                string binary = Path.Combine(StagedBuild, BinaryName());
                if (!File.Exists(binary))
                {
                    LastError = $"the package does not contain {BinaryName()}";
                    return false;
                }
                MakeExecutable(binary);
                return true;
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                Console.WriteLine($"[update] could not stage the update: {ex}");
                return false;
            }
        }

        /// <summary>
        /// Start the unpacked build in its copying mode and return.
        ///
        /// The caller's next act must be to exit: the copy waits for this
        /// process to be gone before it touches anything, and a launcher that
        /// stayed open would leave it waiting until its own deadline.
        /// </summary>
        public static bool Launch()
        {
            try
            {
                string binary = Path.Combine(StagedBuild, BinaryName());
                var start = new ProcessStartInfo(binary)
                {
                    WorkingDirectory = StagedBuild,
                    UseShellExecute = false
                };
                start.ArgumentList.Add("-" + ApplyFlag);
                start.ArgumentList.Add(AppContext.BaseDirectory);
                start.ArgumentList.Add(Environment.ProcessId.ToString());
                return Process.Start(start) != null;
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                Console.WriteLine($"[update] could not start the update: {ex}");
                return false;
            }
        }

        /// <summary>
        /// The copying half, in the new build: wait for the old one to go,
        /// copy over it, start it again.
        ///
        /// Console output rather than a window on purpose. This runs for about
        /// a second between one launcher closing and the next opening, and a
        /// window in the middle of that would be a flash nobody can read; what
        /// it is for is the log somebody reads when the game did not come
        /// back.
        /// </summary>
        public static int Apply(string target, int waitFor)
        {
            Console.WriteLine($"[update] applying to {target}");
            WaitForExit(waitFor);
            string source = AppContext.BaseDirectory;
            try
            {
                Copy(source, target);
            }
            catch (Exception ex)
            {
                // Half a copy is the one outcome worth being loud about: the
                // installation may be a mix of two builds, and the staged one
                // is still on disk to finish by hand.
                Console.WriteLine($"[update] the copy failed: {ex.Message}");
                Console.WriteLine($"[update] the new build is in {source} -- "
                    + $"copy it over {target} by hand");
                return 1;
            }
            try
            {
                string binary = Path.Combine(target, BinaryName());
                MakeExecutable(binary);
                Process.Start(new ProcessStartInfo(binary)
                {
                    WorkingDirectory = target,
                    UseShellExecute = false
                });
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[update] updated, but could not restart: {ex.Message}");
                return 1;
            }
            Console.WriteLine("[update] done");
            return 0;
        }

        /// <summary>
        /// Wait for the old process to be gone, and give up rather than hang.
        ///
        /// Thirty seconds is far longer than a launcher takes to close and
        /// short enough that a process which is never going to exit -- one
        /// stuck on a dialog, one already replaced by something else with the
        /// same id -- does not leave this waiting for ever with nothing on
        /// screen.
        /// </summary>
        private static void WaitForExit(int pid)
        {
            try
            {
                using Process old = Process.GetProcessById(pid);
                if (!old.WaitForExit(30_000))
                {
                    Console.WriteLine($"[update] process {pid} is still running; carrying on");
                }
            }
            catch (ArgumentException)
            {
                // Already gone, which is the normal case: it exits the moment
                // it has started this one.
            }
            // Windows keeps a file handle a moment past exit, and virus
            // scanners keep it longer. The copy retries anyway; this is the
            // cheap part of not needing to.
            Thread.Sleep(400);
        }

        private static void Copy(string source, string target)
        {
            foreach (string path in Directory.EnumerateFiles(source, "*",
                SearchOption.AllDirectories))
            {
                string relative = Path.GetRelativePath(source, path);
                string destination = Path.Combine(target, relative);
                string? directory = Path.GetDirectoryName(destination);
                if (!String.IsNullOrEmpty(directory))
                {
                    Directory.CreateDirectory(directory);
                }
                CopyWithRetries(path, destination);
            }
        }

        /// <summary>
        /// A file that is still held is a file that will be free in a moment,
        /// not a failed update. The binary itself is the one this happens to.
        /// </summary>
        private static void CopyWithRetries(string from, string to)
        {
            for (int attempt = 0; ; attempt++)
            {
                try
                {
                    File.Copy(from, to, overwrite: true);
                    return;
                }
                catch (IOException) when (attempt < 20)
                {
                    Thread.Sleep(250);
                }
                catch (UnauthorizedAccessException) when (attempt < 20)
                {
                    Thread.Sleep(250);
                }
            }
        }

        /// <summary>
        /// Remove what a previous update left behind.
        ///
        /// Called at startup, because the copying process cannot delete the
        /// directory it is running from: by the time anybody could, the
        /// program doing it is the one that was just installed.
        /// </summary>
        public static void Clean()
        {
            try
            {
                if (Directory.Exists(Staging))
                {
                    Directory.Delete(Staging, recursive: true);
                }
            }
            catch (Exception)
            {
                // Litter, not a failure. The next stage overwrites it.
            }
        }

        /// <summary>
        /// The executable inside the package, which is not necessarily the one
        /// running: a server build's file has a name of its own, and this must
        /// name the file the *release* contains for this package.
        /// </summary>
        private static string BinaryName()
        {
            string name = UpdateCheck.IsServerBuild
                ? Branding.FileName + "Server"
                : Branding.FileName;
            return OperatingSystem.IsWindows() ? name + ".exe" : name;
        }

        private static void MakeExecutable(string path)
        {
            if (OperatingSystem.IsWindows())
            {
                return;
            }
            try
            {
                // A zip carries no mode bits and a tar does; setting it either
                // way costs one syscall and removes the difference.
                File.SetUnixFileMode(path, File.GetUnixFileMode(path)
                    | UnixFileMode.UserExecute | UnixFileMode.GroupExecute
                    | UnixFileMode.OtherExecute);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[update] could not make {path} executable: {ex.Message}");
            }
        }
    }
}
