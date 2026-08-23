using System;
using System.Collections.Generic;
using System.Text;
using MphRead.Entities;
using OpenTK.Graphics.OpenGL;
using OpenTK.Mathematics;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.Desktop;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// Loads one room with a full house of players, runs it, and reports what
    /// the map contains and what the players managed to do in it.
    ///
    /// Separate from the network check on purpose: this asks whether the
    /// *game* holds up -- every map, the maximum number of players, the world
    /// entities each level actually has -- with no server involved. A crash
    /// here is a crash that would happen in a real match too, and finding it
    /// on one machine is far cheaper than finding it with eight people
    /// connected.
    ///
    /// One process per room, because Scene.AddRoom refuses a second room and
    /// GLFW wants its window on the main thread. The caller loops over the
    /// room list.
    ///
    /// Usage: -maptest "MP3 PROVING GROUND" [-players 8] [-seconds 10]
    /// </summary>
    public sealed class MapAudit : GameWindow
    {
        private readonly string _room;
        private readonly int _players;
        private readonly double _seconds;
        private int _frame;
        private int _spawned;
        private readonly int[] _deaths = new int[PlayerEntity.SlotCapacity];
        private readonly int[] _lastHealth = new int[PlayerEntity.SlotCapacity];
        private readonly bool[] _everSpawned = new bool[PlayerEntity.SlotCapacity];
        private readonly bool[] _everAltForm = new bool[PlayerEntity.SlotCapacity];
        private readonly bool[] _everFired = new bool[PlayerEntity.SlotCapacity];
        // The three states only an affinity weapon can inflict. Counted per
        // slot rather than as a total, because "somebody was frozen" and "one
        // player was frozen forty times" are different reports.
        private readonly bool[] _everFrozen = new bool[PlayerEntity.SlotCapacity];
        private readonly bool[] _everBurned = new bool[PlayerEntity.SlotCapacity];
        private readonly bool[] _everDisrupted = new bool[PlayerEntity.SlotCapacity];
        private double _lowestY = Double.MaxValue;

        // The world probe: after the tour, stand a player on every jump pad
        // and teleporter in turn and see whether it does anything.
        private readonly List<EntityBase> _probeTargets = new();
        private int _probeIndex = -1;
        private int _probeFrames;
        private int _probeWait;
        private bool _probePlaced;
        private int _probeAttempt;
        private int _probeMarkPads;
        private int _probeMarkTeleports;
        private int _padsProbed;
        private int _padsFired;
        private int _telesProbed;
        private int _telesFired;
        private const int _probeSlot = 0;
        // Long enough for a pad to notice a player standing on it -- it tests
        // the volume once a frame -- and short enough that thirty of them fit
        // in a couple of seconds. A pad that says nothing gets longer looks
        // afterwards, because a pad the tour has just fired is on a cooldown
        // of its own and would otherwise be reported as dead.
        private const int _probeFrameLimit = 12;
        private const int _probeRetryFrameLimit = 45;
        private const int _probeMaxTargets = 24;

        // The affliction probe: freeze, burn and disrupt exist only on a
        // hunter's own weapon, so each is tried by the one hunter that can
        // inflict it, at point-blank range, against a player standing still.
        private static readonly (Hunter Shooter, string Name)[] _afflictions =
        {
            (Hunter.Noxus, "freeze"),
            (Hunter.Spire, "burn"),
            (Hunter.Kanden, "disrupt")
        };
        private int _afflictIndex = -1;
        private int _afflictFrames;
        private int _afflictShooter = -1;
        private int _afflictVictim = -1;
        private readonly bool[] _afflictTried = new bool[3];
        private readonly bool[] _afflictLanded = new bool[3];
        // Whether our shooter ever got a shot off. "Nobody fired" and "the
        // victim was hit and did not catch fire" are different findings, and
        // only the second is about the affliction. Health is no use for this:
        // the victim stands still while six other players duel around it.
        private readonly bool[] _afflictFired = new bool[3];
        private readonly bool[] _afflictHit = new bool[3];
        private int _afflictVictimHealth;
        private int _afflictWait;
        private Affliction _afflictShotAfflictions;
        private int _afflictMaxCharge;
        private int _afflictSetUpWait;
        // Long enough for a projectile to cross three units and for the state
        // it inflicts to be visible for a frame.
        private const int _afflictFrameLimit = 320;

        private static GameWindowSettings GameSettings() => new() { UpdateFrequency = 60 };

        private static NativeWindowSettings WindowSettings() => new()
        {
            ClientSize = new Vector2i(320, 180),
            Title = "MphRead map audit",
            Profile = ContextProfile.Compatability,
            // Explicitly, exactly as the game's own window does. Left
            // unset, OpenTK's default gave this window a *forward-compatible*
            // context, which removes every deprecated entry point -- and this
            // engine draws in immediate mode, so that is all of them. The
            // profile mask still answers "compatibility", so nothing looked
            // wrong; the driver only admitted it in a shader warning that
            // mentioned "OGL 3.0 forward-compatible context". Every frame came
            // out black with GL_INVALID_OPERATION on an Intel Iris Xe, while
            // the game rendered perfectly on the same machine, because the
            // game sets this and these windows did not.
            Flags = ContextFlags.Default,
            APIVersion = new Version(3, 2),
            StartVisible = false
        };

        public Scene Scene { get; }

        /// <summary>
        /// Spawn every player without waiting for a fire button or a timer.
        /// Read by NetHooks.ForceSpawn, which is the one place the engine asks
        /// whether a waiting player should be placed now.
        /// </summary>
        public static bool ForceEveryone { get; internal set; }

        /// <summary>Print what the affliction probe saw. Set by -netdebug.</summary>
        public static bool Diagnostic { get; set; }

        /// <summary>
        /// Leave the other players as bots and let PlayerAi drive them,
        /// instead of replacing them with the scripted tour.
        ///
        /// A different code path from the rest of the audit and the one the
        /// launcher's offline match actually uses: the tour writes Controls
        /// directly and never touches the behaviour trees, so a bot-only
        /// crash is invisible to it.
        /// </summary>
        private readonly bool _bots;

        private MapAudit(string room, int players, double seconds, GameMode mode, bool bots)
            : base(GameSettings(), WindowSettings())
        {
            _bots = bots;
            _room = room;
            _players = players;
            _seconds = seconds;
            // The whole tour has to fit inside one visit, and there are more
            // than thirty rooms to get through.
            NetTestScript.PhaseSeconds = Math.Max(1.5, seconds / NetTestScript.PhaseCount);
            // Offline, MaxPlayers is still the four a DS match could hold, so
            // PlayerEntity.Create hands back null for every slot past the
            // fourth and those players silently never exist.
            PlayerEntity.MaxPlayers = Math.Max(PlayerEntity.MaxPlayers, players);
            ForceEveryone = true;
            // Nothing else in the program reads these; the audit is the only
            // caller that wants to know a pad fired.
            Mods.WorldEvents.Watching = true;
            Mods.WorldEvents.Reset();
            Scene = new Scene(Size, KeyboardState, MouseState, _ => { }, Close);
            // A different hunter per slot, cycling, so one run exercises
            // several alt forms, several affinity weapons and several
            // collision volumes rather than eight copies of Samus.
            for (int i = 0; i < players; i++)
            {
                Scene.AddPlayer((Hunter)(i % 7), recolor: 0, team: -1);
            }
            for (int i = 0; i < PlayerEntity.Players.Count; i++)
            {
                PlayerEntity player = PlayerEntity.Players[i];
                player.IsBot = bots && i > 0;
                player.BotLevel = bots ? 1 : 0;
                if (i >= players)
                {
                    player.LoadFlags &= ~LoadFlags.Active;
                }
            }
            PlayerEntity.PlayerCount = players;
            PlayerEntity.MainPlayerIndex = 0;
            Scene.AddRoom(room, mode, playerCount: NetLaunch.RoomPlayerCount);
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
            Drive();
            Observe();
            SwapBuffers();
            Scene.AfterRenderFrame();
            base.OnRenderFrame(args);
            if (_frame >= _seconds * 60 && (_bots || (!StepProbe() && !StepAfflictionProbe())))
            {
                Close();
            }
        }

        /// <summary>
        /// Every player is driven, not just one. Eight players moving, firing
        /// and morphing at once is the load a map has to survive, and it is
        /// also what pushes them onto the jump pads and into the pits.
        /// </summary>
        private void Drive()
        {
            for (int slot = 0; slot < _players && slot < PlayerEntity.Players.Count; slot++)
            {
                PlayerEntity player = PlayerEntity.Players[slot];
                if (!player.LoadFlags.TestFlag(LoadFlags.Active))
                {
                    continue;
                }
                if (!player.LoadFlags.TestFlag(LoadFlags.Spawned) || player.Health == 0)
                {
                    // Spawning is handled by ForceEveryone rather than by
                    // holding fire: the main player's controls are refilled
                    // from the keyboard every frame, so anything written here
                    // is gone before the spawn check reads it.
                    continue;
                }
                if (_afflictIndex >= 0)
                {
                    // Everybody stands down for the affliction probe. Six
                    // other players duelling around the victim make its health
                    // useless as evidence that our shot landed, and one of
                    // them killing the shooter costs the whole window.
                    if (slot != _afflictShooter && slot != _afflictVictim)
                    {
                        NetTestScript.Rest(player, wantBiped: false);
                    }
                    continue;
                }
                if (_bots)
                {
                    // PlayerAi is writing these; the tour would fight it.
                    continue;
                }
                if (_probeIndex >= 0 && slot == _probeSlot)
                {
                    // The prober stands still: a jump pad may be flagged to
                    // ignore alt forms, and a player still walking the tour
                    // has stepped off the pad before it looks.
                    NetTestScript.Rest(player, wantBiped: true);
                    continue;
                }
                NetTestScript.ApplyOffline(player, slot, _frame);
                // The script decides where to point; offline, nothing else
                // applies it.
                player.ModApplyScriptAim(NetTestScript.AimDeltaX, NetTestScript.AimDeltaY);
            }
        }

        /// <summary>
        /// Stand a player on the next jump pad or teleporter and watch.
        ///
        /// Returns false when there is nothing left to try, which is what
        /// ends the audit. Driving a player onto a pad by the tour alone was
        /// luck: the report said "jumppads 8" whether the map's pads worked
        /// or not. This asks each one directly.
        /// </summary>
        private bool StepProbe()
        {
            if (_probeIndex < 0)
            {
                CollectProbeTargets();
                _probeIndex = 0;
            }
            if (_probeIndex >= _probeTargets.Count || _probeSlot >= PlayerEntity.Players.Count)
            {
                return false;
            }
            PlayerEntity player = PlayerEntity.Players[_probeSlot];
            EntityBase target = _probeTargets[_probeIndex];
            if (!_probePlaced)
            {
                if (!player.LoadFlags.TestFlag(LoadFlags.Active)
                    || !player.LoadFlags.TestFlag(LoadFlags.Spawned) || player.Health == 0)
                {
                    // Between lives: ask to come back, and wait. If it never
                    // does, abandon the probe rather than hold the audit open.
                    NetTestScript.Rest(player, wantBiped: true);
                    if (++_probeWait > 240)
                    {
                        _probeIndex = _probeTargets.Count;
                    }
                    return true;
                }
                _probeWait = 0;
                _probeMarkPads = Mods.WorldEvents.JumpPadsFor(_probeSlot);
                _probeMarkTeleports = Mods.WorldEvents.TeleportsFor(_probeSlot);
                // Three tries before a pad is called silent. Two heights,
                // because a trigger volume is placed relative to the entity
                // and some carry theirs above the model rather than on it; and
                // then once in alt form, because a jump pad can be flagged to
                // ignore bipeds -- the ones in a morph ball tunnel are, and
                // reporting those as broken would be reporting the map for
                // working as designed.
                float lift = _probeAttempt == 1 ? 1.6f : 0.5f;
                player.ModForceForm(altForm: _probeAttempt == 2);
                Vector3 spot = TriggerPoint(target).AddY(lift);
                player.Teleport(spot, target.FacingVector, Scene.GetNodeRefByPosition(spot));
                _probePlaced = true;
                _probeFrames = 0;
                return true;
            }
            _probeFrames++;
            // This pad, this player: seven other players are still touring the
            // room and one of them landing on a different pad is not an answer
            // to the question being asked here.
            bool fired = target.Type == EntityType.JumpPad
                ? Mods.WorldEvents.JumpPadsFor(_probeSlot) > _probeMarkPads
                    && Mods.WorldEvents.LastJumpPadId(_probeSlot) == target.Id
                : Mods.WorldEvents.TeleportsFor(_probeSlot) > _probeMarkTeleports
                    && Mods.WorldEvents.LastTeleporterId(_probeSlot) == target.Id;
            int limit = _probeAttempt == 0 ? _probeFrameLimit : _probeRetryFrameLimit;
            if (!fired && _probeFrames < limit)
            {
                return true;
            }
            if (!fired && _probeAttempt < 2)
            {
                _probeAttempt++;
                _probePlaced = false;
                return true;
            }
            _probeAttempt = 0;
            player.ModForceForm(altForm: false);
            if (target.Type == EntityType.JumpPad)
            {
                _padsProbed++;
                if (fired)
                {
                    _padsFired++;
                }
            }
            else
            {
                _telesProbed++;
                if (fired)
                {
                    _telesFired++;
                }
            }
            _probeIndex++;
            _probePlaced = false;
            return true;
        }

        /// <summary>
        /// Try each affliction once: stand the hunter that can inflict it three
        /// units in front of somebody and hold the trigger.
        ///
        /// The tour reaches these states only when the right hunter happens to
        /// land a shot with its own weapon in the seconds the afflict phase
        /// lasts, which on a large map is rarely. This asks directly, so a
        /// report of "no afflictions" means the state is broken rather than
        /// that nobody got shot.
        /// </summary>
        private bool StepAfflictionProbe()
        {
            if (_afflictIndex < 0)
            {
                _afflictIndex = 0;
                _afflictFrames = 0;
                _afflictShooter = -1;
            }
            if (_afflictIndex >= _afflictions.Length)
            {
                return false;
            }
            if (_afflictShooter < 0)
            {
                if (!SetUpAffliction(_afflictions[_afflictIndex].Shooter))
                {
                    // Usually the hunter that inflicts this one is dead for a
                    // moment; wait for it before deciding it is not here.
                    if (++_afflictSetUpWait > 150)
                    {
                        _afflictSetUpWait = 0;
                        _afflictIndex++;
                    }
                    return true;
                }
                _afflictSetUpWait = 0;
                _afflictTried[_afflictIndex] = true;
                _afflictFrames = 0;
                _afflictVictimHealth = PlayerEntity.Players[_afflictVictim].Health;
                return true;
            }
            PlayerEntity shooter = PlayerEntity.Players[_afflictShooter];
            PlayerEntity victim = PlayerEntity.Players[_afflictVictim];
            if (!Alive(shooter) || !Alive(victim))
            {
                // One of them is between lives. Ask to come back and wait,
                // rather than spend the window on a corpse.
                NetTestScript.Rest(shooter, wantBiped: true);
                NetTestScript.Rest(victim, wantBiped: true);
                if (++_afflictWait > 240)
                {
                    _afflictLanded[_afflictIndex] = false;
                    _afflictIndex++;
                    _afflictShooter = -1;
                    _afflictVictim = -1;
                    _afflictWait = 0;
                }
                return true;
            }
            _afflictWait = 0;
            if (shooter.IsAltForm || shooter.IsMorphing || shooter.IsUnmorphing)
            {
                // A morph ball cannot fire a beam. Ask it to come out and
                // spend no window frames waiting.
                NetTestScript.Rest(shooter, wantBiped: true);
                NetTestScript.Rest(victim, wantBiped: true);
                return true;
            }
            _afflictFrames++;
            {
                shooter.ModArmAffinityWeapon();
                // Level, from chest height to chest height. Aiming between
                // the two collision volumes' centres sounds more precise and
                // is worse: they sit low, so the shot goes into the floor a
                // couple of units short.
                Vector3 toVictim = victim.Position.AddY(0.5f) - shooter.Position.AddY(0.5f);
                if (toVictim.LengthSquared > 0.001f)
                {
                    shooter.ModSetAim(toVictim.Normalized());
                }
                // Charged, not tapped. Every affliction in the game lives on
                // the charged entry of its weapon's affliction pair, so a
                // probe that taps the trigger lands hit after hit that can
                // never freeze, burn or disrupt anybody. Hold until the weapon
                // says it is charged, then let go for a frame -- how long that
                // takes is the weapon's business, not a number guessed here.
                NetTestScript.HoldFire(shooter, !shooter.ModChargeReady);
                NetTestScript.Rest(victim, wantBiped: true);
            }
            foreach (EntityBase entity in Scene.Entities)
            {
                if (entity.Type == EntityType.BeamProjectile
                    && entity is BeamProjectileEntity shot && shot.Owner == shooter)
                {
                    _afflictFired[_afflictIndex] = true;
                    _afflictShotAfflictions |= shot.Afflictions;
                    break;
                }
            }
            if (victim.Health < _afflictVictimHealth)
            {
                _afflictHit[_afflictIndex] = true;
            }
            _afflictVictimHealth = Math.Min(_afflictVictimHealth, victim.Health);
            _afflictMaxCharge = Math.Max(_afflictMaxCharge, shooter.ModChargeLevel);
            bool landed = _afflictIndex switch
            {
                0 => victim.ModFrozen,
                1 => victim.ModBurning,
                _ => victim.ModDisrupted
            };
            if (landed || _afflictFrames >= _afflictFrameLimit)
            {
                if (Diagnostic)
                {
                    Console.WriteLine($"PROBE {_afflictions[_afflictIndex].Name}: "
                        + $"shooter slot {_afflictShooter} {shooter.Hunter} "
                        + $"{shooter.ModWeaponState}, maxcharge={_afflictMaxCharge}, shots carried "
                        + $"{_afflictShotAfflictions}, landed={landed}");
                }
                _afflictLanded[_afflictIndex] = landed;
                _afflictIndex++;
                _afflictShooter = -1;
                _afflictVictim = -1;
                _afflictShotAfflictions = Affliction.None;
                _afflictMaxCharge = 0;
            }
            return true;
        }

        private static bool Alive(PlayerEntity player)
        {
            return player.LoadFlags.TestFlag(LoadFlags.Active)
                && player.LoadFlags.TestFlag(LoadFlags.Spawned) && player.Health > 0;
        }

        /// <summary>Find a shooter of this hunter and somebody to shoot, and place them.</summary>
        private bool SetUpAffliction(Hunter hunter)
        {
            int shooter = -1;
            int victim = -1;
            for (int slot = 0; slot < _players && slot < PlayerEntity.Players.Count; slot++)
            {
                PlayerEntity player = PlayerEntity.Players[slot];
                if (!Alive(player))
                {
                    continue;
                }
                if (shooter < 0 && player.Hunter == hunter)
                {
                    shooter = slot;
                }
                else if (victim < 0)
                {
                    victim = slot;
                }
            }
            if (shooter < 0 || victim < 0)
            {
                return false;
            }
            PlayerEntity shooterPlayer = PlayerEntity.Players[shooter];
            PlayerEntity victimPlayer = PlayerEntity.Players[victim];
            // In front of the victim rather than at an arbitrary bearing: the
            // floor it is standing on is the floor the shooter needs too.
            Vector3 facing = victimPlayer.FacingVector;
            facing = new Vector3(facing.X, 0, facing.Z);
            if (facing.LengthSquared < 0.001f)
            {
                facing = Vector3.UnitZ;
            }
            facing = facing.Normalized();
            // Close: the probe is asking whether the state can be inflicted at
            // all, not whether the hunter can aim.
            Vector3 spot = victimPlayer.Position + facing * 2.2f;
            shooterPlayer.Teleport(spot, -facing, Scene.GetNodeRefByPosition(spot));
            shooterPlayer.ModArmAffinityWeapon();
            _afflictShooter = shooter;
            _afflictVictim = victim;
            return true;
        }

        /// <summary>
        /// Where to stand to be inside a volume.
        ///
        /// The entity's own position is not it: a trigger box is positioned
        /// relative to the entity and several jump pads carry theirs beside or
        /// above the model. Three of MP6 HEADSHOT's eight pads were reported
        /// as dead for exactly that reason -- the player was standing next to
        /// the box, not in it.
        /// </summary>
        private static Vector3 VolumeCenter(CollisionVolume volume)
        {
            return volume.Type switch
            {
                VolumeType.Box => volume.BoxPosition
                    + (volume.BoxVector1 * volume.BoxDot1
                        + volume.BoxVector2 * volume.BoxDot2
                        + volume.BoxVector3 * volume.BoxDot3) / 2,
                VolumeType.Cylinder => volume.CylinderPosition
                    + volume.CylinderVector * (volume.CylinderDot / 2),
                VolumeType.Sphere => volume.SpherePosition,
                _ => Vector3.Zero
            };
        }

        /// <summary>Where a player has to stand for this entity to notice it.</summary>
        private static Vector3 TriggerPoint(EntityBase entity)
        {
            if (entity is JumpPadEntity pad)
            {
                Vector3 center = VolumeCenter(pad.ModVolume);
                if (center != Vector3.Zero)
                {
                    return center;
                }
            }
            return entity.Position;
        }

        private void CollectProbeTargets()
        {
            foreach (EntityBase entity in Scene.Entities)
            {
                if (_probeTargets.Count >= _probeMaxTargets)
                {
                    break;
                }
                // Inactive ones are not meant to do anything, so a silent one
                // is not a finding.
                if (entity.Active
                    && (entity.Type == EntityType.JumpPad || entity.Type == EntityType.Teleporter))
                {
                    _probeTargets.Add(entity);
                }
            }
        }

        private void Observe()
        {
            _spawned = 0;
            for (int slot = 0; slot < _players && slot < PlayerEntity.Players.Count; slot++)
            {
                PlayerEntity player = PlayerEntity.Players[slot];
                if (!player.LoadFlags.TestFlag(LoadFlags.Active)
                    || !player.LoadFlags.TestFlag(LoadFlags.Spawned))
                {
                    continue;
                }
                _spawned++;
                _everSpawned[slot] = true;
                if (player.IsAltForm)
                {
                    _everAltForm[slot] = true;
                }
                if (player.ModFrozen)
                {
                    _everFrozen[slot] = true;
                }
                if (player.ModBurning)
                {
                    _everBurned[slot] = true;
                }
                if (player.ModDisrupted)
                {
                    _everDisrupted[slot] = true;
                }
                if (_lastHealth[slot] > 0 && player.Health == 0)
                {
                    _deaths[slot]++;
                }
                _lastHealth[slot] = player.Health;
                _lowestY = Math.Min(_lowestY, player.Position.Y);
            }
            foreach (EntityBase entity in Scene.Entities)
            {
                if (entity.Type == EntityType.BeamProjectile
                    && (entity as BeamProjectileEntity)?.Owner is PlayerEntity owner
                    && owner.SlotIndex >= 0 && owner.SlotIndex < _everFired.Length)
                {
                    _everFired[owner.SlotIndex] = true;
                }
            }
        }

        protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
        {
            Scene.DoCleanup();
            base.OnClosing(e);
        }

        private int Report()
        {
            int spawnPoints = 0, jumpPads = 0, teleporters = 0, doors = 0, forceFields = 0;
            int itemSpawns = 0, items = 0, platforms = 0, morphCameras = 0, flagBases = 0;
            int nodeDefenses = 0, artifacts = 0, triggers = 0, areaVolumes = 0;
            foreach (EntityBase entity in Scene.Entities)
            {
                switch (entity.Type)
                {
                    case EntityType.PlayerSpawn: spawnPoints++; break;
                    case EntityType.JumpPad: jumpPads++; break;
                    case EntityType.Teleporter: teleporters++; break;
                    case EntityType.Door: doors++; break;
                    case EntityType.ForceField: forceFields++; break;
                    case EntityType.ItemSpawn: itemSpawns++; break;
                    case EntityType.ItemInstance: items++; break;
                    case EntityType.Platform: platforms++; break;
                    case EntityType.MorphCamera: morphCameras++; break;
                    case EntityType.FlagBase: flagBases++; break;
                    case EntityType.NodeDefense: nodeDefenses++; break;
                    case EntityType.Artifact: artifacts++; break;
                    case EntityType.TriggerVolume: triggers++; break;
                    case EntityType.AreaVolume: areaVolumes++; break;
                }
            }
            int spawnedEver = 0, altEver = 0, firedEver = 0, totalDeaths = 0;
            int frozenEver = 0, burnedEver = 0, disruptedEver = 0;
            for (int i = 0; i < _players; i++)
            {
                if (_everSpawned[i]) spawnedEver++;
                if (_everAltForm[i]) altEver++;
                if (_everFired[i]) firedEver++;
                if (_everFrozen[i]) frozenEver++;
                if (_everBurned[i]) burnedEver++;
                if (_everDisrupted[i]) disruptedEver++;
                totalDeaths += _deaths[i];
            }
            var line = new StringBuilder();
            line.Append($"MAPTEST {_room} | players {_players} | frames {_frame}");
            line.Append($" | spawned {spawnedEver}/{_players}");
            line.Append($" | alt form {altEver}/{_players}");
            line.Append($" | fired {firedEver}/{_players}");
            line.Append($" | deaths {totalDeaths}");
            line.Append($" | afflicted freeze {frozenEver} burn {burnedEver}"
                + $" disrupt {disruptedEver}");
            var probe = new StringBuilder();
            for (int i = 0; i < _afflictions.Length; i++)
            {
                probe.Append(i == 0 ? " (probe " : " ");
                probe.Append(_afflictions[i].Name);
                probe.Append(' ');
                probe.Append(!_afflictTried[i] ? "n/a"
                    : _afflictLanded[i] ? "ok"
                    : !_afflictFired[i] ? "nofire"
                    : _afflictHit[i] ? "FAIL" : "nohit");
            }
            probe.Append(')');
            line.Append(probe);
            line.Append($" | spawnpoints {spawnPoints}");
            // Counted and then tried: "8 jump pads" says what the map holds,
            // "7/8 launched" says what it does.
            line.Append($" | jumppads {jumpPads} ({_padsFired}/{_padsProbed} launched)");
            line.Append($" teleporters {teleporters} ({_telesFired}/{_telesProbed} moved)");
            line.Append($" doors {doors}");
            line.Append($" forcefields {forceFields} platforms {platforms}");
            line.Append($" itemspawns {itemSpawns} items {items}");
            line.Append($" morphcams {morphCameras} flagbases {flagBases} nodes {nodeDefenses}");
            line.Append($" artifacts {artifacts} triggers {triggers} areas {areaVolumes}");
            line.Append($" | lowest Y {_lowestY:0.0}");
            Console.WriteLine(line.ToString());

            var problems = new List<string>();
            // One silent pad can be a pad somebody has to jump onto from
            // above; every pad in the room silent is the trigger path broken.
            if (_padsProbed > 0 && _padsFired == 0)
            {
                problems.Add($"none of the {_padsProbed} jump pad(s) launched a player "
                    + "standing on them");
            }
            if (_telesProbed > 0 && _telesFired == 0)
            {
                problems.Add($"none of the {_telesProbed} teleporter(s) moved a player "
                    + "standing on them");
            }
            if (spawnedEver < _players)
            {
                var missing = new StringBuilder();
                for (int i = 0; i < _players; i++)
                {
                    if (!_everSpawned[i])
                    {
                        missing.Append(missing.Length > 0 ? ", " : "");
                        missing.Append($"slot {i} ({PlayerEntity.Players[i].Hunter}, "
                            + $"hp {PlayerEntity.Players[i].Health}, "
                            + $"respawn {PlayerEntity.Players[i].RespawnTimer})");
                    }
                }
                problems.Add($"only {spawnedEver} of {_players} players ever reached the map "
                    + $"-- missing {missing}");
            }
            if (spawnPoints == 0)
            {
                problems.Add("no spawn points at all");
            }
            for (int i = 0; i < _players; i++)
            {
                PlayerEntity player = PlayerEntity.Players[i];
                if (_everSpawned[i] && !player.ModCanBeHurt())
                {
                    problems.Add($"slot {i} ({player.Hunter}) cannot be hurt by any beam");
                }
            }
            foreach (string problem in problems)
            {
                Console.WriteLine($"MAPFAIL {_room} | {problem}");
            }
            return problems.Count;
        }

        public static int Run(string room, int players, double seconds, GameMode mode,
            bool bots = false)
        {
            MapAudit? window = null;
            try
            {
                window = new MapAudit(room, Math.Clamp(players, 1, PlayerEntity.SlotCapacity),
                    seconds, mode, bots);
                window.Run();
                return window.Report();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"MAPCRASH {room} | {ex.GetType().Name}: {ex.Message}");
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
