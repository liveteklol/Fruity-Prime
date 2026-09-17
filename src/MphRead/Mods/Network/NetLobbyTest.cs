using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Threading;

namespace MphRead.Mods.Network
{
    /// <summary>Asset-free, real UDP control-plane regression. It does not substitute for rendered match acceptance.</summary>
    public static class NetLobbyTest
    {
        private static int _checks;
        private static void Check(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
            _checks++;
        }

        public static int Run()
        {
            try
            {
                ProtocolChecks();
                EmptyContinuousRestart();
                ClientStateChecks();
                Scenario();
                TeamScenario();
                ContinuousScenario();
                ClientSessionScenario();
                Console.WriteLine($"[netlobbytest] PASS: {_checks} assertions; protocol, UDP lifecycle, two rounds, owner migration, teams, rebind and continuous rotation.");
                return 0;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[netlobbytest] FAIL after {_checks} assertions: {ex}");
                return 1;
            }
            finally { NetSession.Stop(); NetLag.Configure("0"); NetLag.ConfigureLoss("0"); }
        }

        private static void ProtocolChecks()
        {
            Check(NetConfig.ProtocolVersion == 8 && (byte)PacketType.SessionState == 36
                && (byte)PacketType.MapDone == 35, "protocol and reserved IDs");
            var state = new SessionStatePacket { Phase = SessionPhase.Starting, Policy = ServerSessionPolicy.Lobby,
                OwnerSlot = 7, MaxPlayers = 8, Revision = ushort.MaxValue, MatchId = 19,
                RuleFlags = SessionRules.RequireReady | SessionRules.AllowJoinInProgress,
                ExpectedParticipants = 255, LoadedParticipants = 3,
                Match = new MatchDefinition { RoomKey = new string('X', 40), Mode = GameMode.BattleTeams,
                    Format = MatchFormat.FourVsFour, TimeLimitSeconds = 600, PointGoal = 20,
                    FriendlyFire = true, AffinityWeapons = true, ShadowFreeze = true } };
            byte[] data = new byte[SessionStatePacket.Size]; state.Write(data);
            Check(SessionStatePacket.TryRead(data, out var read) && read.Match == state.Match
                && read.Revision == state.Revision && read.LoadedParticipants == 3, "session round trip/max room/revision");
            for (int length = 0; length < data.Length; length++)
                Check(!SessionStatePacket.TryRead(data.AsSpan(0, length), out _), "truncated session");
            foreach (int offset in new[] { 0, 1, 8, 9 })
            { byte saved = data[offset]; data[offset] = 254; Check(!SessionStatePacket.TryRead(data, out _), "invalid enum"); data[offset] = saved; }
            Check(SessionStatePacket.IsNewer(0, ushort.MaxValue) && !SessionStatePacket.IsNewer(ushort.MaxValue, 0), "revision wrap ordering");
            foreach (LobbyCommandType type in Enum.GetValues<LobbyCommandType>())
            {
                var command = new LobbyCommandPacket { CommandId = 42, ExpectedRevision = 17, Type = type,
                    TargetSlot = 7, TeamIndex = -1, Ready = true, Configuration = state };
                byte[] bytes = new byte[LobbyCommandPacket.Size]; command.Write(bytes);
                Check(LobbyCommandPacket.TryRead(bytes, out var decoded) && decoded.Type == type
                    && decoded.CommandId == 42 && decoded.TeamIndex == -1 && decoded.Ready, "command round trip");
                for (int length = 0; length < bytes.Length; length++)
                    Check(!LobbyCommandPacket.TryRead(bytes.AsSpan(0, length), out _), "truncated command");
            }
            foreach (LobbyResultCode code in Enum.GetValues<LobbyResultCode>())
            {
                var result = new LobbyCommandResultPacket { CommandId = 42, ResultCode = code, CurrentRevision = 65535, Reason = "A reason" };
                byte[] bytes = new byte[LobbyCommandResultPacket.Size]; result.Write(bytes);
                Check(LobbyCommandResultPacket.TryRead(bytes, out var decoded) && decoded.ResultCode == code
                    && decoded.Reason == result.Reason && decoded.CurrentRevision == 65535, "result round trip");
                for (int length = 0; length < bytes.Length; length++) Check(!LobbyCommandResultPacket.TryRead(bytes.AsSpan(0, length), out _), "truncated result");
            }
            byte[] loadedBytes = new byte[MatchLoadedPacket.Size]; new MatchLoadedPacket(65535).Write(loadedBytes);
            Check(MatchLoadedPacket.TryRead(loadedBytes, out var loaded) && loaded.MatchId == 65535, "loaded round trip");
            Check(!MatchLoadedPacket.TryRead(loadedBytes.AsSpan(0, 1), out _), "truncated loaded");
            byte[] failedBytes = new byte[MatchLoadFailedPacket.Size]; new MatchLoadFailedPacket(17, "missing map").Write(failedBytes);
            Check(MatchLoadFailedPacket.TryRead(failedBytes, out var failed) && failed.MatchId == 17 && failed.Reason == "missing map", "failed round trip");
            for (int length = 0; length < failedBytes.Length; length++) Check(!MatchLoadFailedPacket.TryRead(failedBytes.AsSpan(0, length), out _), "truncated failure");
            var roster = RosterPacket.Create(); roster.Count = 8; roster.Revision = 123;
            for (int i = 0; i < 8; i++) { roster.Slots[i] = (byte)i; roster.Teams[i] = (sbyte)(i % 5 - 1); roster.LobbyReady[i] = i % 2 == 0; roster.Names[i] = $"Player{i}"; }
            byte[] rosterBytes = new byte[RosterPacket.Size]; roster.Write(rosterBytes);
            Check(RosterPacket.TryRead(rosterBytes, out var rr) && rr.Teams.SequenceEqual(roster.Teams)
                && rr.LobbyReady.SequenceEqual(roster.LobbyReady) && rr.Revision == 123, "roster team/ready/revision round trip");
            for (int length = 0; length < rosterBytes.Length; length++) Check(!RosterPacket.TryRead(rosterBytes.AsSpan(0, length), out _), "truncated roster");
            var host = new HostRequestPacket { Protocol = 8, MaxPlayers = 8, RoomKey = "room", ServerName = "test",
                Policy = ServerSessionPolicy.Lobby, RequireReady = true, AllowJoinInProgress = true, Format = MatchFormat.FourVsFour };
            byte[] hostBytes = new byte[host.Length]; host.Write(hostBytes); var hr = HostRequestPacket.Read(hostBytes);
            Check(hr.Policy == host.Policy && hr.Format == host.Format && hr.RequireReady && hr.AllowJoinInProgress, "host options appended without rotation");
            var reply = new HostReplyPacket { Started = true, Port = 123, OwnerToken = Guid.NewGuid() };
            byte[] replyBytes = new byte[HostReplyPacket.Size]; reply.Write(replyBytes);
            Check(HostReplyPacket.Read(replyBytes).OwnerToken == reply.OwnerToken, "owner token round trip");
        }

