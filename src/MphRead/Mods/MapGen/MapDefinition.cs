using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace MphRead.Mods.MapGen
{
    /// <summary>
    /// The source form of a custom map: everything needed to generate a room,
    /// in text, with no game data in it.
    ///
    /// This is deliberately the thing that lives in the repository. The three
    /// binaries a room is made of are derived from the player's own extracted
    /// files -- the textures especially -- so they are generated on the
    /// player's machine and never committed or shipped, exactly like the
    /// extraction itself. A map is a JSON file; a map is not a .bin.
    ///
    /// Coordinates are in MPH world units throughout (1 unit = 4096 in the
    /// game's fixed point). Anything imported from another engine is converted
    /// before it lands here, so this file never carries foreign units.
    /// </summary>
    public class MapDefinition
    {
        public string Name { get; set; } = "CUSTOM";
        public string? InGameName { get; set; }

        /// <summary>Room whose textures and palettes are copied into this map.</summary>
        public string TextureSource { get; set; } = "MP3 PROVING GROUND";

        /// <summary>
        /// Model scale as a power of two. Vertices are 16-bit fixed point, so
        /// model space spans +/-8 units and the world extent is +/-8 * 2^this.
        /// 4 gives +/-128 units at a precision of about 4 mm, which suits an
        /// arena; raising it trades precision for reach.
        /// </summary>
        public int ScaleFactor { get; set; } = 4;

        public float KillHeight { get; set; } = -40f;
        public float FarClip { get; set; } = 350f;

        public bool FogEnabled { get; set; } = true;
        public int[] FogColor { get; set; } = new[] { 8, 10, 16 };
        public int FogSlope { get; set; } = 5;
        public int FogOffset { get; set; } = 65180;

        public int[] Light1Color { get; set; } = new[] { 31, 28, 24 };
        public float[] Light1Vector { get; set; } = new[] { 0.3f, -1f, 0.2f };
        public int[] Light2Color { get; set; } = new[] { 10, 11, 16 };
        public float[] Light2Vector { get; set; } = new[] { -0.3f, 1f, -0.2f };

        public uint BattleTimeLimit { get; set; } = 7 * 60 * 30;
        public short PointLimit { get; set; } = 7;

        /// <summary>Set to convert a level from another engine instead of building from brushes.</summary>
        public MapImport? Import { get; set; }

        /// <summary>Where to stand the camera for the launcher's map picture.</summary>
        public MapPreview? Preview { get; set; }

        public List<MapMaterial> Materials { get; set; } = new List<MapMaterial>();
        public List<MapBrush> Brushes { get; set; } = new List<MapBrush>();
        public List<MapSpawn> Spawns { get; set; } = new List<MapSpawn>();
        public List<MapJumpPad> JumpPads { get; set; } = new List<MapJumpPad>();
        public List<MapItem> Items { get; set; } = new List<MapItem>();

        private static readonly JsonSerializerOptions _options = new JsonSerializerOptions()
        {
            PropertyNameCaseInsensitive = true,
            ReadCommentHandling = JsonCommentHandling.Skip,
            AllowTrailingCommas = true,
            WriteIndented = true,
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
        };

        public static MapDefinition Load(string path)
        {
            MapDefinition? result = JsonSerializer.Deserialize<MapDefinition>(File.ReadAllText(path), _options);
            if (result == null)
            {
                throw new ProgramException($"Could not read map definition {path}.");
            }
            return result;
        }

        public void Save(string path)
        {
            File.WriteAllText(path, JsonSerializer.Serialize(this, _options));
        }
    }

    /// <summary>
    /// The shot the launcher shows. A shipped room gets its picture from the
    /// intro camera the developers authored for it; a custom map has none, so
    /// it names a viewpoint instead.
    /// </summary>
    public class MapPreview
    {
        public float[] Position { get; set; } = new float[3];
        public float[] Target { get; set; } = new float[3];
    }

    /// <summary>
    /// How to convert a Quake 3 level. The geometry stays in the player's own
    /// .pk3 -- this only says where to find it and how to translate it, so
    /// what lives in the repository is a conversion recipe and not a level
    /// somebody else wrote.
    /// </summary>
    public class MapImport
    {
        /// <summary>
        /// The .bsp, or the .pk3 with <see cref="MapName"/> naming the level
        /// inside it.
        ///
        /// A bare name is looked for beside the map files and then beside the
        /// game files, which is what lets one map file work on a desktop and a
        /// phone: the level it converts is nobody's to ship -- not ours to put
        /// in an APK and not the repository's to carry -- so it is the
        /// player's copy, in the directory they already put their own files
        /// in. An absolute path is taken as given.
        /// </summary>
        public string Source { get; set; } = "";

        /// <summary>
        /// Where the source level actually is, or null when it is not on this
        /// machine at all.
        /// </summary>
        public string? Resolve()
        {
            if (Source.Length == 0)
            {
                return null;
            }
            foreach (string candidate in Candidates(Source))
            {
                if (File.Exists(candidate))
                {
                    return candidate;
                }
            }
            return null;
        }

        private static IEnumerable<string> Candidates(string name)
        {
            yield return name;
            if (Path.IsPathRooted(name))
            {
                yield break;
            }
            yield return Path.Combine(CustomRooms.MapDirectory, name);
            yield return Path.Combine(Mods.Launcher.GameFiles.Root, name);
        }
        public string? MapName { get; set; }

        /// <summary>
        /// Quake 3 units per MPH unit.
        ///
        /// The number that matters is the one that keeps the level's routes
        /// intact: a gap the map's author expected a player to clear must
        /// still be clearable. Samus jumps 1228/4096 per frame against 77/4096
        /// of gravity, so 2.39 units up and about 7.7 across at her walking
        /// cap; a Quake player leaves the ground at 270 u/s under 800 u/s^2,
        /// so 45.6 up and about 216 across. Dividing by less than 216/7.7 =
        /// 28.2 makes the world too big for its own jumps, and 22 -- which
        /// this was, chosen by feel -- puts a full-length Quake jump at 9.8
        /// units against her 7.7.
        ///
        /// 28 is therefore the floor, and it is the default. 35 would match
        /// the architecture exactly (56-unit Quake player against Samus's
        /// 1.6), and is worth trying on a map with no long jumps: everything
        /// above 28 only makes jumping easier than the author intended, while
        /// anything below it breaks routes.
        /// </summary>
        public float UnitsPerUnit { get; set; } = 28f;

        /// <summary>
        /// A texture pack baked from the level's own art by
        /// tools/bake-textures.py. With one, the map wears the textures it was
        /// made with and borrows nothing from a shipped room -- which also
        /// keeps cartridge data out of the files it generates. Looked for in
        /// the same places as <see cref="Source"/>.
        /// </summary>
        public string? Textures { get; set; }

        /// <summary>Where the texture pack is, or null if there is none here.</summary>
        public string? ResolveTextures()
        {
            if (String.IsNullOrEmpty(Textures))
            {
                return null;
            }
            foreach (string candidate in Candidates(Textures))
            {
                if (File.Exists(candidate))
                {
                    return candidate;
                }
            }
            return null;
        }

        /// <summary>Shader name (or any prefix of it) to material index.</summary>
        public Dictionary<string, int> ShaderMaterials { get; set; } = new Dictionary<string, int>();
        public int DefaultMaterial { get; set; }

        /// <summary>Texels per world unit applied to the imported UVs.</summary>
        public float TexScale { get; set; } = 24f;

        /// <summary>Drop geometry this far below the lowest spawn: the void does not need a floor.</summary>
        public bool KeepSky { get; set; }
    }

    /// <summary>
    /// A material, described as "the texture that material N of the source
    /// room uses". Copying the pair rather than an index into the texture list
    /// means the palette always matches the texture, which is the one thing
    /// that is silently wrong if you pick them separately.
    /// </summary>
    public class MapMaterial
    {
        public string Name { get; set; } = "mat";
        /// <summary>Index of the material in the source room to take the texture and palette from.</summary>
        public int SourceMaterial { get; set; }
        /// <summary>Texels per world unit. 32 means a 32x32 texture tiles once per unit.</summary>
        public float TexScale { get; set; } = 16f;
    }

    /// <summary>An axis-aligned box. Six quads of geometry, six faces of collision.</summary>
    public class MapBrush
    {
        public float[] Min { get; set; } = new float[3];
        public float[] Max { get; set; } = new float[3];
        public int Material { get; set; }
        /// <summary>Brightness applied to the vertex colour, per face, before the top/side falloff.</summary>
        public float Shade { get; set; } = 1f;
        /// <summary>False for decoration the player passes through.</summary>
        public bool Solid { get; set; } = true;
        /// <summary>Kills on contact -- lava, or the floor of a pit.</summary>
        public bool Damaging { get; set; }
        public string? Terrain { get; set; }
    }

    public class MapSpawn
    {
        public float[] Position { get; set; } = new float[3];
        /// <summary>Degrees, 0 = facing +Z, counter-clockwise seen from above.</summary>
        public float Yaw { get; set; }
    }

    /// <summary>
    /// A jump pad. Either give it a Target and let the launch velocity be
    /// solved for, or set Vector and Speed directly.
    /// </summary>
    public class MapJumpPad
    {
        public float[] Position { get; set; } = new float[3];
        public float[]? Target { get; set; }
        public float[]? Vector { get; set; }
        public float Speed { get; set; }
        /// <summary>
        /// The trigger box. Tall enough that a player who arrives falling,
        /// rather than walking, is still inside it on the frame it is tested.
        /// </summary>
        public float[] Size { get; set; } = new[] { 1.6f, 1.8f, 1.6f };
        public uint ModelId { get; set; }
        public ushort CooldownTime { get; set; } = 20;
        public ushort ControlLockTime { get; set; } = 30;
    }

    public class MapItem
    {
        public float[] Position { get; set; } = new float[3];
        public string Type { get; set; } = "MissileExpansion";
        public bool HasBase { get; set; } = true;
        public ushort SpawnInterval { get; set; } = 300;
    }
}
