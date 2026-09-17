using MphRead.Mods.MapGen;
using MphRead.Mods.MapEditor;
using System.IO.Compression;
using System.Text;
using System.Text.Json;

internal static class PackagingGeometryChecks
{
    public static int Run(string temporary, MapDefinition authored)
    {
        int checks = 0;
        var failures = new List<string>();
        void Check(bool valid, string message)
        {
            checks++;
            if (!valid) failures.Add(message);
        }
        void Reject(Action action, string message)
        {
            bool rejected = false;
            try { action(); }
            catch (Exception ex) when (ex is InvalidDataException or MphRead.ProgramException) { rejected = true; }
            Check(rejected, message);
        }

        foreach (string path in new[] { "textures/CON.backup.tex", "audio/aux.music.wav", "COM1.old.archive/preview.png", "LPT9.v1.png" })
        {
            Reject(() => MapPackageReader.CanonicalName(path), "Device path accepted: " + path);
            string archive = Path.Combine(temporary, Guid.NewGuid() + ".fpmap");
            using (var zip = ZipFile.Open(archive, ZipArchiveMode.Create))
            {
                using (var recipe = zip.CreateEntry("project.json").Open())
                    recipe.Write(Encoding.UTF8.GetBytes(MapTemplates.Create("Archive check").Definition.Serialize()));
                using var asset = zip.CreateEntry(path).Open();
                asset.WriteByte(0);
            }
            Reject(() => { using var package = new MapPackageReader(archive); }, "Archive reader accepted device path: " + path);
        }
        Check(MapPackageReader.CanonicalName("textures/floor.v2.tex") == "textures/floor.v2.tex", "Ordinary dotted asset name rejected.");

        var flat = new MapConvexBrush
        {
            Vertices = new() { new[] { 0f, 0, 0 }, new[] { 1f, 0, 0 }, new[] { 0f, 0, 1 }, new[] { 1f, 0, 1 } },
            Faces = new() { new[] { 0, 1, 2 }, new[] { 0, 1, 3 }, new[] { 0, 2, 3 }, new[] { 1, 2, 3 } }
        };
        Reject(() => GeometryCompiler.Compile(flat, 16), "Closed coplanar brush accepted as a volume.");
        var document = new MapDocument(MapTemplates.Create("Convex editor check"));
        document.Edit("Add invalid convex brush", definition => definition.Geometry.Add(flat));
        var invalid = MapCompiler.Compile(document.Snapshot());
        Check(!invalid.Validation.IsValid && invalid.Validation.Diagnostics.Any(d => d.Code == "FP-MAP-013" && d.ObjectId == flat.Id),
            "Invalid brush did not produce an object-specific editor diagnostic.");
        document.History.Undo();
        Check(MapCompiler.Compile(document.Snapshot()).Validation.IsValid, "Undo did not restore valid geometry.");
        document.History.Redo();
        Check(!MapCompiler.Compile(document.Snapshot()).Validation.IsValid, "Redo lost invalid-geometry diagnostic.");
        var thin = new MapConvexBrush
        {
            Vertices = new() { new[] { 0f, 0, 0 }, new[] { 1f, 0, 0 }, new[] { 0f, .01f, 0 }, new[] { 0f, 0, 1 } },
            Faces = flat.Faces
        };
        Check(GeometryCompiler.Compile(thin, 16).Count == 4, "Thin valid convex brush rejected.");

        string root = Path.Combine(temporary, "import-precedence");
        string project = Path.Combine(root, "project");
        Directory.CreateDirectory(project);
        foreach (string file in new[] { "level.bsp", "map.tex" })
        {
            File.WriteAllText(Path.Combine(root, file), "unrelated working-directory file");
            File.WriteAllText(Path.Combine(project, file), "project dependency");
        }
        string previous = Directory.GetCurrentDirectory();
        try
        {
            Directory.SetCurrentDirectory(root);
            var import = new MapImport { BaseDirectory = project, Source = "level.bsp", Textures = "map.tex" };
            Check(import.Resolve() == Path.Combine(project, "level.bsp"), "Working directory shadows project BSP.");
            Check(import.ResolveTextures() == Path.Combine(project, "map.tex"), "Working directory shadows project textures.");
            import.Source = Path.Combine(root, "level.bsp");
            Check(import.Resolve() == import.Source, "Absolute import path changed.");
            import.Source = "fallback.bsp";
            File.WriteAllText(Path.Combine(root, "fallback.bsp"), "fallback");
            Check(import.Resolve() is { } resolved && Path.GetFullPath(resolved) == Path.Combine(root, "fallback.bsp"), "Legacy working-directory fallback lost.");
        }
        finally { Directory.SetCurrentDirectory(previous); }

        // Use only the harness's synthetic texture and isolated game-file root.
        // Installation must not publish a replacement when compilation fails.
        var installSource = MapProjectSerializer.Clone(authored);
        installSource.Name = "INSTALL AUDIT";
        installSource.MapId = Guid.NewGuid();
        string download = MapPackageBuilder.Build(installSource, Path.Combine(temporary, "install.fpmap"));
        string library = Path.Combine(temporary, "install-library");
        string contentHash;
        using (var package = new MapPackageReader(download)) contentHash = package.Manifest!.ContentHash;
        var installed = MapPackageInstaller.Install(download, installSource.MapId, contentHash, MapBuildFingerprint.HashFile(download), library);
        var outputs = CustomRooms.OutputsFor(installed);
        Check(outputs.Complete && MapBuildManifest.IsCurrent(installed, outputs), "Installed map outputs are incomplete or stale.");
        string installedHash = MapBuildFingerprint.HashFile(installed.SourcePath);
        string[] outputHashes = outputs.Files.Select(MapBuildFingerprint.HashFile).ToArray();
        string buildHash = MapBuildFingerprint.HashFile(outputs.Manifest);
        Reject(() => MapPackageInstaller.Install(download, Guid.NewGuid(), contentHash, MapBuildFingerprint.HashFile(download), library),
            "Installer accepted mismatched map identity.");

        var broken = MapProjectSerializer.Clone(installSource);
        broken.Geometry.Add(flat);
        var entries = broken.Assets.ToDictionary(asset => asset.Path, asset => MapAssets.Read(broken, asset.Path));
        entries.Add("project.json", Encoding.UTF8.GetBytes(broken.Serialize()));
        var manifest = new MapPackageManifest
        {
            MapId = broken.MapId, Name = broken.Name, DisplayName = broken.InGameName,
            MapVersion = broken.Version, Author = broken.Author,
            ContentHash = MapPackageReader.ContentHash(entries.Keys, name => entries[name])
        };
        entries.Add("manifest.json", JsonSerializer.SerializeToUtf8Bytes(manifest, MapPackageReader.JsonOptions));
        string badDownload = Path.Combine(temporary, "invalid-install.fpmap");
        using (var zip = ZipFile.Open(badDownload, ZipArchiveMode.Create))
            foreach (var entry in entries)
            {
                using var stream = zip.CreateEntry(entry.Key).Open();
                stream.Write(entry.Value);
            }
        using (var package = new MapPackageReader(badDownload))
            Check(package.Manifest!.ContentHash == manifest.ContentHash, "Invalid-geometry fixture is not a verified archive.");
        Reject(() => MapPackageInstaller.Install(badDownload, broken.MapId, manifest.ContentHash, MapBuildFingerprint.HashFile(badDownload), library),
            "Installer published invalid convex geometry.");
        Check(MapBuildFingerprint.HashFile(installed.SourcePath) == installedHash
            && outputs.Files.Select(MapBuildFingerprint.HashFile).SequenceEqual(outputHashes)
            && MapBuildFingerprint.HashFile(outputs.Manifest) == buildHash,
            "Rejected installation changed the existing package or runtime outputs.");
        if (failures.Count != 0) throw new Exception(string.Join(Environment.NewLine, failures));
        return checks;
    }
}
