using MphRead.Entities;
using OpenTK.Mathematics;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// The two places the engine calls into the network each frame. Kept
    /// separate from NetSession so Renderer.cs holds call sites rather than
    /// role logic, and so the whole feature stays inert when offline.
    /// </summary>
    public static class NetHooks
    {
        /// <summary>
        /// Which slot this machine drives. 0 when offline, so the single
        /// upstream call site reads the same in both cases.
        /// </summary>
        public static int LocalSlot => NetSession.Active && NetSession.LocalSlot >= 0
            ? NetSession.LocalSlot
            : 0;

        /// <summary>
        /// Whether a player slot with no one in it should survive the frame.
        ///
        /// Scene.UpdateScene destroys and unlists any entity whose Process
        /// returns false, and PlayerProcess returns false for an inactive
        /// player in multiplayer. Scene.AddPlayer is inert once the room has
        /// loaded, so an unlisted slot can never be rebuilt -- which is
        /// precisely how a peer joining a running match ended up rostered,
        /// named, flagged active and still frozen at the origin: its entity
        /// had been dropped from the scene on the first frame, long before
        /// anyone occupied it.
        ///
        /// In a networked session every slot is therefore kept, occupied or
        /// not. An unoccupied one is invisible (Scene.GetDrawItems draws only
        /// active players) and does no work (PlayerProcess returns before
        /// simulating), so keeping it costs a list entry.
        /// </summary>
        public static bool KeepSlotAlive(PlayerEntity player)
        {
            return NetSession.Active;
        }

        public static bool SkipRemoteMovement(PlayerEntity player)
        {
            return NetSession.IsAuthority && player.SlotIndex != NetSession.LocalSlot;
        }

        public static Vector3 RemoteShotOrigin(PlayerEntity player, Vector3 current)
        {
            if (!NetSession.IsAuthority || player.SlotIndex == NetSession.LocalSlot
                || player.SlotIndex < 0 || player.SlotIndex >= NetSession.RemoteIntents.Length)
            {
                return current;
            }
            return current + NetSession.RemoteIntents[player.SlotIndex].Position - player.Position;
        }

        public static Vector3 RemoteShotDirection(PlayerEntity player, Vector3 current)
        {
            if (NetSession.IsAuthority && player.SlotIndex != NetSession.LocalSlot
                && player.SlotIndex >= 0 && player.SlotIndex < NetSession.RemoteIntents.Length)
            {
                Vector3 aim = NetSession.RemoteIntents[player.SlotIndex].Aim;
                if (aim.LengthSquared > 0.0001f)
                {
                    return aim.Normalized();
                }
            }
            return current;
        }

        /// <summary>
        /// Fill a remote player's Controls from the network, exactly the way
        /// PlayerAi fills a bot's. Returns true when this slot belongs to
        /// someone else and the keyboard path must be skipped.
        ///
        /// Keeping the decision here rather than in PlayerInput keeps the
        /// upstream hook to one line, which is what makes pulling from
        /// NoneGiven/MphRead a fast forward instead of a conflict.
        /// </summary>
        public static bool TryApplyRemoteInput(PlayerEntity player, int slot)
        {
            if (!NetSession.Active || slot == LocalSlot)
            {
                return false;
            }
            if (player.LoadFlags.TestFlag(LoadFlags.Active) && NetSession.RemoteIntentValid[slot])
            {
                if (player.LoadFlags.TestFlag(LoadFlags.Spawned) && player.Health > 0)
                {
                    // Position and controls must enter the simulation
                    // together. Applying the position after the scene step
                    // left projectile collision testing on the old hitbox.
                    NetPlayerBridge.ApplyReportedPosition(player, NetSession.RemoteIntents[slot]);
                }
                NetPlayerBridge.ApplyIntent(player, NetSession.RemoteIntents[slot]);
            }
            return true;
        }

        /// <summary>
        /// Whether a networked player should be placed at a spawn point now.
        ///
        /// PlayerProcess only spawns a waiting player for single-player, a
        /// bot, an expired timer, or a held fire button -- its own comment
        /// marks the missing case as "or forced". A remote player is none of
        /// those: it is not a bot (AI would overwrite its network input), and
        /// its fire button is only whatever arrived this frame. So it sat at
        /// the origin forever, present and active but never on the map,
        /// which is why the other client saw nothing move.
        ///
        /// Every player the server says is in the match should spawn, this
        /// machine's own included -- the local player is normally spawned by
        /// holding fire, and a joiner should not have to.
        /// </summary>
        public static bool ForceSpawn(PlayerEntity player)
        {
            if (MapAudit.ForceEveryone)
            {
                return true;
            }
            if (!NetSession.Active)
            {
                return false;
            }
            int slot = player.SlotIndex;
            if (slot < 0 || slot >= NetSession.SlotOccupied.Length)
            {
                return false;
            }
            // Spawning belongs to whoever simulates the match. On a client
            // that is not the authority every player is a puppet, this
            // machine's own included: picking a spawn point locally would
            // only be contradicted by the next snapshot, and choosing a
            // different one from the authority is what put each player
            // somewhere else on its own screen than on everyone else's.
            // NetPlayerBridge spawns them when the authority says so.
            if (!NetSession.IsHost && !NetSession.IsAuthority)
            {
                return false;
            }
            return slot == NetSession.LocalSlot || NetSession.SlotOccupied[slot];
        }

        /// <summary>
        /// After input is sampled. Publishes what the local player wants and
        /// keeps the scene's roster in step with the server's.
        /// </summary>
        public static void AfterInput(Scene scene)
        {
            if (!NetSession.Active)
            {
                return;
            }
            // Before the rotation is acted on: a match that has just been won
            // has to be reported before the server can be expected to have
            // rotated because of it.
            NetMatchEnd.Sync();
            // Before anything else this frame: if the server has rotated, the
            // slots and the room this code is about to reason over are the
            // ones being replaced.
            NetRoomChange.Sync(scene);
            NetDiagnostics.Report(NetSession.NetFrame / 60.0);
            // Runs once, as soon as the server has assigned a slot: strips the
            // AI that Scene.AddPlayer attaches to every player after the first.
            NetPlayerSetup.ApplyOnce();
            // The server owns the match clock; adopt it every frame so a
            // joiner shows the running round's time instead of its own.
            NetMatchSync.Apply();
            // Peers join and leave mid-match; bring the scene's active slots
            // in line with the server's roster every frame.
            NetSlotManager.Sync();
            NetLog.Snapshot(NetSession.NetFrame / 60.0, scene);
            if (NetSession.IsAuthority && NetSession.ConsumeAuthorityStateSync())
            {
                ApplyRemoteStates();
            }
            if (NetSession.LocalSlot < 0 || !NetSession.IsClient)
            {
                return;
            }
            int local = NetSession.LocalSlot;
            PlayerEntity? player = local < PlayerEntity.Players.Count
                ? PlayerEntity.Players[local]
                : null;
            if (player != null && player.LoadFlags.TestFlag(LoadFlags.Active))
            {
                // Between the input step and the intent capture, so a
                // scripted player's keys reach both the local simulation and
                // the wire -- the same order a person's keys travel in.
                NetTestScript.Apply(player);
                // Edges every frame, packets every other one. With N players
                // the server relays N*(N-1) updates per frame, and at six
                // players that was losing enough of them to leave visible
                // gaps in everyone's position stream.
                NetPlayerBridge.RecordPresses(player);
                if (NetSession.NetFrame % NetConfig.IntentSendInterval == 0)
                {
                    NetSession.SendIntent(NetPlayerBridge.CaptureIntent(player));
                }
            }
            else
            {
                // Send an empty intent anyway. The server drops a peer
                // that has been silent for TimeoutSeconds, and a client
                // whose player is not active yet -- still loading, or in
                // a slot the local scene has not populated -- would
                // otherwise be disconnected while it is perfectly healthy.
                NetSession.SendIntent(default);
            }
        }

        /// <summary>
        /// After the scene has stepped: the authority publishes what it just
        /// simulated, everyone else adopts what the authority published.
        ///
        /// Order matters both ways. Broadcasting before the step would send
        /// last frame's positions; applying before it would let the local
        /// simulation immediately overwrite the state that had just been
        /// received, which is what left remote players twitching at spawn
        /// instead of moving.
        /// </summary>
        public static void AfterSimulation()
        {
            if (!NetSession.Active)
            {
                return;
            }
            // Before publishing or applying anything: a vector that has
            // stopped being a number spreads from one player to every client
            // and back, and the only cheap moment to stop it is here.
            for (int i = 0; i < PlayerEntity.Players.Count; i++)
            {
                PlayerEntity player = PlayerEntity.Players[i];
                if (player.LoadFlags.TestFlag(LoadFlags.Active))
                {
                    player.ModRecordNetworkPosition(NetSession.NetFrame);
                    player.ModRepairVectors();
                }
            }
            // Also for a client the dedicated server designated as authority:
            // on such a server nobody is NetRole.Host, so gating on IsHost
            // alone means no snapshot is ever published.
            if (NetSession.IsHost || NetSession.IsAuthority)
            {
                NetSession.BroadcastSnapshot();
            }
            else if (NetSession.IsClient)
            {
                ApplyRemoteStates();
            }
        }

        /// <summary>
        /// Adopt the authority's world, this machine's own player included.
        ///
        /// Including the local player is the part that was missing. The
        /// authority simulates every slot from relayed intent and publishes
        /// the result; a client that kept its own answer for its own slot
        /// therefore stood in one place on its screen and somewhere else on
        /// everyone else's, and the gap grew for as long as the round ran.
        /// Facing is the exception -- aim has to follow the mouse now, not
        /// after a round trip -- so only position, speed and health are taken.
        /// </summary>
        private static void ApplyRemoteStates()
        {
            for (int i = 0; i < PlayerEntity.Players.Count; i++)
            {
                if (!NetSession.RemoteStateValid[i])
                {
                    continue;
                }
                PlayerEntity player = PlayerEntity.Players[i];
                if (player.LoadFlags.TestFlag(LoadFlags.Active))
                {
                    NetPlayerBridge.ApplyState(player, NetSession.RemoteStates[i],
                        isLocal: i == NetSession.LocalSlot);
                }
            }
            NetSession.NoteStatesApplied();
        }
    }
}
