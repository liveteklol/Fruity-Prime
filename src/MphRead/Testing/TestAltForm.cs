using System;
using MphRead.Entities;
using MphRead.Formats;
using MphRead.Mods.Input;
using MphRead.Mods.Network;
using OpenTK.Mathematics;
using static MphRead.Mods.Network.NetPlayerBridge;

namespace MphRead.Testing
{
    public static partial class TestPlayer
    {
        // Standalone deterministic checks: no room, models, GL context or test framework.
        public static int CheckAltForms()
        {
            int failures = 0;
            int checks = 0;
            void Check(bool passed, string name)
            {
                checks++;
                if (!passed)
                {
                    failures++;
                    Console.WriteLine($"FAIL: {name}");
                }
            }
            CheckCamera(Check);
            CheckReconciliation(Check);
            PlayerEntity.ModCheckAltFlickRouting(Check);
            PlayerEntity.ModCheckAltFlickInputOrder(Check);
            Reset();
            Console.WriteLine($"ALTFORMCHECK: {checks} checks, {failures} failures");
            return failures == 0 ? 0 : 1;
        }

        private static void CheckCamera(Action<bool, string> check)
        {
            var camera = new CameraInfo();
            camera.Reset();
            Vector4 Basis() => new Vector4(camera.Field48, camera.Field4C, camera.Field50, camera.Field54);
            bool Orthonormal()
            {
                Vector4 b = Basis();
                return Single.IsFinite(b.X) && Single.IsFinite(b.Y)
                    && Single.IsFinite(b.Z) && Single.IsFinite(b.W)
                    && MathF.Abs(b.X * b.X + b.Y * b.Y - 1) < 0.00001f
                    && MathF.Abs(b.Z * b.Z + b.W * b.W - 1) < 0.00001f
                    && MathF.Abs(b.X * b.Z + b.Y * b.W) < 0.00001f;
            }
            check(Basis() == new Vector4(0, -1, -1, 0), "reset basis before first update");
            camera.Update();
            check(Basis() == new Vector4(0, -1, -1, 0), "valid -Z basis");
            camera.Position = Vector3.Zero;
            camera.Target = new Vector3(3, 2, 4);
            camera.Update();
            check(Orthonormal(), "arbitrary basis orthonormal");
            Vector4 previous = Basis();
            Vector3[] invalid =
            {
                Vector3.Zero, Vector3.UnitY, -Vector3.UnitY,
                new Vector3(1f / 16384, 1, 1f / 16384),
                new Vector3(1f / 4096, 1, 0),
                new Vector3(Single.NaN, 1, 1), new Vector3(1, 1, Single.NaN),
                new Vector3(Single.PositiveInfinity, 1, 1),
                new Vector3(1, 1, Single.NegativeInfinity),
                new Vector3(Single.MaxValue, 1, Single.MaxValue)
            };
            foreach (Vector3 target in invalid)
            {
                camera.Target = target;
                camera.Update();
                check(Basis() == previous && Orthonormal(), $"preserve basis at {target}");
            }
            camera.Target = new Vector3(-4, 2, 3);
            camera.Update();
            check(Basis() != previous && Orthonormal(), "recover after invalid direction");
            camera.Reset();
            check(Basis() == new Vector4(0, -1, -1, 0), "reset replaces previous basis");
        }

