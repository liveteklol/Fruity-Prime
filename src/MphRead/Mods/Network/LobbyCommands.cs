using System;
using System.Collections.Generic;
using System.Linq;

namespace MphRead.Mods.Network
{
    public sealed partial class DedicatedServer
    {
        public ServerSessionPolicy SessionPolicy { get; init; } = ServerSessionPolicy.Continuous;
        public MatchFormat Format { get; init; } = MatchFormat.Auto;
        public bool RequireReady { get; private set; } = true;
        public bool AllowJoinInProgress { get; private set; } = true;
        public Guid OwnerToken { get; set; }
        public void SetSessionOptions(bool requireReady, bool allowJoinInProgress)
        { RequireReady = requireReady; AllowJoinInProgress = allowJoinInProgress; }
        private SessionPhase _phase = SessionPhase.InMatch;
        private MatchDefinition _lobbyMatch, _frozenMatch;
        private ushort _sessionRevision = 1;
        private uint _lobbyOwnerClientId;
        private byte _expectedLoadedSlots, _loadedSlots;
        private double _startDeadline;

        private MatchDefinition CurrentDefinition => SessionPolicy == ServerSessionPolicy.Lobby
            ? (_phase == SessionPhase.Lobby ? _lobbyMatch : _frozenMatch)
            : DefinitionFor(_rotation.Current);

        private MatchDefinition DefinitionFor(RotationEntry entry) => new()
        {
            RoomKey = entry.RoomKey, Mode = entry.Mode, Format = Format,
            TimeLimitSeconds = (ushort)Math.Clamp(entry.TimeLimit, 0, ushort.MaxValue),
            PointGoal = (ushort)Math.Clamp(entry.PointGoal, 0, ushort.MaxValue),
            FriendlyFire = FriendlyFire, AffinityWeapons = AffinityWeapons, ShadowFreeze = ShadowFreeze
        };

        private void InvalidateLobbyReady()
        {
            foreach (Peer peer in _peers) peer.LobbyReady = false;
        }

        private void TouchLobbyRevision(string reason)
        {
            ushort previous = _sessionRevision++;
            Log($"[lobby] revision {previous} -> {_sessionRevision}: {reason}");
            BroadcastSessionState();
            BroadcastRoster();
        }

        private void SetPhase(SessionPhase phase)
        {
            Log($"[lobby] phase {_phase} -> {phase}, match {_matchId}");
            _phase = phase;
            TouchLobbyRevision("phase changed");
        }

        private SessionStatePacket BuildSessionState() => new()
        {
            Phase = _phase, Policy = SessionPolicy, Revision = _sessionRevision, MatchId = _matchId,
            OwnerSlot = _lobbyOwnerClientId == 0 ? (byte)255 : (byte)(_peers.Find(p => p.ClientId == _lobbyOwnerClientId)?.SlotIndex ?? 255),
            MaxPlayers = (byte)_maxPlayers, Match = CurrentDefinition,
            RuleFlags = CurrentDefinition.Rules | (RequireReady ? SessionRules.RequireReady : 0)
                | (AllowJoinInProgress ? SessionRules.AllowJoinInProgress : 0),
            ExpectedParticipants = _expectedLoadedSlots, LoadedParticipants = _loadedSlots
        };

        private void BroadcastSessionState()
        {
            var state = BuildSessionState();
            if (_sim != null) NetSession.ApplySessionState(state);
            state.Write(_scratch);
            foreach (Peer peer in _peers)
                _transport?.Send(peer.EndPoint, PacketType.SessionState, _scratch.AsSpan(0, SessionStatePacket.Size));
        }

        private sbyte ChooseTeam(MatchDefinition match, Peer? exclude = null)
        {
            int teams = LobbyRules.TeamCount(match);
            if (teams == 0) return -1;
            int least = int.MaxValue;
            sbyte best = -1;
            for (sbyte team = 0; team < teams; team++)
            {
                int count = _peers.Count(p => p != exclude && p.TeamIndex == team);
                if (count < least && count < LobbyRules.TeamCapacity(match)) { least = count; best = team; }
            }
            return best;
        }

        private void NormalizeTeams()
        {
            foreach (Peer peer in _peers) peer.TeamIndex = -1;
            foreach (Peer peer in _peers) peer.TeamIndex = ChooseTeam(CurrentDefinition, peer);
        }

        private void ClaimOwner(Peer peer, ReadOnlySpan<byte> hello)
        {
            if (SessionPolicy != ServerSessionPolicy.Lobby || _lobbyOwnerClientId != 0 || peer.ClientId == 0) return;
            bool tokenMatches = OwnerToken != Guid.Empty && hello.Length == 22
                && new Guid(hello.Slice(6, 16)) == OwnerToken;
            if (OwnerToken != Guid.Empty && !tokenMatches) return;
            _lobbyOwnerClientId = peer.ClientId;
            OwnerToken = Guid.Empty;
            TouchLobbyRevision($"owner = slot {peer.SlotIndex}");
        }

