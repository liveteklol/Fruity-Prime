using System.Buffers.Binary;
using System.IO.Compression;
using System.Net;
using System.Reflection;
using System.Text;
using System.Text.Json;
using MphRead;
using MphRead.Mods.MapEditor;
using MphRead.Mods.MapGen;
using MphRead.Mods.Network;

// Asset-free production transfer tests. Every download and runtime output lives
// beneath a fresh temporary directory; no user package/library is modified.
internal static class MapNetworkTests
{
    private const BindingFlags Private = BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance;
    private static int _checks, _failures;
    private static void Check(bool ok, string message)
    {
        _checks++;
        if (!ok) { _failures++; Console.Error.WriteLine("FAIL: " + message); }
    }
    private static void Set(string name, object? value) => typeof(NetMapTransfer).GetField(name, Private)!.SetValue(null, value);
    private static T Get<T>(string name) => (T)typeof(NetMapTransfer).GetField(name, Private)!.GetValue(null)!;

    public static int Run()
    {
        string root = Path.Combine(Path.GetTempPath(), "fruity-map-network-" + Guid.NewGuid().ToString("N"));
        string cwd = Directory.GetCurrentDirectory(), maps = CustomRooms.MapDirectory;
        Directory.CreateDirectory(root);
        try
        {
            Directory.SetCurrentDirectory(root);
            CustomRooms.MapDirectory = Path.Combine(root, "library");
            Directory.CreateDirectory(CustomRooms.MapDirectory);
            File.WriteAllText("paths.txt", "AMHE0=" + root);
            Paths.UpdatePaths();
            ReceiveBoundaries(root);
            ServerBudget();
            DownloadBoundaries(root);
            Console.WriteLine($"{(_failures == 0 ? "PASS" : "FAIL")}: {_checks} map network checks; {_failures} failures.");
            return _failures == 0 ? 0 : 1;
        }
        finally
        {
            NetSession.Stop(); Set("_wanted", ""); Set("_download", null);
            Directory.SetCurrentDirectory(cwd); CustomRooms.MapDirectory = maps;
            Directory.Delete(root, true);
        }
    }

    private static void ReceiveBoundaries(string root)
    {
        using var peer = new NetTransport(0);
        var sender = new IPEndPoint(IPAddress.Loopback, peer.LocalPort);
        var stranger = new IPEndPoint(IPAddress.Loopback, peer.LocalPort == 65535 ? 65534 : peer.LocalPort + 1);
        NetSession.StartClient("127.0.0.1", peer.LocalPort);
        var handler = typeof(NetSession).GetMethod("Handle", Private)!;
        void Deliver(PacketType type, byte[] body, IPEndPoint? from = null)
        {
            byte[] data = new byte[body.Length + 1]; data[0] = (byte)type; body.CopyTo(data, 1);
            handler.Invoke(null, new object[] { new ReceivedPacket(from ?? sender, data, data.Length), 0.0 });
        }
        var offer = new MapOffer("WIRE MAP", Guid.NewGuid(), "1", new string('a', 64), new string('b', 64), 18 * 960);
        Set("_wanted", offer.Name); Set("_offer", null); Set("_error", null);
        Deliver(PacketType.MapOffer, JsonSerializer.SerializeToUtf8Bytes(offer), stranger);
        Check(Get<MapOffer?>("_offer") == null, "foreign endpoint cannot establish an offer");
        foreach (var invalid in new[] { offer with { Size = -1 }, offer with { Size = MapPackageReader.MaxArchiveBytes + 1 },
            offer with { ArchiveHash = new string('z', 64) }, offer with { ContentHash = null! }, offer with { MapId = Guid.Empty } })
        {
            Set("_error", null);
            Deliver(PacketType.MapOffer, JsonSerializer.SerializeToUtf8Bytes(invalid));
            Check(Get<MapOffer?>("_offer") == null && Get<string?>("_error") != null, "invalid offer rejected before download allocation");
        }
        Set("_error", null);
        Deliver(PacketType.MapOffer, Encoding.UTF8.GetBytes("{"));
        Check(Get<string?>("_error") != null, "malformed JSON diagnosed");
        Set("_error", null);
        Deliver(PacketType.MapOffer, JsonSerializer.SerializeToUtf8Bytes(offer));
        Check(Get<MapOffer?>("_offer") == offer, "selected server can establish offer");
        using (var file = new FileStream(Path.Combine(root, "chunks"), FileMode.CreateNew, FileAccess.ReadWrite))
        {
            Set("_download", file); Set("_chunks", new bool[18]); Set("_received", 0L); Set("_written", 0L);
            byte[] Chunk(long offset, int length = 960)
            {
                byte[] bytes = new byte[40 + length]; Convert.FromHexString(offer.ArchiveHash).CopyTo(bytes, 0);
                BinaryPrimitives.WriteInt64LittleEndian(bytes.AsSpan(32), offset);
                bytes.AsSpan(40).Fill((byte)(offset / 960)); return bytes;
            }
            Deliver(PacketType.MapChunk, Chunk(0), stranger);
            foreach (long offset in new long[] { -1, 1, long.MaxValue, 16 * 960, offer.Size })
                Deliver(PacketType.MapChunk, Chunk(offset));
            Deliver(PacketType.MapChunk, Chunk(0, 959));
            byte[] wrongHash = Chunk(0); wrongHash[0] ^= 1; Deliver(PacketType.MapChunk, wrongHash);
            Check(file.Length == 0 && Get<long>("_written") == 0, "invalid, foreign and out-of-window chunks cannot write");
            Deliver(PacketType.MapChunk, Chunk(960)); Deliver(PacketType.MapChunk, Chunk(960));
            Check(Get<long>("_written") == 960 && Get<long>("_received") == 0, "out-of-order chunk counted exactly once");
            Deliver(PacketType.MapChunk, Chunk(0));
            Check(Get<long>("_received") == 1920 && Get<long>("_written") == 1920, "gap closure advances contiguous prefix");
            file.Position = 960; Check(file.ReadByte() == 1, "reordered chunk stored at its advertised offset");
            Set("_download", null);
        }
        Set("_wanted", ""); NetSession.Stop();
    }