        private static void ClientStateChecks()
        {
            NetSession.StartPlayback();
            Check(!NetSession.SessionTimedOut, "playback has no network timeout");
            var state = new SessionStatePacket { Policy = ServerSessionPolicy.Lobby,
                Phase = SessionPhase.Lobby, Revision = ushort.MaxValue, MatchId = 4, MaxPlayers = 8,
                OwnerSlot = 255, Match = new MatchDefinition { RoomKey = Rooms()[0], Mode = GameMode.Battle } };
            NetSession.ApplySessionState(state);
            state.Revision = 0; state.Phase = SessionPhase.Starting; state.MatchId++;
            NetSession.ApplySessionState(state);
            Check(NetSession.IsStarting && NetSession.ServerSession?.MatchId == 5,
                "client accepts session revision wrap");
            state.Revision = ushort.MaxValue; state.Phase = SessionPhase.Lobby; state.MatchId--;
            NetSession.ApplySessionState(state);
            Check(NetSession.IsStarting && NetSession.ServerSession?.MatchId == 5,
                "delayed state cannot roll back a new match");
            NetMatchSync.Apply();
            Check(GameState.MatchTime == -1 && GameState.PointGoal == 0,
                "unlimited match uses the finite hidden-clock sentinel and no point goal");
            NetSession.Stop();
        }

