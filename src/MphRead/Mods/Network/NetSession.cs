using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using MphRead.Entities;

namespace MphRead.Mods.Network
{
    public enum NetRole
    {
        Offline,
        Host,
        Client
    }

    internal sealed class RemotePeer
    {
        public IPEndPoint EndPoint = null!;
        public int SlotIndex = -1;
        public IntentPacket LatestIntent;
        public uint LastIntentFrame;
        public double LastSeenTime;
    }

    /// <summary>
    /// Session state and per-frame network step.
    ///
    /// Host authority, not lockstep: the simulation runs in float (Fixed
    /// only converts the ROM's 20.12 values on load), so two machines
    /// stepping the same inputs are not guaranteed to stay bit-identical.
    /// The host therefore owns the simulation and clients apply what it
    /// sends. Divergence becomes a correction rather than a desync.
    /// </summary>
    public static class NetSession
    {
        private static NetTransport? _transport;
        private static readonly List<RemotePeer> _peers = new();
        private static IPEndPoint? _hostEndPoint;
        private static readonly byte[] _scratch = new byte[NetConfig.MaxPacketSize];

        public static NetRole Role { get; private set; } = NetRole.Offline;
        public static bool Active => Role != NetRole.Offline;
        public static bool IsHost => Role == NetRole.Host;
        public static bool IsClient => Role == NetRole.Client;
        public static int LocalSlot { get; private set; } = 0;
        public static uint NetFrame { get; private set; }
        public static uint LastSnapshotFrame => _lastSnapshotFrame;
        public static string? LastError { get; private set; }

        /// <summary>Latest authoritative state per slot, applied by clients.</summary>
        public static readonly PlayerState[] RemoteStates = new PlayerState[PlayerEntity.SlotCapacity];
        public static readonly bool[] RemoteStateValid = new bool[PlayerEntity.SlotCapacity];

        /// <summary>Latest intent per slot, consumed by the host's input step.</summary>
        public static readonly IntentPacket[] RemoteIntents = new IntentPacket[PlayerEntity.SlotCapacity];
        public static readonly bool[] RemoteIntentValid = new bool[PlayerEntity.SlotCapacity];

        /// <summary>
        /// Counters the log reads to tell "nothing arrived" from "it arrived
        /// and was ignored". Two clients that are demonstrably exchanging
        /// packets can still each hold a scene containing only themselves,
        /// and only the difference between these two numbers says which half
        /// of the path is at fault.
        /// </summary>
        public static long SnapshotsReceived { get; private set; }
        public static long SnapshotsSent { get; private set; }
        public static long StatesApplied { get; private set; }
        public static long IntentsReceived { get; private set; }

        public static void NoteStatesApplied() => StatesApplied++;

        public static void StartHost(int port = NetConfig.DefaultPort)
        {
            Stop();
            try
            {
                _transport = new NetTransport(port);
                Role = NetRole.Host;
                LocalSlot = 0;
                NetFrame = 0;
                LastError = null;
                Console.WriteLine($"[net] hosting on UDP {_transport.LocalPort}");
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                Console.WriteLine($"[net] host failed: {ex.Message}");
                Role = NetRole.Offline;
            }
        }

        public static void StartClient(string address, int port = NetConfig.DefaultPort)
        {
            Stop();
            try
            {
                _transport = new NetTransport(0);
                // Resolve rather than Parse: IPAddress.Parse only accepts a
                // literal, so a hostname threw here and the join silently
                // failed -- the session stayed offline while the launcher
                // reported nothing wrong.
                _hostEndPoint = new IPEndPoint(ResolveIPv4(address), port);
                Role = NetRole.Client;
                LocalSlot = -1; // assigned by the host's Welcome
                NetFrame = 0;
                LastError = null;
                NetLog.Open(PlayerName);
                NetLog.Event($"joining {address}:{port} as \"{PlayerName}\"");
                SendHello();
                SendIdentify();
                Console.WriteLine($"[net] joining {address}:{port} as \"{PlayerName}\"");
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                Console.WriteLine($"[net] join failed: {ex.Message}");
                Role = NetRole.Offline;
            }
        }