        private static void CheckReconciliation(Action<bool, string> check)
        {
            const int slot = 2;
            FormCorrection Tick(bool current, bool wanted, bool active = false)
                => ReconcileForm(slot, current, wanted, active);
            void ExpectFrames(int count, bool current, bool wanted, string name)
            {
                bool passed = true;
                for (int i = 0; i < count; i++)
                {
                    passed &= Tick(current, wanted) == FormCorrection.None;
                }
                check(passed, name);
            }
            Reset();
            check(ReconcileForm(-1, false, true, false) == FormCorrection.None
                && ReconcileForm(PlayerEntity.SlotCapacity, false, true, false) == FormCorrection.None,
                "invalid slots ignored");
            foreach (bool target in new[] { false, true })
            {
                Reset();
                check(Tick(target, target) == FormCorrection.None, "matching form");
                ExpectFrames(7, !target, target, "stable mismatch waits seven frames");
                check(Tick(!target, target) == FormCorrection.Switch, "switch on eighth mismatch");
                ExpectFrames(11, !target, target, "refused or unstarted switch has bounded wait");
                check(Tick(!target, target) == FormCorrection.Force, "force on twelfth failed-start frame");
                ExpectFrames(7, !target, target, "force resets counters");
                check(Tick(!target, target) == FormCorrection.Switch, "next recovery tries engine first");
                bool observed = true;
                for (int frame = 0; frame < 120; frame++)
                {
                    observed &= Tick(!target, target, true) == FormCorrection.None;
                }
                check(observed, "never interrupt corrective animation, even past fallback deadline");
                check(Tick(target, target) == FormCorrection.None, "successful correction resets");
                ExpectFrames(7, !target, target, "successful correction has no leftover attempt");
                check(Tick(!target, target) == FormCorrection.Switch, "new correction uses real transition");
                check(Tick(!target, target, true) == FormCorrection.None, "observe failed correction");
                check(Tick(!target, target) == FormCorrection.Force, "wrong corrective result forced without grace");

                Reset();
                check(Tick(!target, !target, true) == FormCorrection.None,
                    "morph/unmorph observed even when form currently matches");
                ExpectFrames(30, target, !target, "completed normal transition gets thirty frames");
                ExpectFrames(7, target, !target, "mismatch counted only after grace");
                check(Tick(target, !target) == FormCorrection.Switch, "stale state eventually repaired");

                Reset();
                Tick(!target, !target, true);
                Tick(target, !target);
                check(Tick(target, target) == FormCorrection.None, "snapshot catches up during grace");
                ExpectFrames(7, target, !target, "matching snapshot clears grace");
                check(Tick(target, !target) == FormCorrection.Switch, "no stale grace after match");
            }
            foreach (Action reset in new Action[] { Reset, NoteRoomChanged, () => ForgetSlot(slot) })
            {
                // Exercise each history component at each lifecycle boundary.
                for (int state = 0; state < 4; state++)
                {
                    Reset();
                    if (state == 0) { Tick(false, true); }
                    if (state == 1) { for (int i = 0; i < 8; i++) Tick(false, true); }
                    if (state >= 2) { Tick(false, false, true); }
                    if (state == 3) { Tick(true, false); }
                    reset();
                    ExpectFrames(7, false, true, "lifecycle clears history");
                    check(Tick(false, true) == FormCorrection.Switch, "clean recovery after lifecycle reset");
                }
            }
            Reset();
            Tick(false, true);
            check(ReconcileForm(slot + 1, false, true, false) == FormCorrection.None,
                "slots maintain independent histories");
            // Measure after initialization/JIT: per-frame decisions allocate nothing.
            Tick(false, false);
            long before = GC.GetAllocatedBytesForCurrentThread();
            for (int i = 0; i < 10000; i++) ReconcileForm(slot, false, true, true);
            check(GC.GetAllocatedBytesForCurrentThread() == before, "allocation-free reconciliation");
        }
    }
}

namespace MphRead.Entities
{
    public partial class PlayerEntity
    {
        internal static void ModCheckAltFlickInputOrder(Action<bool, string> check)
        {
            var scene = (Scene)System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(typeof(Scene));
            typeof(Scene).GetField("_movieFrameIndex", System.Reflection.BindingFlags.Instance
                | System.Reflection.BindingFlags.NonPublic)!.SetValue(scene, -1);
            Reset();
            Construct(scene);
            var player = Main;
            player.LoadFlags = LoadFlags.Active;
            player._health = 99;
            player.Flags1 = PlayerFlags1.AltForm;
            player._abilities = AbilityFlags.SpireAltAttack;
            var keyboard = SyntheticInput.CreateKeyboard();
            var mouse = SyntheticInput.CreateMouse();
            try
            {
                NetPlayerBridge.Reset();
                player.AltFlickRequested = true; // Android delivers the swipe before the input pass.
                ProcessInput(keyboard, mouse, false);
                NetPlayerBridge.RecordPresses(player); // NetHooks.AfterInput runs BEFORE ProcessAlt.
                var intent = NetPlayerBridge.CaptureIntent(player);
                check(((IntentButtons)intent.Presses![0] & IntentButtons.AltAttack) != 0,
                    "production input order records Spire flick before simulation");
                check(player.Controls.AltAttack.IsPressed, "Spire local attack is ready before simulation");
                ProcessInput(keyboard, mouse, false);
                NetPlayerBridge.RecordPresses(player);
                intent = NetPlayerBridge.CaptureIntent(player);
                check(((IntentButtons)intent.Presses![0] & IntentButtons.AltAttack) == 0,
                    "next input pass does not repeat Spire flick");
            }
            finally { Reset(); NetPlayerBridge.Reset(); MouseFlick.Reset(); }
        }