        private sealed class Client : IDisposable
        {
            public NetTransport Transport = new(0);
            public readonly uint Id;
            public readonly IPEndPoint Server;
            public int Slot = -1;
            public SessionStatePacket? State;
            public RosterPacket Roster = RosterPacket.Create();
            public MatchStatePacket Match;
            public bool Authority;
            public readonly Dictionary<uint, LobbyCommandResultPacket> Results = new();
            private uint _command, _frame;
            public Client(int port, uint id, Guid token = default)
            {
                Id = id; Server = new IPEndPoint(IPAddress.Loopback, port);
                Transport.AnswerPingsImmediately(); Hello(token);
            }
            public void Hello(Guid token = default)
            {
                byte[] bytes = new byte[22]; bytes[0] = 8; bytes[1] = Slot < 0 ? (byte)255 : (byte)Slot;
                BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(2), Id); token.TryWriteBytes(bytes.AsSpan(6));
                Send(PacketType.Hello, bytes);
            }
            public void Identify(byte hunter = 0)
            { byte[] bytes = new byte[8]; bytes[0] = hunter; NetText.Write(bytes.AsSpan(2), $"Test{Id}"); Send(PacketType.Identify, bytes); }
            public void Send(PacketType type, byte[] bytes) => Transport.Send(Server, type, bytes);
            public LobbyCommandPacket Command(LobbyCommandType type, bool ready = false, SessionStatePacket? config = null,
                byte target = 255, sbyte team = -1, ushort? revision = null)
            {
                var packet = new LobbyCommandPacket { CommandId = ++_command, ExpectedRevision = revision ?? State!.Value.Revision,
                    Type = type, Ready = ready, Configuration = config ?? State!.Value, TargetSlot = target, TeamIndex = team };
                Resend(packet); return packet;
            }
            public void Resend(LobbyCommandPacket command)
            { byte[] bytes = new byte[LobbyCommandPacket.Size]; command.Write(bytes); Send(PacketType.LobbyCommand, bytes); }
            public void Loaded(ushort? id = null)
            { byte[] bytes = new byte[2]; new MatchLoadedPacket(id ?? State!.Value.MatchId).Write(bytes); Send(PacketType.MatchLoaded, bytes); }
            public void ReadyResults()
            { var intent = new IntentPacket { Frame = ++_frame, Buttons = IntentButtons.ReadyState }; byte[] bytes = new byte[IntentPacket.FullSize]; intent.Write(bytes); Send(PacketType.Intent, bytes); }
            public void Drain()
            {
                foreach (var packet in Transport.Drain())
                {
                    if (packet.Type == PacketType.Welcome) Slot = packet.Payload[0];
                    if (packet.Type == PacketType.Authority) Authority = true;
                    if (packet.Type == PacketType.SessionState && SessionStatePacket.TryRead(packet.Payload, out var state)
                        && (State == null || state.Revision == State.Value.Revision || SessionStatePacket.IsNewer(state.Revision, State.Value.Revision))) State = state;
                    if (packet.Type == PacketType.Roster && RosterPacket.TryRead(packet.Payload, out var roster)
                        && (roster.Revision == Roster.Revision || SessionStatePacket.IsNewer(roster.Revision, Roster.Revision))) Roster = roster;
                    if (packet.Type == PacketType.LobbyCommandResult && LobbyCommandResultPacket.TryRead(packet.Payload, out var result)) Results[result.CommandId] = result;
                    if (packet.Type == PacketType.MatchState && packet.Payload.Length == MatchStatePacket.Size) Match = MatchStatePacket.Read(packet.Payload);
                }
            }
            public void Rebind() { Transport.Dispose(); Transport = new NetTransport(0); Transport.AnswerPingsImmediately(); Hello(); }
            public void Dispose() { Send(PacketType.Bye, Array.Empty<byte>()); Transport.Dispose(); }
        }

