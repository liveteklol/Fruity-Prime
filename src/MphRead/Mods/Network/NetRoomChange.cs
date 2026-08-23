using System;
using MphRead.Entities;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// Follows the server's map rotation.
    ///
    /// The rotation existed on the server and nowhere else: NetSession raised
    /// a MapChanged event that nothing subscribed to, so the server advanced,
    /// announced the new level, and every client carried on playing the old
    /// one indefinitely.
    ///
    /// Polled from the server's periodic match state rather than driven by
    /// the MapChange packet, for the same reason the clock is: a datagram
    /// announcing the rotation can be lost, and a client that missed it would
    /// be stranded on the previous map for the rest of the session. Comparing
    /// against the state that arrives every second recovers on its own.
    /// </summary>
    public static class NetRoomChange
    {
        private static string _requested = "";
        private static uint _requestedFrame;
        /// <summary>
        /// The server's match number this client has loaded a room for.
        ///
        /// The room key on its own is not enough to tell one match from the
        /// next. A server whose rotation is a single map -- which is what
        /// "Host a game" builds -- plays that map over and over, so a client
        /// comparing names saw nothing change when a round ended and stayed on
        /// its results screen until somebody quit. Keyed on the match number,
        /// the same room simply loads again, which is a clean restart: every
        /// slot rebuilt, every score back to zero.
        /// </summary>
        private static ushort _loadedMatch;
        private static uint _loadedFrame;

        /// <summary>
        /// Frames after a room load during which a peer's reported position is
        /// ignored.
        ///
        /// Clients do not finish loading at the same instant, so for about a
        /// second after a rotation some peers are still standing in the room
        /// this one has just left. Following those positions puts their
        /// puppets wherever the old room's coordinates happen to land in the
        /// new one -- which is what turned every rotation into a burst of
        /// visible teleports, over a hundred of them across three maps. The
        /// authority's snapshot still places everybody, so nothing is lost by
        /// waiting: it is the peer-reported position, and only that, which is
        /// meaningless across a room change.
        /// </summary>
        private const uint SettleFrames = 60;

        /// <summary>True for about a second after this client changed rooms.</summary>
        public static bool Settling => _loadedFrame != 0
            && NetSession.NetFrame - _loadedFrame < SettleFrames;

        /// <summary>
        /// Player count the room layout is built from during a transition.
        /// Fixed for the same reason it is fixed at first load: the layout
        /// decides where the spawn points are, and clients that disagreed
        /// about it would be playing subtly different levels.
        /// </summary>
        public static int RoomPlayerCount => NetSession.Active ? NetLaunch.RoomPlayerCount : 0;

        /// <summary>True while a networked session is rebuilding its players for a new room.</summary>
        public static bool Rebuilding => NetSession.Active;

        public static void Reset()
        {
            _requested = "";
            _requestedFrame = 0;
            _loadedMatch = 0;
            _loadedFrame = 0;
        }

        /// <summary>
        /// Start loading the server's map if this client is on a different
        /// one. Cheap enough to call every frame; only a mismatch does work.
        /// </summary>
        public static void Sync(Scene scene)
        {
            if (!NetSession.Active || scene.Room == null || GameState.InRoomTransition)
            {
                return;
            }
            MatchStatePacket? state = NetSession.ServerMatch;
            string wanted = state?.RoomKey ?? "";
            if (wanted.Length == 0)
            {
                return;
            }
            ushort match = state!.Value.MatchId;
            string current = Metadata.GetRoomById(scene.RoomId, noThrow: true)?.Name ?? "";
            // A joiner arrives already on the server's map and must not
            // immediately reload it, so the first match number seen is adopted
            // rather than acted on.
            if (current == wanted && (_loadedMatch == 0 || _loadedMatch == match))
            {
                _loadedMatch = match;
                _requested = "";
                return;
            }
            // The fade runs for several frames before the load begins, and
            // TransitionState only flips once it does, so without our own
            // note of what was asked for this would restart the fade every
            // frame and never reach the room.
            if (_requested == wanted && _loadedMatch == match
                && NetSession.NetFrame - _requestedFrame < 300)
            {
                return;
            }
            (RoomMetadata? meta, _) = Metadata.GetRoomByName(wanted);
            if (meta == null)
            {
                Console.WriteLine($"[net] server switched to \"{wanted}\", which this build does not know");
                NetLog.Event($"unknown server map \"{wanted}\"");
                _requested = wanted;
                _requestedFrame = NetSession.NetFrame;
                return;
            }
            _requested = wanted;
            _requestedFrame = NetSession.NetFrame;
            _loadedMatch = match;
            Console.WriteLine(current == wanted
                ? $"[net] server started a new match on {wanted}; loading it"
                : $"[net] server rotated to {wanted}; loading it");
            NetLog.Event($"loading {wanted} for match {match}");
            GameState.TransitionRoomId = meta.Id;
            scene.SetFade(FadeType.FadeOutBlack, length: 10 / 30f, overwrite: true, AfterFade.LoadRoom);
        }

        /// <summary>
        /// Rebuild every player slot for the new room and return the one this
        /// machine drives.
        ///
        /// RoomEntity.LoadRoom creates a single player, which is right for the
        /// single-player transitions it was written for and leaves a networked
        /// match with one hunter in an empty level. The slots are rebuilt the
        /// way they are at first load, for the same reason: Scene.AddPlayer is
        /// inert afterwards, so a slot not built here can never be filled.
        /// </summary>
        public static PlayerEntity RebuildPlayers(Scene scene, Hunter hunter, int recolor)
        {
            int localSlot = Math.Max(NetSession.LocalSlot, 0);
            for (int slot = 0; slot < PlayerEntity.MaxPlayers; slot++)
            {
                Hunter slotHunter = slot == localSlot ? hunter : NetSession.SlotHunter[slot];
                PlayerEntity? created = PlayerEntity.Create(slotHunter, slot == localSlot ? recolor : 0);
                if (created == null)
                {
                    continue;
                }
                created.LoadFlags |= LoadFlags.SlotActive;
                created.LoadFlags |= LoadFlags.Active;
                created.LoadFlags |= LoadFlags.Initial;
                created.IsBot = false;
                created.BotLevel = 0;
                bool occupied = slot == localSlot
                    || (slot < NetSession.SlotOccupied.Length && NetSession.SlotOccupied[slot]);
                if (!occupied)
                {
                    created.LoadFlags &= ~LoadFlags.Active;
                }
            }
            PlayerEntity.PlayerCount = 1;
            PlayerEntity.MainPlayerIndex = localSlot;
            // Everything keyed to the old room has to go: which slots are
            // switched on, the per-match damage tallies, and the scores,
            // which start again with the map exactly as they do on a Quake
            // server.
            //
            // Not the damage *sequence*, which is the one thing here that
            // must survive a rotation -- see NetDamage.ResetForRoomChange.
            // The authority and its clients do not change room on the same
            // frame, so a counter that restarts on each machine separately
            // is a counter the two sides disagree about for as long as the
            // gap lasts.
            NetSlotManager.Reset();
            NetPlayerSetup.Reset();
            NetDamage.ResetForRoomChange();
            ResetScores();
            Console.WriteLine($"[net] player slots rebuilt for the new room, main player = slot {localSlot}");
            return PlayerEntity.Players[localSlot];
        }

        /// <summary>
        /// Put the slots RoomEntity.LoadRoom did not handle into the scene.
        /// It inserts and initialises the main player only, which is all a
        /// single-player transition has.
        /// </summary>
        public static void AfterRebuild(Scene scene)
        {
            _loadedFrame = Math.Max(NetSession.NetFrame, 1);
            // Everything the bridge remembered about where players were
            // standing was about the room that has just been left.
            NetPlayerBridge.NoteRoomChanged();
            // A rotation is a fresh match: re-assert that nothing in the
            // cheat list is on, in case a long session had one restored.
            NetLaunch.DisableCheatsForMatch();
            for (int slot = 0; slot < PlayerEntity.Players.Count; slot++)
            {
                PlayerEntity player = PlayerEntity.Players[slot];
                if (slot == PlayerEntity.MainPlayerIndex)
                {
                    continue;
                }
                if (!player.LoadFlags.TestFlag(LoadFlags.SlotActive))
                {
                    NetLog.Event($"slot {slot} skipped on rebuild: flags={player.LoadFlags}");
                    continue;
                }
                scene.InsertEntity(player);
                player.Initialize();
                scene.InitEntity(player);
                scene.InitEntity(player.Halfturret);
                NetLog.Event($"slot {slot} re-inserted into the new room");
            }
        }

        private static void ResetScores()
        {
            // Every slot, not the four a DS match could hold: with eight
            // players the last four carried their points, kills and deaths
            // across every map rotation, and only their rows disagreed with
            // everyone else's scoreboard.
            for (int i = 0; i < PlayerEntity.SlotCapacity; i++)
            {
                GameState.Points[i] = 0;
                GameState.TeamPoints[i] = 0;
                GameState.Kills[i] = 0;
                GameState.TeamKills[i] = 0;
                GameState.Deaths[i] = 0;
                GameState.TeamDeaths[i] = 0;
                GameState.Standings[i] = 0;
                GameState.TeamStandings[i] = 0;
                GameState.DamageCount[i] = 0;
                GameState.KillStreak[i] = 0;
            }
            // The match itself, not just its scoreboard: the room this is
            // loading is a new round, and the flags that say the last one had
            // already ended have to go with the points.
            GameState.ResetMatchProgress();
            NetMatchEnd.Reset();
        }
    }
}
