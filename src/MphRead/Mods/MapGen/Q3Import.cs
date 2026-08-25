using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using MphRead.Editor;
using MphRead.Entities;
using OpenTK.Mathematics;

namespace MphRead.Mods.MapGen
{
    /// <summary>
    /// Converts a Quake 3 level into a Fruity Prime room.
    ///
    /// Three things are being translated, not copied. The axes: Quake is
    /// Z-up, this engine is Y-up, and the mapping used here keeps the
    /// handedness so no surface ends up inside out. The scale: see
    /// MapImport.UnitsPerUnit -- there is no single correct number, because
    /// the two games' players are different sizes *and* jump differently.
    /// And the jump pads: Quake solves a pad's launch velocity at runtime from
    /// where it points, so the arc is recomputed here under this game's
    /// gravity rather than carried across, which is the only way a pad still
    /// lands where it was aimed.
    /// </summary>
    public static class Q3Import
    {
        public static BuiltMap Build(MapDefinition def, bool verbose = true)
        {
            MapImport import = def.Import
                ?? throw new ProgramException($"Map {def.Name} has no import settings.");
            Q3Bsp bsp = Q3Bsp.Load(import.Resolve() ?? import.Source, import.MapName);
            var map = new BuiltMap(def);
            float unit = import.UnitsPerUnit;
            var textureSizes = GetTextureSizes(def);

            int skipped = 0;
            int patches = 0;
            foreach (Q3Face face in bsp.Faces)
            {
                if (face.Type == 2)
                {
                    // a Bezier patch: control points, not triangles. Its
                    // collision comes from the patch too, so skipping it
                    // leaves a hole rather than an invisible wall
                    patches++;
                    continue;
                }
                if (face.Type != 1 && face.Type != 3)
                {
                    skipped++;
                    continue;
                }
                Q3Texture texture = bsp.Textures[face.Texture];
                if ((texture.Flags & (Q3Bsp.SurfaceNoDraw | Q3Bsp.SurfaceHint | Q3Bsp.SurfaceSkip)) != 0)
                {
                    skipped++;
                    continue;
                }
                if ((texture.Flags & Q3Bsp.SurfaceSky) != 0 && !import.KeepSky)
                {
                    skipped++;
                    continue;
                }
                int material = MatchMaterial(import, texture.Name);
                (int width, int height) = textureSizes[material];
                for (int i = 0; i + 2 < face.MeshVertCount; i += 3)
                {
                    var points = new Vector3[3];
                    var uvs = new Vector2[3];
                    var normal = Vector3.Zero;
                    float shade = 0;
                    for (int j = 0; j < 3; j++)
                    {
                        Q3Vertex vertex = bsp.Vertices[face.Vertex + bsp.MeshVerts[face.MeshVert + i + j]];
                        points[j] = ToWorld(vertex.Position, unit);
                        uvs[j] = new Vector2(vertex.Surface[0] * width, vertex.Surface[1] * height);
                        normal += ToDirection(vertex.Normal);
                        // the lightmap is gone, but the vertex colour the
                        // compiler baked is a usable stand-in for it
                        shade += (vertex.Color[0] + vertex.Color[1] + vertex.Color[2]) / (3f * 255f);
                    }
                    if (normal.LengthSquared < 0.0001f)
                    {
                        normal = ToDirection(face.Normal);
                    }
                    normal = normal.Normalized();
                    map.Faces.Add(new BuiltFace(points, Rebase(uvs), normal, material,
                        Math.Clamp(0.45f + shade / 3f * 1.1f, 0.25f, 1f)));
                }
            }

            // The drawn surfaces are what a player can see, and therefore
            // roughly where they can go. The brushes reach much further: a
            // Quake level is sealed inside a shell of sky and caulk that
            // exists to keep the compiler happy, and importing it would put
            // hundreds of units of collision grid around a map nobody can
            // reach -- the grid is indexed with 16 bits, so that is not merely
            // wasteful, it is the difference between fitting and not.
            Bounds(map, out Vector3 drawnMin, out Vector3 drawnMax);
            var margin = new Vector3(6);
            Vector3 keepMin = drawnMin - margin;
            Vector3 keepMax = drawnMax + margin;
            int solidBrushes = 0;
            int shellBrushes = 0;
            foreach (Q3Brush brush in bsp.Brushes)
            {
                Q3Texture texture = bsp.Textures[brush.Texture];
                if ((texture.Contents & (Q3Bsp.ContentsSolid | Q3Bsp.ContentsPlayerClip)) == 0)
                {
                    continue;
                }
                if (IsSky(bsp, brush))
                {
                    shellBrushes++;
                    continue;
                }
                var faces = BrushFaces(bsp, brush, unit).ToList();
                if (faces.Count == 0)
                {
                    continue;
                }
                if (faces.All(f => f.Points.All(p => !Inside(p, keepMin, keepMax))))
                {
                    shellBrushes++;
                    continue;
                }
                solidBrushes++;
                foreach (BuiltFace face in faces)
                {
                    map.Solid.Add(face);
                }
            }

            AddEntities(map, def, bsp, import, verbose);

            // A converted level has no authored viewpoint to borrow, and its
            // extent is only known once it is built, so frame it from what
            // came out: three quarters of the way out on the diagonal, looking
            // at the middle.
            if (def.Preview == null)
            {
                Bounds(map, out Vector3 low, out Vector3 high);
                Vector3 centre = (low + high) / 2;
                Vector3 size = high - low;
                def.Preview = new MapPreview()
                {
                    Position = new[]
                    {
                        centre.X + size.X * 0.6f,
                        high.Y + size.Y * 0.5f,
                        centre.Z + size.Z * 0.6f
                    },
                    Target = new[] { centre.X, centre.Y, centre.Z }
                };
                if (verbose)
                {
                    // printed rather than written back: the map file is the
                    // author's, and a generator that silently rewrites its own
                    // input is a generator nobody trusts
                    Console.WriteLine("  suggested preview (paste into the map file to keep it):");
                    Console.WriteLine($"  \"preview\": {{ \"position\": [{def.Preview.Position[0]:0.#}, "
                        + $"{def.Preview.Position[1]:0.#}, {def.Preview.Position[2]:0.#}], "
                        + $"\"target\": [{def.Preview.Target[0]:0.#}, {def.Preview.Target[1]:0.#}, "
                        + $"{def.Preview.Target[2]:0.#}] }},");
                }
            }

            if (verbose)
            {
                Console.WriteLine($"  imported {bsp.Faces.Count} surfaces -> {map.Faces.Count} triangles"
                    + $" ({patches} patches and {skipped} non-drawing surfaces skipped)");
                Console.WriteLine($"  {solidBrushes} solid brushes -> {map.Solid.Count} collision faces"
                    + $" ({shellBrushes} shell brushes outside the level left out)");
                Bounds(map, out Vector3 min, out Vector3 max);
                Console.WriteLine($"  extent {max.X - min.X:0.0} x {max.Y - min.Y:0.0} x {max.Z - min.Z:0.0} units"
                    + $" at {unit:0.#} Quake units each");
            }
            return map;
        }