    private static void ServerBudget()
    {
        var server = new DedicatedServer(0) { RunsTheMatch = false };
        var sender = new IPEndPoint(IPAddress.Loopback, 31001);
        void Send(PacketType type, byte[] body, double now)
        {
            byte[] data = new byte[body.Length + 1]; data[0] = (byte)type; body.CopyTo(data, 1);
            typeof(DedicatedServer).GetMethod("Handle", Private)!.Invoke(server,
                new object[] { new ReceivedPacket(sender, data, data.Length), now });
        }
        byte[] hello = new byte[6]; hello[0] = NetConfig.ProtocolVersion; hello[1] = 255;
        BinaryPrimitives.WriteUInt32LittleEndian(hello.AsSpan(2), 1); Send(PacketType.Hello, hello, 1);
        var peers = (System.Collections.IList)typeof(DedicatedServer).GetField("_peers", Private)!.GetValue(server)!;
        object peer = peers[0]!;
        double Tokens() => (double)peer.GetType().GetField("MapRequestTokens")!.GetValue(peer)!;
        for (int i = 0; i < 100; i++) Send(PacketType.MapWant, new byte[1], 1);
        Check(Tokens() == 0, "map requests consume bounded per-peer burst budget");
        Send(PacketType.MapWant, new byte[1], 1.004);
        Check(Tokens() >= 0 && Tokens() < 2, "request budget replenishes at configured rate");
    }

    private static (bool Ok, string? Error, int Chunks) Exchange(MapOffer offer, byte[] archive, bool rotate = false)
    {
        using var server = new NetTransport(0);
        using var cancel = new CancellationTokenSource();
        NetSession.StartClient("127.0.0.1", server.LocalPort);
        NetSession.ApplyMatchState(new MatchStatePacket { RoomKey = offer.Name, MatchId = 1, Mode = (byte)GameMode.Battle }, false);
        int chunks = 0;
        var responder = Task.Run(() =>
        {
            while (!cancel.IsCancellationRequested)
            {
                foreach (var request in server.Drain())
                {
                    if (request.Type != PacketType.MapWant) continue;
                    if (request.Payload.Length == 1)
                    {
                        if (rotate)
                        {
                            byte[] state = new byte[MatchStatePacket.Size];
                            new MatchStatePacket { RoomKey = offer.Name, MatchId = 2, Mode = (byte)GameMode.Battle }.Write(state);
                            server.Send(request.Sender, PacketType.MatchState, state);
                        }
                        server.Send(request.Sender, PacketType.MapOffer, JsonSerializer.SerializeToUtf8Bytes(offer));
                    }
                    else if (request.Payload.Length == 41)
                    {
                        int offset = checked((int)BinaryPrimitives.ReadInt64LittleEndian(request.Payload[33..]));
                        int length = Math.Min(960, archive.Length - offset);
                        byte[] chunk = new byte[40 + length]; Convert.FromHexString(offer.ArchiveHash).CopyTo(chunk, 0);
                        BinaryPrimitives.WriteInt64LittleEndian(chunk.AsSpan(32), offset); archive.AsSpan(offset, length).CopyTo(chunk.AsSpan(40));
                        server.Send(request.Sender, PacketType.MapChunk, chunk); Interlocked.Increment(ref chunks);
                    }
                }
                Thread.Sleep(1);
            }
        });
        try { bool ok = NetMapTransfer.Ensure(offer.Name, force: true); return (ok, NetMapTransfer.LastError, chunks); }
        finally { cancel.Cancel(); responder.GetAwaiter().GetResult(); NetSession.Stop(); }
    }

