using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Text.Json;
using System.Threading;
using MphRead.Mods.MapGen;

namespace MphRead.Mods.Network
{
    public sealed record MapOffer(string Name,Guid MapId,string? Version,string ContentHash,string ArchiveHash,long Size,string? Error=null);

    // Packages are cooked once before listening, never on the simulation tick.
    // Requests carry a hash and an offset, not a filename supplied by a peer.
    public sealed class MapTransferServer
    {
        public const int ChunkSize=960;
        private readonly Dictionary<string,(MapOffer Offer,byte[] Bytes)> _maps=new(StringComparer.OrdinalIgnoreCase);
        public void Prepare(IEnumerable<MapDefinition>? definitions=null)
        {
            _maps.Clear();
            long total=0;
            foreach(var definition in definitions??CustomRooms.Definitions)
            {
                try
                {
                    string directory=Path.Combine(CustomRooms.MapDirectory,".transfer");Directory.CreateDirectory(directory);
                    string path=MapPackageBuilder.Build(definition,Path.Combine(directory,MapBuildFingerprint.HashText(definition.Name)+".fpmap"));
                    var bytes=File.ReadAllBytes(path);
                    if(total+bytes.Length>MapPackageReader.MaxExpandedBytes)throw new InvalidDataException("Server map transfer cache is full.");
                    total+=bytes.Length;
                    using var package=new MapPackageReader(path);var manifest=package.Manifest!;
                    _maps.Add(definition.Name,(new(definition.Name,manifest.MapId,manifest.MapVersion,manifest.ContentHash,MapBuildFingerprint.HashFile(path),bytes.Length),bytes));
                }
                catch(Exception ex)when(ex is IOException or InvalidDataException or ProgramException or UnauthorizedAccessException or JsonException or ArgumentException)
                {
                    // JSON can escape each UTF-16 code unit as six bytes. Keep
                    // even localized/path errors inside the packet payload.
                    _maps[definition.Name]=(new(definition.Name,Guid.Empty,null,"","",0,ex.Message.Length>100?ex.Message[..100]:ex.Message),Array.Empty<byte>());
                }
            }
        }
        public void Handle(ReadOnlySpan<byte> request,string currentRoom,Action<PacketType,byte[]> send)
        {
            if(request.Length==1&&request[0]==0)
            {
                var offer=_maps.TryGetValue(currentRoom,out var map)?map.Offer:new MapOffer(currentRoom,Guid.Empty,null,"","",0);
                send(PacketType.MapOffer,JsonSerializer.SerializeToUtf8Bytes(offer));return;
            }
            if(request.Length!=41||request[0]!=1||!_maps.TryGetValue(currentRoom,out var current)||current.Offer.Error!=null)return;
            if(!request.Slice(1,32).SequenceEqual(Convert.FromHexString(current.Offer.ArchiveHash)))return;
            long offset=BinaryPrimitives.ReadInt64LittleEndian(request[33..]);
            if(offset<0||offset>=current.Bytes.Length||offset%ChunkSize!=0)return;
            int length=Math.Min(ChunkSize,current.Bytes.Length-(int)offset);var response=new byte[40+length];
            request.Slice(1,32).CopyTo(response);BinaryPrimitives.WriteInt64LittleEndian(response.AsSpan(32),offset);
            current.Bytes.AsSpan((int)offset,length).CopyTo(response.AsSpan(40));send(PacketType.MapChunk,response);
        }
    }

    public static class NetMapTransfer
    {
        private static MapOffer? _offer;
        private static FileStream? _download;
        private static string _wanted="";
        private static string? _error;
        private static long _received;
        private static long _written;
        private static bool[] _chunks=Array.Empty<bool>();
        private static long[] _requestedAt=Array.Empty<long>();
        private const int WindowSize=16;
        private static readonly Dictionary<string,string> _verified=new(StringComparer.OrdinalIgnoreCase);
        public static string? LastError { get; private set; }
        public static string LibraryDirectory=>Path.Combine(CustomRooms.MapDirectory,".installed");