        private static bool Inside(Vector3 point, Vector3 min, Vector3 max)
        {
            return point.X >= min.X && point.X <= max.X
                && point.Y >= min.Y && point.Y <= max.Y
                && point.Z >= min.Z && point.Z <= max.Z;
        }

        /// <summary>True when every side of the brush is sky: the shell, not the level.</summary>
        private static bool IsSky(Q3Bsp bsp, Q3Brush brush)
        {
            for (int i = 0; i < brush.SideCount; i++)
            {
                Q3Texture texture = bsp.Textures[bsp.BrushSides[brush.FirstSide + i].Texture];
                if ((texture.Flags & Q3Bsp.SurfaceSky) == 0)
                {
                    return false;
                }
            }
            return brush.SideCount > 0;
        }

        private static void Bounds(BuiltMap map, out Vector3 min, out Vector3 max)
        {
            min = new Vector3(Single.MaxValue);
            max = new Vector3(Single.MinValue);
            foreach (BuiltFace face in map.Faces.Concat(map.Solid))
            {
                foreach (Vector3 point in face.Points)
                {
                    min = Vector3.ComponentMin(min, point);
                    max = Vector3.ComponentMax(max, point);
                }
            }
        }

        /// <summary>
        /// Texture coordinates are 1.11.4 fixed point, so they run out at 2047
        /// texels -- a long floor tiled from the map origin passes that easily.
        /// Shifting each face by a whole number of repeats keeps the numbers
        /// small and the tiling identical.
        /// </summary>
        private static Vector2[] Rebase(Vector2[] uvs)
        {
            float minU = uvs.Min(uv => uv.X);
            float minV = uvs.Min(uv => uv.Y);
            var offset = new Vector2(MathF.Floor(minU), MathF.Floor(minV));
            return uvs.Select(uv => uv - offset).ToArray();
        }

