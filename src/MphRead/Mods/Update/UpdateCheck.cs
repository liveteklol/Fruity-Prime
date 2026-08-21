using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;

namespace MphRead.Mods.Update
{
    /// <summary>What is published, and whether it is worth having.</summary>
    public readonly struct UpdateInfo
    {
        /// <summary>The release tag, e.g. v1.2.0.</summary>
        public string Tag { get; init; }
        public Version Version { get; init; }
        /// <summary>
        /// The asset built for this exact package, or "" when the release has
        /// none. A hint for the person doing the download -- which of the four
        /// files on the page is theirs -- and not something this fetches.
        /// </summary>
        public string AssetName { get; init; }
        /// <summary>The release's page on GitHub. Where "Update now" goes.</summary>
        public string PageUrl { get; init; }
        public string Notes { get; init; }
    }

    /// <summary>
    /// Ask GitHub what the latest release is and whether it is newer than this
    /// binary.
    ///
    /// This exists because of a rule the multiplayer already had:
    /// <c>NetConfig.ProtocolVersion</c> makes a server refuse a client on a
    /// different build outright. That is the right behaviour -- the alternative
    /// is reading the wrong bytes at the wrong offsets -- but it means "update
    /// your client" is the answer to a large share of "I cannot join", and
    /// telling somebody that is worse than doing it.
    /// </summary>
    public static class UpdateCheck
    {
        /// <summary>The only host this asks, and only ever for metadata.</summary>
        private const string _api =
            "https://api.github.com/repos/" + Mods.Branding.Repository + "/releases/latest";

        /// <summary>Never silently, and never for long.</summary>
        private static readonly TimeSpan _timeout = TimeSpan.FromSeconds(20);

        /// <summary>Why there is nothing to install, when there is nothing.</summary>
        public static string? LastReason { get; private set; }

        /// <summary>
        /// The release worth installing over this build, or null.
        ///
        /// Null covers every ordinary case -- already current, no network, a
        /// release with no asset for this platform, a local build -- and none
        /// of them is an error. <see cref="LastReason"/> says which it was for
        /// anything that wants to print it.
        /// </summary>
        public static UpdateInfo? Latest(CancellationToken cancel = default)
        {
            LastReason = null;
            // A build nobody published cannot be improved on by one that was:
            // there is no way to tell whether it is ahead of the release or
            // behind it, and overwriting a developer's own binary with a
            // download is the one failure this must never have.
            if (!BuildVersion.IsRelease)
            {
                LastReason = "this is a local build, so it is left alone";
                return null;
            }
            string json;
            try
            {
                using var client = new HttpClient { Timeout = _timeout };
                // GitHub refuses anonymous API calls with no User-Agent.
                client.DefaultRequestHeaders.Add("User-Agent",
                    $"{Mods.Branding.FileName}/{BuildVersion.Display}");
                client.DefaultRequestHeaders.Add("Accept",
                    "application/vnd.github+json");
                using HttpResponseMessage response = client.Send(
                    new HttpRequestMessage(HttpMethod.Get, _api), cancel);
                // 404 is the answer for a repository that has never published a
                // release, which is an ordinary state and not a failure. Told
                // apart from a network problem because "could not reach GitHub"
                // would send somebody looking at their firewall.
                if (response.StatusCode == System.Net.HttpStatusCode.NotFound)
                {
                    LastReason = "no releases have been published yet";
                    return null;
                }
                if (response.StatusCode == (System.Net.HttpStatusCode)403
                    || response.StatusCode == (System.Net.HttpStatusCode)429)
                {
                    // Anonymous calls are limited by IP, and a shared address
                    // can exhaust it without this machine having asked once.
                    LastReason = "GitHub is rate-limiting this address; try later";
                    return null;
                }
                if (!response.IsSuccessStatusCode)
                {
                    LastReason = $"GitHub answered {(int)response.StatusCode}";
                    return null;
                }
                json = response.Content.ReadAsStringAsync(cancel).GetAwaiter().GetResult();
            }
            catch (Exception ex)
            {
                // Offline, GitHub down, DNS hijacked by a hotel: all the same
                // thing here, which is "not now".
                LastReason = $"could not reach GitHub ({ex.GetType().Name})";
                return null;
            }
            return Parse(json);
        }