        private void HandleLobbyCommand(ReceivedPacket packet, double now)
        {
            Peer? peer = Find(packet.Sender);
            if (peer == null || !LobbyCommandPacket.TryRead(packet.Payload, out var command)) return;
            peer.LastSeen = now;
            if (!peer.Commands.TryGetValue(command.CommandId, out var result))
            {
                var code = ExecuteLobbyCommand(peer, command, out string reason);
                result = new LobbyCommandResultPacket { CommandId = command.CommandId,
                    ResultCode = code, CurrentRevision = _sessionRevision, Reason = reason };
                // Cache is attached to ClientId's peer, so socket rebinding does not repeat a command.
                if (peer.Commands.Count >= 64) peer.Commands.Remove(peer.CommandOrder.Dequeue());
                peer.Commands.Add(command.CommandId, result);
                peer.CommandOrder.Enqueue(command.CommandId);
                if (code != LobbyResultCode.Ok) Log($"[lobby] slot {peer.SlotIndex} {command.Type} denied: {reason}");
            }
            result.Write(_scratch);
            _transport?.Send(peer.EndPoint, PacketType.LobbyCommandResult, _scratch.AsSpan(0, LobbyCommandResultPacket.Size));
            // Also repairs a lost state/roster even if the original command succeeded.
            BroadcastSessionState();
            BroadcastRoster();
        }

        private LobbyResultCode ExecuteLobbyCommand(Peer peer, LobbyCommandPacket command, out string reason)
        {
            reason = "";
            if (SessionPolicy != ServerSessionPolicy.Lobby || _phase != SessionPhase.Lobby)
            { reason = "Wait until the server returns to the lobby."; return LobbyResultCode.InvalidPhase; }
            bool owner = peer.ClientId == _lobbyOwnerClientId && peer.ClientId != 0;
            if (command.Type is not LobbyCommandType.SetReady and not LobbyCommandType.SetTeam && !owner)
            { reason = "Only the lobby owner can do that."; return LobbyResultCode.NotOwner; }
            if (command.ExpectedRevision != _sessionRevision)
            { reason = "The lobby changed. Review the updated settings and try again."; return LobbyResultCode.StaleRevision; }
            switch (command.Type)
            {
                case LobbyCommandType.SetReady:
                    peer.LobbyReady = command.Ready;
                    break;
                case LobbyCommandType.SetTeam:
                    Peer? target = _peers.Find(p => p.SlotIndex == command.TargetSlot);
                    if (target == null) { reason = "That player has left."; return LobbyResultCode.TargetNotFound; }
                    if (target != peer && !owner) { reason = "Only the owner can move another player."; return LobbyResultCode.NotOwner; }
                    if (command.TeamIndex < 0 || command.TeamIndex >= LobbyRules.TeamCount(_lobbyMatch))
                    { reason = "Choose a team for the current format."; return LobbyResultCode.InvalidTeam; }
                    if (_peers.Count(p => p != target && p.TeamIndex == command.TeamIndex) >= LobbyRules.TeamCapacity(_lobbyMatch))
                    { reason = "That team is full."; return LobbyResultCode.TeamFull; }
                    target.TeamIndex = command.TeamIndex;
                    target.LobbyReady = false;
                    break;
                case LobbyCommandType.UpdateMatch:
                    var proposed = command.Configuration.Match;
                    var valid = LobbyRules.ValidateDefinition(proposed, out reason);
                    if (valid != LobbyResultCode.Ok) return valid;
                    string? room = ResolveRoomKey(proposed.RoomKey);
                    if (room == null) { reason = "The server does not have that map."; return LobbyResultCode.MapUnavailable; }
                    bool topologyChanged = proposed.Format != _lobbyMatch.Format
                        || GameState.IsTeamMode(proposed.Mode) != GameState.IsTeamMode(_lobbyMatch.Mode);
                    _lobbyMatch = proposed with { RoomKey = room };
                    RequireReady = command.Configuration.RequireReady;
                    AllowJoinInProgress = command.Configuration.AllowJoinInProgress;
                    if (topologyChanged) NormalizeTeams();
                    InvalidateLobbyReady();
                    break;
                case LobbyCommandType.StartMatch:
                    var start = LobbyRules.Validate(_lobbyMatch, BuildRoster(), RequireReady, out reason);
                    if (start != LobbyResultCode.Ok) return start;
                    _frozenMatch = _lobbyMatch;
                    // Build before publishing Starting. A failure must not strand clients in loading.
                    double buildStarted = NetSession.Clock;
                    try { StartSimulation(); }
                    catch (Exception ex)
                    {
                        _sim?.Stop(); _sim = null;
                        Log($"[lobby] map load failed: {ex.Message}");
                        reason = "The server could not load this map.";
                        return LobbyResultCode.MapUnavailable;
                    }
                    _matchId++;
                    _matchEndedAt = -1;
                    _lastSnapshot = null;
                    _expectedLoadedSlots = 0; _loadedSlots = 0;
                    foreach (Peer participant in _peers)
                    { _expectedLoadedSlots |= (byte)(1 << participant.SlotIndex); participant.PostMatchReady = false; }
                    _startDeadline = _now + (NetSession.Clock - buildStarted) + 15;
                    SetPhase(SessionPhase.Starting);
                    SyncSimulationState(_now);
                    Log($"[lobby] waiting for slots mask {_expectedLoadedSlots:X2}");
                    return LobbyResultCode.Ok;
                case LobbyCommandType.KickPlayer:
                case LobbyCommandType.TransferOwner:
                    Peer? selected = _peers.Find(p => p.SlotIndex == command.TargetSlot);
                    if (selected == null || selected == peer) { reason = "Choose another connected player."; return LobbyResultCode.TargetNotFound; }
                    if (command.Type == LobbyCommandType.TransferOwner) _lobbyOwnerClientId = selected.ClientId;
                    else
                    {
                        SendRefusal(selected.EndPoint, RefusedPacket.ReasonKicked);
                        Remove(selected, "removed by lobby owner");
                    }
                    break;
            }
            TouchLobbyRevision($"slot {peer.SlotIndex}: {command.Type}");
            return LobbyResultCode.Ok;
        }

