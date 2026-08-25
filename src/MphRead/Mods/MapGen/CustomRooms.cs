using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenTK.Mathematics;

namespace MphRead.Mods.MapGen
{
    /// <summary>
    /// Makes custom maps into rooms the rest of the game already knows how to
    /// handle: the launcher lists them, -maptest loads them, the server can
    /// run them.
    ///
    /// A map is a JSON file in `maps/` next to the executable. Its three
    /// binaries are generated into the player's own extracted files, since
    /// that is where a room's paths point and where the textures come from,
    /// and they are regenerated whenever the JSON is newer than they are.
    /// </summary>
    public static class CustomRooms
    {
        private static IReadOnlyList<MapDefinition>? _definitions;
        private static int _firstId = -1;

        public static string MapDirectory { get; }
            = Path.Combine(AppContext.BaseDirectory, "maps");

        public static IReadOnlyList<MapDefinition> Definitions
        {
            get
            {
                if (_definitions == null)
                {
                    _definitions = LoadDefinitions();
                }
                return _definitions;
            }
        }

        private static IReadOnlyList<MapDefinition> LoadDefinitions()
        {
            var results = new List<MapDefinition>();
            if (!Directory.Exists(MapDirectory))
            {
                return results;
            }
            foreach (string path in Directory.EnumerateFiles(MapDirectory, "*.json").OrderBy(p => p))
            {
                try
                {
                    MapDefinition definition = MapDefinition.Load(path);
                    definition.Name = definition.Name.ToUpperInvariant();
                    results.Add(definition);
                }
                catch (Exception ex)
                {
                    // a broken map file must not stop the game from starting
                    Console.WriteLine($"Ignoring map {Path.GetFileName(path)}: {ex.Message}");
                }
            }
            return results;
        }

        /// <summary>Called from the room ID table, which fixes each room's ID as its index.</summary>
        public static IReadOnlyList<string> AppendIds(List<string> ids)
        {
            _firstId = ids.Count;
            ids.AddRange(Definitions.Select(d => d.Name));
            return ids;
        }

        /// <summary>Called from the room table, after the IDs have been assigned.</summary>
        public static IReadOnlyList<RoomMetadata> AppendRooms(List<RoomMetadata> rooms)
        {
            for (int i = 0; i < Definitions.Count; i++)
            {
                rooms.Add(MakeMetadata(Definitions[i], _firstId + i));
            }
            return rooms;
        }

        private static RoomMetadata MakeMetadata(MapDefinition def, int id)
        {
            string prefix = def.Name.ToLowerInvariant();
            return new RoomMetadata(
                id: id,
                name: def.Name,
                inGameName: def.InGameName ?? def.Name,
                archive: prefix,
                modelPath: $"{prefix}_Model.bin",
                animationPath: $"{prefix}_Anim.bin",
                collisionPath: $"{prefix}_Collision.bin",
                texturePath: null, // the textures are inside the model file
                entityPath: $"{prefix}_Ent.bin",
                nodePath: null, // no navigation mesh: the bots find their own way
                roomNodeName: null,
                battleTimeLimit: def.BattleTimeLimit,
                timeLimit: def.BattleTimeLimit,
                pointLimit: def.PointLimit,
                nodeLayer: 0,
                fogEnabled: def.FogEnabled,
                clearFog: false,
                fogColor: ToColor(def.FogColor),
                fogSlope: def.FogSlope,
                fogOffset: (ushort)def.FogOffset,
                light1Color: ToColor(def.Light1Color),
                light1Vector: ToVector(def.Light1Vector),
                light2Color: ToColor(def.Light2Color),
                light2Vector: ToVector(def.Light2Vector),
                farClip: Fixed.ToInt(def.FarClip),
                killHeight: Fixed.ToInt(def.KillHeight),
                size: RoomSize.Large,
                // no camera or player limits: a custom map decides its own
                // extent, and a limit box inherited from someone else's room
                // is how the camera ends up stuck behind a wall
                multiplayer: true);
        }

        private static ColorRgb ToColor(int[] values)
        {
            return new ColorRgb((byte)values[0], (byte)values[1], (byte)values[2]);
        }

        private static Vector3 ToVector(float[] values)
        {
            return new Vector3(values[0], values[1], values[2]);
        }

        public static string ArchiveDirectory(MapDefinition def)
        {
            return Paths.Combine(Paths.FileSystem, @"_archives", def.Name.ToLowerInvariant());
        }

        public static string EntityDirectory()
        {
            return Paths.Combine(Paths.FileSystem, @"levels\entities");
        }

        /// <summary>
        /// Generates every map whose binaries are missing or older than its
        /// source. Returns the number generated.
        /// </summary>
        public static int GenerateAll(bool force = false, bool verbose = true)
        {
            int count = 0;
            foreach (MapDefinition def in Definitions)
            {
                if (force || NeedsGenerating(def))
                {
                    MapPacker.Generate(def, ArchiveDirectory(def), EntityDirectory(), verbose);
                    count++;
                }
            }
            return count;
        }

        /// <summary>
        /// Generates any map whose binaries are missing or out of date, and
        /// says so only when there is something to say. Never throws: a map
        /// that cannot be built must not stop the game from starting, and the
        /// line it prints is what explains the room that is not there.
        /// </summary>
        public static void GenerateMissing()
        {
            IReadOnlyList<MapDefinition> definitions;
            try
            {
                definitions = Definitions;
            }
            catch
            {
                return;
            }
            foreach (MapDefinition def in definitions)
            {
                try
                {
                    if (!NeedsGenerating(def))
                    {
                        continue;
                    }
                    Console.WriteLine($"[mapgen] building {def.Name}");
                    MapPacker.Generate(def, ArchiveDirectory(def), EntityDirectory(), verbose: false);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[mapgen] {def.Name} could not be built: {ex.Message}");
                }
            }
        }

        private static bool NeedsGenerating(MapDefinition def)
        {
            string prefix = def.Name.ToLowerInvariant();
            string model = Path.Combine(ArchiveDirectory(def), $"{prefix}_Model.bin");
            if (!File.Exists(model))
            {
                return true;
            }
            string? source = Directory.Exists(MapDirectory)
                ? Directory.EnumerateFiles(MapDirectory, "*.json")
                    .FirstOrDefault(p => Path.GetFileNameWithoutExtension(p).ToUpperInvariant() == def.Name)
                : null;
            return source != null && File.GetLastWriteTimeUtc(source) > File.GetLastWriteTimeUtc(model);
        }
    }
}
