using System;
using MphRead.Entities;
using OpenTK.Graphics.OpenGL;
using OpenTK.Mathematics;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.Desktop;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// How much damage a weapon does per second, held on a target that does
    /// not move, fight back or die.
    ///
    /// Written because a weapon was changed on the strength of reading the
    /// code. The Shock Coil is the only beam that stays alive and re-tests
    /// collision every frame, and every beam hit carries NoDmgInvuln, so
    /// nothing limits its rate but the frame rate -- which this engine runs at
    /// twice the original. That reasoning was sound and it was still only
    /// reasoning: the scripted tour keeps Sylux in alt form laying bombs and
    /// barely fires the beam at anybody, so no check in the project could
    /// confirm or deny it.
    ///
    /// The victim's health is restored every frame after the drop is counted.
    /// Letting it die would spend the window on respawn timers and measure the
    /// gaps between lives rather than the weapon.
    /// </summary>
    public sealed class WeaponDps : GameWindow
    {
        private readonly string _room;
        private readonly Hunter _hunter;
        private readonly BeamType _beam;
        private readonly double _seconds;
        private readonly float _distance;
        private int _frame;
        private int _firingFrames;
        private int _damage;
        private int _hits;
        private int _lastHealth = -1;
        private int _startHealth;
        private int _killFrames = -1;
        private int _beamFrames;
        private int _placedFrame = -1;
        private bool _placed;

        /// <summary>
        /// Topped up to the player's own maximum, not to a large number. The
        /// engine clamps health to HealthMax, so writing 999 reads back as 99
        /// on the next frame -- which the first version of this probe counted
        /// as nine hundred points of damage in one hit.
        /// </summary>
        private static int FullHealth(PlayerEntity player) => Math.Max(1, player.HealthMax);

        public Scene Scene { get; }

        private static GameWindowSettings GameSettings() => new() { UpdateFrequency = 60 };

        private static NativeWindowSettings WindowSettings() => new()
        {
            ClientSize = new Vector2i(320, 180),
            Title = "MphRead weapon probe",
            Profile = ContextProfile.Compatability,
            APIVersion = new Version(3, 2),
            StartVisible = false
        };

        private WeaponDps(string room, Hunter hunter, BeamType beam, double seconds, float distance)
            : base(GameSettings(), WindowSettings())
        {
            _room = room;
            _hunter = hunter;
            _beam = beam;
            _seconds = seconds;
            _distance = distance;
            PlayerEntity.MaxPlayers = Math.Max(PlayerEntity.MaxPlayers, 2);
            MapAudit.ForceEveryone = true;
            Scene = new Scene(Size, KeyboardState, MouseState, _ => { }, Close);
            // The victim is slot 0 and the shooter is slot 1, deliberately.
            // PlayerEntity.ProcessInput refills the *main* player's controls
            // from the keyboard every frame, so anything written into slot 0
            // is gone before the simulation reads it -- the first version of
            // this probe held fire for twelve seconds and spawned no beam at
            // all. The victim is meant to stand still, which is exactly what
            // an empty keyboard gives it.
            Scene.AddPlayer(Hunter.Samus, recolor: 0, team: -1);
            Scene.AddPlayer(hunter, recolor: 0, team: -1);
            for (int i = 2; i < PlayerEntity.Players.Count; i++)
            {
                PlayerEntity.Players[i].LoadFlags &= ~LoadFlags.Active;
            }
            for (int i = 0; i < PlayerEntity.Players.Count; i++)
            {
                PlayerEntity.Players[i].IsBot = false;
            }
            PlayerEntity.PlayerCount = 2;
            PlayerEntity.MainPlayerIndex = 0;
            Scene.AddRoom(room, GameMode.Battle, playerCount: NetLaunch.RoomPlayerCount);
        }

        protected override void OnLoad()
        {
            Scene.Size = ClientSize;
            Scene.OnLoad();
            base.OnLoad();
            GL.Viewport(0, 0, ClientSize.X, ClientSize.Y);
            Scene.OnResize();
        }

        protected override void OnRenderFrame(FrameEventArgs args)
        {
            GameState.ApplyPause();
            Scene.OnUpdateFrame();
            if (!Scene.OnRenderFrame())
            {
                return;
            }
            _frame++;
            Step();
            SwapBuffers();
            Scene.AfterRenderFrame();
            base.OnRenderFrame(args);
            if (_placed && _frame - _placedFrame >= _seconds * 60)
            {
                Close();
            }
            else if (_frame > (_seconds + 20) * 60)
            {
                Close(); // never got set up; the report says so
            }
        }

        private static bool Alive(PlayerEntity player)
        {
            return player.LoadFlags.TestFlag(LoadFlags.Active)
                && player.LoadFlags.TestFlag(LoadFlags.Spawned) && player.Health > 0;
        }

        private void Step()
        {
            if (PlayerEntity.Players.Count < 2)
            {
                return;
            }
            PlayerEntity victim = PlayerEntity.Players[0];
            PlayerEntity shooter = PlayerEntity.Players[1];
            if (!Alive(shooter) || !Alive(victim))
            {
                NetTestScript.Rest(shooter, wantBiped: true);
                NetTestScript.Rest(victim, wantBiped: true);
                return;
            }
            if (shooter.IsAltForm || shooter.IsMorphing || shooter.IsUnmorphing)
            {
                NetTestScript.Rest(shooter, wantBiped: true);
                NetTestScript.Rest(victim, wantBiped: true);
                return;
            }
            if (!_placed)
            {
                // In front of the victim, on the floor the victim is standing
                // on -- an arbitrary bearing puts the shooter in a wall.
                Vector3 facing = victim.FacingVector;
                facing = new Vector3(facing.X, 0, facing.Z);
                facing = facing.LengthSquared < 0.001f ? Vector3.UnitZ : facing.Normalized();
                Vector3 spot = victim.Position + facing * _distance;
                shooter.Teleport(spot, -facing, Scene.GetNodeRefByPosition(spot));
                _placed = true;
                _placedFrame = _frame;
                _lastHealth = victim.Health;
                _startHealth = victim.Health;
            }
            if (_killFrames >= 0)
            {
                // Already answered. Standing here shooting a corpse only
                // measures the respawn timer.
                NetTestScript.Rest(shooter, wantBiped: true);
                NetTestScript.Rest(victim, wantBiped: true);
                return;
            }
            shooter.ModArmWeapon(_beam);
            // Chest to chest. Between the two collision volumes' centres
            // sounds more precise and is worse: they sit low, so the shot
            // goes into the floor a couple of units short.
            Vector3 toVictim = victim.Position.AddY(0.5f) - shooter.Position.AddY(0.5f);
            if (toVictim.LengthSquared > 0.001f)
            {
                shooter.ModSetAim(toVictim.Normalized());
            }
            NetTestScript.HoldFire(shooter, down: true);
            NetTestScript.Rest(victim, wantBiped: true);
            // "It never fired" and "it fired and missed" are different
            // answers and only the second is about the weapon.
            foreach (EntityBase entity in Scene.Entities)
            {
                if (entity.Type == EntityType.BeamProjectile
                    && entity is BeamProjectileEntity shot && shot.Owner == shooter)
                {
                    _beamFrames++;
                    break;
                }
            }
            if (victim.Health < _lastHealth)
            {
                _damage += _lastHealth - victim.Health;
                _hits++;
            }
            _lastHealth = victim.Health;
            // The shooter is kept alive -- splash from its own weapon, or a
            // fall, would end the window for a reason that is not the
            // measurement. The victim is left to die: time to kill from full
            // health is the one number here that cannot be misread.
            shooter.Health = FullHealth(shooter);
            _firingFrames++;
            if (victim.Health == 0 && _killFrames < 0)
            {
                _killFrames = _firingFrames;
            }
        }

        private int Report()
        {
            if (!_placed || _firingFrames == 0)
            {
                Console.WriteLine($"DPSFAIL {_room} | {_hunter} {_beam} | never got set up");
                return 1;
            }
            double seconds = _firingFrames / 60.0;
            double window = _killFrames > 0 ? _killFrames / 60.0 : seconds;
            string kill = _killFrames > 0
                ? $"killed {_startHealth} hp in {_killFrames / 60.0:0.00} s"
                : $"did not kill {_startHealth} hp in {seconds:0.0} s";
            Console.WriteLine($"DPS {_room} | {_hunter} holding {_beam} at {_distance:0.0} units | {kill} | "
                + $"damage {_damage} | hits {_hits} | "
                + $"{_damage / window:0.0} per second | "
                + $"{(_hits > 0 ? _damage / (double)_hits : 0):0.0} per hit | "
                + $"{_hits / window:0.0} hits per second | "
                + $"beam alive on {_beamFrames} of {_firingFrames} frame(s)");
            return 0;
        }

        public static int Run(string room, Hunter hunter, BeamType beam, double seconds, float distance)
        {
            WeaponDps? window = null;
            try
            {
                window = new WeaponDps(room, hunter, beam, seconds, Math.Clamp(distance, 0.5f, 40f));
                window.Run();
                return window.Report();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"DPSCRASH {room} | {ex.GetType().Name}: {ex.Message}");
                Console.WriteLine(ex.StackTrace);
                return 1;
            }
            finally
            {
                window?.Dispose();
            }
        }
    }
}
