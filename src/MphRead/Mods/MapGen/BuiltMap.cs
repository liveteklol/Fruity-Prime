using System.Collections.Generic;
using MphRead.Editor;
using OpenTK.Mathematics;

namespace MphRead.Mods.MapGen
{
    /// <summary>
    /// What the packer actually consumes: polygons, collision faces and
    /// entities, with no memory of where they came from.
    ///
    /// Hand-written maps arrive here from a MapDefinition's boxes; a converted
    /// map arrives from another engine's level. Keeping the packer behind this
    /// means an importer only has to produce geometry, and gets the model
    /// format, the collision grid and the room registration for free.
    /// </summary>
    public class BuiltMap
    {
        public MapDefinition Definition { get; }
        public List<BuiltFace> Faces { get; } = new List<BuiltFace>();
        public List<BuiltFace> Solid { get; } = new List<BuiltFace>();
        public List<EntityEditorBase> Entities { get; } = new List<EntityEditorBase>();

        public BuiltMap(MapDefinition definition)
        {
            Definition = definition;
        }
    }

    /// <summary>
    /// One convex polygon. Texture coordinates are in texels, which is what
    /// the hardware wants; the caller decides whether they come from a
    /// projection or from the source data.
    /// </summary>
    public class BuiltFace
    {
        public Vector3[] Points { get; }
        public Vector2[] Texcoords { get; }
        public Vector3 Normal { get; }
        public int Material { get; }
        public float Shade { get; }
        public bool Damaging { get; set; }
        public Terrain Terrain { get; set; } = Terrain.Metal;

        public BuiltFace(Vector3[] points, Vector2[] texcoords, Vector3 normal, int material, float shade)
        {
            Points = points;
            Texcoords = texcoords;
            Normal = normal;
            Material = material;
            Shade = shade;
        }
    }
}