        private sealed class Rig : IDisposable
        {
            public readonly DedicatedServer Server;
            public readonly List<Client> Clients = new();
            private readonly Thread _thread;
            private Exception? _error;
            public Rig(ServerSessionPolicy policy = ServerSessionPolicy.Lobby, Guid token = default)
            {
                Server = new DedicatedServer(0, 8, MapRotation.SingleMatch(Rooms()[0], GameMode.Battle, 0, 0))
                    { SessionPolicy = policy, OwnerToken = token, RunsTheMatch = false };
                _thread = new Thread(() => { try { Server.Run(); } catch (Exception ex) { _error = ex; } }) { IsBackground = true };
                _thread.Start(); Wait(() => Server.Listening, "server listening");
            }
            public Client Add(uint id, Guid token = default)
            {
                var client = new Client(Server.BoundPort, id, token); Clients.Add(client);
                Wait(() => client.Slot >= 0 && client.State != null, "client admitted"); client.Identify();
                Stable(); return client;
            }
            public void Stable() => Wait(() => Clients.Count > 0 && Clients.All(c => c.State?.Revision == Clients[0].State?.Revision
                && c.Roster.Revision == c.State?.Revision && c.Roster.Count == Clients.Count
                && Enumerable.Range(0, c.Roster.Count).All(i => c.Roster.Names[i] == $"Test{Clients.Single(p => p.Slot == c.Roster.Slots[i]).Id}")), "roster and state converge");
            public void Wait(Func<bool> condition, string label, int ms = 4000)
            {
                var clock = Stopwatch.StartNew();
                do
                {
                    foreach (var client in Clients) client.Drain();
                    if (_error != null) throw _error;
                    if (condition()) { Check(true, label); return; }
                    Thread.Sleep(5);
                } while (clock.ElapsedMilliseconds < ms);
                throw new InvalidOperationException($"Timed out: {label}");
            }
            public LobbyCommandResultPacket Expect(Client client, LobbyCommandPacket command, LobbyResultCode expected)
            {
                // Deliberately duplicate every command and discard the first result before retrying.
                client.Resend(command);
                Wait(() => client.Results.ContainsKey(command.CommandId), "command answered");
                var first = client.Results[command.CommandId]; client.Results.Remove(command.CommandId); client.Resend(command);
                Wait(() => client.Results.ContainsKey(command.CommandId), "duplicate answered");
                var result = client.Results[command.CommandId];
                Check(first.CurrentRevision == result.CurrentRevision && first.ResultCode == result.ResultCode, "duplicate is idempotent");
                Check(result.ResultCode == expected, $"{command.Type}: expected {expected}, got {result.ResultCode}: {result.Reason}");
                Stable(); return result;
            }
            public void ReadyAll()
            { foreach (Client client in Clients) { Stable(); Expect(client, client.Command(LobbyCommandType.SetReady, true), LobbyResultCode.Ok); } }
            public void Dispose()
            { foreach (Client client in Clients) client.Dispose(); Server.Stop(); _thread.Join(5000); }
        }
        private static string[] Rooms() => Metadata.RoomMetadata.Where(p => p.Value.Multiplayer).Select(p => p.Key).Take(2).ToArray();