        /// <summary>
        /// Turn a hostname or literal address into an IPv4 endpoint address.
        /// IPv4 specifically: the transport binds an InterNetwork socket, so
        /// handing it a v6 address would fail at send time instead of here.
        /// </summary>
        private static IPAddress ResolveIPv4(string address)
        {
            if (IPAddress.TryParse(address, out IPAddress? literal)
                && literal.AddressFamily == AddressFamily.InterNetwork)
            {
                return literal;
            }
            IPAddress[] resolved = Dns.GetHostAddresses(address);
            foreach (IPAddress candidate in resolved)
            {
                if (candidate.AddressFamily == AddressFamily.InterNetwork)
                {
                    return candidate;
                }
            }
            throw new InvalidOperationException($"{address} has no IPv4 address");
        }

        public static void Stop()
        {
            NetPlayerSetup.Reset();
            SpectatorMode.Reset();
            NetMatchSync.Reset();
            NetSlotManager.Reset();
            NetDamage.Reset();
            NetRoomChange.Reset();
            NetMatchEnd.Reset();
            NetPlayerBridge.Reset();
            IsAuthority = false;
            _authorityNeedsStateApply = false;
            if (_transport != null)
            {
                if (Role == NetRole.Client && _hostEndPoint != null)
                {
                    _transport.Send(_hostEndPoint, PacketType.Bye, ReadOnlySpan<byte>.Empty);
                }
                _transport.Dispose();
                _transport = null;
            }
            _peers.Clear();
            _hostEndPoint = null;
            Role = NetRole.Offline;
            LocalSlot = 0;
            Array.Clear(RemoteStateValid);
            Array.Clear(RemoteIntentValid);
            Array.Clear(SlotPing);
            Array.Clear(_lastSlotIntentFrame);
            _lastServerPacket = 0;
            Array.Clear(SlotOccupied);
            SnapshotsReceived = 0;
            SnapshotsSent = 0;
            SnapshotsOutOfOrder = 0;
            _lastSnapshotFrame = 0;
            StatesApplied = 0;
            IntentsReceived = 0;
            ServerMatch = null;
        }

        /// <summary>
        /// Tell the server who we are: display name and hunter. The hunter
        /// leads so the name stays a plain trailing string, which is what the
        /// server reads it as.
        /// </summary>
        public static void SendIdentify()
        {
            if (_transport == null || _hostEndPoint == null)
            {
                return;
            }
            byte[] name = System.Text.Encoding.ASCII.GetBytes(PlayerName);
            int count = Math.Min(name.Length, RosterPacket.MaxNameBytes);
            _scratch[0] = (byte)LocalHunter;
            name.AsSpan(0, count).CopyTo(_scratch.AsSpan(1));
            _transport.Send(_hostEndPoint, PacketType.Identify, _scratch.AsSpan(0, count + 1));
        }

        /// <summary>The hunter this machine plays, announced in Identify.</summary>
        public static Hunter LocalHunter { get; set; } = Hunter.Samus;

        /// <summary>
        /// Which hunter each slot is playing, per the server's roster. A
        /// client that assumed its own choice for everybody drew the other
        /// player as the wrong character -- correct position, correct name,
        /// wrong model.
        /// </summary>
        public static readonly Hunter[] SlotHunter = new Hunter[PlayerEntity.SlotCapacity];

        /// <summary>
        /// Round trip to the server per slot, in milliseconds, as the server
        /// measured it. Zero means "not measured yet", which the scoreboard
        /// draws as a dash rather than as a suspiciously perfect connection.
        ///
        /// Measured by the server rather than by each client because clients
        /// never exchange packets with each other: a client can time its own
        /// round trip and nobody else's, and a scoreboard that showed one real
        /// number and five zeroes would be worse than none.
        /// </summary>
        public static readonly int[] SlotPing = new int[PlayerEntity.SlotCapacity];