        private static IReadOnlyList<(int, int)> GetTextureSizes(MapDefinition def)
        {
            Model source = Read.GetRoomModelInstance(def.TextureSource).Model;
            Recolor recolor = source.Recolors[0];
            var results = new List<(int, int)>();
            foreach (MapMaterial material in def.Materials)
            {
                Material srcMaterial = source.Materials[material.SourceMaterial];
                Texture texture = recolor.Textures[srcMaterial.TextureId];
                results.Add((texture.Width, texture.Height));
            }
            return results;
        }

        private static int MatchMaterial(MapImport import, string shader)
        {
            if (import.ShaderMaterials.TryGetValue(shader, out int exact))
            {
                return exact;
            }
            // longest matching prefix wins, so "textures/base_wall" can be set
            // as a whole and one shader inside it overridden
            string? best = null;
            foreach (string key in import.ShaderMaterials.Keys)
            {
                if (shader.StartsWith(key, StringComparison.OrdinalIgnoreCase)
                    && (best == null || key.Length > best.Length))
                {
                    best = key;
                }
            }
            return best == null ? import.DefaultMaterial : import.ShaderMaterials[best];
        }

        /// <summary>
        /// A Quake brush is stored as the planes that bound it, so each face
        /// has to be recovered by starting with a plane-sized sheet and
        /// trimming it against every other plane of the brush.
        /// </summary>
        private static IEnumerable<BuiltFace> BrushFaces(Q3Bsp bsp, Q3Brush brush, float unit)
        {
            for (int i = 0; i < brush.SideCount; i++)
            {
                Q3Plane plane = bsp.Planes[bsp.BrushSides[brush.FirstSide + i].Plane];
                var normal = new Vector3(plane.X, plane.Y, plane.Z);
                List<Vector3> points = MakeSheet(normal, plane.Distance);
                for (int j = 0; j < brush.SideCount && points.Count >= 3; j++)
                {
                    if (j == i)
                    {
                        continue;
                    }
                    Q3Plane other = bsp.Planes[bsp.BrushSides[brush.FirstSide + j].Plane];
                    points = Clip(points, new Vector3(other.X, other.Y, other.Z), other.Distance);
                }
                if (points.Count < 3)
                {
                    continue;
                }
                points = Weld(points);
                if (points.Count < 3)
                {
                    continue;
                }
                Vector3[] world = points.Select(p => ToWorld(new[] { p.X, p.Y, p.Z }, unit)).ToArray();
                var texcoords = new Vector2[world.Length];
                yield return new BuiltFace(world, texcoords, ToDirection(new[] { plane.X, plane.Y, plane.Z }), 0, 1f);
            }
        }