        private static void Scenario()
        {
            Guid token = Guid.NewGuid(); using var rig = new Rig(token: token);
            Client b = rig.Add(2); Check(b.State!.Value.OwnerSlot == 255, "first arrival cannot steal hosted ownership");
            Client a = rig.Add(1, token); Check(a.State!.Value.OwnerSlot == a.Slot, "creator claims owner token");
            var originalA = a.Transport; var originalB = b.Transport; int slotA = a.Slot, slotB = b.Slot;
            rig.Expect(b, b.Command(LobbyCommandType.StartMatch), LobbyResultCode.NotOwner);
            rig.Expect(a, a.Command(LobbyCommandType.StartMatch), LobbyResultCode.PlayersNotReady);
            rig.Expect(a, a.Command(LobbyCommandType.SetReady, true, revision: 0), LobbyResultCode.StaleRevision);
            rig.ReadyAll();
            var config = a.State.Value; config.Match = config.Match with { RoomKey = Rooms()[1], TimeLimitSeconds = 600 };
            rig.Expect(a, a.Command(LobbyCommandType.UpdateMatch, config: config), LobbyResultCode.Ok);
            Check(a.Roster.LobbyReady.Take(a.Roster.Count).All(r => !r), "configuration clears ready");
            var lobbyStatus = NetStatus.Query("127.0.0.1", rig.Server.BoundPort, allowJoinProbe: false);
            Check(lobbyStatus.Online && lobbyStatus.Phase == SessionPhase.Lobby && lobbyStatus.TimeRemaining == 600,
                "browser status clock stays at the full time limit in the lobby");
            rig.ReadyAll();
            var start = a.Command(LobbyCommandType.StartMatch); rig.Expect(a, start, LobbyResultCode.Ok);
            Check(a.State.Value.Phase == SessionPhase.Starting, "start enters barrier");
            a.Loaded((ushort)(a.State.Value.MatchId - 1));
            a.Loaded(); rig.Wait(() => a.State.Value.LoadedParticipants == (1 << a.Slot), "one participant loaded");
            Check(a.State.Value.Phase == SessionPhase.Starting, "one loaded cannot release barrier");
            var loadingStatus = NetStatus.Query("127.0.0.1", rig.Server.BoundPort, allowJoinProbe: false);
            Check(loadingStatus.Phase == SessionPhase.Starting && loadingStatus.TimeRemaining == 600,
                "browser status clock stays frozen through the load barrier");
            Client late = rig.Add(3); Check((late.State!.Value.ExpectedParticipants & (1 << late.Slot)) == 0, "late join excluded from barrier");
            b.Loaded(); rig.Wait(() => a.State.Value.Phase == SessionPhase.InMatch, "barrier released");
            b.Send(PacketType.MatchEnd, Array.Empty<byte>());
            rig.Wait(() => a.State.Value.Phase == SessionPhase.PostMatch, "results entered");
            foreach (var client in rig.Clients) client.ReadyResults();
            rig.Wait(() => a.State.Value.Phase == SessionPhase.Lobby, "results return to lobby", 18000);
            rig.Stable();
            Check(ReferenceEquals(originalA, a.Transport) && ReferenceEquals(originalB, b.Transport)
                && a.Slot == slotA && b.Slot == slotB, "same UDP transports and slots across rounds");
            Check(a.Roster.LobbyReady.Take(a.Roster.Count).All(r => !r), "return clears lobby ready");
            ushort firstMatch = a.State.Value.MatchId;
            rig.ReadyAll(); rig.Expect(a, a.Command(LobbyCommandType.StartMatch), LobbyResultCode.Ok);
            foreach (var client in rig.Clients) client.Loaded();
            rig.Wait(() => a.State.Value.Phase == SessionPhase.InMatch, "second round starts");
            Check(a.State.Value.MatchId != firstMatch, "new match id on same map");
            a.Dispose(); rig.Clients.Remove(a);
            rig.Wait(() => b.State!.Value.OwnerSlot == b.Slot, "oldest peer becomes owner");
            b.Rebind(); rig.Stable(); Check(b.Slot == slotB && b.State.Value.OwnerSlot == slotB, "owner rebind keeps identity and slot");
        }

