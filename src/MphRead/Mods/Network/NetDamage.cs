using System;
using MphRead.Entities;
using OpenTK.Mathematics;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// Carries hits from the machine that resolved them to the machines that
    /// have to show them.
    ///
    /// Health alone is not a hit. Everything a player feels when they are
    /// shot -- the directional indicator, the damage animation, the grunt,
    /// the knockback, the "X KILLED YOU" banner -- is produced inside
    /// PlayerEntity.TakeDamage, and a client whose health was simply assigned
    /// from a snapshot ran none of it: the bar dropped in silence. So the
    /// authority records what it resolved, the snapshot carries it, and each
    /// client replays it through TakeDamage, which is the only way to get
    /// the engine's own feedback rather than an imitation of it.
    ///
    /// The counter is what makes the replay safe: snapshots repeat and can
    /// arrive out of order, and comparing health would replay one hit several
    /// times or miss two that cancelled out.
    /// </summary>
    public static class NetDamage
    {
        private const int Slots = PlayerEntity.SlotCapacity;

        private static readonly byte[] _sequence = new byte[Slots];
        private static readonly byte[] _attacker = new byte[Slots];
        private static readonly byte[] _beam = new byte[Slots];
        private static readonly byte[] _flags = new byte[Slots];
        private static readonly Vector3[] _direction = new Vector3[Slots];

        private static readonly byte[] _lastSeen = new byte[Slots];
        private static readonly bool[] _everSeen = new bool[Slots];

        public const byte NoSlot = 0xFF;
        public const byte NoBeam = 0xFF;

        /// <summary>
        /// Flags worth sending. The rest either describe how the damage was
        /// delivered locally (invulnerability handling) or would change the
        /// replay's outcome, which the authority has already decided.
        /// </summary>
        private const int RelayedFlags = (int)(DamageFlags.Headshot | DamageFlags.Deathalt
            | DamageFlags.Burn);

        /// <summary>
        /// True while a client is replaying the authority's hit, which is the
        /// one moment damage is allowed to land on a machine that does not
        /// own the simulation.
        /// </summary>
        public static bool Replaying { get; private set; }

        /// <summary>
        /// The weapon of the hit being replayed, for the kill banner. The
        /// beam entity only ever existed on the authority's machine, so
        /// without this the victim was told it had been killed by whatever
        /// the fallback branch guessed.
        /// </summary>
        public static BeamType ReplayBeam { get; private set; } = BeamType.None;

        /// <summary>
        /// Hits the authority resolved, and hits each client replayed, per
        /// slot. The two are the ends of the damage pipeline: if the first is
        /// zero the shot never connected anywhere, and if the second lags the
        /// first the hit was resolved and then failed to reach the victim.
        /// </summary>
        public static readonly int[] Resolved = new int[Slots];
        public static readonly int[] Replayed = new int[Slots];

        /// <summary>
        /// Beams each slot actually spawned on this machine.
        ///
        /// The missing third of the picture. Resolved says whether a hit
        /// landed and Replayed whether the victim was told, but neither can
        /// tell "the shot missed" from "the shot was never fired here at
        /// all" -- and on the authority, which is the only machine whose
        /// shots count, those are completely different faults. A puppet that
        /// holds fire on its owner's screen and spawns nothing here means the
        /// input never arrived; one that spawns plenty and resolves nothing
        /// means it is firing into the wrong place.
        /// </summary>
        public static readonly int[] Fired = new int[Slots];

        /// <summary>
        /// Total degrees between where a slot's shots went and where its gun
        /// was pointing, and the worst single case. UpdateAimVecs drags the
        /// shot half way from the aim towards the body's facing, so a puppet
        /// whose facing does not follow its aim fires beside itself.
        /// </summary>
        public static readonly double[] AimDrift = new double[Slots];
        public static readonly double[] WorstDrift = new double[Slots];

        /// <summary>Called wherever a beam is spawned, for <see cref="Fired"/>.</summary>
        public static void NoteFired(PlayerEntity shooter, Vector3 shotVec, Vector3 aimVec)
        {
            if (!NetSession.Active)
            {
                return;
            }
            int slot = shooter.SlotIndex;
            if (slot < 0 || slot >= Slots)
            {
                return;
            }
            Fired[slot]++;
            if (shotVec.LengthSquared > 0.0001f && aimVec.LengthSquared > 0.0001f)
            {
                float dot = Math.Clamp(Vector3.Dot(shotVec.Normalized(), aimVec.Normalized()), -1f, 1f);
                double degrees = Math.Acos(dot) * 180.0 / Math.PI;
                AimDrift[slot] += degrees;
                WorstDrift[slot] = Math.Max(WorstDrift[slot], degrees);
            }
        }

        /// <summary>
        /// The most hits one snapshot may report as new. Generous next to
        /// anything a real fight produces between two frames, and far below
        /// the wrap that a regressed counter looks like.
        /// </summary>
        private const byte MaxCatchUp = 32;

        public static void Reset()
        {
            Array.Clear(_sequence);
            Array.Clear(_attacker);
            Array.Clear(_beam);
            Array.Clear(_flags);
            Array.Clear(_direction);
            Array.Clear(_lastSeen);
            Array.Clear(_everSeen);
            Array.Clear(Resolved);
            Array.Clear(Replayed);
            Array.Clear(Fired);
            Array.Clear(AimDrift);
            Array.Clear(WorstDrift);
            Replaying = false;
            ReplayBeam = BeamType.None;
        }

        /// <summary>
        /// Whether damage resolved on this machine should be thrown away.
        ///
        /// On a client that is not the authority, every player is a puppet
        /// and its beams are a local echo: the authority already decided
        /// whether that shot connected. Letting the echo land too gave the
        /// same hit twice on the scoreboard and produced kills that never
        /// happened anywhere else.
        /// </summary>
        public static bool Suppress(PlayerEntity victim)
        {
            if (!NetSession.Active || Replaying)
            {
                return false;
            }
            return !NetSession.IsHost && !NetSession.IsAuthority;
        }

        /// <summary>Called by the authority for every hit it resolves.</summary>
        public static void Note(PlayerEntity victim, PlayerEntity? attacker, BeamType beam,
            DamageFlags flags, Vector3? direction)
        {
            if (!NetSession.Active || Replaying)
            {
                return;
            }
            int slot = victim.SlotIndex;
            if (slot < 0 || slot >= Slots)
            {
                return;
            }
            _sequence[slot]++;
            Resolved[slot]++;
            _attacker[slot] = attacker != null && attacker.SlotIndex >= 0 && attacker.SlotIndex < Slots
                ? (byte)attacker.SlotIndex
                : NoSlot;
            _beam[slot] = beam == BeamType.None ? NoBeam : (byte)beam;
            _flags[slot] = (byte)((int)flags & RelayedFlags);
            // The impulse the engine applied, verbatim -- not a vector between
            // two players.
            //
            // TakeDamage adds `direction` straight onto Speed, so whatever
            // travels here is a velocity in units per frame. A beam supplies
            // one that GetDamageDirection built from a unit vector and the
            // weapon's own magnitude, which is a fraction of a unit; the
            // difference between two players' positions is the distance
            // between them, which at ten metres apart launched the victim at
            // ten units a frame and put them through the wall. That was the
            // "hits send people flying off the map" bug, and it was
            // asymmetric because the authority applies its own damage
            // directly and only ever replayed everyone else's.
            //
            // Zero is a real answer and is kept as one: most beams carry
            // DamageDirType 0 and knock nobody back. The receiver turns it
            // into a null direction, which is what makes TakeDamage fall back
            // to the attacker's position for the damage indicator -- for the
            // indicator only, exactly as it does for a local hit.
            _direction[slot] = ClampImpulse(direction ?? Vector3.Zero);
        }

        /// <summary>
        /// The largest knockback a hit is allowed to carry over the wire.
        ///
        /// Every impulse the weapon tables produce is well under this; the
        /// cap is here so that a value which is not one of those -- a future
        /// damage source, a corrupted read -- costs the victim a shove rather
        /// than the match.
        /// </summary>
        private const float MaxImpulse = 1.5f;

        private static Vector3 ClampImpulse(Vector3 impulse)
        {
            if (!Single.IsFinite(impulse.X) || !Single.IsFinite(impulse.Y)
                || !Single.IsFinite(impulse.Z))
            {
                return Vector3.Zero;
            }
            float length = impulse.Length;
            if (length <= MaxImpulse)
            {
                return impulse;
            }
            NetLog.Event($"knockback clamped from {length:0.##} to {MaxImpulse}");
            return impulse * (MaxImpulse / length);
        }

        /// <summary>Fill a snapshot entry for one slot.</summary>
        public static void Write(int slot, ref PlayerState state)
        {
            if (slot < 0 || slot >= Slots)
            {
                return;
            }
            state.DamageSeq = _sequence[slot];
            state.AttackerSlot = _attacker[slot];
            state.DamageBeam = _beam[slot];
            state.DamageFlags = _flags[slot];
            state.HitDirection = _direction[slot];
        }

        /// <summary>
        /// Replay a hit the authority resolved, if this snapshot carries one
        /// this machine has not shown yet.
        ///
        /// The first snapshot for a slot only records where the counter
        /// stands: a client joining a match in progress would otherwise open
        /// with a burst of damage for every hit landed before it arrived.
        /// </summary>
        public static void Replay(PlayerEntity player, in PlayerState state)
        {
            int slot = player.SlotIndex;
            if (slot < 0 || slot >= Slots)
            {
                return;
            }
            if (!_everSeen[slot])
            {
                _everSeen[slot] = true;
                _lastSeen[slot] = state.DamageSeq;
                return;
            }
            // How many hits happened since this client last looked, not
            // whether any did. Treating the counter as a change flag meant
            // two hits landing between two received snapshots showed as one,
            // and under fire or packet loss a sixth of them vanished --
            // "my shots are not registering", from the shooter's side.
            byte landed = (byte)(state.DamageSeq - _lastSeen[slot]);
            if (landed == 0)
            {
                return;
            }
            _lastSeen[slot] = state.DamageSeq;
            if (landed > MaxCatchUp)
            {
                // Not a burst of fire: the counter is a byte, so a sequence
                // that has gone *backwards* reads as almost a full wrap
                // forwards. Nothing lands two hundred hits between two
                // snapshots, so this is a straggler or a counter that was
                // reset underneath us. Take the new value as the truth and
                // show nothing -- replaying it would flinch the player, shove
                // them, and, if the stale snapshot happened to say zero
                // health, kill them for a hit that had already been shown.
                NetLog.Event($"slot {slot} damage sequence jumped {landed}; resynced");
                return;
            }
            // The feedback runs once even for several hits: the engine's
            // damage path applies knockback and an indicator, and stacking
            // those in a single frame would look worse than the hit it is
            // reporting. The health that ends up on screen is the
            // authority's, which already accounts for every one of them.
            Replayed[slot] += landed;
            if (player.Health <= 0)
            {
                return; // already down here; the respawn is what matters next
            }
            PlayerEntity? attacker = state.AttackerSlot < PlayerEntity.Players.Count
                ? PlayerEntity.Players[state.AttackerSlot]
                : null;
            bool lethal = state.Health == 0;
            // Never let the replay decide the outcome: the authority already
            // has. A non-fatal hit is clamped so local rounding cannot kill,
            // and a fatal one carries the Death flag so it cannot fail to.
            int amount = Math.Max(1, player.Health - state.Health);
            if (!lethal)
            {
                amount = Math.Min(amount, Math.Max(1, player.Health - 1));
            }
            DamageFlags flags = (DamageFlags)state.DamageFlags | DamageFlags.NoDmgInvuln;
            if (lethal)
            {
                flags |= DamageFlags.Death;
            }
            // Clamped again on arrival: what a peer sends is not something
            // this machine controls, and one bad impulse is the difference
            // between a hit and a player outside the level.
            Vector3 impulse = ClampImpulse(state.HitDirection);
            Vector3? direction = impulse == Vector3.Zero ? null : impulse;
            Replaying = true;
            ReplayBeam = state.DamageBeam == NoBeam ? BeamType.None : (BeamType)state.DamageBeam;
            try
            {
                player.TakeDamage((uint)amount, flags, direction, attacker);
            }
            finally
            {
                Replaying = false;
                ReplayBeam = BeamType.None;
            }
        }
    }
}
