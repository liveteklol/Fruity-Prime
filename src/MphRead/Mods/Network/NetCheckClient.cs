using System;
using System.IO;
using MphRead.Entities;
using OpenTK.Graphics.OpenGL;
using OpenTK.Mathematics;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.Desktop;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// A real client, driven by a script instead of a person, that reports
    /// what it can actually see.
    ///
    /// The distinction that matters: this is not a test double. It runs
    /// RenderWindow's own frame -- Scene.OnUpdateFrame, Scene.OnRenderFrame,
    /// the real PlayerEntity simulation, the real net hooks -- in a window
    /// that is never shown. Earlier harnesses drove hand-built packets or
    /// pumped the network without stepping the engine, and both stayed green
    /// while the shipping client could not put two players in one match.
    ///
    /// What it asserts is deliberately the thing a person would look for:
    /// somebody else's hunter, spawned, somewhere other than the origin, at a
    /// position that keeps changing, taking and dealing damage, drawn into
    /// this client's own frame.
    ///
    /// Usage: -netcheck &lt;host&gt; [-port N] [-name NAME] [-hunter H]
    ///        [-seconds N] [-shots DIR] [-size WxH]
    /// </summary>
    public sealed class NetCheckClient : GameWindow
    {
        /// <summary>
        /// What this client saw of one other player. Per slot, because a
        /// single running total across everybody counted one opponent's
        /// position as the next one's and reported thousands of deaths in a
        /// three-player match -- the same class of meaningless number this
        /// harness exists to avoid producing.
        /// </summary>
        private sealed class RemoteView
        {
            public int FramesActive;
            public int FramesSpawned;
            public int FirstSpawnFrame = -1;
            public int Deaths;
            public int Hits;
            public int MinHealth = Int32.MaxValue;
            public int LastHealth = -1;
            public bool WasAlive;
            public double Travelled;
            public int DistinctPositions;
            public Vector3 LastPosition;
            public bool HavePosition;
            public int AltFormFrames;
            public int AltFormWantedFrames;
            public int AltFormDisagreeFrames;
            public Hunter Hunter;
        }

        private readonly string _name;
        private readonly string? _shotDirectory;
        private readonly double _seconds;
        private readonly RemoteView[] _remotes = new RemoteView[PlayerEntity.MaxPlayers];
        private int _frame;
        private int _shots;
        private int _duelShots;
        private int _lastDuelShotFrame = -1000;
        private bool _opponentInView;
        private int _localSpawnFrame = -1;
        private int _minHealthSeen = Int32.MaxValue;
        private int _lastLocalHealth = -1;
        private int _myDeaths;
        private int _damageTaken;
        private bool _wasAliveLocal;
        private int _indicatorFrames;
        private double _litFraction;
        private int _roomChanges;
        private int _lastRoomId = -1;
        private bool _everSawSomeone;
        private int _myAltFrames;
        private readonly NetFeatureCheck _features = new();
        private int _featureFailures;

        private static GameWindowSettings GameSettings() => new() { UpdateFrequency = 60 };

        private static NativeWindowSettings WindowSettings(int width, int height) => new()
        {
            ClientSize = new Vector2i(width, height),
            Title = "MphRead net check",
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

        private NetCheckClient(string name, string roomKey, GameMode mode, Hunter hunter,
            double seconds, string? shotDirectory, int width, int height)
            : base(GameSettings(), WindowSettings(width, height))
        {
            _name = name;
            _seconds = seconds;
            _shotDirectory = shotDirectory;
            for (int i = 0; i < _remotes.Length; i++)
            {
                _remotes[i] = new RemoteView();
            }
            Scene = new Scene(Size, KeyboardState, MouseState, _ => { }, Close);
            NetLaunch.BuildPlayers(Scene, hunter, 0);
            Scene.AddRoom(roomKey, mode, playerCount: NetLaunch.RoomPlayerCount);
        }

        protected override void OnLoad()
        {
            Scene.Size = ClientSize;
            Scene.OnLoad();
            base.OnLoad();
            // A window that is never shown or resized never gets OnResize,
            // which is what normally sets the viewport and sizes the
            // offscreen targets.
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
            Observe();
            _features.Observe(Scene);
            if (_shotDirectory != null && _frame % 120 == 0)
            {
                string path = Path.Combine(_shotDirectory, $"{_name}-{_shots:00}.png");
                if (ScreenCapture.Save(Scene, path))
                {
                    _shots++;
                    _litFraction = Math.Max(_litFraction, ScreenCapture.NonBlackFraction(Scene));
                }
            }
            // A frame with an opponent centred and close is the picture worth
            // keeping: a periodic capture mostly catches a wall, and "the
            // other player is drawn on my screen" is the claim that cannot be
            // settled from a log at all.
            if (_shotDirectory != null && _opponentInView && _duelShots < 8
                && _frame - _lastDuelShotFrame > 45)
            {
                string path = Path.Combine(_shotDirectory, $"{_name}-duel-{_duelShots:00}.png");
                if (ScreenCapture.Save(Scene, path))
                {
                    _duelShots++;
                    _lastDuelShotFrame = _frame;
                }
            }
            SwapBuffers();
            Scene.AfterRenderFrame();
            base.OnRenderFrame(args);
            if (_frame >= _seconds * 60)
            {
                Close();
            }
        }

        /// <summary>
        /// Record, once a frame, what this client believes about everybody
        /// else. Sampled every frame rather than at the end because the
        /// failure being hunted is intermittent by nature: a player that
        /// spawns and is immediately snapped back to the origin looks fine in
        /// a single final reading.
        /// </summary>
        private void Observe()
        {
            _opponentInView = false;
            if (Scene.RoomId != _lastRoomId)
            {
                if (_lastRoomId != -1)
                {
                    _roomChanges++;
                    // A rotation resets every per-slot observation: the
                    // players are new entities in a new level, and carrying
                    // the old totals over would report movement and hits that
                    // belong to the previous map. The verdict is kept, so a
                    // map that happens to end with nobody else connected
                    // cannot erase a session that plainly worked.
                    _everSawSomeone |= AnyoneSeen();
                    // A rotation replaces every entity in the scene, so the
                    // feature tour starts again with the new one.
                    _features.Reset();
                    for (int i = 0; i < _remotes.Length; i++)
                    {
                        _remotes[i] = new RemoteView();
                    }
                    _localSpawnFrame = -1;
                    _lastLocalHealth = -1;
                    _wasAliveLocal = false;
                }
                _lastRoomId = Scene.RoomId;
            }
            int local = Math.Max(NetSession.LocalSlot, 0);
            PlayerEntity? me = local < PlayerEntity.Players.Count ? PlayerEntity.Players[local] : null;
            if (me != null && me.LoadFlags.TestFlag(LoadFlags.Spawned))
            {
                if (_localSpawnFrame < 0)
                {
                    _localSpawnFrame = _frame;
                }
                _minHealthSeen = Math.Min(_minHealthSeen, me.Health);
                if (_wasAliveLocal && me.Health == 0)
                {
                    _myDeaths++;
                }
                // A health drop with the player still alive is a hit and
                // nothing else. Deaths alone would not distinguish being shot
                // from falling into a pit, which is not the claim being made.
                if (_lastLocalHealth > 0 && me.Health > 0 && me.Health < _lastLocalHealth)
                {
                    _damageTaken++;
                }
                _lastLocalHealth = me.Health;
                _wasAliveLocal = me.Health > 0;
                if (me.IsAltForm)
                {
                    _myAltFrames++;
                }
                if (me.ModDamageIndicatorActive)
                {
                    _indicatorFrames++;
                }
            }
            for (int slot = 0; slot < PlayerEntity.MaxPlayers; slot++)
            {
                if (slot == local || slot >= PlayerEntity.Players.Count)
                {
                    continue;
                }
                PlayerEntity other = PlayerEntity.Players[slot];
                RemoteView view = _remotes[slot];
                if (!other.LoadFlags.TestFlag(LoadFlags.Active))
                {
                    continue;
                }
                view.FramesActive++;
                if (!other.LoadFlags.TestFlag(LoadFlags.Spawned))
                {
                    continue;
                }
                view.FramesSpawned++;
                if (view.FirstSpawnFrame < 0)
                {
                    view.FirstSpawnFrame = _frame;
                }
                view.MinHealth = Math.Min(view.MinHealth, other.Health);
                if (view.WasAlive && other.Health == 0)
                {
                    view.Deaths++;
                }
                if (view.LastHealth > 0 && other.Health > 0 && other.Health < view.LastHealth)
                {
                    view.Hits++;
                }
                view.LastHealth = other.Health;
                view.WasAlive = other.Health > 0;
                if (view.HavePosition)
                {
                    float step = (other.Position - view.LastPosition).Length;
                    // Ignore a respawn teleport: it is real movement in the
                    // match but says nothing about whether input is flowing.
                    if (step < 5)
                    {
                        view.Travelled += step;
                    }
                    if (step > 0.05f)
                    {
                        view.DistinctPositions++;
                    }
                }
                view.LastPosition = other.Position;
                view.HavePosition = true;
                view.Hunter = other.Hunter;
                if (other.IsAltForm)
                {
                    view.AltFormFrames++;
                }
                // What the authority last said about this player's form,
                // against what this client is actually drawing. The gap is the
                // replication error; the totals alone cannot separate "never
                // arrived" from "arrived late and left early".
                if (NetSession.RemoteStateValid[slot])
                {
                    bool wanted = (NetSession.RemoteStates[slot].Flags
                        & PlayerState.FlagAltForm) != 0;
                    if (wanted)
                    {
                        view.AltFormWantedFrames++;
                    }
                    if (wanted != other.IsAltForm)
                    {
                        view.AltFormDisagreeFrames++;
                    }
                }
                if (me != null && other.Health > 0 && !_opponentInView)
                {
                    (float turnX, float turnY) = me.ModAimDeltaTowards(other.ModAimTarget);
                    float distance = (other.Position - me.Position).Length;
                    _opponentInView = distance < 25 && MathF.Abs(turnX) < 18
                        && MathF.Abs(turnY) < 18;
                }
            }
        }

        protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
        {
            Scene.DoCleanup();
            base.OnClosing(e);
        }

        /// <summary>
        /// Passing means somebody else was on the map, moving, for long
        /// enough to be more than a spawn glitch. Deliberately not "a packet
        /// arrived": every version of this feature that shipped broken had
        /// packets arriving.
        /// </summary>
        private bool Passed => _everSawSomeone || AnyoneSeen();

        private bool AnyoneSeen()
        {
            for (int slot = 0; slot < _remotes.Length; slot++)
            {
                RemoteView view = _remotes[slot];
                if (view.FirstSpawnFrame >= 0 && view.DistinctPositions > 5
                    && view.Travelled > 2)
                {
                    return true;
                }
            }
            return false;
        }

        private void Report()
        {
            int local = Math.Max(NetSession.LocalSlot, 0);
            Console.WriteLine();
            Console.WriteLine($"=== {_name}: what this client saw ===");
            Console.WriteLine($"  slot {local}, authority={NetSession.IsAuthority}, frames={_frame}");
            Console.WriteLine($"  room: {Metadata.GetRoomById(Scene.RoomId, noThrow: true)?.Name ?? "?"} "
                + $"(server says {NetSession.ServerMatch?.RoomKey ?? "?"}), "
                + $"{_roomChanges} rotation(s) followed");
            Console.WriteLine($"  packets: snapshots sent={NetSession.SnapshotsSent} "
                + $"received={NetSession.SnapshotsReceived} "
                // Reordered snapshots this client refused. Not loss: these
                // arrived, late, after a newer one had already been applied.
                // Applying them ran health, score and the damage counter
                // backwards, and a byte counter run backwards reads as a
                // couple of hundred fresh hits.
                + $"late={NetSession.SnapshotsOutOfOrder} "
                + $"intents received={NetSession.IntentsReceived} "
                // Relayed intents refused as out of order. A handful is UDP
                // doing what UDP does; a steady stream from one slot is that
                // slot's occupant being ignored outright -- which is what a
                // rejoining player looked like before the reset gap.
                + $"intents late={NetSession.IntentsOutOfOrder} "
                + $"states applied={NetSession.StatesApplied} "
                // Non-zero means this client could not keep up with what it
                // was sent, which reads on the other clients' reports as
                // "they never saw me turn".
                + $"dropped={NetTransport.TotalPacketsDropped}");
            var pings = new System.Text.StringBuilder();
            for (int slot = 0; slot < PlayerEntity.MaxPlayers; slot++)
            {
                if (NetSession.SlotOccupied[slot])
                {
                    pings.Append(pings.Length > 0 ? "  " : "");
                    pings.Append($"slot {slot} {NetSession.SlotPing[slot]} ms");
                }
            }
            if (pings.Length > 0)
            {
                // What the server measured for everybody, which is also what
                // the scoreboard draws.
                Console.WriteLine($"  pings: {pings}");
            }
            for (int slot = 0; slot < PlayerEntity.MaxPlayers; slot++)
            {
                if (slot >= PlayerEntity.Players.Count)
                {
                    continue;
                }
                PlayerEntity p = PlayerEntity.Players[slot];
                bool active = p.LoadFlags.TestFlag(LoadFlags.Active);
                if (!active && !NetSession.SlotOccupied[slot])
                {
                    continue;
                }
                Console.WriteLine($"  slot {slot} {GameState.Nicknames[slot],-10} {p.Hunter,-8} "
                    + $"active={(active ? "y" : "n")} "
                    + $"spawned={(p.LoadFlags.TestFlag(LoadFlags.Spawned) ? "y" : "n")} "
                    + $"hp={p.Health,-4} "
                    + $"pos=({p.Position.X:0.0},{p.Position.Y:0.0},{p.Position.Z:0.0})");
            }
            Console.WriteLine($"  my player: {(local < PlayerEntity.Players.Count ? PlayerEntity.Players[local].Hunter.ToString() : "?")}, "
                + $"alt form on {_myAltFrames} frame(s)");
            Console.WriteLine($"  my player: spawned on frame {_localSpawnFrame}, "
                + $"lowest health {(_minHealthSeen == Int32.MaxValue ? -1 : _minHealthSeen)}, "
                + $"died {_myDeaths} time(s), took a hit {_damageTaken} time(s)");
            Console.WriteLine($"  HUD damage indicator lit on {_indicatorFrames} frame(s)");
            for (int slot = 0; slot < _remotes.Length; slot++)
            {
                RemoteView view = _remotes[slot];
                if (view.FramesActive == 0)
                {
                    continue;
                }
                Console.WriteLine($"  slot {slot} ({GameState.Nicknames[slot]}) as I saw them: "
                    + $"{view.Hunter}, active {view.FramesActive} frame(s), "
                    + $"spawned {view.FramesSpawned}, first on frame {view.FirstSpawnFrame}, "
                    + $"in alt form for {view.AltFormFrames} "
                    + $"(authority said alt form on {view.AltFormWantedFrames}, "
                    + $"disagreed on {view.AltFormDisagreeFrames})");
                Console.WriteLine($"    moved {view.Travelled:0.0} units over "
                    + $"{view.DistinctPositions} distinct position(s); "
                    + $"I saw them hit {view.Hits} time(s), killed {view.Deaths} time(s), "
                    + $"lowest health {(view.MinHealth == Int32.MaxValue ? -1 : view.MinHealth)}");
            }
            Console.WriteLine($"  script: {NetTestScript.FramesOnTarget} frame(s) with somebody in its sights, "
                + $"phase now {NetTestScript.Phase}");
            if (_shots > 0)
            {
                Console.WriteLine($"  {_shots} screenshot(s) written to {_shotDirectory}, "
                    + $"of which {_duelShots} with an opponent in view, "
                    + $"busiest frame {_litFraction * 100:0.0}% lit");
            }
            bool featuresOk = _features.Report(out int featureFailures);
            _featureFailures = featureFailures;
            Console.WriteLine();
            Console.WriteLine(Passed && featuresOk
                ? "  RESULT: PASS"
                : $"  RESULT: FAIL -- "
                    + (Passed ? "" : "no other player was on the map and moving; ")
                    + $"{featureFailures} feature(s) did not cross");
        }

        public static int Run(string host, int port, string name, Hunter hunter, double seconds,
            string? shotDirectory, int width, int height, bool recordDemo = false)
        {
            if (!NetLaunch.Join(host, port, name, hunter))
            {
                Console.WriteLine($"[netcheck] {name} could not join");
                NetSession.Stop();
                return 1;
            }
            NetTestScript.Reset();
            NetTestScript.Enabled = true;
            (string RoomKey, GameMode Mode) room = NetLaunch.ServerRoom()!.Value;
            Console.WriteLine($"[netcheck] {name} joined slot {NetSession.LocalSlot}, "
                + $"loading {room.RoomKey} ({room.Mode})");
            // Started here rather than from a menu: the point of recording
            // from the harness is to get a demo out of a client whose role in
            // the match is known -- above all the authority, whose own
            // outgoing snapshots nothing else in the session ever sees.
            if (recordDemo && DemoRecorder.Start())
            {
                Console.WriteLine($"[netcheck] {name} is recording to {DemoRecorder.CurrentPath}");
            }
            NetCheckClient? window = null;
            try
            {
                window = new NetCheckClient(name, room.RoomKey, room.Mode, hunter,
                    seconds, shotDirectory, width, height);
                window.Run();
                window.Report();
                return window.Passed && window._featureFailures == 0 ? 0 : 1;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[netcheck] {name} crashed: {ex}");
                return 2;
            }
            finally
            {
                if (DemoRecorder.IsRecording)
                {
                    Console.WriteLine($"[netcheck] {name} recorded {DemoRecorder.CurrentPath}");
                    DemoRecorder.Stop();
                }
                window?.Dispose();
                NetTestScript.Enabled = false;
                NetSession.Stop();
                NetLog.Close();
            }
        }
    }
}