        private static void TeamScenario()
        {
            using var rig = new Rig(); Client owner = rig.Add(10); Client other = rig.Add(11);
            var config = owner.State!.Value;
            config.Match = config.Match with { Mode = GameMode.BattleTeams, Format = MatchFormat.TwoVsTwo };
            config.RuleFlags &= ~SessionRules.RequireReady;
            rig.Expect(owner, owner.Command(LobbyCommandType.UpdateMatch, config: config), LobbyResultCode.Ok);
            rig.Expect(owner, owner.Command(LobbyCommandType.StartMatch), LobbyResultCode.NotEnoughPlayers);
            rig.Add(12); rig.Add(13);
            Check(owner.Roster.Teams.Take(4).SequenceEqual(new sbyte[] { 0, 1, 0, 1 }), "deterministic 2v2 assignment");
            rig.Expect(owner, owner.Command(LobbyCommandType.SetTeam, target: (byte)other.Slot, team: 0), LobbyResultCode.TeamFull);
            rig.Expect(owner, owner.Command(LobbyCommandType.StartMatch), LobbyResultCode.Ok);
            Client leaving = rig.Clients[^1]; leaving.Dispose(); rig.Clients.Remove(leaving);
            rig.Wait(() => owner.State.Value.Phase == SessionPhase.Lobby, "disconnect invalidates exact team format during load");
            config = owner.State.Value; config.Match = config.Match with { Format = MatchFormat.FourVsFour };
            rig.Expect(owner, owner.Command(LobbyCommandType.UpdateMatch, config: config), LobbyResultCode.Ok);
            for (uint id = 14; rig.Clients.Count < 8; id++) rig.Add(id);
            Check(owner.Roster.Teams.Take(8).Count(t => t == 0) == 4 && owner.Roster.Teams.Take(8).Count(t => t == 1) == 4, "4v4 assignment");
            config = owner.State.Value; config.Match = config.Match with { Format = MatchFormat.TwoVsTwoVsTwoVsTwo };
            rig.Expect(owner, owner.Command(LobbyCommandType.UpdateMatch, config: config), LobbyResultCode.InvalidConfiguration);
            rig.Expect(owner, owner.Command(LobbyCommandType.StartMatch), LobbyResultCode.Ok);
            rig.Wait(() => owner.State.Value.Phase == SessionPhase.InMatch, "load timeout releases barrier", 18000);
        }

        private static void EmptyContinuousRestart()
        {
            // Synchronous production handlers make the last-leave/first-join boundary deterministic.
            const System.Reflection.BindingFlags flags = System.Reflection.BindingFlags.Instance
                | System.Reflection.BindingFlags.NonPublic;
            var server = new DedicatedServer(0) { RunsTheMatch = false };
            var sender = new IPEndPoint(IPAddress.Loopback, 31001);
            T Field<T>(string name) => (T)typeof(DedicatedServer).GetField(name, flags)!.GetValue(server)!;
            SessionStatePacket State() => (SessionStatePacket)typeof(DedicatedServer)
                .GetMethod("BuildSessionState", flags)!.Invoke(server, null)!;
            void Send(PacketType type, byte[] body)
            {
                byte[] data = new byte[body.Length + 1]; data[0] = (byte)type; body.CopyTo(data, 1);
                typeof(DedicatedServer).GetMethod("Handle", flags)!.Invoke(server,
                    new object[] { new ReceivedPacket(sender, data, data.Length), 1.0 });
            }
            void Hello(uint id)
            {
                byte[] body = new byte[6]; body[0] = NetConfig.ProtocolVersion; body[1] = 255;
                BinaryPrimitives.WriteUInt32LittleEndian(body.AsSpan(2), id);
                Send(PacketType.Hello, body);
            }
            Hello(1); Send(PacketType.MatchEnd, Array.Empty<byte>());
            Check(State().Phase == SessionPhase.PostMatch && Field<bool>("_ballotOpen"), "restart fixture reaches results and ballot");
            ushort match = State().MatchId;
            Send(PacketType.Bye, Array.Empty<byte>());
            // A former authority's cached snapshot must not be offered to the next occupant.
            typeof(DedicatedServer).GetField("_lastSnapshot", flags)!.SetValue(server, new byte[] { 1 });
            Hello(2);
            Check(State().Phase == SessionPhase.InMatch && State().MatchId != match,
                "empty continuous server restarts a playable match");
            Check(!Field<bool>("_ballotOpen") && Field<byte[]?>("_lastSnapshot") == null,
                "empty restart closes results ballot and clears cached snapshot");
            Send(PacketType.MatchEnd, Array.Empty<byte>());
            Check(State().Phase == SessionPhase.PostMatch && Field<double>("_matchEndedAt") >= 0,
                "restarted continuous match can finish again");
        }

        private static void ContinuousScenario()
        {
            using var rig = new Rig(ServerSessionPolicy.Continuous); Client client = rig.Add(20);
            Check(client.State!.Value.Phase == SessionPhase.InMatch, "continuous starts in match");
            ushort match = client.State.Value.MatchId;
            client.Send(PacketType.MatchEnd, Array.Empty<byte>());
            rig.Wait(() => client.State.Value.Phase == SessionPhase.PostMatch, "continuous results"); client.ReadyResults();
            rig.Wait(() => client.State.Value.Phase == SessionPhase.InMatch && client.State.Value.MatchId != match, "continuous rotates automatically", 18000);
        }

