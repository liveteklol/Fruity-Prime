using System;
using System.IO;
using MphRead.Mods.Launcher;
using MphRead.Mods.MapGen;

namespace MphRead.Droid
{
    /// <summary>
    /// Gets the custom maps out of the APK and into a directory the game can
    /// read.
    ///
    /// The desktop keeps map files beside the executable and the build copies
    /// them there. Neither half of that works here: an APK's own directory is
    /// read only, and its assets are not files at all -- they live inside the
    /// package and only <c>AssetManager</c> can open them, so
    /// <c>Directory.EnumerateFiles</c> finds nothing however the path is
    /// spelled.
    ///
    /// So the maps are unpacked once, into the same external directory the
    /// extracted game files use. That is also the directory a player can reach
    /// over USB, which means a map they wrote themselves can simply be dropped
    /// in beside the ones that shipped -- and it will survive an app update,
    /// because only the names that came out of the package are overwritten.
    /// </summary>
    internal static class AndroidMaps
    {
        private const string AssetFolder = "maps";

        /// <summary>
        /// Unpack the bundled maps and point the game at them. Call before
        /// anything reads the room tables: the map list is loaded once.
        /// </summary>
        public static void Install(Android.Content.Res.AssetManager? assets, string root, long packageTime)
        {
            if (root.Length == 0)
            {
                return;
            }
            string directory = Path.Combine(root, AssetFolder);
            CustomRooms.MapDirectory = directory;
            if (assets == null)
            {
                return;
            }
            try
            {
                Directory.CreateDirectory(directory);
                foreach (string name in assets.List(AssetFolder) ?? Array.Empty<string>())
                {
                    if (!name.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }
                    string target = Path.Combine(directory, name);
                    // Only when the package is newer than what was unpacked
                    // last time, so an update ships its map changes and a
                    // player's own edits are not rewritten on every launch.
                    if (File.Exists(target) && File.GetLastWriteTimeUtc(target)
                        >= DateTimeOffset.FromUnixTimeMilliseconds(packageTime).UtcDateTime)
                    {
                        continue;
                    }
                    using Stream source = assets.Open($"{AssetFolder}/{name}");
                    using var file = File.Create(target);
                    source.CopyTo(file);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[android] could not unpack the bundled maps: {ex.Message}");
            }
        }

        /// <summary>
        /// Build the binaries for any map that has none yet.
        ///
        /// The desktop does this from <c>ModEntry.TryHandle</c>, which every
        /// entry point passes through. This head has no <c>Main</c> at all --
        /// the entry point is an activity -- so nothing there ever runs, and a
        /// map would be listed by the launcher and then fail to load. It needs
        /// the extracted game files, since that is where a map's borrowed
        /// textures come from and where its own binaries go.
        /// </summary>
        public static void EnsureBuilt()
        {
            try
            {
                if (!GameFiles.Ready)
                {
                    return;
                }
                GameFiles.ApplyPaths();
                CustomRooms.GenerateMissing();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[android] could not build the custom maps: {ex.Message}");
            }
        }
    }
}