        private static void SendHello()
        {
            if (_transport == null || _hostEndPoint == null)
            {
                return;
            }
            _scratch[0] = NetConfig.ProtocolVersion;
            // Ask for the slot we already hold. A reconnection is normally a
            // client the server forgot while it was loading, and coming back
            // as a different player would swap two people's scores, names and
            // hunters mid-match.
            _scratch[1] = LocalSlot >= 0 && LocalSlot < 0xFF ? (byte)LocalSlot : (byte)0xFF;
            _transport.Send(_hostEndPoint, PacketType.Hello, _scratch.AsSpan(0, 2));
        }

        /// <summary>
        /// Pump the network once per simulation frame. Call before input is
        /// sampled so a remote intent that arrived this frame is visible to
        /// the input step that follows.
        /// </summary>
        public static void Update(double time)
        {
            if (_transport == null)
            {
                return;
            }
            NetFrame++;
            foreach (ReceivedPacket packet in _transport.Drain())
            {
                Handle(packet, time);
            }
            if (Role == NetRole.Host)
            {
                DropTimedOutPeers(time);
            }
            else if (Role == NetRole.Client && LocalSlot < 0 && NetFrame % 60 == 0)
            {
                SendHello(); // still waiting to be admitted
            }
            else if (Role == NetRole.Client && NetFrame % 60 == 0
                && time - _lastServerPacket > SilenceBeforeRejoin)
            {
                // The server has not said anything for a long time, which
                // means it has forgotten us -- dropped while a room was
                // loading, or restarted. Saying hello again re-registers this
                // endpoint, and since the slot we held is free by then, we
                // normally get it straight back. Without this a client that
                // was dropped once kept playing alone forever, sending
                // packets to a server that ignored every one of them.
                Console.WriteLine("[net] no word from the server; re-announcing");
                NetLog.Event("server silent, re-announcing");
                SendHello();
                SendIdentify();
            }
            else if (Role == NetRole.Client && NetFrame % 120 == 0
                && LocalSlot >= 0 && LocalSlot < GameState.Nicknames.Length
                && GameState.Nicknames[LocalSlot] != PlayerName)
            {
                // The roster still has a placeholder for this slot, so the
                // Identify that went out with the join was lost. Nothing else
                // resends it, and a client whose name never landed shows up as
                // "PlayerN" on every other scoreboard for the whole match.
                SendIdentify();
            }
        }

        /// <summary>Seconds of silence from the server before saying hello again.</summary>
        private const double SilenceBeforeRejoin = 5.0;

        private static double _lastServerPacket;

        private static void Handle(ReceivedPacket packet, double time)
        {
            if (Role == NetRole.Client)
            {
                _lastServerPacket = time;
            }
            switch (packet.Type)
            {
                case PacketType.Hello when Role == NetRole.Host:
                    HandleHello(packet, time);
                    break;
                case PacketType.Welcome when Role == NetRole.Client:
                    if (packet.Payload.Length >= 1)
                    {
                        LocalSlot = packet.Payload[0];
                        Console.WriteLine($"[net] joined as slot {LocalSlot}");
                        NetLog.Event($"server assigned slot {LocalSlot}");
                    }
                    break;
                case PacketType.Intent when Role == NetRole.Host:
                    HandleIntent(packet, time);
                    break;
                case PacketType.SlotIntent when Role == NetRole.Client:
                    HandleSlotIntent(packet);
                    break;
                case PacketType.Snapshot when Role == NetRole.Client:
                    HandleSnapshot(packet);
                    break;
                case PacketType.Authority when Role == NetRole.Client:
                    if (!IsAuthority)
                    {
                        IsAuthority = true;
                        _authorityNeedsStateApply = true;
                        Console.WriteLine("[net] this client is now the simulation authority");
                        NetLog.Event("became the simulation authority");
                    }
                    break;
                case PacketType.Roster when Role == NetRole.Client:
                    HandleRoster(packet);
                    break;
                case PacketType.Ping when Role == NetRole.Client:
                    // Echo the payload: it carries the id the server uses to
                    // tell this reply from the one before it.
                    if (_hostEndPoint != null)
                    {
                        _transport?.Send(_hostEndPoint, PacketType.Pong, packet.Payload);
                    }
                    break;
                case PacketType.MatchState when Role == NetRole.Client:
                case PacketType.MapChange when Role == NetRole.Client:
                    HandleMatchState(packet, packet.Type == PacketType.MapChange);
                    break;
                case PacketType.Bye:
                    HandleBye(packet);
                    break;
            }
        }

