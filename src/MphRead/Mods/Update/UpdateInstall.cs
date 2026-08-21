using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Formats.Tar;
using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Threading;

namespace MphRead.Mods.Update
{
    /// <summary>
    /// Fetch a release and put it in place of this one.
    ///
    /// The whole difficulty is that the program is running out of the files it
    /// is replacing. Neither Windows nor Linux will let a running executable be
    /// overwritten -- Windows holds the image open, Linux answers ETXTBSY --
    /// but both will let it be **renamed**, because the name and the inode are
    /// different things and the process is using the second one. So every file
    /// is moved aside rather than replaced, the new one is written to the name
    /// that just came free, and the aside copies are deleted on the next start,
    /// by which time nothing has them open.
    ///
    /// That is one code path for both platforms, and it is also its own
    /// rollback: until the next start, the previous build is still on disk.
    /// </summary>
    public static class UpdateInstall
    {
        private const string _asideSuffix = ".old-update";

        /// <summary>Hosts a release asset is allowed to come from.</summary>
        private static readonly string[] _allowedHosts =
        {
            "github.com", "objects.githubusercontent.com",
            "release-assets.githubusercontent.com", "githubusercontent.com"
        };

        /// <summary>
        /// Download <paramref name="update"/> and install it beside this
        /// binary. Returns the path to run afterwards, or null on failure.
        /// </summary>
        /// <param name="report">Progress, for a launcher card or a log.</param>
        public static string? Apply(UpdateInfo update, Action<string> report,
            CancellationToken cancel = default)
        {
            string? exe = Environment.ProcessPath;
            if (exe == null)
            {
                report("Cannot tell which file this program is running from.");
                return null;
            }
            string baseDir = Path.GetDirectoryName(exe)!;
            if (!Writable(baseDir))
            {
                // An install under Program Files, or a server unpacked as root
                // and run as a service user. Worth saying plainly: the download
                // would succeed and the swap would fail half way.
                report($"No permission to write to {baseDir}; "
                    + "reinstall by hand, or run this once as an administrator.");
                return null;
            }
            if (!Allowed(update.DownloadUrl))
            {
                report($"Refusing to download from {update.DownloadUrl}");
                return null;
            }

            string staging = Path.Combine(baseDir, ".update-" + Environment.ProcessId);
            try
            {
                Directory.CreateDirectory(staging);
                string archive = Path.Combine(staging, update.AssetName);
                report($"Downloading {update.AssetName}...");
                if (!Download(update, archive, report, cancel))
                {
                    return null;
                }
                string unpacked = Path.Combine(staging, "unpacked");
                Directory.CreateDirectory(unpacked);
                report("Unpacking...");
                if (!Unpack(archive, unpacked, report))
                {
                    return null;
                }
                string? newExe = FindExecutable(unpacked);
                if (newExe == null)
                {
                    // Better to stop here than to move half a package into
                    // place and leave nothing that starts.
                    report("That package does not contain a program to run; "
                        + "nothing has been changed.");
                    return null;
                }
                report("Installing...");
                return Swap(unpacked, baseDir, exe, newExe, report);
            }
            catch (Exception ex)
            {
                report($"The update failed: {ex.Message}");
                return null;
            }
            finally
            {
                Delete(staging);
            }
        }

        private static bool Download(UpdateInfo update, string path,
            Action<string> report, CancellationToken cancel)
        {
            using var client = new HttpClient { Timeout = TimeSpan.FromMinutes(15) };
            client.DefaultRequestHeaders.Add("User-Agent",
                $"{Mods.Branding.FileName}/{BuildVersion.Display}");
            using HttpResponseMessage response = client.Send(
                new HttpRequestMessage(HttpMethod.Get, update.DownloadUrl),
                HttpCompletionOption.ResponseHeadersRead, cancel);
            if (!response.IsSuccessStatusCode)
            {
                report($"GitHub answered {(int)response.StatusCode} for that package.");
                return false;
            }
            long expected = response.Content.Headers.ContentLength ?? update.Size;
            using (Stream from = response.Content.ReadAsStream(cancel))
            using (var to = new FileStream(path, FileMode.Create, FileAccess.Write))
            {
                var buffer = new byte[128 * 1024];
                long done = 0;
                int lastPercent = -5;
                int read;
                while ((read = from.Read(buffer, 0, buffer.Length)) > 0)
                {
                    cancel.ThrowIfCancellationRequested();
                    to.Write(buffer, 0, read);
                    done += read;
                    if (expected > 0)
                    {
                        int percent = (int)(done * 100 / expected);
                        if (percent >= lastPercent + 5)
                        {
                            lastPercent = percent;
                            report($"Downloading... {percent}%");
                        }
                    }
                }
            }
            long got = new FileInfo(path).Length;
            if (expected > 0 && got != expected)
            {
                // A truncated download that unpacks far enough to look fine is
                // exactly how a half-installed build happens.
                report($"The download stopped short ({got} of {expected} bytes).");
                return false;
            }
            return true;
        }

