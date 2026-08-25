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
        /// <summary>Path to a .bsp, or to a .pk3 with MapName naming the level inside it.</summary>
        public string Source { get; set; } = "";
        public string? MapName { get; set; }

        /// <summary>
        /// Quake 3 units per MPH unit. The honest answer is not one number:
        /// matching the player's height gives about 32, matching how high he
        /// jumps gives about 19, and Samus is slower across the ground than a
        /// Quake player. Somewhere in the low twenties keeps the gaps
        /// jumpable and the arena from feeling empty.
        /// </summary>
        public float UnitsPerUnit { get; set; } = 22f;

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