        private static List<Vector3> MakeSheet(Vector3 normal, float distance)
        {
            Vector3 axis = MathF.Abs(normal.Z) < 0.9f ? Vector3.UnitZ : Vector3.UnitX;
            Vector3 right = Vector3.Cross(axis, normal).Normalized();
            Vector3 up = Vector3.Cross(normal, right).Normalized();
            Vector3 centre = normal * distance;
            const float extent = 65536f;
            return new List<Vector3>()
            {
                centre - right * extent - up * extent,
                centre + right * extent - up * extent,
                centre + right * extent + up * extent,
                centre - right * extent + up * extent
            };
        }

        /// <summary>Keeps the part of the polygon on the inside of the plane.</summary>
        private static List<Vector3> Clip(List<Vector3> points, Vector3 normal, float distance)
        {
            const float epsilon = 0.01f;
            var result = new List<Vector3>();
            for (int i = 0; i < points.Count; i++)
            {
                Vector3 current = points[i];
                Vector3 next = points[(i + 1) % points.Count];
                float distCurrent = Vector3.Dot(normal, current) - distance;
                float distNext = Vector3.Dot(normal, next) - distance;
                if (distCurrent <= epsilon)
                {
                    result.Add(current);
                }
                if (distCurrent > epsilon != distNext > epsilon && MathF.Abs(distCurrent - distNext) > 1e-6f)
                {
                    result.Add(current + (next - current) * (distCurrent / (distCurrent - distNext)));
                }
            }
            return result;
        }

        /// <summary>Drops points the clipping left within a hair of each other.</summary>
        private static List<Vector3> Weld(List<Vector3> points)
        {
            var result = new List<Vector3>();
            foreach (Vector3 point in points)
            {
                if (!result.Any(p => (p - point).LengthSquared < 0.0004f))
                {
                    result.Add(point);
                }
            }
            return result;
        }

        private static void AddEntities(BuiltMap map, MapDefinition def, Q3Bsp bsp, MapImport import, bool verbose)
        {
            var targets = new Dictionary<string, Vector3>(StringComparer.OrdinalIgnoreCase);
            foreach (Dictionary<string, string> entity in bsp.Entities)
            {
                if (entity.TryGetValue("targetname", out string? name) && entity.TryGetValue("origin", out string? origin))
                {
                    targets[name] = ToWorld(ParseVector(origin), import.UnitsPerUnit);
                }
            }
            int pads = 0;
            int items = 0;
            foreach (Dictionary<string, string> entity in bsp.Entities)
            {
                if (!entity.TryGetValue("classname", out string? classname))
                {
                    continue;
                }
                if (classname.Equals("info_player_deathmatch", StringComparison.OrdinalIgnoreCase)
                    || classname.Equals("info_player_start", StringComparison.OrdinalIgnoreCase))
                {
                    if (!entity.TryGetValue("origin", out string? origin))
                    {
                        continue;
                    }
                    float angle = entity.TryGetValue("angle", out string? value)
                        && Single.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out float parsed)
                        ? parsed
                        : 0;
                    Vector3 position = ToWorld(ParseVector(origin), import.UnitsPerUnit);
                    // Quake spawns are set at the player's feet plus a little;
                    // dropping them slightly avoids starting inside the floor
                    def.Spawns.Add(new MapSpawn()
                    {
                        Position = new[] { position.X, position.Y - 24 / import.UnitsPerUnit, position.Z },
                        Yaw = 90 + angle
                    });
                }
                else if (classname.Equals("trigger_push", StringComparison.OrdinalIgnoreCase))
                {
                    if (!entity.TryGetValue("model", out string? model) || !model.StartsWith("*")
                        || !entity.TryGetValue("target", out string? target)
                        || !targets.TryGetValue(target, out Vector3 destination))
                    {
                        continue;
                    }
                    if (!Int32.TryParse(model[1..], out int modelIndex)
                        || modelIndex < 0 || modelIndex >= bsp.Models.Count)
                    {
                        continue;
                    }
                    Q3Model volume = bsp.Models[modelIndex];
                    Vector3 min = ToWorld(volume.Mins, import.UnitsPerUnit);
                    Vector3 max = ToWorld(volume.Maxs, import.UnitsPerUnit);
                    var lower = Vector3.ComponentMin(min, max);
                    var upper = Vector3.ComponentMax(min, max);
                    var centre = new Vector3((lower.X + upper.X) / 2, lower.Y, (lower.Z + upper.Z) / 2);
                    def.JumpPads.Add(new MapJumpPad()
                    {
                        Position = new[] { centre.X, centre.Y, centre.Z },
                        Target = new[] { destination.X, destination.Y, destination.Z },
                        Size = new[]
                        {
                            MathF.Max(upper.X - lower.X, 0.8f),
                            MathF.Max(upper.Y - lower.Y, 0.8f),
                            MathF.Max(upper.Z - lower.Z, 0.8f)
                        }
                    });
                    pads++;
                }
                else if (entity.TryGetValue("origin", out string? itemOrigin))
                {
                    ItemType type = MapItemType(classname);
                    if (type == ItemType.None)
                    {
                        continue;
                    }
                    Vector3 position = ToWorld(ParseVector(itemOrigin), import.UnitsPerUnit);
                    def.Items.Add(new MapItem()
                    {
                        Position = new[] { position.X, position.Y, position.Z },
                        Type = type.ToString()
                    });
                    items++;
                }
            }
            if (verbose)
            {
                Console.WriteLine($"  {def.Spawns.Count} spawns, {pads} jump pads, {items} items");
            }
            MapBuilder.AddEntities(map, def);
        }