        /// <summary>
        /// Split out so it can be tested against a saved response.
        /// </summary>
        /// <param name="installed">
        /// The version to compare against; this build's when null. Taking it as
        /// an argument is what lets the comparison be tested, and it is also
        /// why this method does not dereference a version that is null in every
        /// build a developer makes.
        /// </param>
        public static UpdateInfo? Parse(string json, Version? installed = null)
        {
            // Cleared here as well as in Latest: anything reading it after a
            // successful parse would otherwise be told why the previous call
            // found nothing.
            LastReason = null;
            installed ??= BuildVersion.Current;
            if (installed == null)
            {
                LastReason = "this is a local build, so it is left alone";
                return null;
            }
            string tag;
            string notes;
            string page;
            var assets = new List<string>();
            try
            {
                using JsonDocument document = JsonDocument.Parse(json);
                JsonElement root = document.RootElement;
                tag = root.TryGetProperty("tag_name", out JsonElement t)
                    ? t.GetString() ?? "" : "";
                notes = root.TryGetProperty("body", out JsonElement b)
                    ? b.GetString() ?? "" : "";
                page = root.TryGetProperty("html_url", out JsonElement h)
                    ? h.GetString() ?? "" : "";
                if (root.TryGetProperty("assets", out JsonElement list))
                {
                    foreach (JsonElement asset in list.EnumerateArray())
                    {
                        string name = asset.TryGetProperty("name", out JsonElement n)
                            ? n.GetString() ?? "" : "";
                        if (name.Length > 0)
                        {
                            assets.Add(name);
                        }
                    }
                }
            }
            catch (JsonException)
            {
                LastReason = "GitHub's answer could not be read";
                return null;
            }

            Version? published = BuildVersion.Parse(tag);
            if (published == null)
            {
                LastReason = $"the latest release ({tag}) is not a plain version tag";
                return null;
            }
            Version current = BuildVersion.Normalise(installed);
            if (published <= current)
            {
                LastReason = $"v{current.ToString(3)} is already the latest";
                return null;
            }

            // A release with no package for this platform is still announced.
            // Nothing is fetched from here any more, so there is no reason to
            // hide a release because its file names were not what was expected
            // -- the person going to the page can see what is actually on it.
            return new UpdateInfo
            {
                Tag = tag,
                Version = published,
                AssetName = PickAsset(assets) ?? "",
                PageUrl = page.Length > 0 ? page : ReleasesPage,
                Notes = notes
            };
        }

        /// <summary>
        /// Which of the files on the release page is this one's, so the screen
        /// can name it. Matched on the runtime identifier and on whether this
        /// is a server build; null when nothing matches, which is not a reason
        /// to withhold the release.
        /// </summary>
        private static string? PickAsset(List<string> assets)
        {
            string rid = Rid();
            foreach (string asset in assets)
            {
                string name = asset.ToLowerInvariant();
                if (!name.Contains(rid))
                {
                    continue;
                }
                // "server" appears in the server packages' names and in no
                // others, so it tells the two builds for one platform apart --
                // which matters on Windows, where both exist for win-x64.
                if (name.Contains("-server-") != IsServerBuild)
                {
                    continue;
                }
                return asset;
            }
            return null;
        }

        /// <summary>Where the releases live, when a specific one has no page.</summary>
        public const string ReleasesPage =
            "https://github.com/" + Mods.Branding.Repository + "/releases";

        public static bool IsServerBuild =>
#if MPHREAD_SERVER
            true;
#else
            false;
#endif

        /// <summary>
        /// The runtime identifier this was published for. Taken from the
        /// runtime rather than a build constant: a self-contained build always
        /// knows, and it cannot go stale.
        /// </summary>
        public static string Rid()
        {
            string os = OperatingSystem.IsWindows() ? "win"
                : OperatingSystem.IsMacOS() ? "osx" : "linux";
            string arch = RuntimeInformation.ProcessArchitecture switch
            {
                Architecture.X64 => "x64",
                Architecture.Arm64 => "arm64",
                Architecture.X86 => "x86",
                Architecture.Arm => "arm",
                _ => RuntimeInformation.ProcessArchitecture.ToString().ToLowerInvariant()
            };
            return $"{os}-{arch}";
        }

        public static string PackageSuffix() =>
            IsServerBuild ? $"server-{Rid()}" : Rid();
    }
}