        // Called only by NetSession after checking the selected server endpoint.
        internal static void Receive(PacketType type,ReadOnlySpan<byte> payload)
        {
            if(_wanted.Length==0)return;
            if(type==PacketType.MapOffer&&payload.Length<=1023)
            {
                try
                {
                    var offer=JsonSerializer.Deserialize<MapOffer>(payload);
                    if(offer==null||offer.Name!=_wanted)return;
                    if(offer.Version?.Length>64||offer.Error?.Length>200){_error="Oversized map offer metadata.";return;}
                    if(offer.Error!=null){_error=offer.Error;return;}
                    if(offer.Size<0||offer.Size>MapPackageReader.MaxArchiveBytes||offer.Size>0&&(offer.MapId==Guid.Empty
                        ||offer.ContentHash?.Length!=64||offer.ArchiveHash?.Length!=64
                        ||!offer.ContentHash.All(Uri.IsHexDigit)||!offer.ArchiveHash.All(Uri.IsHexDigit)))
                    {_error="Server advertised an invalid map package.";return;}
                    _offer??=offer with { ContentHash=offer.ContentHash?.ToLowerInvariant()??"",
                        ArchiveHash=offer.ArchiveHash?.ToLowerInvariant()??"" };
                }
                catch(JsonException){_error="Malformed map offer.";}
            }
            if(type==PacketType.MapChunk&&_offer!=null&&_download!=null&&payload.Length>=40)
            {
                if(!payload[..32].SequenceEqual(Convert.FromHexString(_offer.ArchiveHash)))return;
                long offset=BinaryPrimitives.ReadInt64LittleEndian(payload[32..]);
                if(offset<0||offset>=_offer.Size||offset%MapTransferServer.ChunkSize!=0)return;
                int index=(int)(offset/MapTransferServer.ChunkSize);
                int expected=(int)Math.Min(MapTransferServer.ChunkSize,_offer.Size-offset);
                if(offset<_received||offset>=_received+WindowSize*MapTransferServer.ChunkSize||payload.Length!=40+expected||_chunks[index])return;
                _download.Position=offset;_download.Write(payload[40..]);_chunks[index]=true;_written+=expected;
                while(_received<_offer.Size&&_chunks[(int)(_received/MapTransferServer.ChunkSize)])
                    _received+=Math.Min(MapTransferServer.ChunkSize,_offer.Size-_received);
            }
        }

        private static bool MatchesOffer(MapPackageManifest? manifest,MapOffer offer)
            => manifest!=null&&manifest.MapId==offer.MapId&&manifest.Name==offer.Name
                &&manifest.MapVersion==offer.Version
                &&string.Equals(manifest.ContentHash,offer.ContentHash,StringComparison.OrdinalIgnoreCase);

        private static bool HasMatchingLocalPackage(string path,MapOffer offer)
        {
            // A damaged/deleted local cache is a miss, not a reason to refuse a
            // replacement that will go through the full download validation.
            try { using var package=new MapPackageReader(path);return MatchesOffer(package.Manifest,offer); }
            catch(Exception ex)when(ex is IOException or InvalidDataException or ProgramException
                or UnauthorizedAccessException or JsonException or ArgumentException) { return false; }
        }

