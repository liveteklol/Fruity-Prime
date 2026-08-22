using System;
using MphRead.Entities;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// What a scripted client is doing right now.
    ///
    /// Phases rather than one behaviour, because each one exercises a
    /// different part of what has to cross the wire, and a report that says
    /// only "they moved" cannot tell a working beam from a working morph.
    /// </summary>
    public enum TestPhase
    {
        Idle,
        Walk,
        Jump,
        Turn,
        Shoot,
        SwitchWeapons,
        Charge,
        MorphA,
        AltAttackA,
        MorphB,
        AltAttackB,
        Unmorph,
        Zoom,
        Afflict,
        Duel
    }

    /// <summary>
    /// A fixed tour of the game's features, run by both clients at once.
    ///
    /// Keyed to the server's match clock rather than to each client's own
    /// frame counter, so two processes that joined seconds apart are in the
    /// same phase at the same moment. That is what lets each side assert
    /// something about the other without any comparison step between them:
    /// during the morph phase both are morphing, so "I saw them in alt form"
    /// is a claim one client can check on its own.
    ///
    /// It writes into player.Controls, the same surface the keyboard and
    /// PlayerAi write into, so the engine cannot tell it apart from a person
    /// playing.
    /// </summary>
    public static class NetTestScript
    {
        /// <summary>
        /// Seconds per phase. Lowered by the map audit, which visits every
        /// room and cannot afford seventy seconds each.
        /// </summary>
        public static double PhaseSeconds { get; set; } = 5;

        private static readonly TestPhase[] _order =
        {
            TestPhase.Idle,
            TestPhase.Walk,
            TestPhase.Jump,
            TestPhase.Turn,
            TestPhase.Shoot,
            TestPhase.SwitchWeapons,
            TestPhase.Charge,
            TestPhase.MorphA,
            TestPhase.AltAttackA,
            TestPhase.MorphB,
            TestPhase.AltAttackB,
            TestPhase.Unmorph,
            TestPhase.Zoom,
            TestPhase.Afflict,
            TestPhase.Duel
        };

        /// <summary>How many phases one pass of the tour has.</summary>
        public static int PhaseCount => _order.Length;

        public static bool Enabled { get; set; }

        /// <summary>Degrees per frame, so a turn is a sweep rather than a snap.</summary>
        private const float TurnRate = 6f;
        /// <summary>Hold fire once the aim is this close, in degrees.</summary>
        private const float FiringCone = 6f;
        private const float PreferredRange = 4f;

        private static int _frame;
        private static int _stuckFrames;
        private static bool _stuckDirection;
        private static OpenTK.Mathematics.Vector3 _lastPosition;

        public static float AimDeltaX { get; private set; }
        public static float AimDeltaY { get; private set; }
        public static int FramesOnTarget { get; private set; }

        /// <summary>
        /// The phase every scripted client is in, from the server's clock.
        /// Falls back to the local frame counter only when no server state has
        /// arrived, where nothing is being compared anyway.
        /// </summary>
        public static TestPhase Phase
        {
            get
            {
                double elapsed = NetSession.ServerMatch?.TimeElapsed ?? (_frame / 60.0);
                int index = (int)(elapsed / PhaseSeconds) % _order.Length;
                return _order[index];
            }
        }

        public static void Reset()
        {
            _frame = 0;
            _stuckFrames = 0;
            _lastPosition = OpenTK.Mathematics.Vector3.Zero;
            AimDeltaX = 0;
            AimDeltaY = 0;
            FramesOnTarget = 0;
        }

        /// <summary>
        /// Drive one player with no session behind it, for the map audit.
        /// The slot decides the morph/shoot parity and the caller owns the
        /// frame counter, so several players can be driven in one frame.
        /// </summary>
        public static void ApplyOffline(PlayerEntity player, int slot, int frame)
        {
            _offlineSlot = slot;
            _frame = frame;
            Drive(player);
            _offlineSlot = -1;
        }

        private static int _offlineSlot = -1;

        /// <summary>
        /// Hold the trigger and nothing else. Used by the audit's affliction
        /// probe, which decides where the shot goes itself.
        /// </summary>
        public static void HoldFire(PlayerEntity player, bool down)
        {
            PlayerControls c = player.Controls;
            Clear(c);
            Hold(c.Shoot, down);
            Finish(c);
        }

        /// <summary>
        /// Stop driving a player, and ask it out of alt form if it is in one.
        ///
        /// Used by the map audit's world probe, which needs a biped standing
        /// still on a pad: several jump pads are flagged to ignore alt forms,
        /// and a report of "this pad does nothing" that only means "it was
        /// asked while the player was a morph ball" is worse than no report.
        /// </summary>
        public static void Rest(PlayerEntity player, bool wantBiped)
        {
            PlayerControls c = player.Controls;
            Clear(c);
            if (player.Health == 0)
            {
                // A dead player waits out its respawn timer unless it asks to
                // come back early by holding fire. Standing still through it
                // costs the audit's probes their whole window: they gave up on
                // a pad because the player they put on it was still dead.
                Hold(c.Shoot, true);
            }
            else if (wantBiped && player.IsAltForm && Settled(player))
            {
                Hold(c.Morph, true);
            }
            Finish(c);
        }

        public static void Apply(PlayerEntity player)
        {
            if (!Enabled)
            {
                return;
            }
            _frame++;
            Drive(player);
        }

        private static void Drive(PlayerEntity player)
        {
            PlayerControls c = player.Controls;
            Clear(c);
            PlayerEntity? target = FindTarget(player);
            bool onTarget = AimAt(player, target);
            TestPhase phase = Phase;
            switch (phase)
            {
                case TestPhase.Idle:
                    break;
                case TestPhase.Walk:
                    Square(c);
                    break;
                case TestPhase.Jump:
                    Square(c);
                    Hold(c.Jump, _frame % 45 < 3);
                    break;
                case TestPhase.Turn:
                    // Sweep on the spot: separates "the other player's facing
                    // reaches me" from "their position does".
                    AimDeltaX = 4f;
                    AimDeltaY = MathF.Sin(_frame / 40f) * 2f;
                    break;
                case TestPhase.Shoot:
                    Hold(c.Shoot, _frame % 30 < 20);
                    break;
                case TestPhase.SwitchWeapons:
                    // One press per half second, so a receiver can see the
                    // weapon change rather than a blur.
                    Hold(c.NextWeapon, _frame % 30 == 0);
                    Hold(c.Shoot, _frame % 30 > 10 && _frame % 30 < 25);
                    break;
                case TestPhase.Charge:
                    Hold(c.Shoot, true);
                    break;
                case TestPhase.MorphA:
                    // Half the players morph while the other half shoot at
                    // them, then the roles swap in MorphB. Both sides morphing
                    // at once would never answer the question that matters
                    // here -- whether a player in alt form can still be hit.
                    MorphOrShoot(player, c, morphing: Even);
                    break;
                case TestPhase.AltAttackA:
                    AltAttackOrShoot(c, attacking: Even, onTarget);
                    break;
                case TestPhase.MorphB:
                    MorphOrShoot(player, c, morphing: !Even);
                    break;
                case TestPhase.AltAttackB:
                    // The mirror of AltAttackA. Without it the odd slots never
                    // pressed alt attack at all, and the report blamed the
                    // network for a bomb that was never laid.
                    AltAttackOrShoot(c, attacking: !Even, onTarget);
                    break;
                case TestPhase.Unmorph:
                    Hold(c.Morph, Settled(player) && player.IsAltForm && _frame % 40 == 0);
                    Square(c);
                    break;
                case TestPhase.Zoom:
                    // Zoom belongs to particular weapons, so equip one first
                    // rather than holding a button that does nothing and
                    // reporting the feature as broken.
                    if (!player.ModCanZoom)
                    {
                        EquipZoomWeapon(player, c);
                    }
                    Hold(c.Zoom, player.ModCanZoom);
                    Hold(c.Shoot, player.ModCanZoom && _frame % 40 < 8);
                    break;
                case TestPhase.Afflict:
                    // Freeze, burn and disrupt are properties of a hunter's
                    // own weapon, so the phase begins by handing it over. Then
                    // it is an ordinary duel: the point is to land those shots
                    // on somebody, not to prove the gun exists.
                    player.ModArmAffinityWeapon();
                    Duel(player, c, target, onTarget);
                    break;
                case TestPhase.Duel:
                    Duel(player, c, target, onTarget);
                    break;
            }
            Finish(c);
        }

        /// <summary>Not in the middle of changing form.</summary>
        private static bool Settled(PlayerEntity player)
        {
            return !player.IsMorphing && !player.IsUnmorphing;
        }

        /// <summary>Whether the slot being driven is an even-numbered one.</summary>
        private static bool Even => (_offlineSlot >= 0
            ? _offlineSlot
            : Math.Max(NetSession.LocalSlot, 0)) % 2 == 0;

        private static void MorphOrShoot(PlayerEntity player, PlayerControls c, bool morphing)
        {
            if (morphing)
            {
                // Only while settled in the other form. Pressing again while
                // the transition is still playing is what a person would not
                // do, and it lands on a remote copy that may already be a
                // step further along -- morphing it straight back out.
                Hold(c.Morph, Settled(player) && !player.IsAltForm && _frame % 40 == 0);
                Square(c);
                Hold(c.Boost, _frame % 60 < 20);
                return;
            }
            // Unmorph first if the previous phase left us in alt form, then
            // shoot: a biped shooting a morph ball is the case being tested.
            Hold(c.Morph, Settled(player) && player.IsAltForm && _frame % 40 == 0);
            Hold(c.Shoot, !player.IsAltForm && _frame % 30 < 24);
        }

        private static void AltAttackOrShoot(PlayerControls c, bool attacking, bool onTarget)
        {
            if (attacking)
            {
                Square(c);
                Hold(c.AltAttack, _frame % 45 < 6);
                return;
            }
            Hold(c.Shoot, onTarget && _frame % 30 < 24);
        }

        private static void EquipZoomWeapon(PlayerEntity player, PlayerControls c)
        {
            if (player.ModHasWeapon(BeamType.Imperialist))
            {
                Hold(c.Imperialist, _frame % 30 == 0);
            }
            else if (player.ModHasWeapon(BeamType.Judicator))
            {
                Hold(c.Judicator, _frame % 30 == 0);
            }
        }

        /// <summary>
        /// Everything released, every frame, before the phase presses what it
        /// wants. A key left down by the previous phase would otherwise keep
        /// acting, and a morph that never ended would make the rest of the
        /// tour meaningless.
        /// </summary>
        /// <summary>
        /// Previous frame's button state, taken by <see cref="Clear"/> and
        /// used by <see cref="Finish"/>. Static and reused: the players are
        /// driven one after another on one thread, and each is bracketed by
        /// the pair.
        /// </summary>
        private static bool[] _wasDown = Array.Empty<bool>();

        /// <summary>
        /// Start a frame of input for this player: remember what was held,
        /// then let go of everything.
        ///
        /// The edges are worked out in <see cref="Finish"/> rather than here.
        /// They used to be computed inside every Hold call, and a phase that
        /// wrote a button twice in a frame -- once by this clear, once by
        /// setting it to the same value -- wiped the edge the first write had
        /// produced. A charged weapon fires when the trigger is *released*, so
        /// scripted players held the charge and never let it go: the charge
        /// phase fired nothing, and the three afflictions, which exist only on
        /// a charged shot, could not be reached at all.
        /// </summary>
        private static void Clear(PlayerControls c)
        {
            if (_wasDown.Length < c.All.Length)
            {
                _wasDown = new bool[c.All.Length];
            }
            for (int i = 0; i < c.All.Length; i++)
            {
                Keybind bind = c.All[i];
                _wasDown[i] = bind.IsDown;
                bind.IsDown = false;
                bind.IsPressed = false;
                bind.IsReleased = false;
            }
        }

        /// <summary>Work out this frame's press and release edges.</summary>
        private static void Finish(PlayerControls c)
        {
            for (int i = 0; i < c.All.Length && i < _wasDown.Length; i++)
            {
                Keybind bind = c.All[i];
                bind.IsPressed = bind.IsDown && !_wasDown[i];
                bind.IsReleased = !bind.IsDown && _wasDown[i];
            }
        }

        private static bool AimAt(PlayerEntity player, PlayerEntity? target)
        {
            AimDeltaX = 0;
            AimDeltaY = 0;
            if (target == null)
            {
                return false;
            }
            (float turnX, float turnY) = player.ModAimDeltaTowards(target.ModAimTarget);
            if (!Single.IsFinite(turnX) || !Single.IsFinite(turnY))
            {
                // Aiming at a player whose position has gone bad would turn
                // this one's aim to nonsense too, and then publish it.
                return false;
            }
            AimDeltaX = Math.Clamp(turnX, -TurnRate, TurnRate);
            AimDeltaY = Math.Clamp(turnY, -TurnRate, TurnRate);
            bool onTarget = MathF.Abs(turnX) < FiringCone && MathF.Abs(turnY) < FiringCone;
            if (onTarget)
            {
                FramesOnTarget++;
            }
            return onTarget;
        }

        private static void Square(PlayerControls c)
        {
            int phase = _frame / 60 % 4;
            Hold(c.MoveUp, phase == 0);
            Hold(c.MoveRight, phase == 1);
            Hold(c.MoveDown, phase == 2);
            Hold(c.MoveLeft, phase == 3);
        }

        private static void Duel(PlayerEntity player, PlayerControls c, PlayerEntity? target, bool onTarget)
        {
            if (target == null)
            {
                Square(c);
                Hold(c.Shoot, _frame % 60 < 20);
                return;
            }
            float distance = (target.Position - player.Position).Length;
            // Walking straight at somebody across a level with walls in it
            // ends up sliding along one of them forever. When the ground stops
            // going by, break off sideways for a moment.
            float moved = (player.Position - _lastPosition).Length;
            _lastPosition = player.Position;
            _stuckFrames = moved < 0.02f ? _stuckFrames + 1 : 0;
            bool stuck = _stuckFrames > 20;
            if (stuck && _stuckFrames > 90)
            {
                _stuckFrames = 0;
                _stuckDirection = !_stuckDirection;
            }
            Hold(c.MoveUp, !stuck && distance > PreferredRange);
            Hold(c.MoveDown, !stuck && distance < PreferredRange / 2);
            Hold(c.MoveLeft, stuck ? _stuckDirection
                : distance <= PreferredRange && _frame / 90 % 2 == 0);
            Hold(c.MoveRight, stuck ? !_stuckDirection
                : distance <= PreferredRange && _frame / 90 % 2 == 1);
            Hold(c.Jump, stuck ? _stuckFrames % 30 < 3 : _frame % 150 < 3);
            Hold(c.Shoot, onTarget && _frame % 30 < 24);
        }

        private static PlayerEntity? FindTarget(PlayerEntity self)
        {
            if (NetSession.Active && self.SlotIndex >= 0)
            {
                int targetSlot = (self.SlotIndex + 1) % 3;
                if (targetSlot < PlayerEntity.Players.Count)
                {
                    PlayerEntity target = PlayerEntity.Players[targetSlot];
                    if (target != self && target.LoadFlags.TestFlag(LoadFlags.Active)
                        && target.LoadFlags.TestFlag(LoadFlags.Spawned) && target.Health > 0)
                    {
                        return target;
                    }
                }
            }
            PlayerEntity? best = null;
            float bestDistance = Single.MaxValue;
            for (int i = 0; i < PlayerEntity.Players.Count; i++)
            {
                PlayerEntity other = PlayerEntity.Players[i];
                if (other == self || !other.LoadFlags.TestFlag(LoadFlags.Active)
                    || !other.LoadFlags.TestFlag(LoadFlags.Spawned) || other.Health == 0)
                {
                    continue;
                }
                float distance = (other.Position - self.Position).Length;
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = other;
                }
            }
            return best;
        }

        /// <summary>Ask for a button. Edges come from <see cref="Finish"/>.</summary>
        private static void Hold(Keybind bind, bool down)
        {
            bind.IsDown = down;
        }
    }
}
