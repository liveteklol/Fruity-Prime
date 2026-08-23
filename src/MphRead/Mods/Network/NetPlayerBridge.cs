using System;
using MphRead.Entities;
using OpenTK.Mathematics;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// Translates between MphRead's player state and the wire format.
    ///
    /// The injection design leans on something the project already does:
    /// PlayerAi.ProcessInput() drives bots by writing into player.Controls,
    /// the exact surface the keyboard writes into. A remote player is
    /// therefore just a third writer of that same surface -- no new input
    /// path, no engine change.
    /// </summary>
    public static class NetPlayerBridge
    {
        /// <summary>
        /// How long a form disagreement is tolerated before it is forced.
        /// Longer than the morph animation, so a transition that is simply
        /// playing out is never cut short -- that was what removed the
        /// morph-in animation entirely.
        /// </summary>
        private const int FormGraceFrames = 90;
        private static readonly int[] _formMismatch = new int[PlayerEntity.SlotCapacity];

        /// <summary>
        /// Whether the last snapshot had each slot standing on the map, so the
        /// frame the authority places somebody can be told from the thousands
        /// of frames afterwards on which it merely still has them placed.
        /// </summary>
        private static readonly bool[] _authoritySpawned = new bool[PlayerEntity.SlotCapacity];

        /// <summary>
        /// Beyond this a remote player is placed outright, not eased. Well
        /// past anything a lost burst of updates can account for, so what is
        /// left is a respawn or a teleporter -- where a jump is correct.
        /// </summary>
        private const float SnapDistance = 15f;
        /// <summary>How much of the remaining gap a remote player closes each frame.</summary>
        private const float CatchUpRate = 0.35f;
        /// <summary>Closed faster when the gap is wide, so catching up is not slow motion.</summary>
        private const float FastCatchUpRate = 0.6f;
        private const float FastCatchUpAbove = 3f;
        private static readonly int[] _formAttempts = new int[PlayerEntity.SlotCapacity];

        /// <summary>
        /// How many updates were thrown away for holding a value that is not
        /// a number, or one no room could contain.
        ///
        /// One of these is enough to ruin a match for everybody: a NaN
        /// position is written into a player, spreads to whoever aims at it,
        /// and is then published as authoritative. The player stops moving,
        /// dies repeatedly, and every measurement of it reads NaN. Dropping
        /// the update keeps the last good value instead, which is wrong for
        /// one frame rather than permanently.
        /// </summary>
        public static long RejectedUpdates { get; private set; }

        /// <summary>
        /// Times a remote player had to be placed rather than eased, and the
        /// worst of them. This is the teleport a player actually sees: the
        /// smoothed catch-up is invisible, a snap is not.
        /// </summary>
        public static long Snaps { get; private set; }
        public static float WorstSnap { get; private set; }

        /// <summary>
        /// How far this machine's own player may be from the authority's copy
        /// of it before it is pulled back.
        ///
        /// Wide on purpose. The authority's copy is this client's own report
        /// from a round trip ago, so under boost across a bad line the two
        /// are several units apart while nothing at all is wrong, and a
        /// threshold tight enough to call that a desync is a threshold that
        /// fires constantly. This is here for corruption, not for latency.
        /// </summary>
        private const float DesyncDistance = 30f;

        /// <summary>
        /// The fastest a puppet may be said to be travelling, in units per
        /// frame. Boost -- the quickest a hunter moves under its own power --
        /// caps at 0.6, so this is eight times anything legitimate and exists
        /// only to stop a derived velocity from becoming a launch.
        /// </summary>
        private const float MaxReportedSpeed = 5f;

        /// <summary>Positions beyond this are not a level, they are corruption.</summary>
        private const float PositionLimit = 100000f;

        private static bool Sane(Vector3 value)
        {
            return Single.IsFinite(value.X) && Single.IsFinite(value.Y) && Single.IsFinite(value.Z)
                && MathF.Abs(value.X) < PositionLimit && MathF.Abs(value.Y) < PositionLimit
                && MathF.Abs(value.Z) < PositionLimit;
        }

        /// <summary>
        /// Rising edges from the last few frames, newest first, so a
        /// one-frame press survives a lost packet. See IntentPacket.Presses.
        /// </summary>
        private static readonly uint[] _pressHistory = new uint[IntentPacket.PressHistory];

        /// <summary>
        /// Record this frame's rising edges, whether or not a packet goes out
        /// this frame.
        ///
        /// Separate from building the packet because the two happen at
        /// different rates: edges have to be caught every frame -- a one-frame
        /// press exists only on the frame it happens -- while packets are sent
        /// less often to keep the relay from drowning. Folding this into the
        /// packet build meant a slower send rate silently dropped half of all
        /// morphs and weapon switches.
        /// </summary>
        public static void RecordPresses(PlayerEntity player)
        {
            PlayerControls c = player.Controls;
            IntentButtons pressed = IntentButtons.None;
            if (c.MoveLeft.IsPressed) pressed |= IntentButtons.MoveLeft;
            if (c.MoveRight.IsPressed) pressed |= IntentButtons.MoveRight;
            if (c.MoveUp.IsPressed) pressed |= IntentButtons.MoveUp;
            if (c.MoveDown.IsPressed) pressed |= IntentButtons.MoveDown;
            if (c.Shoot.IsPressed) pressed |= IntentButtons.Shoot;
            if (c.Zoom.IsPressed) pressed |= IntentButtons.Zoom;
            if (c.Jump.IsPressed) pressed |= IntentButtons.Jump;
            if (c.Morph.IsPressed) pressed |= IntentButtons.Morph;
            if (c.Boost.IsPressed) pressed |= IntentButtons.Boost;
            if (c.AltAttack.IsPressed) pressed |= IntentButtons.AltAttack;
            if (c.ScanVisor.IsPressed) pressed |= IntentButtons.ScanVisor;
            if (c.NextWeapon.IsPressed) pressed |= IntentButtons.NextWeapon;
            if (c.PrevWeapon.IsPressed) pressed |= IntentButtons.PrevWeapon;
            if (c.RolltLeft.IsPressed) pressed |= IntentButtons.RollLeft;
            if (c.RollRight.IsPressed) pressed |= IntentButtons.RollRight;
            if (c.RollUp.IsPressed) pressed |= IntentButtons.RollUp;
            if (c.RollDown.IsPressed) pressed |= IntentButtons.RollDown;
            for (int i = _pressHistory.Length - 1; i > 0; i--)
            {
                _pressHistory[i] = _pressHistory[i - 1];
            }
            _pressHistory[0] = (uint)pressed;
        }

        /// <summary>Local player's controls and aim -> wire intent (client side).</summary>
        public static IntentPacket CaptureIntent(PlayerEntity player)
        {
            PlayerControls c = player.Controls;
            IntentButtons buttons = IntentButtons.None;
            if (c.MoveLeft.IsDown) buttons |= IntentButtons.MoveLeft;
            if (c.MoveRight.IsDown) buttons |= IntentButtons.MoveRight;
            if (c.MoveUp.IsDown) buttons |= IntentButtons.MoveUp;
            if (c.MoveDown.IsDown) buttons |= IntentButtons.MoveDown;
            if (c.Shoot.IsDown) buttons |= IntentButtons.Shoot;
            if (c.Zoom.IsDown) buttons |= IntentButtons.Zoom;
            if (c.Jump.IsDown) buttons |= IntentButtons.Jump;
            if (c.Morph.IsDown) buttons |= IntentButtons.Morph;
            if (c.Boost.IsDown) buttons |= IntentButtons.Boost;
            if (c.AltAttack.IsDown) buttons |= IntentButtons.AltAttack;
            if (c.ScanVisor.IsDown) buttons |= IntentButtons.ScanVisor;
            if (c.NextWeapon.IsDown) buttons |= IntentButtons.NextWeapon;
            if (c.PrevWeapon.IsDown) buttons |= IntentButtons.PrevWeapon;
            if (c.RolltLeft.IsDown) buttons |= IntentButtons.RollLeft;
            if (c.RollRight.IsDown) buttons |= IntentButtons.RollRight;
            if (c.RollUp.IsDown) buttons |= IntentButtons.RollUp;
            if (c.RollDown.IsDown) buttons |= IntentButtons.RollDown;
            return new IntentPacket
            {
                Buttons = buttons,
                Aim = player.ModGunVector,
                Position = player.Position,
                // The owner's own weapon, every frame. The authority never
                // receives snapshots, so without this it showed a remote
                // player holding whatever a relayed NextWeapon press happened
                // to select from the weapons *it* believed that player had --
                // and availability comes from pickups, which are not shared.
                WeaponSelect = (byte)player.CurrentWeapon,
                // The owner's own count. Everyone simulates this player's
                // shots and spends the ammo; only the owner walks over the
                // pickups that refill it, so every other machine's copy runs
                // down and eventually refuses to spawn a beam at all.
                AmmoUa = (ushort)Math.Clamp(player.ModAmmo.Ua, 0, UInt16.MaxValue),
                AmmoMissiles = (ushort)Math.Clamp(player.ModAmmo.Missiles, 0, UInt16.MaxValue),
                Presses = (uint[])_pressHistory.Clone()
            };
        }

        /// <summary>
        /// Wire intent -> a remote player's controls (authority side). Mirrors
        /// how the keyboard path derives IsPressed/IsReleased from the
        /// previous frame, so gameplay code that tests those edges behaves
        /// the same for a remote player as for a local one.
        /// </summary>
        /// <summary>Newest press frame already applied, per slot.</summary>
        private static readonly uint[] _lastPressFrame = new uint[PlayerEntity.SlotCapacity];
        private static readonly bool[] _pressSeen = new bool[PlayerEntity.SlotCapacity];

        public static void ApplyIntent(PlayerEntity player, in IntentPacket intent)
        {
            if (!Sane(intent.Aim))
            {
                RejectedUpdates++;
                NetLog.Event($"slot {player.SlotIndex} intent rejected: aim={intent.Aim}");
                return;
            }
            PlayerControls c = player.Controls;
            IntentButtons missed = MissedPresses(player.SlotIndex, intent);
            Set(c.MoveLeft, intent.Buttons.HasFlag(IntentButtons.MoveLeft), missed.HasFlag(IntentButtons.MoveLeft));
            Set(c.MoveRight, intent.Buttons.HasFlag(IntentButtons.MoveRight), missed.HasFlag(IntentButtons.MoveRight));
            Set(c.MoveUp, intent.Buttons.HasFlag(IntentButtons.MoveUp), missed.HasFlag(IntentButtons.MoveUp));
            Set(c.MoveDown, intent.Buttons.HasFlag(IntentButtons.MoveDown), missed.HasFlag(IntentButtons.MoveDown));
            Set(c.Shoot, intent.Buttons.HasFlag(IntentButtons.Shoot), missed.HasFlag(IntentButtons.Shoot));
            Set(c.Zoom, intent.Buttons.HasFlag(IntentButtons.Zoom), missed.HasFlag(IntentButtons.Zoom));
            Set(c.Jump, intent.Buttons.HasFlag(IntentButtons.Jump), missed.HasFlag(IntentButtons.Jump));
            Set(c.Morph, intent.Buttons.HasFlag(IntentButtons.Morph), missed.HasFlag(IntentButtons.Morph));
            if (c.Morph.IsPressed)
            {
                NetLog.Event($"slot {player.SlotIndex} morph press received, now {player.ModFormState()}");
            }
            Set(c.Boost, intent.Buttons.HasFlag(IntentButtons.Boost), missed.HasFlag(IntentButtons.Boost));
            Set(c.AltAttack, intent.Buttons.HasFlag(IntentButtons.AltAttack), missed.HasFlag(IntentButtons.AltAttack));
            Set(c.ScanVisor, intent.Buttons.HasFlag(IntentButtons.ScanVisor), missed.HasFlag(IntentButtons.ScanVisor));
            Set(c.NextWeapon, intent.Buttons.HasFlag(IntentButtons.NextWeapon), missed.HasFlag(IntentButtons.NextWeapon));
            Set(c.PrevWeapon, intent.Buttons.HasFlag(IntentButtons.PrevWeapon), missed.HasFlag(IntentButtons.PrevWeapon));
            Set(c.RolltLeft, intent.Buttons.HasFlag(IntentButtons.RollLeft), missed.HasFlag(IntentButtons.RollLeft));
            Set(c.RollRight, intent.Buttons.HasFlag(IntentButtons.RollRight), missed.HasFlag(IntentButtons.RollRight));
            Set(c.RollUp, intent.Buttons.HasFlag(IntentButtons.RollUp), missed.HasFlag(IntentButtons.RollUp));
            Set(c.RollDown, intent.Buttons.HasFlag(IntentButtons.RollDown), missed.HasFlag(IntentButtons.RollDown));
            if (NetSession.IsAuthority)
            {
                // The owner already simulated movement and sent its resulting
                // position. Re-simulating movement here lets local collision
                // resolution move the authoritative hitbox a second time.
                Set(c.MoveLeft, false);
                Set(c.MoveRight, false);
                Set(c.MoveUp, false);
                Set(c.MoveDown, false);
                Set(c.Jump, false);
                Set(c.Boost, false);
                Set(c.RolltLeft, false);
                Set(c.RollRight, false);
                Set(c.RollUp, false);
                Set(c.RollDown, false);
            }
            if (intent.WeaponSelect != 0xFF)
            {
                player.ModSetWeapon((BeamType)intent.WeaponSelect);
            }
            player.ModSetAmmo(intent.AmmoUa, intent.AmmoMissiles);
        }

        /// <summary>
        /// Rising edges this packet carries that this slot has not applied
        /// yet, taken from the packet's short history of them.
        ///
        /// Without this, an edge existed only in the single packet whose
        /// frame it fell on, and losing that packet lost the action outright.
        /// The frame each entry belongs to is what stops a press being
        /// applied twice when the redundant copies arrive.
        /// </summary>
        private static IntentButtons MissedPresses(int slot, in IntentPacket intent)
        {
            if (slot < 0 || slot >= _lastPressFrame.Length || intent.Presses == null)
            {
                return IntentButtons.None;
            }
            if (!_pressSeen[slot])
            {
                // First packet from this peer: note where their frame counter
                // stands and replay nothing. The history reaches back several
                // frames, and applying all of it would open with a burst of
                // presses from before this client was listening.
                _pressSeen[slot] = true;
                _lastPressFrame[slot] = intent.Frame;
                return IntentButtons.None;
            }
            IntentButtons missed = IntentButtons.None;
            for (int i = intent.Presses.Length - 1; i >= 0; i--)
            {
                if (intent.Frame < (uint)i)
                {
                    continue;
                }
                uint frame = intent.Frame - (uint)i;
                if (frame <= _lastPressFrame[slot])
                {
                    continue;
                }
                missed |= (IntentButtons)intent.Presses[i];
            }
            // Every frame up to this packet is now accounted for, whether or
            // not it carried a press. Leaving gaps here let the same frame be
            // consumed again by a later packet.
            _lastPressFrame[slot] = Math.Max(_lastPressFrame[slot], intent.Frame);
            return missed;
        }

        /// <summary>
        /// Drive one control from a relayed intent.
        ///
        /// The held state comes from the packet's button levels, but the
        /// rising edge comes only from the press history -- never from the
        /// level as well. Deriving it from both applied the same press twice:
        /// once when the level went down, once when the redundant copy
        /// arrived. For a toggle like morph, twice is the same as never, and
        /// the puppet ended up one transition behind its owner for the rest
        /// of the match -- drawn as a biped while morphed, and as a morph
        /// ball while walking.
        /// </summary>
        private static void Set(Keybind bind, bool down, bool pressed = false)
        {
            bool wasDown = bind.IsDown;
            bind.IsDown = down || pressed;
            bind.IsPressed = pressed;
            bind.IsReleased = !down && wasDown && !pressed;
        }

        /// <summary>
        /// Authoritative state -> a player, on a client that is not the
        /// authority.
        ///
        /// Snapping, not interpolating: correctness first. Smoothing belongs
        /// on top of a working baseline, not underneath one -- interpolating
        /// before the plain path is proven only hides where the two sides
        /// disagree.
        ///
        /// The cases are deliberately different. Somebody else's player is a
        /// puppet and takes everything, including the spawn itself, because
        /// Spawn() is what unhides the model. This machine's own player takes
        /// its spawn, its death and its health from the authority too -- those
        /// are the match, and a client that decided them for itself was
        /// playing a different one -- but keeps its facing, because aim has to
        /// answer the mouse now rather than after a round trip, and keeps its
        /// own position -- see the isLocal branch, and
        /// <see cref="DesyncDistance"/> for the one case that overrides it.
        /// </summary>
        public static void ApplyState(PlayerEntity player, in PlayerState state, bool isLocal)
        {
            if (!Sane(state.Position) || !Sane(state.Speed) || !Sane(state.Facing))
            {
                RejectedUpdates++;
                NetLog.Event($"slot {player.SlotIndex} snapshot rejected: "
                    + $"pos={state.Position} speed={state.Speed} facing={state.Facing}");
                return;
            }
            bool spawned = (state.Flags & PlayerState.FlagSpawned) != 0;
            bool wasInPlay = player.LoadFlags.TestFlag(LoadFlags.Spawned) && player.Health > 0;
            int slot = player.SlotIndex;
            // The frame the authority put this player back on the map.
            bool justPlaced = spawned && slot >= 0 && slot < _authoritySpawned.Length
                && !_authoritySpawned[slot];
            if (slot >= 0 && slot < _authoritySpawned.Length)
            {
                _authoritySpawned[slot] = spawned;
            }
            // The authority keeps the score for everybody, including for this
            // client's own player. Counting locally worked only for whoever
            // had been present since the first kill.
            if (slot >= 0 && slot < GameState.Points.Length)
            {
                GameState.Points[slot] = state.Points;
                GameState.Kills[slot] = state.Kills;
                GameState.Deaths[slot] = state.Deaths;
            }
            // Before health is reconciled, because the engine's damage
            // feedback is produced by the hit rather than by the number: a
            // client that only assigned the new health showed a bar dropping
            // in silence, with no indicator, no animation and no kill banner.
            NetDamage.Replay(player, state);
            if (!spawned)
            {
                // Waiting to be placed, or just killed. Health is the whole
                // point of this branch: it is how a client learns that it
                // died, and skipping it left a player who had been killed on
                // every other screen still walking around on its own.
                if (wasInPlay && state.Health == 0 && player.Health > 0)
                {
                    // Killed by something that leaves no damage record: a
                    // fall, a kill plane, the match ending them. Assigning
                    // zero health would look right and count nothing, so the
                    // scoreboards drifted apart by exactly those deaths.
                    player.ModNetDie();
                }
                player.Health = state.Health;
                return;
            }
            if (!wasInPlay)
            {
                // The authority has this player on the map and this machine
                // does not. Spawn() rather than a position write: it is what
                // clears HideModel, so a player that skipped it tracked
                // perfectly while drawing nothing at all.
                player.ModNetSpawn(state.Position, state.Facing);
            }
            if (isLocal)
            {
                // Position and speed stay with the machine playing this
                // character; only the match -- health, score, spawn, death --
                // comes from the authority.
                //
                // Taking them from the snapshot closes a loop with no way
                // out. The authority does not simulate a remote player's
                // movement: it puts the puppet wherever the owner's last
                // intent said. So the position it publishes for this client
                // *is* this client's own report from a round trip ago, and
                // writing it back here means the next intent carries it
                // again unchanged. The two values agree forever, the
                // character is pinned to the spot the loop closed on, and
                // every frame of local movement is computed and thrown away
                // before it is ever published. That froze every client except
                // the authority -- 0 units travelled in seventy seconds, on
                // loopback as much as over the wire.
                //
                // A respawn or a death does not come through here: those are
                // the !wasInPlay and !spawned branches above. What is left is
                // a divergence no latency can explain, and only that is
                // taken.
                if (NetRoomChange.Settling)
                {
                    // Nothing about position means anything for the second
                    // after a room change: this client has loaded the new
                    // room and the authority may not have, so its snapshot is
                    // still describing where everybody stood in the old one.
                    // Taking it drags this player to whatever those
                    // coordinates land on here, the local simulation walks
                    // back, and the two alternate -- measured at a six-player
                    // rotation, twenty-six corrections in four seconds
                    // between two fixed points a room apart.
                    //
                    // NoteRoomChanged has cleared the placement record, so
                    // the first snapshot after this window that says this
                    // player is spawned counts as a fresh placement and puts
                    // it where the authority wants it.
                }
                else if (justPlaced)
                {
                    // Except at a respawn, which is the one moment the
                    // authority owns this player's position outright.
                    //
                    // `GetRespawnPoint` chooses from the spawn points that are
                    // free of living players *on the machine running it*, and
                    // rotates its choice with the frame counter, so two
                    // machines running it a few frames apart do not pick the
                    // same one. The local player also respawns early by
                    // holding fire, so it makes that choice well before the
                    // authority makes its own. Both then believe they know
                    // where this player is standing, half a level apart, and
                    // nothing brings them back together: the client publishes
                    // its own spot in every intent and the authority replies
                    // with the other one, for the rest of the life.
                    //
                    // Fifty-five of these in a hundred seconds, at up to 175
                    // units. The old code hid it -- it overwrote this
                    // player's position from every snapshot, which froze it
                    // solid but did make the two agree.
                    //
                    // Spawning locally first is kept: it is what makes a
                    // respawn feel immediate rather than arrive a round trip
                    // later, and it is what still works if snapshots stall.
                    // Only the placement is handed over.
                    Move(player, state.Position);
                    // Not the authority's speed: a player that has just been
                    // put on a spawn point is standing still, and whatever the
                    // snapshot carries here was derived across the teleport
                    // that put it there.
                    player.Speed = Vector3.Zero;
                }
                else if ((state.Position - player.Position).LengthSquared > DesyncDistance * DesyncDistance)
                {
                    NetLog.Event($"slot {player.SlotIndex} pulled back to the authority "
                        + $"from {player.Position} to {state.Position}");
                    Move(player, state.Position);
                    player.Speed = state.Speed;
                }
                player.Health = state.Health;
                return;
            }
            Move(player, state.Position);
            player.Speed = state.Speed;
            player.Health = state.Health;
            player.ModSetFacing(state.Facing);
            player.ModSetWeapon((BeamType)state.CurrentWeapon);
            player.EquipInfo.Zoomed = (state.Flags & PlayerState.FlagZoomed) != 0;
            ApplyForm(player, (state.Flags & PlayerState.FlagAltForm) != 0);
        }

        /// <summary>
        /// Keep a remote player's form in step with the authority's, without
        /// stepping on the transition.
        ///
        /// The owner's relayed input drives the morph on every machine, so
        /// this is only a safety net for a transition that never happened at
        /// all -- a lost press, or a puppet that somehow stalled.
        ///
        /// It deliberately does nothing for a long while. A puppet acts on
        /// the press the moment it arrives, whereas the snapshot confirming
        /// it cannot come back until the authority has seen the press and
        /// published: for that round trip the puppet is *ahead* of the
        /// snapshot, not wrong. Treating that as a disagreement and
        /// "correcting" it made the puppet morph, unmorph and morph again on
        /// every single transition.
        /// </summary>
        private static void ApplyForm(PlayerEntity player, bool altForm)
        {
            int slot = player.SlotIndex;
            if (slot < 0 || slot >= _formMismatch.Length)
            {
                return;
            }
            if (player.IsAltForm == altForm)
            {
                _formMismatch[slot] = 0;
                _formAttempts[slot] = 0;
                return;
            }
            _formMismatch[slot]++;
            if (_formMismatch[slot] <= FormGraceFrames)
            {
                return;
            }
            _formMismatch[slot] = 0;
            // First the real transition, because that is what creates the
            // parts of a form that are separate entities -- Weavel's
            // halfturret exists only because EnterAltForm adds it, so a
            // client that skipped straight to the flag showed a Weavel in alt
            // form with no turret. Only if that does not take does the flag
            // get forced.
            if (_formAttempts[slot] == 0)
            {
                _formAttempts[slot] = 1;
                player.ModStartFormSwitch();
                return;
            }
            _formAttempts[slot] = 0;
            player.ModForceForm(altForm);
        }

        /// <summary>
        /// Forget where the authority had everybody standing, because it was
        /// in a different room. The next snapshot that reports a player
        /// spawned then counts as a placement rather than as a continuation,
        /// which is what re-seats everyone after a rotation.
        /// </summary>
        public static void NoteRoomChanged()
        {
            Array.Clear(_authoritySpawned);
            Array.Clear(_reportSeen);
        }

        public static void Reset()
        {
            Array.Clear(_formMismatch);
            Snaps = 0;
            WorstSnap = 0;
            Array.Clear(_formAttempts);
            Array.Clear(_lastPressFrame);
            Array.Clear(_pressSeen);
            Array.Clear(_pressHistory);
            Array.Clear(_authoritySpawned);
            Array.Clear(_lastReportPosition);
            Array.Clear(_lastReportFrame);
            Array.Clear(_reportSeen);
        }

        /// <summary>
        /// Put a remote player where its owner says it is.
        ///
        /// Called for every client, the authority included, so there is
        /// exactly one simulation of each player: the one on the machine
        /// whose keyboard is driving it. Everyone else follows.
        /// </summary>
        public static void ApplyReportedPosition(PlayerEntity player, in IntentPacket intent)
        {
            if (!Sane(intent.Position))
            {
                RejectedUpdates++;
                return;
            }
            if (intent.Position == Vector3.Zero)
            {
                return; // the owner has not spawned yet
            }
            NoteReportedVelocity(player, intent);
            Vector3 delta = intent.Position - player.Position;
            float distance = delta.Length;
            if (distance > SnapDistance)
            {
                // Too far to be movement: a respawn, a teleporter, or a long
                // gap in the packets. Snapping is right here -- gliding across
                // half the level would be worse than a jump.
                Snaps++;
                WorstSnap = Math.Max(WorstSnap, distance);
                Move(player, intent.Position);
                return;
            }
            // The owner also sends the aim that was calculated against this
            // position. Smoothing here leaves the authoritative hitbox behind
            // that aim under latency, so moving directly is required for
            // collision and rendering to agree.
            Move(player, intent.Position);
        }

        private static readonly Vector3[] _lastReportPosition = new Vector3[PlayerEntity.SlotCapacity];
        private static readonly uint[] _lastReportFrame = new uint[PlayerEntity.SlotCapacity];
        private static readonly bool[] _reportSeen = new bool[PlayerEntity.SlotCapacity];

        /// <summary>
        /// How fast a puppet is travelling, worked out from the positions its
        /// owner reported rather than from a simulation of it.
        ///
        /// Nothing else fills this in. The authority skips a remote player's
        /// movement step entirely -- the owner already ran it and sent the
        /// result -- so Speed would keep whatever it last held, and it was
        /// therefore forced to zero. But Speed is in the snapshot, so that
        /// zero became the authoritative velocity of every remote player on
        /// every screen: opponents slid around at a dead stop, and each
        /// client had its own speed cleared sixty times a second.
        ///
        /// The gap between two reports is what it is divided by, so this
        /// stays right when a packet goes missing and the next one covers
        /// four frames instead of two.
        /// </summary>
        private static void NoteReportedVelocity(PlayerEntity player, in IntentPacket intent)
        {
            int slot = player.SlotIndex;
            if (slot < 0 || slot >= _lastReportFrame.Length)
            {
                return;
            }
            if (_reportSeen[slot] && intent.Frame > _lastReportFrame[slot])
            {
                // Capped: a report that follows a long silence describes a
                // gap, not a frame of movement, and dividing by two hundred
                // is as wrong as dividing by one.
                uint elapsed = Math.Min(intent.Frame - _lastReportFrame[slot], 8);
                Vector3 travelled = intent.Position - _lastReportPosition[slot];
                float step = travelled.Length;
                if (!Sane(travelled) || step > SnapDistance)
                {
                    // Not movement: a respawn, a teleporter, or a gap in the
                    // packets. Dividing a jump across the level by two frames
                    // produces a velocity of a hundred and fifty units a
                    // frame, and that number does not stay here -- it goes
                    // into the snapshot as this player's authoritative speed,
                    // every client applies it to its puppet, and the owner
                    // takes it back at its next respawn and is launched out of
                    // the level. Measured before this guard: the authority
                    // held a player at Y=163 and climbing 35 units a frame.
                    player.Speed = Vector3.Zero;
                }
                else
                {
                    Vector3 speed = travelled / elapsed;
                    float magnitude = speed.Length;
                    // Belt and braces. Boost, the fastest a hunter moves, caps
                    // at 0.6 units a frame; anything near this ceiling is
                    // already not a hunter running.
                    if (magnitude > MaxReportedSpeed)
                    {
                        speed *= MaxReportedSpeed / magnitude;
                    }
                    player.Speed = speed;
                }
            }
            if (!_reportSeen[slot] || intent.Frame > _lastReportFrame[slot])
            {
                _reportSeen[slot] = true;
                _lastReportFrame[slot] = intent.Frame;
                _lastReportPosition[slot] = intent.Position;
            }
        }

        /// <summary>
        /// Move the player's room node along with it. NodeRef is what the
        /// renderer culls against (PlayerDraw: `IsMainPlayer ||
        /// IsVisible(NodeRef)`), and the engine normally advances it during
        /// simulation. Writing a position straight in skips that, so a remote
        /// player kept the node it spawned in and vanished -- or showed only
        /// a shadow -- as soon as the viewer was elsewhere.
        /// </summary>
        private static void Move(PlayerEntity player, Vector3 position)
        {
            Vector3 previous = player.Position;
            player.Position = position;
            // This runs after PlayerProcess has captured PrevPosition. Keep
            // the next collision sweep anchored to the corrected position;
            // otherwise the engine treats the network correction as player
            // movement and can push the puppet away from the hitbox.
            player.PrevPosition = position;
            player.ModRefreshNodeRef(previous);
        }
    }
}