        // Unloaded players exercise the shared gesture and real wire/press paths.
        // No game assets are needed until an attack animation is actually played.
        internal static void ModCheckAltFlickRouting(Action<bool, string> check)
        {
            var player = new PlayerEntity(1, null!);
            player._health = 99;
            player.Flags1 = PlayerFlags1.AltForm;
            player._abilities = AbilityFlags.SpireAltAttack;
            player.Controls.Boost.IsDown = true;
            player.AltFlickRequested = true;
            player.AltFlickX = 0.6f;
            player.AltFlickY = -0.8f;
            player.ModPrepareSpireFlick();
            check(player.Controls.AltAttack.IsPressed && !player.Controls.AltAttack.IsDown
                && player.Input.HasInput, "Spire emits canonical one-shot despite Boost bind");
            check(!player.AltFlickRequested && player.AltFlickX == 0 && player.AltFlickY == 0,
                "gesture state cleared together");

            NetPlayerBridge.Reset();
            NetPlayerBridge.RecordPresses(player);
            IntentPacket intent = NetPlayerBridge.CaptureIntent(player);
            check(((IntentButtons)intent.Presses![0] & IntentButtons.AltAttack) != 0,
                "Spire flick recorded by existing network press history");
            // Avoid weapon/model setup in this unloaded-player check.
            intent.WeaponSelect = 0xFF;
            intent.Aim = Vector3.UnitZ;
            intent.Frame = 11;
            byte[] wire = new byte[IntentPacket.FullSize];
            intent.Write(wire);
            IntentPacket received = IntentPacket.Read(wire);
            var authority = new PlayerEntity(2, null!);
            var viewer = new PlayerEntity(3, null!);
            IntentPacket baseline = received;
            baseline.Frame = 10;
            baseline.Presses = new uint[IntentPacket.PressHistory];
            NetPlayerBridge.ApplyIntent(authority, baseline);
            NetPlayerBridge.ApplyIntent(viewer, baseline);
            NetPlayerBridge.ApplyIntent(authority, received);
            NetPlayerBridge.ApplyIntent(viewer, received);
            check(authority.Controls.AltAttack.IsPressed && viewer.Controls.AltAttack.IsPressed,
                "serialized flick edge reaches authority and viewer controls");
            NetPlayerBridge.ApplyIntent(authority, received);
            NetPlayerBridge.ApplyIntent(viewer, received);
            check(!authority.Controls.AltAttack.IsPressed && !viewer.Controls.AltAttack.IsPressed,
                "repeated history does not replay attack");

            player.Controls.AltAttack.IsPressed = false;
            check(!player.ModConsumeAltFlick(out _, out _) && !player.Controls.AltAttack.IsPressed,
                "one gesture cannot fire twice");
            player._abilities = AbilityFlags.Boost;
            player.AltFlickRequested = true;
            player.AltFlickX = -1;
            check(player.ModConsumeAltFlick(out float x, out float y) && x == -1 && y == 0
                && !player.Controls.AltAttack.IsPressed, "Samus flick retains aimed boost event");
            foreach (AbilityFlags ability in new[] { AbilityFlags.Bombs, AbilityFlags.NoxusAltAttack,
                AbilityFlags.TraceAltAttack, AbilityFlags.WeavelAltAttack })
            {
                player._abilities = ability;
                player.AltFlickRequested = true;
                check(!player.ModConsumeAltFlick(out _, out _) && !player.Controls.AltAttack.IsPressed
                    && !player.AltFlickRequested, "unsupported hunter discards flick");
            }
            player._abilities = AbilityFlags.SpireAltAttack;
            foreach (PlayerFlags1 flags in new[] { PlayerFlags1.None, PlayerFlags1.Morphing,
                PlayerFlags1.AltForm | PlayerFlags1.Morphing, PlayerFlags1.AltForm | PlayerFlags1.Unmorphing })
            {
                player.Flags1 = flags;
                player.AltFlickRequested = true;
                check(!player.ModConsumeAltFlick(out _, out _) && !player.AltFlickRequested,
                    "biped/transition discards flick");
            }
            player.Flags1 = PlayerFlags1.AltForm;
            player._health = 0;
            player.AltFlickRequested = true;
            check(!player.ModConsumeAltFlick(out _, out _), "dead player discards flick");
            player._health = 99;
            player._frozenTimer = 10;
            player.AltFlickRequested = true;
            check(!player.ModConsumeAltFlick(out _, out _), "frozen player discards flick");
            player._frozenTimer = 0;
            check(!player.ModConsumeAltFlick(out _, out _), "discarded flick cannot fire after thaw");

            MouseFlick.Reset();
            float delta = 600 / Mods.InputSettings.MouseSensitivity;
            MouseFlick.Check(0, 0, 1000, out _, out _);
            check(MouseFlick.Check(delta, 0, 1001, out x, out y) && x == 1 && y == 0,
                "desktop horizontal flick normalized");
            bool repeated = false;
            for (ulong frame = 1002; frame < 1050; frame++)
                repeated |= MouseFlick.Check(delta, 0, frame, out _, out _);
            check(!repeated, "continued mouse sweep cannot repeat flick");
            MouseFlick.Check(0, 0, 1050, out _, out _);
            check(MouseFlick.Check(delta, -delta, 1051, out x, out y)
                && x > 0 && y < 0 && MathF.Abs(x * x + y * y - 1) < 0.00001f,
                "desktop diagonal flick keeps normalized diagonal");
            MouseFlick.Reset();
        }
    }
}