        private static bool Unpack(string archive, string into, Action<string> report)
        {
            try
            {
                if (archive.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
                {
                    ZipFile.ExtractToDirectory(archive, into, overwriteFiles: true);
                    return true;
                }
                if (archive.EndsWith(".tar.gz", StringComparison.OrdinalIgnoreCase)
                    || archive.EndsWith(".tgz", StringComparison.OrdinalIgnoreCase))
                {
                    using FileStream file = File.OpenRead(archive);
                    using var gzip = new GZipStream(file, CompressionMode.Decompress);
                    // Both of these refuse entries that would land outside the
                    // destination, which is the whole of the archive-traversal
                    // problem and not something to reimplement here.
                    TarFile.ExtractToDirectory(gzip, into, overwriteFiles: true);
                    return true;
                }
                report($"Cannot unpack {Path.GetFileName(archive)}.");
                return false;
            }
            catch (Exception ex)
            {
                report($"That package could not be unpacked: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// The program inside the unpacked package.
        ///
        /// By name, because a package holds native libraries too and any of
        /// them may carry the executable bit. Both the current name and the one
        /// this project used to have are accepted, so that an installed
        /// MphRead can update itself into a Fruity Prime release.
        /// </summary>
        private static string? FindExecutable(string dir)
        {
            var wanted = new List<string>();
            foreach (string stem in new[]
            {
                Mods.Branding.FileName, Mods.Branding.FileName + "Server",
                Mods.Branding.Upstream, Mods.Branding.Upstream + "Server"
            })
            {
                wanted.Add(stem + ".exe");
                wanted.Add(stem);
            }
            foreach (string candidate in wanted)
            {
                string path = Path.Combine(dir, candidate);
                if (File.Exists(path))
                {
                    return path;
                }
            }
            return null;
        }

        /// <summary>
        /// Move the old files aside and the new ones into place.
        ///
        /// The executable is handled separately from the rest: the package's
        /// copy goes to the path this process is running from, whatever that
        /// file happens to be called. A server started by a systemd unit that
        /// names the binary keeps working across a rename that way, and a
        /// shortcut on somebody's desktop keeps pointing at something real.
        /// </summary>
        private static string? Swap(string unpacked, string baseDir, string runningExe,
            string newExe, Action<string> report)
        {
            var moved = new List<(string From, string To)>();
            try
            {
                foreach (string source in Directory.GetFiles(unpacked, "*",
                    SearchOption.AllDirectories))
                {
                    string relative = Path.GetRelativePath(unpacked, source);
                    string target = String.Equals(source, newExe, StringComparison.Ordinal)
                        ? runningExe
                        : Path.Combine(baseDir, relative);
                    Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                    if (File.Exists(target))
                    {
                        string aside = target + _asideSuffix;
                        Delete(aside);
                        File.Move(target, aside);
                        moved.Add((target, aside));
                    }
                    File.Move(source, target);
                    if (!OperatingSystem.IsWindows()
                        && String.Equals(target, runningExe, StringComparison.Ordinal))
                    {
                        Executable(target);
                    }
                }
            }
            catch (Exception ex)
            {
                report($"The update could not be installed: {ex.Message}");
                report("Putting the previous version back.");
                // Undo in reverse, so a name freed last is filled first.
                moved.Reverse();
                foreach ((string target, string aside) in moved)
                {
                    try
                    {
                        Delete(target);
                        File.Move(aside, target);
                    }
                    catch (Exception)
                    {
                        // Nothing better is available at this point; the aside
                        // copies are still on disk and named for what they are.
                    }
                }
                return null;
            }
            return runningExe;
        }

        private static void Executable(string path)
        {
            // Guarded here as well as at the call site: the attribute on
            // SetUnixFileMode is what the platform analyser reads, and it does
            // not follow the caller's check.
            if (OperatingSystem.IsWindows())
            {
                return;
            }
            try
            {
                File.SetUnixFileMode(path,
                    UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute
                    | UnixFileMode.GroupRead | UnixFileMode.GroupExecute
                    | UnixFileMode.OtherRead | UnixFileMode.OtherExecute);
            }
            catch (Exception)
            {
                // A filesystem with no permission bits; the archive's own mode
                // is usually right anyway.
            }
        }

        /// <summary>
        /// Delete what a previous update moved aside.
        ///
        /// On the next start, not at the end of the update: that is the point
        /// at which the files are certainly not open any more, and until then
        /// they are the way back.
        /// </summary>
        public static void CleanUp()
        {
            try
            {
                string dir = Path.GetDirectoryName(Environment.ProcessPath ?? "") ?? "";
                if (dir.Length == 0)
                {
                    return;
                }
                foreach (string stale in Directory.GetFiles(dir, "*" + _asideSuffix))
                {
                    Delete(stale);
                }
                foreach (string staging in Directory.GetDirectories(dir, ".update-*"))
                {
                    Delete(staging);
                }
            }
            catch (Exception)
            {
                // Housekeeping. Never worth interrupting a start over.
            }
        }

        private static bool Allowed(string url)
        {
            if (!Uri.TryCreate(url, UriKind.Absolute, out Uri? uri))
            {
                return false;
            }
            if (uri.Scheme != Uri.UriSchemeHttps)
            {
                return false;
            }
            foreach (string host in _allowedHosts)
            {
                if (uri.Host.Equals(host, StringComparison.OrdinalIgnoreCase)
                    || uri.Host.EndsWith("." + host, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
            return false;
        }

        private static bool Writable(string dir)
        {
            try
            {
                string probe = Path.Combine(dir, ".update-probe-" + Environment.ProcessId);
                File.WriteAllText(probe, "");
                File.Delete(probe);
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static void Delete(string path)
        {
            try
            {
                if (Directory.Exists(path))
                {
                    Directory.Delete(path, recursive: true);
                }
                else if (File.Exists(path))
                {
                    File.Delete(path);
                }
            }
            catch (Exception)
            {
            }
        }

        /// <summary>
        /// Start the installed build with this process's arguments and leave.
        ///
        /// Not used by a service: systemd and NSSM restart what they started,
        /// and a second process launched from inside the one they are watching
        /// is a process they do not know about.
        /// </summary>
        public static void Relaunch(string exe)
        {
            var info = new ProcessStartInfo(exe) { UseShellExecute = false };
            foreach (string argument in Environment.GetCommandLineArgs()[1..])
            {
                info.ArgumentList.Add(argument);
            }
            info.WorkingDirectory = Path.GetDirectoryName(exe)!;
            Process.Start(info);
        }
    }
}
