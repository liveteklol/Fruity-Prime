using System;
using System.Collections.Generic;
using System.Linq;

namespace MphRead.Mods.MapGen
{
    /// <summary>
    /// Answers the one question a map author has before writing any geometry:
    /// which textures can I borrow, and what are they called.
    /// </summary>
    public static class MapReport
    {
        /// <summary>
        /// The shaders a level actually draws with, commonest first. This is
        /// the list a conversion maps onto borrowed textures: without it the
        /// whole level comes out in one material.
        /// </summary>
        public static int ListShaders(string source, string? mapName)
        {
            Q3Bsp bsp;
            try
            {
                bsp = Q3Bsp.Load(source, mapName);
            }
            catch (Exception ex)
            {
                Console.WriteLine(ex.Message);
                return 1;
            }
            var counts = new Dictionary<string, int>();
            foreach (Q3Face face in bsp.Faces)
            {
                if (face.Type != 1 && face.Type != 3)
                {
                    continue;
                }
                Q3Texture texture = bsp.Textures[face.Texture];
                if ((texture.Flags & (Q3Bsp.SurfaceNoDraw | Q3Bsp.SurfaceSky
                    | Q3Bsp.SurfaceHint | Q3Bsp.SurfaceSkip)) != 0)
                {
                    continue;
                }
                counts.TryGetValue(texture.Name, out int count);
                counts[texture.Name] = count + face.MeshVertCount / 3;
            }
            Console.WriteLine($"{mapName ?? source}: {counts.Count} shaders drawn");
            foreach ((string name, int count) in counts.OrderByDescending(p => p.Value))
            {
                Console.WriteLine($"  {count,6} triangles  {name}");
            }
            return 0;
        }

        public static int ListMaterials(string room)
        {
            Model model;
            try
            {
                model = Read.GetRoomModelInstance(room).Model;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Could not load {room}: {ex.Message}");
                return 1;
            }
            Recolor recolor = model.Recolors[0];
            Console.WriteLine($"{room}: {model.Materials.Count} materials, {recolor.Textures.Count} textures");
            for (int i = 0; i < model.Materials.Count; i++)
            {
                Material material = model.Materials[i];
                string size = "no texture";
                string format = "";
                if (material.TextureId >= 0 && material.TextureId < recolor.Textures.Count)
                {
                    Texture texture = recolor.Textures[material.TextureId];
                    size = $"{texture.Width}x{texture.Height}";
                    format = texture.Format.ToString();
                }
                Console.WriteLine($"  {i,3}  {material.Name,-32} tex {material.TextureId,3} pal {material.PaletteId,3}"
                    + $"  {size,-9} {format} {(material.RenderMode == RenderMode.Normal ? "" : material.RenderMode.ToString())}");
            }
            return 0;
        }
    }
}