        private void HandleMatchLoaded(ReceivedPacket packet, double now)
        {
            Peer? peer = Find(packet.Sender);
            if (peer == null || !MatchLoadedPacket.TryRead(packet.Payload, out var loaded) || loaded.MatchId != _matchId) return;
            peer.LastSeen = now;
            byte mask = (byte)(1 << peer.SlotIndex);
            if (_phase != SessionPhase.Starting || (_loadedSlots & mask) != 0) return;
            _loadedSlots |= mask;
            TouchLobbyRevision($"slot {peer.SlotIndex} loaded match {_matchId}");
            CheckLoadBarrier(now);
        }

        private void CheckLoadBarrier(double now)
        {
            if (_phase != SessionPhase.Starting) return;
            if ((_loadedSlots & _expectedLoadedSlots) != _expectedLoadedSlots && now < _startDeadline) return;
            Log(now >= _startDeadline ? "[lobby] load timeout; late clients may join in progress" : "[lobby] all clients loaded");
            _matchStarted = now;
            SetPhase(SessionPhase.InMatch);
            BroadcastMatchState(now);
        }

        private void HandleMatchLoadFailed(ReceivedPacket packet)
        {
            Peer? peer = Find(packet.Sender);
            if (peer == null || !MatchLoadFailedPacket.TryRead(packet.Payload, out var failed) || failed.MatchId != _matchId) return;
            Remove(peer, $"could not load match: {failed.Reason}");
        }

        private void ReturnToLobby()
        {
            _sim?.Stop(); _sim = null; _lastSnapshot = null;
            _lobbyMatch = DefinitionFor(_rotation.Advance()) with
            {
                Format = _frozenMatch.Format, FriendlyFire = _frozenMatch.FriendlyFire,
                AffinityWeapons = _frozenMatch.AffinityWeapons, ShadowFreeze = _frozenMatch.ShadowFreeze
            };
            // Rotation may change between team and FFA modes; keep the pending format legal.
            if (LobbyRules.ValidateDefinition(_lobbyMatch, out _) != LobbyResultCode.Ok)
                _lobbyMatch = _lobbyMatch with { Format = MatchFormat.Auto };
            _matchEndedAt = -1;
            _expectedLoadedSlots = _loadedSlots = 0;
            CloseBallot(); BroadcastMapChoices();
            InvalidateLobbyReady();
            SessionPhase previous = _phase;
            _phase = SessionPhase.Lobby;
            NormalizeTeams();
            _phase = previous;
            SetPhase(SessionPhase.Lobby);
        }

        private void LobbyPeerRemoved(Peer peer)
        {
            _expectedLoadedSlots &= (byte)~(1 << peer.SlotIndex);
            _loadedSlots &= (byte)~(1 << peer.SlotIndex);
            if (_lobbyOwnerClientId == peer.ClientId)
                _lobbyOwnerClientId = _peers.Count > 0 ? _peers[0].ClientId : 0;
            TouchLobbyRevision($"slot {peer.SlotIndex} left; owner {_lobbyOwnerClientId}");
            if (_phase == SessionPhase.Starting)
            {
                if (LobbyRules.Validate(_frozenMatch, BuildRoster(), false, out string why) != LobbyResultCode.Ok)
                {
                    Log($"[lobby] start cancelled: {why}");
                    _sim?.Stop(); _sim = null; _lastSnapshot = null;
                    InvalidateLobbyReady(); SetPhase(SessionPhase.Lobby);
                }
                else CheckLoadBarrier(_now);
            }
        }
    }
}
