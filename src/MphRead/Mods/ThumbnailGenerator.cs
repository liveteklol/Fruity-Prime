using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace MphRead.Mods
{
    /// <summary>
    /// Builds map preview images by rendering each room, rather than
    /// extracting them.
    ///
    /// The original game has no map thumbnails to extract: its multiplayer
    /// select screen lists map *names*, so nothing image-shaped was ever
    /// shipped for them. (files/&lt;version&gt;/stage/*_Model.bin are the rooms'
    /// 3D models, not previews.) Rendering the real geometry gives a more
    /// useful picture anyway.
    ///
    /// Everything here is local: previews are generated from the user's own
    /// extracted files into a cache directory that is git-ignored. No game
    /// asset is ever committed or redistributed.
    /// </summary>
    public static class ThumbnailGenerator
    {
        // High enough that downscaling in a launcher still looks sharp, but
        // small enough to fit inside a 1080p desktop work area: GLFW clamps a
        // window to what the display can hold, and a clamped window renders
        // smaller than the requested target, which is what produced black
        // bands on two edges. Override with -size when the display allows it.
        public const int ThumbnailWidth = 1600;
        public const int ThumbnailHeight = 900;

        /// <summary>Cache directory, beside the executable so it travels with the install.</summary>
        public static string CacheDirectory =>
            Path.Combine(AppContext.BaseDirectory, "thumbnails");

        public static string PathFor(string roomKey)
        {
            // Room keys contain spaces and occasionally punctuation; keep the
            // filename recoverable but filesystem-safe.
            string safe = string.Concat(roomKey.Select(c =>
                char.IsLetterOrDigit(c) ? char.ToLowerInvariant(c) : '_'));
            return Path.Combine(CacheDirectory, $"{safe}.png");
        }

        public static bool Exists(string roomKey) => File.Exists(PathFor(roomKey));

        /// <summary>
        /// Multiplayer rooms a launcher would offer.
        ///
        /// First Hunt rooms ("Level MP1", "E3 level", ...) are excluded
        /// unless that game's files were extracted too: they live in a
        /// separate archive set, and asking for one without it fails at load
        /// rather than producing a picture. Skipping them up front keeps a
        /// normal MPH-only install from reporting failures it cannot fix.
        /// </summary>
        public static IReadOnlyList<string> MultiplayerRooms()
        {
            bool firstHuntAvailable = FirstHuntAvailable();
            var rooms = new List<string>();
            foreach (KeyValuePair<string, RoomMetadata> entry in Metadata.RoomMetadata)
            {
                if (!entry.Value.Multiplayer)
                {
                    continue;
                }
                if (entry.Value.FirstHunt && !firstHuntAvailable)
                {
                    continue;
                }
                rooms.Add(entry.Key);
            }
            rooms.Sort(StringComparer.OrdinalIgnoreCase);
            return rooms;
        }

        /// <summary>True when First Hunt's extracted files are present.</summary>
        public static bool FirstHuntAvailable()
        {
            try
            {
                string path = Paths.FhFileSystem;
                return !string.IsNullOrEmpty(path) && Directory.Exists(path);
            }
            catch
            {
                // Paths throws when no FH version is configured at all.
                return false;
            }
        }

        public static IReadOnlyList<string> MissingThumbnails()
        {
            return MultiplayerRooms().Where(r => !Exists(r)).ToList();
        }

        public static void EnsureCacheDirectory()
        {
            Directory.CreateDirectory(CacheDirectory);
        }
    }
}