        private static void HandleHello(ReceivedPacket packet, double time)
        {
            if (packet.Payload.Length < 1 || packet.Payload[0] != NetConfig.ProtocolVersion)
            {
                return;
            }
            RemotePeer? peer = FindPeer(packet.Sender);
            if (peer == null)
            {
                int slot = NextFreeSlot();
                if (slot < 0)
                {
                    return; // session full
                }
                peer = new RemotePeer
                {
                    EndPoint = packet.Sender,
                    SlotIndex = slot
                };
                _peers.Add(peer);
                Console.WriteLine($"[net] peer {packet.Sender} -> slot {slot}");
            }
            peer.LastSeenTime = time;
            // Re-answered on every Hello: the first Welcome may have been lost.
            _scratch[0] = (byte)peer.SlotIndex;
            _transport!.Send(peer.EndPoint, PacketType.Welcome, _scratch.AsSpan(0, 1));
        }

        private static void HandleIntent(ReceivedPacket packet, double time)
        {
            if (packet.Payload.Length < IntentPacket.Size)
            {
                return;
            }
            RemotePeer? peer = FindPeer(packet.Sender);
            if (peer == null || peer.SlotIndex < 0)
            {
                return;
            }
            IntentPacket intent = IntentPacket.Read(packet.Payload);
            // UDP reorders; an older frame must not overwrite a newer one.
            if (peer.LastIntentFrame != 0 && intent.Frame <= peer.LastIntentFrame)
            {
                return;
            }
            peer.LastIntentFrame = intent.Frame;
            peer.LatestIntent = intent;
            peer.LastSeenTime = time;
            RemoteIntents[peer.SlotIndex] = intent;
            RemoteIntentValid[peer.SlotIndex] = true;
        }

        /// <summary>
        /// A peer's input, relayed by the server and tagged with its slot.
        ///
        /// Every client gets these, not only the authority. The authority
        /// needs them to simulate the match; everyone else needs them because
        /// a player's input is what makes it do anything a position cannot
        /// express -- firing, morphing, laying a bomb, an alt attack. Clients
        /// that had only positions drew opponents gliding around in silence.
        /// The authority still owns where everyone ends up: its snapshot
        /// corrects whatever the local simulation of that input produced.
        /// </summary>
        private static void HandleSlotIntent(ReceivedPacket packet)
        {
            if (packet.Payload.Length < 1 + IntentPacket.Size)
            {
                return;
            }
            int slot = packet.Payload[0];
            if (slot < 0 || slot >= RemoteIntents.Length || slot == LocalSlot)
            {
                return;
            }
            IntentPacket intent = IntentPacket.Read(packet.Payload[1..]);
            // UDP reorders; an older frame must not overwrite a newer one.
            if (_lastSlotIntentFrame[slot] != 0 && intent.Frame <= _lastSlotIntentFrame[slot])
            {
                return;
            }
            _lastSlotIntentFrame[slot] = intent.Frame;
            RemoteIntents[slot] = intent;
            RemoteIntentValid[slot] = true;
            IntentsReceived++;
        }

