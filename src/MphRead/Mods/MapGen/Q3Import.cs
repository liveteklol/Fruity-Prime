using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
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
            // With a baked pack the level wears its own textures and a
            // material is one shader; without one, materials are whatever the
            // map file mapped its shaders onto in a borrowed room.
            // Its own baked art if it has any -- in its bundle or beside its
            // recipe -- and otherwise baked now, from the level.
            MapTexturePack? pack = import.LoadTexturePack();
            if (pack == null)
            {
                string? baked = BakeTextures(bsp, import, verbose);
                pack = baked == null ? null : MapTexturePack.Load(baked);
            }
            IReadOnlyList<(int, int)> textureSizes = pack == null
                ? GetTextureSizes(def)
                : pack.Entries.Select(e => ((int)e.Width, (int)e.Height)).ToList();
            if (textureSizes.Count == 0)
            {
                // Neither its own art nor a borrowed room's: every face would
                // index a material that is not there, which came out as an
                // index-out-of-range with nothing in it to say what was
                // missing. The case that produced it is a bundle cooked on a
                // machine where the baked pack was not beside the recipe --
                // a CI runner, where it is derived and therefore not in git --
                // so the bundle carried a level with no textures at all.
                throw new ProgramException(String.IsNullOrEmpty(import.Textures)
                    ? $"{def.Name} names no texture pack and maps no shaders onto a shipped "
                        + "room's materials, so it has no materials at all. A bundle cooked "
                        + "without its baked textures is the usual cause."
                    : $"{def.Name} has no textures: {import.Textures} is not beside its recipe, "
                        + "not in its bundle, and could not be baked from the level.");
            }
            int unpainted = 0;

            // How big the sky's own texture should be drawn. A sky surface's
            // texture coordinates are meaningless: Quake never reads them, it
            // draws a dome or a box from the shader, so the numbers in the
            // file tile a cloud texture some fifty times across the lid and it
            // comes out as a checkerboard. Projected over the level instead,
            // at a couple of repeats across it, which is what a sky looks like.
            float skySpan = bsp.Models.Count > 0
                ? Math.Max(bsp.Models[0].Maxs[0] - bsp.Models[0].Mins[0],
                    bsp.Models[0].Maxs[1] - bsp.Models[0].Mins[1]) / unit
                : 100f;
            int skipped = 0;
            int patches = 0;
            int patchTriangles = 0;
            // The bounds the shell test below uses, taken from the level's
            // architecture only. Sky surfaces are drawn now, and they sit
            // outside everything: letting them widen this box would keep every
            // brush the test exists to drop.
            var drawnMin = new Vector3(Single.MaxValue);
            var drawnMax = new Vector3(Single.MinValue);
            foreach (Q3Face face in bsp.Faces)
            {
                if (face.Type != 1 && face.Type != 2 && face.Type != 3)
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
                bool sky = (texture.Flags & Q3Bsp.SurfaceSky) != 0;
                if (sky && !import.KeepSky)
                {
                    skipped++;
                    continue;
                }
                int material;
                if (pack == null)
                {
                    material = MatchMaterial(import, texture.Name);
                }
                else if (!pack.BySourceIndex.TryGetValue(face.Texture, out material))
                {
                    // A shader with no image of its own -- a light or an
                    // effect, defined in a .shader script rather than a file.
                    // Dropping the surface is better than painting it with
                    // somebody else's texture.
                    unpainted++;
                    continue;
                }
                (int width, int height) = textureSizes[material];
                bool patch = face.Type == 2;
                if (patch)
                {
                    patches++;
                }
                foreach (BuiltFace built in patch
                    ? Tessellate(bsp, face, unit, width, height, material, sky, import.PatchLevel)
                    : Triangles(bsp, face, unit, width, height, material, sky))
                {
                    if (sky)
                    {
                        ProjectSky(built, width * SkyTiles / Math.Max(1f, skySpan));
                    }
                    map.Faces.Add(built);
                    if (patch)
                    {
                        patchTriangles++;
                    }
                    if (!sky)
                    {
                        foreach (Vector3 point in built.Points)
                        {
                            drawnMin = Vector3.ComponentMin(drawnMin, point);
                            drawnMax = Vector3.ComponentMax(drawnMax, point);
                        }
                    }
                    // A patch is where the level's curves are -- an archway, a
                    // ramp, a pipe -- and in Quake its collision comes from the
                    // patch itself rather than from a brush behind it. Skipping
                    // them left a doorway you could see through and walk
                    // through and an arch that was not there.
                    if (patch && (texture.Contents & Q3Bsp.ContentsSolid) != 0)
                    {
                        map.Solid.Add(built);
                    }
                }
            }

            // The drawn surfaces are what a player can see, and therefore
            // roughly where they can go. The brushes reach much further: a
            // Quake level is sealed inside a shell of sky and caulk that
            // exists to keep the compiler happy, and importing it would put
            // hundreds of units of collision grid around a map nobody can
            // reach -- the grid is indexed with 16 bits, so that is not merely
            // wasteful, it is the difference between fitting and not.
            var margin = new Vector3(6);
            Vector3 keepMin = drawnMin - margin;
            Vector3 keepMax = drawnMax + margin;
            int solidBrushes = 0;
            int shellBrushes = 0;
            int clipBrushes = 0;
            var brushPlanes = new List<Vector4[]>();
            var brushBounds = new List<(Vector3 Min, Vector3 Max)>();
            var sides = new List<(int Brush, Vector3[] Points, Vector3 Normal)>();
            // Model 0 is the level; models 1 and up are its moving and
            // triggering parts, and their brushes are in the same list. A
            // trigger's brush is a volume, not a wall -- but this format keeps
            // the trigger shader's contents, and in at least one real level
            // those say solid. Importing them put seven invisible walls in the
            // middle of the map, standing exactly where the level's author had
            // put a tripwire.
            (int firstBrush, int brushCount) = bsp.Models.Count > 0
                ? (bsp.Models[0].Brush, bsp.Models[0].BrushCount)
                : (0, bsp.Brushes.Count);
            for (int brushIndex = firstBrush; brushIndex < firstBrush + brushCount; brushIndex++)
            {
                Q3Brush brush = bsp.Brushes[brushIndex];
                Q3Texture texture = bsp.Textures[brush.Texture];
                bool solid = (texture.Contents & Q3Bsp.ContentsSolid) != 0;
                bool clip = (texture.Contents & Q3Bsp.ContentsPlayerClip) != 0;
                if (!solid && clip && !import.KeepClip)
                {
                    // The level's invisible walls. They are load-bearing in the
                    // game they were authored for -- a race map fences the
                    // route so nobody shortcuts it -- and in the way in this
                    // one, where the point is to roam.
                    clipBrushes++;
                    continue;
                }
                if (!solid && !clip)
                {
                    continue;
                }
                if (IsSky(bsp, brush))
                {
                    shellBrushes++;
                    continue;
                }
                List<(Vector3[] Points, Vector3 Normal)> polygons = BrushSides(bsp, brush);
                if (polygons.Count == 0)
                {
                    continue;
                }
                if (polygons.All(s => s.Points.All(p => !Inside(ToWorld(p, unit), keepMin, keepMax))))
                {
                    shellBrushes++;
                    continue;
                }
                solidBrushes++;
                var planes = new Vector4[brush.SideCount];
                for (int i = 0; i < brush.SideCount; i++)
                {
                    Q3Plane plane = bsp.Planes[bsp.BrushSides[brush.FirstSide + i].Plane];
                    planes[i] = new Vector4(plane.X, plane.Y, plane.Z, plane.Distance);
                }
                var brushMin = new Vector3(Single.MaxValue);
                var brushMax = new Vector3(Single.MinValue);
                foreach ((Vector3[] points, Vector3 _) in polygons)
                {
                    foreach (Vector3 point in points)
                    {
                        brushMin = Vector3.ComponentMin(brushMin, point);
                        brushMax = Vector3.ComponentMax(brushMax, point);
                    }
                }
                int index = brushPlanes.Count;
                brushPlanes.Add(planes);
                brushBounds.Add((brushMin, brushMax));
                foreach ((Vector3[] points, Vector3 normal) in polygons)
                {
                    sides.Add((index, points, normal));
                }
            }

            // A level's walls are stacks of brushes, so most brush sides face
            // into another brush and nothing can ever touch them. They still
            // cost: the collision grid lists every face in every cell it
            // reaches and indexes those listings with 16 bits, and on a level
            // this size the buried ones alone overflow it. Dropping them is
            // free -- a surface no point outside the solid can reach cannot be
            // collided with -- and it is what makes the format fit: on
            // df_dust2, 10,624 sides down to 4,505 and the grid to a third of
            // its listings.
            Dictionary<(int, int, int), List<int>> lookup = BuildBrushLookup(brushBounds);
            int buried = 0;
            foreach ((int owner, Vector3[] points, Vector3 normal) in sides)
            {
                if (IsBuried(points, normal, owner, brushPlanes, brushBounds, lookup))
                {
                    buried++;
                    continue;
                }
                Vector3[] world = points.Select(p => ToWorld(p, unit)).ToArray();
                map.Solid.Add(new BuiltFace(world, new Vector2[world.Length],
                    ToDirection(new[] { normal.X, normal.Y, normal.Z }), 0, 1f));
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
                if (pack != null)
                {
                    Console.WriteLine($"  {pack.Entries.Count} baked textures"
                        + (unpainted > 0 ? $", {unpainted} surfaces dropped for want of one" : ""));
                }
                Console.WriteLine($"  imported {bsp.Faces.Count} surfaces -> {map.Faces.Count} triangles"
                    + $" ({patches} patches tessellated to {patchTriangles},"
                    + $" {skipped} non-drawing surfaces skipped)");
                Console.WriteLine($"  {solidBrushes} solid brushes -> {map.Solid.Count} collision faces"
                    + $" ({shellBrushes} shell brushes outside the level left out,"
                    + $" {buried} sides buried inside other brushes"
                    + (clipBrushes > 0 ? $", {clipBrushes} invisible-wall brushes dropped" : "") + ")");
                Bounds(map, out Vector3 min, out Vector3 max);
                Console.WriteLine($"  extent {max.X - min.X:0.0} x {max.Y - min.Y:0.0} x {max.Z - min.Z:0.0} units"
                    + $" at {unit:0.#} Quake units each");
            }
            return map;
        }

        /// <summary>
        /// The triangles of an ordinary drawn surface -- a polygon or a
        /// triangle soup, which reach here in the same form.
        /// </summary>
        private static IEnumerable<BuiltFace> Triangles(Q3Bsp bsp, Q3Face face, float unit,
            int width, int height, int material, bool sky)
        {
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
                yield return MakeFace(points, uvs, normal, material, Shade(shade / 3, sky));
            }
        }

        /// <summary>
        /// A Bezier patch, tessellated.
        ///
        /// This is where a level keeps its curves -- an archway, a ramp, a
        /// pipe -- as a grid of control points rather than as triangles, and
        /// in Quake its collision comes from the patch itself rather than from
        /// a brush behind it. Dropping them, which is what this did, took out
        /// both at once: a doorway with a hole where its arch should be, that
        /// you could also walk through.
        ///
        /// The grid is an odd number of control points each way and splits
        /// into biquadratic patches sharing their edges, so stepping two at a
        /// time and evaluating each 3x3 leaves no seam.
        /// </summary>
        private static IEnumerable<BuiltFace> Tessellate(Q3Bsp bsp, Q3Face face, float unit,
            int width, int height, int material, bool sky, int level)
        {
            int w = face.Size[0];
            int h = face.Size[1];
            if (w < 3 || h < 3 || w % 2 == 0 || h % 2 == 0)
            {
                yield break;
            }
            level = Math.Clamp(level, 1, 8);
            for (int py = 0; py + 2 < h; py += 2)
            {
                for (int px = 0; px + 2 < w; px += 2)
                {
                    var points = new Vector3[level + 1, level + 1];
                    var uvs = new Vector2[level + 1, level + 1];
                    var normals = new Vector3[level + 1, level + 1];
                    var shades = new float[level + 1, level + 1];
                    for (int a = 0; a <= level; a++)
                    {
                        float v = a / (float)level;
                        (float v0, float v1, float v2) = Weights(v);
                        for (int b = 0; b <= level; b++)
                        {
                            float u = b / (float)level;
                            (float u0, float u1, float u2) = Weights(u);
                            Vector3 position = Vector3.Zero;
                            var uv = Vector2.Zero;
                            Vector3 normal = Vector3.Zero;
                            float shade = 0;
                            for (int r = 0; r < 3; r++)
                            {
                                float rw = r == 0 ? v0 : r == 1 ? v1 : v2;
                                for (int c = 0; c < 3; c++)
                                {
                                    float weight = rw * (c == 0 ? u0 : c == 1 ? u1 : u2);
                                    Q3Vertex vertex = bsp.Vertices[face.Vertex + (py + r) * w + px + c];
                                    position += ToWorld(vertex.Position, unit) * weight;
                                    uv += new Vector2(vertex.Surface[0] * width,
                                        vertex.Surface[1] * height) * weight;
                                    normal += ToDirection(vertex.Normal) * weight;
                                    shade += (vertex.Color[0] + vertex.Color[1] + vertex.Color[2])
                                        / (3f * 255f) * weight;
                                }
                            }
                            points[a, b] = position;
                            uvs[a, b] = uv;
                            normals[a, b] = normal.LengthSquared < 0.0001f
                                ? ToDirection(face.Normal)
                                : normal.Normalized();
                            shades[a, b] = shade;
                        }
                    }
                    for (int a = 0; a < level; a++)
                    {
                        for (int b = 0; b < level; b++)
                        {
                            // two triangles rather than a quad: a tessellated
                            // cell is not flat, and a collision face carries
                            // one plane for all its points
                            yield return Cell(points, uvs, normals, shades, material, sky,
                                (a, b), (a, b + 1), (a + 1, b + 1));
                            yield return Cell(points, uvs, normals, shades, material, sky,
                                (a, b), (a + 1, b + 1), (a + 1, b));
                        }
                    }
                }
            }
        }

        private static (float, float, float) Weights(float t)
        {
            float inverse = 1 - t;
            return (inverse * inverse, 2 * t * inverse, t * t);
        }

        private static BuiltFace Cell(Vector3[,] points, Vector2[,] uvs, Vector3[,] normals,
            float[,] shades, int material, bool sky,
            (int A, int B) p0, (int A, int B) p1, (int A, int B) p2)
        {
            var corners = new[] { points[p0.A, p0.B], points[p1.A, p1.B], points[p2.A, p2.B] };
            var texcoords = new[] { uvs[p0.A, p0.B], uvs[p1.A, p1.B], uvs[p2.A, p2.B] };
            Vector3 normal = normals[p0.A, p0.B] + normals[p1.A, p1.B] + normals[p2.A, p2.B];
            normal = normal.LengthSquared < 0.0001f ? Vector3.UnitY : normal.Normalized();
            float shade = (shades[p0.A, p0.B] + shades[p1.A, p1.B] + shades[p2.A, p2.B]) / 3;
            return MakeFace(corners, texcoords, normal, material, Shade(shade, sky));
        }

        /// <summary>
        /// Quake's own lighting does not come across: the lightmaps are gone
        /// and this engine is not lighting these materials, so the vertex
        /// colour the compiler baked is all there is. Taken literally it is far
        /// too dark -- a map lit for a lightmap has dim vertices -- so it is
        /// lifted into the top half of the range, where it reads as shading
        /// rather than as gloom. The sky is not shaded at all: it is the light
        /// source, and dimming it by whatever the compiler wrote on a surface
        /// nobody lights makes a bright day look like a storm.
        /// </summary>
        private static float Shade(float baked, bool sky)
        {
            return sky ? 1f : Math.Clamp(0.62f + baked * 0.75f, 0.55f, 1f);
        }

        /// <summary>
        /// A triangle, wound the way this engine wants it.
        ///
        /// Not carried across: Quake winds a front face clockwise and culls
        /// GL's front, while this engine culls the back of a counter-clockwise
        /// one, and a tessellated patch has its own order again. Asking the
        /// winding to agree with the surface normal settles all of them at
        /// once, and it is checkable -- a hand-built brush in MapBuilder winds
        /// its top face so that (p1-p0) x (p2-p0) points the way the surface
        /// faces, which is the rule applied here. Get it backwards and the
        /// level is visible only from the side nobody stands on, which reads
        /// as missing geometry rather than as inside out.
        /// </summary>
        private static BuiltFace MakeFace(Vector3[] points, Vector2[] uvs, Vector3 normal,
            int material, float shade)
        {
            Vector3 wound = Vector3.Cross(points[1] - points[0], points[2] - points[0]);
            if (Vector3.Dot(wound, normal) < 0)
            {
                (points[1], points[2]) = (points[2], points[1]);
                (uvs[1], uvs[2]) = (uvs[2], uvs[1]);
            }
            return new BuiltFace(points, uvs, normal, material, shade);
        }

        /// <summary>How many times the sky's texture repeats across the level.</summary>
        private const float SkyTiles = 2f;

        /// <summary>
        /// World-aligned texture coordinates for a sky surface, measured from
        /// the triangle's own corner so the texel numbers stay small -- they
        /// are 1.11.4 fixed point and run out at 2047.
        /// </summary>
        private static void ProjectSky(BuiltFace face, float texelsPerUnit)
        {
            Vector3 origin = face.Points[0];
            float ax = MathF.Abs(face.Normal.X);
            float ay = MathF.Abs(face.Normal.Y);
            float az = MathF.Abs(face.Normal.Z);
            for (int i = 0; i < face.Points.Length; i++)
            {
                Vector3 offset = (face.Points[i] - origin) * texelsPerUnit;
                face.Texcoords[i] = ay > ax && ay >= az
                    ? new Vector2(offset.X, offset.Z)
                    : ax >= az ? new Vector2(offset.Z, -offset.Y) : new Vector2(offset.X, -offset.Y);
            }
        }

        /// <summary>
        /// Bakes the texture pack the map file asks for but does not have.
        ///
        /// What travels with a map is the level and the recipe; the pack is
        /// neither. It is derived from the level, exactly like the room
        /// binaries, so a map file that names one and a folder that has only
        /// the .pk3 is the normal state of a fresh clone, not a broken map.
        ///
        /// It stays a file rather than becoming a step in the conversion,
        /// because the machine that plays the game is not always the one that
        /// can do this: the Android head has no image decoder at all, its STB
        /// natives being desktop builds left out of the APK on purpose. There,
        /// this fails and says so, and the pack has to arrive already baked.
        /// </summary>
        internal static string? BakeTextures(Q3Bsp bsp, MapImport import, bool verbose)
        {
            string? level = import.Resolve();
            if (String.IsNullOrEmpty(import.Textures) || level == null)
            {
                return null;
            }
            string target = Path.Combine(
                import.BaseDirectory ?? CustomRooms.MapDirectory, import.Textures);
            try
            {
                MapTextureBake.Result result = MapTextureBake.Bake(bsp, new[] { level }, target);
                if (result.Baked == 0)
                {
                    File.Delete(target);
                    return null;
                }
                if (verbose)
                {
                    Console.WriteLine($"  baked {result.Baked} textures from {Path.GetFileName(level)}"
                        + $" -> {Path.GetFileName(target)}");
                }
                return target;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[mapgen] could not bake textures from "
                    + $"{Path.GetFileName(level)}: {ex.Message}");
                return null;
            }
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
        /// trimming it against every other plane of the brush. The polygons
        /// come back in Quake space, because that is where the burial test
        /// below has to run.
        /// </summary>
        private static List<(Vector3[] Points, Vector3 Normal)> BrushSides(Q3Bsp bsp, Q3Brush brush)
        {
            var result = new List<(Vector3[], Vector3)>();
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
                result.Add((points.ToArray(), normal));
            }
            return result;
        }

        /// <summary>Brush indices bucketed by a coarse grid, so the burial test asks few brushes.</summary>
        private static Dictionary<(int, int, int), List<int>> BuildBrushLookup(
            IReadOnlyList<(Vector3 Min, Vector3 Max)> bounds)
        {
            var lookup = new Dictionary<(int, int, int), List<int>>();
            for (int i = 0; i < bounds.Count; i++)
            {
                (Vector3 min, Vector3 max) = bounds[i];
                for (int x = Cell(min.X); x <= Cell(max.X); x++)
                {
                    for (int y = Cell(min.Y); y <= Cell(max.Y); y++)
                    {
                        for (int z = Cell(min.Z); z <= Cell(max.Z); z++)
                        {
                            if (!lookup.TryGetValue((x, y, z), out List<int>? list))
                            {
                                list = new List<int>();
                                lookup.Add((x, y, z), list);
                            }
                            list.Add(i);
                        }
                    }
                }
            }
            return lookup;
        }

        private const float BrushCellSize = 512f;

        private static int Cell(float value)
        {
            return (int)MathF.Floor(value / BrushCellSize);
        }

        /// <summary>
        /// True when every part of this brush side has solid on its outward
        /// face -- it is an internal seam between stacked brushes, and no
        /// point outside the solid can reach it.
        ///
        /// Sampled rather than solved: the centre, each corner and each edge's
        /// midpoint, all drawn a little way in from the rim so a shared edge
        /// with the brush next door does not decide it, and all pushed one
        /// unit out along the normal. Missing a buried face only costs a
        /// listing in the grid; dropping one that is not buried would leave a
        /// hole in the floor, so every sample has to agree.
        /// </summary>
        private static bool IsBuried(Vector3[] points, Vector3 normal, int owner,
            IReadOnlyList<Vector4[]> brushes, IReadOnlyList<(Vector3 Min, Vector3 Max)> bounds,
            Dictionary<(int, int, int), List<int>> lookup)
        {
            Vector3 centre = Vector3.Zero;
            foreach (Vector3 point in points)
            {
                centre += point;
            }
            centre /= points.Length;
            Vector3 offset = normal.Normalized();
            for (int i = 0; i < points.Length; i++)
            {
                Vector3 edge = (points[i] + points[(i + 1) % points.Length]) / 2;
                if (!Covered(points[i] + (centre - points[i]) * 0.15f + offset, owner, brushes, bounds, lookup)
                    || !Covered(edge + (centre - edge) * 0.15f + offset, owner, brushes, bounds, lookup))
                {
                    return false;
                }
            }
            return Covered(centre + offset, owner, brushes, bounds, lookup);
        }

        private static bool Covered(Vector3 point, int owner, IReadOnlyList<Vector4[]> brushes,
            IReadOnlyList<(Vector3 Min, Vector3 Max)> bounds,
            Dictionary<(int, int, int), List<int>> lookup)
        {
            if (!lookup.TryGetValue((Cell(point.X), Cell(point.Y), Cell(point.Z)), out List<int>? candidates))
            {
                return false;
            }
            foreach (int index in candidates)
            {
                if (index == owner)
                {
                    continue;
                }
                (Vector3 min, Vector3 max) = bounds[index];
                if (point.X < min.X - 1 || point.X > max.X + 1 || point.Y < min.Y - 1 || point.Y > max.Y + 1
                    || point.Z < min.Z - 1 || point.Z > max.Z + 1)
                {
                    continue;
                }
                bool inside = true;
                foreach (Vector4 plane in brushes[index])
                {
                    if (plane.X * point.X + plane.Y * point.Y + plane.Z * point.Z - plane.W > -0.1f)
                    {
                        inside = false;
                        break;
                    }
                }
                if (inside)
                {
                    return true;
                }
            }
            return false;
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
                    if (!import.KeepSpawns || !entity.TryGetValue("origin", out string? origin))
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
        ///
        /// Every answer is a pickup a multiplayer room actually holds. Quake's
        /// mega health and body armour are the obvious match for an energy
        /// tank and a UA expansion and are deliberately not that: those are
        /// the story's permanent upgrades, and one in a match makes whoever
        /// took it better for the rest of the session. They map to the largest
        /// thing that runs out instead. See MapBuilder.MultiplayerItems.
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
                "item_health_mega" => ItemType.HealthBig,
                "item_armor_shard" => ItemType.UASmall,
                "item_armor_combat" => ItemType.UABig,
                "item_armor_body" => ItemType.UABig,
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

        private static Vector3 ToWorld(Vector3 position, float unit)
        {
            return new Vector3(position.X / unit, position.Z / unit, -position.Y / unit);
        }

        private static Vector3 ToDirection(float[] direction)
        {
            var result = new Vector3(direction[0], direction[2], -direction[1]);
            return result.LengthSquared < 0.0001f ? Vector3.UnitY : result.Normalized();
        }
    }
}
