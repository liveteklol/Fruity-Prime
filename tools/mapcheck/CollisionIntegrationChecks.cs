using System.IO.Compression;
using System.Text;
using MphRead.Mods.MapGen;

internal static class CollisionIntegrationChecks
{
    public static int Run(string temporary)
    {
        int checks = 0;
        void Check(bool condition, string message)
        {
            checks++;
            if (!condition) throw new Exception(message);
        }
        void Reject(Action action, string message)
        {
            bool rejected = false;
            try { action(); }
            catch (Exception ex) when (ex is InvalidDataException or MphRead.ProgramException) { rejected = true; }
            Check(rejected, message);
        }
        const string mesh = "v -2 0 -2\nv -2 0 2\nv 2 0 2\nv 2 0 -2\nusemtl ice_nobeams\nf 1 2 3 4\n";
        string root = Path.Combine(temporary, "collision-project");
        Directory.CreateDirectory(root);
        string obj = Path.Combine(root, "mesh.obj");
        File.WriteAllText(obj, mesh);
        var definition = new MapDefinition
        {
            Name = "COLLISION CHECK", Materials = new() { new() },
            Brushes = new() { new() { Min = new[] { -8f, -1, -8 }, Max = new[] { 8f, 0, 8 } } },
            Spawns = new() { new() { Position = new[] { 0f, .1f, 0 } } },
            Collision = new() { Source = "mesh.obj" }
        };
        string recipe = Path.Combine(root, "map.json");
        definition.Save(recipe);
        definition = MapDefinition.Load(recipe);
        MapCompilation compiled = MapCompiler.Compile(definition);
        Check(compiled.Validation.IsValid && compiled.Map!.Solid.Count == 1,
            "Compiler did not replace generated collision with project-relative OBJ.");
        Check(compiled.Map!.Solid[0].IgnoreBeams && compiled.Map.Solid[0].Terrain == MphRead.Terrain.Ice,
            "Collision surface flags were lost.");
        var fingerprint = MapBuildFingerprint.Create(definition);
        DateTime stamp = File.GetLastWriteTimeUtc(obj);
        File.AppendAllText(obj, "# edited\n");
        File.SetLastWriteTimeUtc(obj, stamp);
        Check(fingerprint != MapBuildFingerprint.Create(definition), "Collision edit did not invalidate build cache.");
        string previous = Directory.GetCurrentDirectory();
        try
        {
            File.WriteAllText(Path.Combine(temporary, "mesh.obj"), "unrelated file");
            Directory.SetCurrentDirectory(temporary);
            Check(definition.Collision!.Resolve() == obj, "Working directory shadows project collision.");
        }
        finally { Directory.SetCurrentDirectory(previous); }
        string archive = MapPackageBuilder.Build(definition, Path.Combine(temporary, "collision.fpmap"));
        string duplicate = MapPackageBuilder.Build(definition, Path.Combine(temporary, "collision-copy.fpmap"));
        Check(File.ReadAllBytes(archive).SequenceEqual(File.ReadAllBytes(duplicate)), "Collision package is nondeterministic.");
        string looseCopy = Path.Combine(temporary, "export-loose", "map.json");
        MapProjectExport.Save(MapProjectSerializer.Clone(definition), looseCopy);
        Check(MapCompiler.Compile(MapDefinition.Load(looseCopy)).Validation.IsValid, "Loose project Save As lost collision dependency.");
        File.Delete(obj);
        var roundtrip = MapDefinition.Load(archive);
        compiled = MapCompiler.Compile(roundtrip);
        Check(compiled.Validation.IsValid && compiled.Map!.Solid.Count == 1 && compiled.Map.Solid[0].IgnoreBeams,
            "Packaged collision cannot compile without original files.");
        string repacked = MapPackageBuilder.Build(roundtrip, Path.Combine(temporary, "collision-repacked.fpmap"));
        Check(MapCompiler.Compile(MapDefinition.Load(repacked)).Validation.IsValid, "Cannot rebuild package from bundled collision.");
        string exported = Path.Combine(temporary, "export-bundle", "map.json");
        MapProjectExport.Save(MapProjectSerializer.Clone(roundtrip), exported);
        File.Delete(archive);
        Check(MapCompiler.Compile(MapDefinition.Load(exported)).Validation.IsValid,
            "Package Save As lost collision when original archive was removed.");
        Check(!MapCompiler.Compile(definition).Validation.IsValid, "Missing collision silently used generated faces.");
        foreach (string source in new[] { "mesh.obj", "../mesh.obj", "C:/mesh.obj" })
        {
            definition.Collision!.Source = source;
            string invalid = Path.Combine(temporary, Guid.NewGuid() + ".fpmap");
            using (var zip = ZipFile.Open(invalid, ZipArchiveMode.Create))
            using (var stream = zip.CreateEntry("map.json").Open())
                stream.Write(Encoding.UTF8.GetBytes(definition.Serialize()));
            Reject(() => { using var reader = new MapPackageReader(invalid); }, "Package accepted invalid/missing collision: " + source);
        }
        foreach (string value in new[] { "NaN", "Infinity", "1e30", "524288" })
            Reject(() => CollisionObj.Read(Encoding.UTF8.GetBytes(mesh.Replace("v -2", "v " + value)), "invalid.obj", false),
                "OBJ accepted nonfinite or out-of-range coordinate.");
        Check(CollisionObj.Read(Encoding.UTF8.GetBytes("v 524287.96875 0 0\nv 524287.96875 1 0\nv 524287.96875 0 1\nf 1 2 3\n"), "edge.obj", false).Faces.Count == 1,
            "Largest positive representable OBJ coordinate was rejected.");
        Check(CollisionObj.Read(Encoding.UTF8.GetBytes("v -524288 0 0\nv -524288 1 0\nv -524288 0 1\nf 1 2 3\n"), "edge.obj", false).Faces.Count == 1,
            "Smallest negative representable OBJ coordinate was rejected.");
        definition.Collision!.Source = null!;
        Check(!MapCompiler.Compile(definition).Validation.IsValid, "Null collision source bypassed diagnostics.");
        Reject(() => CollisionObj.Read(Encoding.UTF8.GetBytes("v 0 -524288 0\nv 1 -524288 0\nv 0 -524288 1\nf 1 2 3\n"), "zup-edge.obj", true),
            "Z-up transform produced an out-of-range fixed-point coordinate.");
        return checks;
    }
}