        private static readonly uint[] _lastSlotIntentFrame = new uint[PlayerEntity.SlotCapacity];

        /// <summary>
        /// The running match, as last reported by a dedicated server. Null
        /// when hosting or offline, where there is no server clock to follow.
        /// </summary>
        public static MatchStatePacket? ServerMatch { get; private set; }

        /// <summary>
        /// True when a dedicated server has designated this client as the
        /// simulation authority. On a dedicated server every peer is
        /// NetRole.Client, so without this nothing would ever broadcast
        /// snapshots and no player would see another move.
        /// </summary>
        public static bool IsAuthority { get; private set; }
        private static bool _authorityNeedsStateApply;

        public static bool ConsumeAuthorityStateSync()
        {
            if (!_authorityNeedsStateApply)
            {
                return false;
            }
            _authorityNeedsStateApply = false;
            return true;
        }

        /// <summary>How many peers the server last reported, including us.</summary>
        public static int ServerPlayerCount => ServerMatch?.PlayerCount ?? 0;

        /// <summary>Display name sent to the server on join.</summary>
        public static string PlayerName { get; set; } = "Player";

        /// <summary>Raised when the server rotates to a different map.</summary>
        public static event Action<MatchStatePacket>? MapChanged;

        /// <summary>Slots currently occupied by a real peer, per the server.</summary>
        public static readonly bool[] SlotOccupied = new bool[PlayerEntity.SlotCapacity];

        private static void HandleRoster(ReceivedPacket packet)
        {
            if (packet.Payload.Length < RosterPacket.Size)
            {
                return;
            }
            RosterPacket roster = RosterPacket.Read(packet.Payload);
            Array.Clear(SlotOccupied);
            for (int i = 0; i < roster.Count; i++)
            {
                int slot = roster.Slots[i];
                if (slot < 0 || slot >= SlotOccupied.Length)
                {
                    continue;
                }
                SlotOccupied[slot] = true;
                // Nicknames is what the scoreboard draws, so writing here is
                // what makes the other player's name appear on Tab.
                GameState.Nicknames[slot] = roster.Names[i];
                if (Enum.IsDefined(typeof(Hunter), roster.Hunters[i]))
                {
                    SlotHunter[slot] = (Hunter)roster.Hunters[i];
                }
                SlotPing[slot] = roster.Pings[i];
            }
        }

        private static void HandleMatchState(ReceivedPacket packet, bool rotated)
        {
            if (packet.Payload.Length < MatchStatePacket.Size)
            {
                return;
            }
            MatchStatePacket state = MatchStatePacket.Read(packet.Payload);
            string? previous = ServerMatch?.RoomKey;
            ServerMatch = state;
            // Fire on an actual map change, whether the server announced it
            // as a rotation or the periodic state simply differs -- a joiner
            // arriving mid-match learns the map this same way.
            if (rotated || previous == null || previous != state.RoomKey)
            {
                Console.WriteLine($"[net] server map: {state.RoomKey} "
                    + $"({(GameMode)state.Mode}, {state.TimeRemaining:0} s left)");
                MapChanged?.Invoke(state);
            }
        }

        /// <summary>
        /// The newest snapshot frame this client has accepted. Snapshots are
        /// the one stream that was not ordered.
        /// </summary>
        private static uint _lastSnapshotFrame;

        /// <summary>
        /// How far behind the newest snapshot a packet may be and still be
        /// treated as a reordered straggler rather than a fresh start. Ten
        /// seconds at sixty frames: a client that has been away longer than
        /// that has been away long enough for the authority to have changed
        /// or the room to have reloaded.
        /// </summary>
        private const uint SnapshotResetGap = 600;

        /// <summary>Reordered snapshots thrown away, for the report.</summary>
        public static long SnapshotsOutOfOrder { get; private set; }