        /// <summary>
        /// The nearest thing this game has to each of Quake's pickups. Weapons
        /// are matched by what they do -- the railgun to the Imperialist, the
        /// rocket launcher to the Magmaul -- rather than by name.
        /// </summary>
        private static ItemType MapItemType(string classname)
        {
            return classname.ToLowerInvariant() switch
            {
                "weapon_railgun" => ItemType.Imperialist,
                "weapon_rocketlauncher" => ItemType.Magmaul,
                "weapon_lightning" => ItemType.ShockCoil,
                "weapon_plasmagun" => ItemType.VoltDriver,
                "weapon_shotgun" => ItemType.Battlehammer,
                "weapon_grenadelauncher" => ItemType.Judicator,
                "weapon_bfg" => ItemType.OmegaCannon,
                "item_quad" => ItemType.DoubleDamage,
                "item_invis" => ItemType.Cloak,
                "item_health" => ItemType.HealthMedium,
                "item_health_small" => ItemType.HealthSmall,
                "item_health_large" => ItemType.HealthBig,
                "item_health_mega" => ItemType.EnergyTank,
                "item_armor_shard" => ItemType.UASmall,
                "item_armor_combat" => ItemType.UABig,
                "item_armor_body" => ItemType.UAExpansion,
                "ammo_rockets" => ItemType.MissileBig,
                "ammo_slugs" => ItemType.UABig,
                "ammo_cells" => ItemType.UASmall,
                "ammo_shells" => ItemType.UASmall,
                "ammo_bullets" => ItemType.UASmall,
                "ammo_grenades" => ItemType.MissileSmall,
                "ammo_lightning" => ItemType.UASmall,
                _ => ItemType.None
            };
        }

        private static float[] ParseVector(string value)
        {
            string[] parts = value.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            var result = new float[3];
            for (int i = 0; i < 3 && i < parts.Length; i++)
            {
                Single.TryParse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture, out result[i]);
            }
            return result;
        }

        /// <summary>
        /// Quake is Z-up and X-forward; this engine is Y-up. Sending Y to -Z
        /// keeps the coordinate system right-handed, so polygon winding, and
        /// with it every surface's facing, survives the trip.
        /// </summary>
        private static Vector3 ToWorld(float[] position, float unit)
        {
            return new Vector3(position[0] / unit, position[2] / unit, -position[1] / unit);
        }

        private static Vector3 ToDirection(float[] direction)
        {
            var result = new Vector3(direction[0], direction[2], -direction[1]);
            return result.LengthSquared < 0.0001f ? Vector3.UnitY : result.Normalized();
        }
    }
}