        private static void ClientSessionScenario()
        {
            NetLag.Configure("80:20");
            using var rig = new Rig();
            Check(NetLaunch.Connect("127.0.0.1", rig.Server.BoundPort, "RealClient", Hunter.Samus), "NetSession connects to an idle lobby");
            int port = NetSession.ConnectionPort, slot = NetSession.LocalSlot;
            uint clientId = NetSession.ClientId;
            void PumpUntil(Func<bool> condition, string message, int timeout = 5000)
            {
                rig.Wait(() => { NetSession.Pump(); return condition(); }, message, timeout);
            }
            // Admission and Identify are separate exchanges. Matching revisions
            // can describe the initial unnamed roster; wait for the acknowledged
            // identity before freezing the revision and dropping retry traffic.
            PumpUntil(() => NetSession.LocalIsLobbyOwner
                && GameState.Nicknames[slot] == "RealClient"
                && NetSession.LobbyRoster().Revision == NetSession.SessionRevision,
                "real client owns a consistent lobby");
            Check(NetSession.ServerMatch == null, "connection does not require a running match");
            // Lose the first command and its first retry entirely. The same command ID must recover.
            NetLag.ConfigureLoss("100");
            Check(NetSession.SendLobbyCommand(LobbyCommandType.SetReady, ready: true), "enqueue ready");
            var loss = Stopwatch.StartNew();
            while (loss.ElapsedMilliseconds < 400) { NetSession.Pump(); Thread.Sleep(10); }
            Check(NetSession.LobbyCommandPending, "lost command remains pending");
            NetLag.ConfigureLoss("0");
            PumpUntil(() => !NetSession.LobbyCommandPending && NetSession.SlotLobbyReady[slot], "retransmission recovers lost ready");
            PumpUntil(() => NetSession.LobbyRoster().Revision == NetSession.SessionRevision, "ready state converged");
            Check(NetSession.SendLobbyCommand(LobbyCommandType.StartMatch), "real client starts");
            PumpUntil(() => NetSession.IsStarting && !NetSession.LobbyCommandPending, "real client load barrier");
            Check(NetSession.FreezeGameplay, "gameplay frozen before loaded");
            NetSession.MarkMatchLoaded();
            PumpUntil(() => NetSession.IsPlaying, "real load ack starts match");
            Check(!NetSession.FreezeGameplay, "gameplay released after barrier");
            NetSession.SendMatchEnd();
            PumpUntil(() => NetSession.IsPostMatch, "real client results");
            uint frame = 100;
            PumpUntil(() =>
            {
                NetSession.SendIntent(new IntentPacket { Frame = frame++, Buttons = IntentButtons.ReadyState });
                return NetSession.IsInLobby;
            }, "real client returns to lobby", 18000);
            NetSession.ResetMatchState();
            Check(NetSession.Active && NetSession.ConnectionPort == port && NetSession.LocalSlot == slot
                && NetSession.ClientId == clientId && NetSession.LocalIsLobbyOwner, "real client socket/slot/id/owner survive match teardown");
            PumpUntil(() => NetSession.LobbyRoster().Revision == NetSession.SessionRevision, "next lobby consistent");
            Check(NetSession.SendLobbyCommand(LobbyCommandType.SetReady, ready: true), "ready for second real-client match");
            PumpUntil(() => !NetSession.LobbyCommandPending && NetSession.SlotLobbyReady[slot], "second ready received");
            Check(NetSession.SendLobbyCommand(LobbyCommandType.StartMatch), "second real-client start");
            PumpUntil(() => NetSession.IsStarting, "second real-client load barrier");
            NetSession.MarkMatchLoaded(); PumpUntil(() => NetSession.IsPlaying, "second real-client round");
            Check(NetSession.ConnectionPort == port && NetSession.LocalSlot == slot, "same client UDP session in second match");
            NetSession.Stop();
            NetLag.Configure("0");
        }
    }
}