        private static void HandleSnapshot(ReceivedPacket packet)
        {
            ReadOnlySpan<byte> payload = packet.Payload;
            if (payload.Length < SnapshotHeader.Size)
            {
                return;
            }
            SnapshotHeader header = SnapshotHeader.Read(payload);
            // Both the intent streams already refuse an older frame; this one
            // did not, and it is the stream that carries health, score and the
            // damage counter. A datagram overtaken in flight therefore put a
            // player back where they had been, undid a kill on the scoreboard,
            // and -- worst of it -- ran the damage counter backwards, which the
            // replay reads as two hundred and fifty-odd new hits because the
            // counter is a byte. UDP reorders as a matter of course; a
            // snapshot arrives sixty times a second, so throwing away a late
            // one costs nothing at all.
            if (_lastSnapshotFrame != 0 && header.Frame <= _lastSnapshotFrame
                && _lastSnapshotFrame - header.Frame < SnapshotResetGap)
            {
                SnapshotsOutOfOrder++;
                return;
            }
            _lastSnapshotFrame = header.Frame;
            SnapshotsReceived++;
            // Rng.cs reproduces the game's original LCG and its state is
            // global, so adopting the host's words keeps every random
            // consumer agreeing without replicating each one individually.
            Rng.SetRng1(header.Rng1);
            Rng.SetRng2(header.Rng2);
            int offset = SnapshotHeader.Size;
            Array.Clear(RemoteStateValid);
            for (int i = 0; i < header.PlayerCount; i++)
            {
                if (offset + PlayerState.Size > payload.Length)
                {
                    break;
                }
                PlayerState state = PlayerState.Read(payload[offset..]);
                offset += PlayerState.Size;
                if (state.SlotIndex < RemoteStates.Length)
                {
                    RemoteStates[state.SlotIndex] = state;
                    RemoteStateValid[state.SlotIndex] = true;
                }
            }
        }

        private static void HandleBye(ReceivedPacket packet)
        {
            if (Role == NetRole.Host)
            {
                RemotePeer? peer = FindPeer(packet.Sender);
                if (peer != null)
                {
                    Console.WriteLine($"[net] peer {peer.EndPoint} left (slot {peer.SlotIndex})");
                    RemoteIntentValid[peer.SlotIndex] = false;
                    _peers.Remove(peer);
                }
            }
            else
            {
                Console.WriteLine("[net] host closed the session");
                Stop();
            }
        }

        private static void DropTimedOutPeers(double time)
        {
            for (int i = _peers.Count - 1; i >= 0; i--)
            {
                RemotePeer peer = _peers[i];
                if (time - peer.LastSeenTime > NetConfig.TimeoutSeconds)
                {
                    Console.WriteLine($"[net] peer {peer.EndPoint} timed out (slot {peer.SlotIndex})");
                    RemoteIntentValid[peer.SlotIndex] = false;
                    _peers.RemoveAt(i);
                }
            }
        }

        private static RemotePeer? FindPeer(IPEndPoint endPoint)
        {
            for (int i = 0; i < _peers.Count; i++)
            {
                if (_peers[i].EndPoint.Equals(endPoint))
                {
                    return _peers[i];
                }
            }
            return null;
        }

        private static int NextFreeSlot()
        {
            for (int slot = 1; slot < PlayerEntity.MaxPlayers; slot++)
            {
                bool taken = false;
                for (int i = 0; i < _peers.Count; i++)
                {
                    if (_peers[i].SlotIndex == slot)
                    {
                        taken = true;
                        break;
                    }
                }
                if (!taken)
                {
                    return slot;
                }
            }
            return -1;
        }

        /// <summary>Client -> host: this frame's intent for the local player.</summary>
        public static void SendIntent(IntentPacket intent)
        {
            if (_transport == null || Role != NetRole.Client || _hostEndPoint == null)
            {
                return;
            }
            intent.Frame = NetFrame;
            intent.Write(_scratch);
            _transport.Send(_hostEndPoint, PacketType.Intent, _scratch.AsSpan(0, IntentPacket.Size));
        }

