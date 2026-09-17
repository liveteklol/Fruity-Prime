using System.IO.Compression;
using System.Text;
using MphRead.Mods.MapGen;
using MphRead.Mods.MapEditor;
using MphRead.Mods.Network;
using System.Buffers.Binary;
using System.Text.Json;

if(args.Length==1&&args[0]=="--network")return MapNetworkTests.Run();

if(args.Length==2&&args[0]=="--join")
{
    Directory.SetCurrentDirectory(AppContext.BaseDirectory);
    CustomRooms.MapDirectory=Path.Combine(AppContext.BaseDirectory,"empty-client-maps");
    bool joined=NetLaunch.Join("127.0.0.1",int.Parse(args[1]),"Map transfer check",MphRead.Hunter.Samus);
    Console.WriteLine(joined?"PASS: loopback map download, compile and install.":"FAIL: "+NetLaunch.LastJoinError);
    NetSession.Stop();return joined?0:1;
}

string temporary = Path.Combine(Path.GetTempPath(), "fruity-mapcheck-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(temporary);
int checks = 0;
void Check(bool condition, string message)
{
    if (!condition) throw new Exception(message);
    checks++;
}
void Reject(Action action, string message)
{
    try { action(); } catch (Exception ex) when (ex is InvalidDataException or IOException or MphRead.ProgramException or System.Text.Json.JsonException) { checks++; return; }
    throw new Exception(message);
}
MapDefinition Arena() => new()
{
    Name = "CHECK ARENA", Materials = new() { new() },
    Brushes = new() { new() { Min = new[] { -8f, -1, -8 }, Max = new[] { 8f, 0, 8 } } },
    Spawns = new() { new() { Position = new[] { 0f, 0.1f, 0 } } }
};
void Zip(string path, params (string Name, byte[] Bytes)[] files)
{
    using var archive = ZipFile.Open(path, ZipArchiveMode.Create);
    foreach (var file in files)
    {
        using var stream = archive.CreateEntry(file.Name).Open();
        stream.Write(file.Bytes);
    }
}
try
{
    checks += CollisionIntegrationChecks.Run(temporary);
    var arena = Arena();
    Check(MapCompiler.Compile(arena).Validation.IsValid, "Valid native map rejected.");
    string recipe = Path.Combine(temporary, "arena.json");
    arena.Save(recipe);
    byte[] original = File.ReadAllBytes(recipe);
    var project = MapProjectSerializer.Load(recipe);
    var upgraded = MapProjectMigrator.Upgrade(project);
    Check(upgraded.Definition.FormatVersion == 2 && upgraded.Metadata.Id != Guid.Empty, "Migration identity missing.");
    Check(project.Metadata.Id == Guid.Empty && File.ReadAllBytes(recipe).SequenceEqual(original), "Migration rewrote legacy source.");
    Check(MapProjectMigrator.Upgrade(upgraded).Metadata.Id == upgraded.Metadata.Id, "Migration changed persistent identity.");
    foreach (string name in new[] { "", "../outside", "C:\\bad", "bad/name", "CON", "name.", "name " })
        Check(!MapValidator.ValidRuntimeName(name), "Unsafe runtime name accepted: " + name);
    var bad = Arena(); bad.Spawns[0].Position = new[] { 0f, -0.5f, 0 };
    Check(MapValidator.Validate(bad).Diagnostics.Any(d => d.Code == "FP-MAP-002"), "Embedded spawn missed.");
    bad = Arena(); bad.Brushes[0].Material = 9;
    Check(!MapValidator.Validate(bad).IsValid, "Invalid material accepted.");
    bad = Arena(); bad.Brushes[0].Min[0] = float.NaN;
    Check(!MapValidator.Validate(bad).IsValid, "Nonfinite geometry accepted.");
    bad = Arena(); bad.JumpPads.Add(new() { Target = new[] { 1f, 1, 1 }, Vector = new[] { 0f, 1, 0 }, Speed = 1 });
    Check(!MapValidator.Validate(bad).IsValid, "Ambiguous jump pad accepted.");
    var loaded = MapDefinition.Load(recipe);
    var output = MapOutputSet.Create(loaded, temporary, temporary, temporary);
    foreach (string file in output.Files) File.WriteAllBytes(file, new byte[] { 1, 2, 3 });
    MapBuildManifest.Write(loaded, output);
    Check(MapBuildManifest.IsCurrent(loaded, output), "Unchanged build is stale.");
    foreach (string file in output.Files)
    {
        byte[] bytes = File.ReadAllBytes(file); File.Delete(file);
        Check(!MapBuildManifest.IsCurrent(loaded, output), "Missing output ignored: " + file);
        File.WriteAllBytes(file, bytes);
    }
    File.WriteAllBytes(output.Animation, new byte[] { 9 });
    Check(!MapBuildManifest.IsCurrent(loaded, output), "Damaged output ignored.");
    string source = Path.Combine(temporary, "level.pk3"), texture = Path.Combine(temporary, "level.tex");
    File.WriteAllText(source, "level"); File.WriteAllText(texture, "texture");
    loaded.Import = new() { Source = source, Textures = texture };
    var before = MapBuildFingerprint.Create(loaded);
    DateTime time = File.GetLastWriteTimeUtc(source);
    File.WriteAllText(source, "other"); File.SetLastWriteTimeUtc(source, time);
    Check(before != MapBuildFingerprint.Create(loaded), "Source hash depended on timestamp.");
    before = MapBuildFingerprint.Create(loaded); File.WriteAllText(texture, "changed");
    Check(before != MapBuildFingerprint.Create(loaded), "Texture changes ignored.");
    string a = MapPackageBuilder.Build(arena, Path.Combine(temporary, "a.fpmap"));
    string b = MapPackageBuilder.Build(arena, Path.Combine(temporary, "b.fpmap"));
    Check(File.ReadAllBytes(a).SequenceEqual(File.ReadAllBytes(b)), "Package output is not deterministic.");
    var roundtrip = MapDefinition.Load(a);
    Check(roundtrip.Name == arena.Name && roundtrip.MapId != Guid.Empty && MapCompiler.Compile(roundtrip).Validation.IsValid, "Native package roundtrip failed.");
    string legacy = Path.Combine(temporary, "legacy.fpmap");
    Zip(legacy, ("arena.json", original));
    Check(MapDefinition.Load(legacy).FormatVersion == 1, "Legacy package compatibility failed.");
    foreach (string unsafeName in new[] { "../bad.json", "/bad.json", "C:/bad.json", "x\\bad.json", "x/./bad.json", "CON.json" })
    {
        string zip = Path.Combine(temporary, Guid.NewGuid() + ".fpmap");
        Zip(zip, (unsafeName, original));
        Reject(() => MapDefinition.Load(zip), "Unsafe archive accepted.");
    }
    string duplicate = Path.Combine(temporary, "duplicate.fpmap");
    Zip(duplicate, ("arena.json", original), ("ARENA.JSON", original));
    Reject(() => MapDefinition.Load(duplicate), "Duplicate package path accepted.");
    string missing = Path.Combine(temporary, "missing.fpmap");
    Zip(missing, ("preview.png", new byte[] { 0 }));
    Reject(() => MapDefinition.Load(missing), "Missing project accepted.");
    using (var zip = ZipFile.Open(b, ZipArchiveMode.Update))
    {
        zip.GetEntry("project.json")!.Delete();
        using var writer = new StreamWriter(zip.CreateEntry("project.json").Open());
        writer.Write("{}");
    }
    Reject(() => MapDefinition.Load(b), "Invalid content hash accepted.");
    string catalogPath = Path.Combine(temporary, "catalog"); Directory.CreateDirectory(catalogPath);
    arena.Save(Path.Combine(catalogPath, "one.json")); arena.Save(Path.Combine(catalogPath, "two.json"));
    Check(new MapCatalog(catalogPath).Refresh().All(e => !e.Validation.IsValid), "Duplicate runtime names accepted.");
    foreach(MapGeometry geometry in new MapGeometry[]{new MapBox(),new MapWedge(),new MapPrism{Sides=12},new MapConvexBrush
    {Vertices=new(){new[]{0f,0,0},new[]{1f,0,0},new[]{0f,1,0},new[]{0f,0,1}},Faces=new(){new[]{0,1,2},new[]{0,1,3},new[]{0,2,3},new[]{1,2,3}}}})
    {
        geometry.Transform.Position=new[]{2f,2,2};geometry.Transform.Scale=new[]{2f,2,2};
        var faces=GeometryCompiler.Compile(geometry,16);
        var center=faces.SelectMany(f=>f.Points).Distinct().Aggregate(OpenTK.Mathematics.Vector3.Zero,(sum,p)=>sum+p)/faces.SelectMany(f=>f.Points).Distinct().Count();
        Check(faces.All(f=>OpenTK.Mathematics.Vector3.Dot(f.Normal,f.Points[0]-center)>0),"Primitive face points inward.");
        var d=Arena();d.Geometry.Add(geometry);
        Check(MapCompiler.Compile(MapProjectSerializer.Clone(d)).Validation.IsValid,"Primitive roundtrip failed.");
    }
    var document=new MapDocument(MapTemplates.Create("Editor check"));
    string saved=Path.Combine(temporary,"editor.json");document.Save(saved);
    Check(!document.IsDirty,"Newly saved document is dirty.");
    Guid selected=document.Project.Definition.Geometry[0].Id;document.Selection.Add(selected);
    document.Edit("Move",d=>MapObjects.All(d).Single(o=>o.Id==selected).Move(new[]{2f,0,0}));
    Check(document.IsDirty&&document.Project.Definition.Geometry[0].Transform.Position[0]==2,"Edit was not committed.");
    document.History.Undo();Check(!document.IsDirty&&document.Selection.Contains(selected),"Undo did not restore saved state/selection.");
    document.History.Redo();Check(document.IsDirty,"Redo did not restore edit.");
    document.Autosave(temporary);Check(document.HasRecovery(temporary)&&MapDefinition.Load(saved).Geometry[0].Transform.Position[0]==0,"Autosave overwrote source.");
    document.History.Undo();document.Restore(temporary);Check(document.Project.Definition.Geometry[0].Transform.Position[0]==2,"Recovery lost changes.");
    document.Edit("Duplicate",d=>MapObjects.Duplicate(d,document.Selection));
    Check(document.Project.Definition.Geometry.Select(g=>g.Id).Distinct().Count()==2,"Duplicate reused geometry identity.");
    document.Edit("Delete",d=>MapObjects.Delete(d,document.Selection));Check(!document.Selection.Contains(selected),"Deleted selection retained.");
    document.DiscardRecovery(temporary);Check(!document.HasRecovery(temporary),"Recovery discard failed.");
    string corrupt=Path.Combine(temporary,"corrupt.fpmap");File.WriteAllText(corrupt,"not a zip");Reject(()=>new MapPackageReader(corrupt),"Corrupt ZIP accepted.");
    string oversized=Path.Combine(temporary,"oversized.fpmap");
    using(var archive=ZipFile.Open(oversized,ZipArchiveMode.Create))
    using(var entry=archive.CreateEntry("audio/large.wav").Open())
    {byte[] zero=new byte[1024*1024];for(int i=0;i<65;i++)entry.Write(zero);}
    Reject(()=>new MapPackageReader(oversized),"Expanded entry budget ignored.");
    string many=Path.Combine(temporary,"many.fpmap");
    using(var archive=ZipFile.Open(many,ZipArchiveMode.Create))for(int i=0;i<2049;i++)archive.CreateEntry($"a{i}.png");
    Reject(()=>new MapPackageReader(many),"Archive entry count budget ignored.");
    CustomRooms.MapDirectory=temporary;
    var server=new MapTransferServer();server.Prepare(new[]{arena});
    byte[]? reply=null;PacketType replyType=default;
    void Receive(PacketType type,byte[] bytes){replyType=type;reply=bytes;}
    server.Handle(new byte[]{0},arena.Name,Receive);
    var offer=JsonSerializer.Deserialize<MapOffer>(reply!)!;
    Check(replyType==PacketType.MapOffer&&offer.MapId!=Guid.Empty&&offer.Size>0&&offer.Error==null,"Server failed to offer native map.");
    using var download=new MemoryStream();
    for(long offset=0;offset<offer.Size;offset+=MapTransferServer.ChunkSize)
    {
        byte[] request=new byte[41];request[0]=1;Convert.FromHexString(offer.ArchiveHash).CopyTo(request,1);BinaryPrimitives.WriteInt64LittleEndian(request.AsSpan(33),offset);
        reply=null;server.Handle(request,arena.Name,Receive);Check(replyType==PacketType.MapChunk&&reply!=null&&reply.Length<=1000,"Invalid transfer chunk.");
        byte[] first=reply!;server.Handle(request,arena.Name,Receive);Check(first.SequenceEqual(reply!),"Retransmission changed bytes.");
        download.Write(first.AsSpan(40));
    }
    string received=Path.Combine(temporary,"received.fpmap");File.WriteAllBytes(received,download.ToArray());
    Check(MapBuildFingerprint.HashFile(received)==offer.ArchiveHash,"Chunk assembly hash differs.");
    using(var receivedPackage=new MapPackageReader(received))Check(receivedPackage.Manifest?.ContentHash==offer.ContentHash,"Offer identity differs from package.");
    foreach(long offset in new long[]{-1,1,long.MaxValue})
    {
        byte[] request=new byte[41];request[0]=1;Convert.FromHexString(offer.ArchiveHash).CopyTo(request,1);BinaryPrimitives.WriteInt64LittleEndian(request.AsSpan(33),offset);
        reply=null;server.Handle(request,arena.Name,Receive);Check(reply==null,"Invalid chunk offset accepted.");
    }
    reply=null;server.Handle(new byte[41],arena.Name,Receive);Check(reply==null,"Malformed chunk request accepted.");
    var missingAsset=MapProjectSerializer.Clone(arena);
    missingAsset.Assets.Add(new(){Path="textures/"+new string('\u754c',200)+".png",Kind="texture"});
    var failedServer=new MapTransferServer();failedServer.Prepare(new[]{missingAsset});
    reply=null;failedServer.Handle(new byte[]{0},missingAsset.Name,Receive);
    Check(reply is {Length:<=1023}&&JsonSerializer.Deserialize<MapOffer>(reply)!.Error!=null,"Escaped error metadata exceeds a network packet.");
    var authored=MapTemplates.Create("Studio regression",true);
    authored.Definition.BaseDirectory=temporary;
    string textureAsset="textures/check.tex";Directory.CreateDirectory(Path.Combine(temporary,"textures"));
    using(var stream=File.Create(Path.Combine(temporary,textureAsset)))
    using(var writer=new BinaryWriter(stream))
    {
        writer.Write(Encoding.ASCII.GetBytes("FPTX"));writer.Write((ushort)1);writer.Write((ushort)1);
        writer.Write((ushort)0);writer.Write((ushort)8);writer.Write((ushort)8);writer.Write((ushort)2);writer.Write((ushort)5);
        writer.Write(Encoding.UTF8.GetBytes("check"));writer.Write((ushort)32767);writer.Write((ushort)1023);
        for(int pixel=0;pixel<64;pixel++)writer.Write((byte)(pixel%2));
    }
    authored.Definition.Assets.Add(new(){Path=textureAsset,Kind="texture"});authored.Definition.Materials[0].Texture=textureAsset;
    authored.Definition.Geometry.Add(new MapWedge {Transform=new(){Position=new[]{-5f,1,0},Scale=new[]{3f,2,4}}});
    authored.Definition.Geometry.Add(new MapPrism {Sides=12,Transform=new(){Position=new[]{5f,1,0},Scale=new[]{3f,2,3}}});
    string runtime=Path.Combine(temporary,"runtime");
    MapPacker.Generate(authored.Definition,runtime,runtime,runtime,false);
    var packed=MapOutputSet.Create(authored.Definition,runtime,runtime,runtime);
    Check(packed.Complete&&MapBuildManifest.IsCurrent(authored.Definition,packed),"Native custom-texture runtime build failed.");
    byte[] collision=File.ReadAllBytes(packed.Collision);
    var header=MphRead.Read.ReadStruct<MphRead.Formats.Collision.CollisionHeader>(collision);
    var decoded=MphRead.Formats.Collision.Collision.ReadMphCollision(header,collision,-1);
    Check(decoded.Data.Count>40&&decoded.Points.Count>20,"Primitive collision readback lost geometry.");
    MphRead.Paths.UpdatePaths();
    var entities=MphRead.Read.GetEntitiesFromPath(packed.Entities,0,false);
    Check(entities.Count>=9,"Packed authoring entities failed readback.");
    var graph=MapNodePacker.Analyze(MapCompiler.Compile(authored).Map!.Solid);
    Check(graph.Positions.Length>1&&graph.Edges>0,"Authored map navigation is empty.");
    string ownPackage=MapPackageBuilder.Build(authored.Definition,Path.Combine(temporary,"owned.fpmap"));
    var ownDocument=new MapDocument(MapProjectSerializer.Load(ownPackage),ownPackage);
    string export=Path.Combine(temporary,"export","map.json");ownDocument.Save(export);
    Check(MapCompiler.Compile(MapDefinition.Load(export)).Validation.IsValid&&File.Exists(Path.Combine(temporary,"export",textureAsset)),"Packaged assets did not survive Save As.");
    foreach(Action<MapDefinition> corruptDefinition in new Action<MapDefinition>[]
    {d=>d.Materials=null!,d=>d.Light1Vector=null!,d=>d.Geometry.Add(null!),d=>d.Spawns.Add(null!),d=>d.Assets.Add(null!),d=>d.Import=new(){Source=null!},d=>d.Geometry[0].Transform=null!})
    {
        var invalid=authored.ToDefinition();corruptDefinition(invalid);Check(!MapValidator.Validate(invalid).IsValid,"Malformed source was not diagnosed.");
    }
    string originalDirectory=Directory.GetCurrentDirectory();
    try
    {
        File.WriteAllText(Path.Combine(temporary,"paths.txt"),"AMHE0="+temporary);Directory.SetCurrentDirectory(temporary);MphRead.Paths.UpdatePaths();
        var outputSet=CustomRooms.OutputsFor(authored.Definition);
        void Publish()
        {
            MapPacker.Generate(authored.Definition,Path.GetDirectoryName(outputSet.Model)!,Path.GetDirectoryName(outputSet.Entities)!,Path.GetDirectoryName(outputSet.Nodes)!,false);
            typeof(MphRead.Metadata).GetMethod("RegisterDownloadedMap",System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Static)!.Invoke(null,new object[]{authored.Definition});
        }
        Publish();var oldModel=MphRead.Read.GetRoomModelInstance(authored.Definition.Name).Model;
        var oldCollision=MphRead.Formats.Collision.Collision.GetCollision(MphRead.Metadata.GetRoomByName(authored.Definition.Name).Item1!,-1).Info;
        oldModel.Meshes[0].ListId=1234; // Simulate a live scene retaining GL lists; no GL call is made.
        authored.Definition.Geometry.Add(new MapBox {Transform=new(){Position=new[]{0f,3,0},Scale=new[]{2f,1,2}}});Publish();
        Check(!ReferenceEquals(oldModel,MphRead.Read.GetRoomModelInstance(authored.Definition.Name).Model),"Map replacement reused a stale render model.");
        Check(MphRead.Read.CachedModels.Contains(oldModel),"Live GL resources became unreachable before scene cleanup.");
        Check(!ReferenceEquals(oldCollision,MphRead.Formats.Collision.Collision.GetCollision(MphRead.Metadata.GetRoomByName(authored.Definition.Name).Item1!,-1).Info),"Map replacement reused stale collision.");
        oldModel.Meshes[0].ListId=0;MphRead.Read.ClearCache();authored.Definition.Geometry.RemoveAt(authored.Definition.Geometry.Count-1);
    }
    finally{Directory.SetCurrentDirectory(originalDirectory);}
    checks += PackagingGeometryChecks.Run(temporary, authored.Definition);
    if(args.Length==2&&args[0]=="--fixtures")MapProjectExport.Save(authored.Definition,Path.Combine(Path.GetFullPath(args[1]),"studio-regression.json"));
    Console.WriteLine($"PASS: {checks} map pipeline checks.");
}
finally
{
    // Only the uniquely created test directory is removed.
    Directory.Delete(temporary, true);
}
return 0;