        public static bool Ensure(string room,bool force=false)
        {
            LastError=null;
            if(!NetSession.IsClient||DemoPlayback.IsActive)return true;
            if(!force&&_verified.ContainsKey(room))return true;
            ushort? matchId=NetSession.ServerMatch?.MatchId;
            _wanted=room;_offer=null;_error=null;_received=0;_written=0;
            string directory=Path.Combine(CustomRooms.MapDirectory,".downloads"),temporary="";
            var clock=Stopwatch.StartNew();long lastRequest=-1000,lastReceived=0,progressAt=0;
            try
            {
                while(clock.Elapsed<TimeSpan.FromMinutes(3))
                {
                    NetSession.PumpMapTransfer();
                    if(_error!=null)throw new InvalidDataException(_error);
                    if(!NetSession.IsClient||NetSession.ServerMatch?.RoomKey!=room||NetSession.ServerMatch?.MatchId!=matchId)
                        throw new IOException("Server changed matches during download; join again.");
                    if(_offer is {} offer)
                    {
                        if(offer.Size==0)
                        {
                            if(offer.MapId!=Guid.Empty||CustomRooms.Definitions.Any(d=>d.Name.Equals(room,StringComparison.OrdinalIgnoreCase))||Metadata.GetRoomByName(room).Item1==null)
                                throw new InvalidDataException("Server did not provide an identity for this custom map.");
                            _verified[room]="builtin";return true;
                        }
                        if(_download==null)
                        {
                            if(Metadata.IsBuiltInRoom(room))throw new InvalidDataException("A downloaded map cannot replace a built-in room.");
                            var local=CustomRooms.Definitions.FirstOrDefault(d=>d.Name.Equals(room,StringComparison.OrdinalIgnoreCase));
                            if(local?.BundlePath is {} bundle&&HasMatchingLocalPackage(bundle,offer))
                            {
                                _verified[room]=offer.ContentHash;return true;
                            }
                            Directory.CreateDirectory(directory);temporary=Path.Combine(directory,Guid.NewGuid().ToString("N")+".fpmap");
                            _download=new FileStream(temporary,FileMode.CreateNew,FileAccess.Write,FileShare.None);
                            int count=(int)((offer.Size+MapTransferServer.ChunkSize-1)/MapTransferServer.ChunkSize);
                            _chunks=new bool[count];_requestedAt=Enumerable.Repeat(-1000L,count).ToArray();
                            Console.WriteLine($"[mappackage] downloading {room} ({offer.Size:N0} bytes)");lastRequest=-1000;
                        }
                        if(_received==offer.Size)
                        {
                            _download.Dispose();_download=null;
                            // Hash/ID agreement alone does not bind the package to
                            // the room being loaded. Reject a renamed offer before
                            // installing any package or publishing runtime outputs.
                            using(var package=new MapPackageReader(temporary))
                                if(!MatchesOffer(package.Manifest,offer))
                                    throw new InvalidDataException("Downloaded map identity does not match the server offer.");
                            Launcher.GameFiles.ApplyPaths();
                            var installed=MapPackageInstaller.Install(temporary,offer.MapId,offer.ContentHash,offer.ArchiveHash,LibraryDirectory);
                            Metadata.RegisterDownloadedMap(installed);
                            _verified[room]=offer.ContentHash;return true;
                        }
                    }
                    if(_written!=lastReceived){lastReceived=_written;progressAt=clock.ElapsedMilliseconds;}
                    if(clock.ElapsedMilliseconds-progressAt>15000)throw new IOException("Map transfer stalled.");
                    if(_offer==null&&clock.ElapsedMilliseconds-lastRequest>=200)
                    {
                        NetSession.SendMapRequest(new byte[1]);lastRequest=clock.ElapsedMilliseconds;
                    }
                    else if(_offer!=null&&_download!=null)
                    {
                        int first=(int)(_received/MapTransferServer.ChunkSize);
                        for(int index=first;index<Math.Min(_chunks.Length,first+WindowSize);index++)
                        {
                            if(_chunks[index]||clock.ElapsedMilliseconds-_requestedAt[index]<200)continue;
                            var request=new byte[41];request[0]=1;Convert.FromHexString(_offer.ArchiveHash).CopyTo(request,1);
                            BinaryPrimitives.WriteInt64LittleEndian(request.AsSpan(33),(long)index*MapTransferServer.ChunkSize);
                            NetSession.SendMapRequest(request);_requestedAt[index]=clock.ElapsedMilliseconds;
                        }
                    }
                    Thread.Sleep(2);
                }
                throw new IOException("Map download timed out.");
            }
            catch(Exception ex)when(ex is IOException or InvalidDataException or ProgramException or UnauthorizedAccessException
                or JsonException or ArgumentException)
            {LastError=ex.Message;Console.WriteLine("[mappackage] "+ex.Message);return false;}
            finally{_wanted="";_download?.Dispose();_download=null;if(temporary.Length>0&&File.Exists(temporary))File.Delete(temporary);}
        }
        public static void Reset(){_verified.Clear();}
    }
}