        /// <summary>
        /// Authority -> server: the match this client is simulating is over.
        ///
        /// The server keeps the rotation but has no scoreboard, so a match
        /// won on points ends on the authority's machine and nowhere else.
        /// Sent repeatedly by NetMatchEnd until the server's state comes back
        /// saying it heard.
        /// </summary>
        public static void SendMatchEnd()
        {
            if (_transport == null || Role != NetRole.Client || _hostEndPoint == null)
            {
                return;
            }
            _transport.Send(_hostEndPoint, PacketType.MatchEnd, ReadOnlySpan<byte>.Empty);
        }

        /// <summary>Host -> clients: authoritative state for every active player.</summary>
        public static void BroadcastSnapshot()
        {
            if (_transport == null)
            {
                return;
            }
            bool asHost = Role == NetRole.Host && _peers.Count > 0;
            bool asAuthority = Role == NetRole.Client && IsAuthority && _hostEndPoint != null;
            if (!asHost && !asAuthority)
            {
                return;
            }
            int count = 0;
            int offset = SnapshotHeader.Size;
            for (int i = 0; i < PlayerEntity.Players.Count; i++)
            {
                PlayerEntity player = PlayerEntity.Players[i];
                if (!player.LoadFlags.TestFlag(LoadFlags.Active))
                {
                    continue;
                }
                if (offset + PlayerState.Size > NetConfig.MaxPacketSize - 1)
                {
                    break;
                }
                if (!Single.IsFinite(player.Position.X) || !Single.IsFinite(player.Position.Y)
                    || !Single.IsFinite(player.Position.Z))
                {
                    // Publishing this would hand the corruption to everyone
                    // else, and they would hand it back as an authoritative
                    // correction. Skip the slot until it makes sense again.
                    NetLog.Event($"slot {i} not published: position is {player.Position}");
                    continue;
                }
                var state = new PlayerState
                {
                    SlotIndex = (byte)i,
                    Flags = (byte)(PlayerState.FlagActive
                        | (player.IsAltForm ? PlayerState.FlagAltForm : 0)
                        | (player.ModIsInPlay ? PlayerState.FlagSpawned : 0)
                        | (player.EquipInfo.Zoomed ? PlayerState.FlagZoomed : 0)),
                    Position = player.Position,
                    Speed = player.Speed,
                    Facing = player.FacingVector,
                    Health = (ushort)Math.Clamp(player.Health, 0, ushort.MaxValue),
                    CurrentWeapon = (byte)player.CurrentWeapon,
                    Team = (byte)player.Team
                };
                state.Points = (short)Math.Clamp(GameState.Points[i], Int16.MinValue, Int16.MaxValue);
                state.Kills = (ushort)Math.Clamp(GameState.Kills[i], 0, UInt16.MaxValue);
                state.Deaths = (ushort)Math.Clamp(GameState.Deaths[i], 0, UInt16.MaxValue);
                NetDamage.Write(i, ref state);
                state.Write(_scratch.AsSpan(offset));
                offset += PlayerState.Size;
                count++;
            }
            var header = new SnapshotHeader
            {
                Frame = NetFrame,
                Rng1 = Rng.Rng1,
                Rng2 = Rng.Rng2,
                PlayerCount = (byte)count
            };
            header.Write(_scratch);
            SnapshotsSent++;
            if (asAuthority)
            {
                // One send to the server, which relays to every other peer.
                _transport.Send(_hostEndPoint!, PacketType.Snapshot, _scratch.AsSpan(0, offset));
                return;
            }
            for (int i = 0; i < _peers.Count; i++)
            {
                _transport.Send(_peers[i].EndPoint, PacketType.Snapshot, _scratch.AsSpan(0, offset));
            }
        }
    }
}