    private static void DownloadBoundaries(string root)
    {
        var definition = MapTemplates.Create("ACTUAL MAP").Definition;
        definition.BaseDirectory = root;
        Directory.CreateDirectory(Path.Combine(root, "textures"));
        const string asset = "textures/check.tex";
        using (var writer = new BinaryWriter(File.Create(Path.Combine(root, asset))))
        {
            writer.Write(Encoding.ASCII.GetBytes("FPTX")); writer.Write((ushort)1); writer.Write((ushort)1);
            writer.Write((ushort)0); writer.Write((ushort)8); writer.Write((ushort)8); writer.Write((ushort)2); writer.Write((ushort)5);
            writer.Write(Encoding.UTF8.GetBytes("check")); writer.Write((ushort)32767); writer.Write((ushort)1023);
            for (int i = 0; i < 64; i++) writer.Write((byte)(i % 2));
        }
        definition.Assets.Add(new() { Path = asset, Kind = "texture" }); definition.Materials[0].Texture = asset;
        string packagePath = MapPackageBuilder.Build(definition, Path.Combine(root, "actual.fpmap"));
        using var package = new MapPackageReader(packagePath);
        var manifest = package.Manifest!;
        byte[] archive = File.ReadAllBytes(packagePath);
        var offer = new MapOffer(definition.Name, manifest.MapId, manifest.MapVersion, manifest.ContentHash,
            MapBuildFingerprint.HashFile(packagePath), archive.Length);
        var mismatch = Exchange(offer with { Name = "EXPECTED MAP" }, archive);
        Check(!mismatch.Ok && mismatch.Error?.Contains("identity", StringComparison.OrdinalIgnoreCase) == true,
            "network package for a different room rejected before install");
        Check(!File.Exists(Path.Combine(NetMapTransfer.LibraryDirectory, offer.MapId.ToString("N") + ".fpmap"))
            && !CustomRooms.OutputsFor(definition).Complete, "identity mismatch publishes neither package nor runtime outputs");
        string corruptPath = Path.Combine(root, "corrupt.fpmap"); File.WriteAllText(corruptPath, "broken");
        var local = MapProjectSerializer.Clone(definition); local.BundlePath = corruptPath;
        typeof(CustomRooms).GetMethod("InstallSnapshot", Private)!.Invoke(null, new object[] { local });
        var recovered = Exchange(offer, archive);
        Check(recovered.Ok && recovered.Chunks > 0, "corrupt local cache is replaced by verified server download");
        var matching = Exchange(offer, archive);
        Check(matching.Ok && matching.Chunks == 0, "matching installed package skips download");
        var uppercase = Exchange(offer with { ContentHash = offer.ContentHash.ToUpperInvariant(),
            ArchiveHash = offer.ArchiveHash.ToUpperInvariant() }, archive);
        Check(uppercase.Ok && uppercase.Chunks == 0, "hex hash spelling does not invalidate matching package");
        var version = Exchange(offer with { Version = "different" }, archive);
        Check(!version.Ok && version.Chunks > 0 && version.Error?.Contains("identity", StringComparison.OrdinalIgnoreCase) == true,
            "offered version is bound to the verified package");
        string malformedPath = Path.Combine(root, "malformed.fpmap");
        using (var zip = ZipFile.Open(malformedPath, ZipArchiveMode.Create))
        {
            using var writer = new StreamWriter(zip.CreateEntry("manifest.json").Open()); writer.Write("{");
        }
        byte[] malformed = File.ReadAllBytes(malformedPath);
        try
        {
            var rejected = Exchange(offer with { Name = "MALFORMED MAP", Size = malformed.Length,
                ArchiveHash = MapBuildFingerprint.HashFile(malformedPath) }, malformed);
            Check(!rejected.Ok && !string.IsNullOrWhiteSpace(rejected.Error), "malformed downloaded JSON fails join without escaping exception");
        }
        catch (JsonException) { Check(false, "malformed downloaded JSON escaped network failure boundary"); }
        var rotated = Exchange(new MapOffer("MP3 PROVING GROUND", Guid.Empty, null, "", "", 0), Array.Empty<byte>(), rotate: true);
        Check(!rotated.Ok && rotated.Error?.Contains("changed", StringComparison.OrdinalIgnoreCase) == true,
            "same-room match rotation cancels stale download/bootstrap");
        Check(!Directory.Exists(Path.Combine(CustomRooms.MapDirectory, ".downloads"))
            || !Directory.EnumerateFiles(Path.Combine(CustomRooms.MapDirectory, ".downloads")).Any(), "temporary downloads cleaned up");
    }
}
