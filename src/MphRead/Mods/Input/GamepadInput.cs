using System;
using MphRead.Entities;

namespace MphRead.Mods.Input
{
    /// <summary>
    /// A gamepad, on any platform, driving the game.
    ///
    /// The same trick the Android touch controls use, from the other end:
    /// rather than fork <c>ProcessAllInput</c>, this waits until it has run
    /// and then *adds* the pad's contribution to the same <see cref="Keybind"/>
    /// flags the keyboard just filled in. Everything downstream -- firing,
    /// morphing, the weapon wheel, what goes on the wire as an intent -- reads
    /// those flags and cannot tell where they came from, so a pad and a
    /// keyboard work at once and neither had to be special-cased.
    ///
    /// Aim is the one thing that cannot go through a keybind, because a stick
    /// is analogue and a key is not. It goes in where the mouse's does, at
    /// <c>ApplyModAim</c>, in the same units (degrees of turn per frame) and
    /// at the same point in the frame -- an aim applied at a different moment
    /// from the mouse's would feel different for reasons nobody could name.
    ///
    /// Where the state comes from is the platform's business:
    /// <see cref="GamepadDesktop"/> polls GLFW, and the Android head adds up
    /// the events its window receives. Both write <see cref="State"/>.
    /// </summary>
    public static class GamepadInput
    {
        /// <summary>The pad as of this frame.</summary>
        public static GamepadState State;

        private static GamepadButtons _previous;

        /// <summary>Buttons that went down this frame, for the one-shot actions.</summary>
        private static GamepadButtons _pressed;

        /// <summary>True while a pad is connected and turned on in the settings.</summary>
        public static bool Active => State.Connected && InputSettings.GamepadEnabled;

        /// <summary>
        /// What the right stick asked for this frame, in degrees of turn --
        /// the same unit <c>UpdateAimX</c> and <c>UpdateAimY</c> take, and the
        /// same unit the mouse arrives in after its own division.
        /// </summary>
        public static float AimDeltaX { get; private set; }
        public static float AimDeltaY { get; private set; }

        /// <summary>
        /// Degrees of turn per frame at full stick deflection, before the
        /// player's sensitivity multiplier. 3.5 is 210 degrees a second, which
        /// is where console shooters have sat since they settled the question.
        /// </summary>
        private const float TurnRate = 3.5f;

        /// <summary>
        /// How hard a trigger has to be pulled to count as a press. Two
        /// thirds rather than a hair off zero: a trigger resting under a
        /// finger is not a request to fire, and every pad's resting value
        /// drifts.
        /// </summary>
        private const float TriggerThreshold = 0.65f;

        /// <summary>
        /// How far a stick has to go before it counts as movement. The walk
        /// keys are on or off, so this is where a stick becomes a direction.
        /// Larger than the aim dead zone below it, because a thumb resting on
        /// the stick should not walk you off a ledge.
        /// </summary>
        private const float WalkThreshold = 0.5f;

        /// <summary>
        /// Called once a frame, before the pad is read for anything. Works out
        /// the rising edges and this frame's aim.
        /// </summary>
        public static void BeginFrame()
        {
            if (!Active)
            {
                _pressed = GamepadButtons.None;
                _previous = GamepadButtons.None;
                AimDeltaX = 0;
                AimDeltaY = 0;
                return;
            }
            _pressed = State.Buttons & ~_previous;
            _previous = State.Buttons;
            (float x, float y) = ApplyDeadZone(State.RightX, State.RightY);
            // Squared response, keeping the sign: the useful half of a stick's
            // travel is the first half, where a shooter wants to make small
            // corrections. Linear, the same stick has to do both the flick and
            // the nudge and is bad at the nudge.
            float sensitivity = InputSettings.GamepadLookSensitivity;
            AimDeltaX = -x * MathF.Abs(x) * TurnRate * sensitivity;
            AimDeltaY = y * MathF.Abs(y) * TurnRate * sensitivity
                * (InputSettings.GamepadInvertY ? -1 : 1);
        }

        /// <summary>
        /// A radial dead zone, applied to the pair rather than to each axis.
        ///
        /// Per-axis is the common mistake and it is visible: the neutral area
        /// comes out a square, so a stick pushed diagonally starts to move
        /// before one pushed straight, and slow circles turn into slow
        /// octagons. Rescaled past the edge, so the first movement past the
        /// dead zone is the smallest movement rather than a jump to the dead
        /// zone's own size.
        /// </summary>
        private static (float X, float Y) ApplyDeadZone(float x, float y)
        {
            float dead = InputSettings.GamepadDeadZone;
            float length = MathF.Sqrt(x * x + y * y);
            if (length <= dead)
            {
                return (0, 0);
            }
            if (length == 0)
            {
                return (0, 0);
            }
            float scaled = MathF.Min((length - dead) / (1 - dead), 1);
            return (x / length * scaled, y / length * scaled);
        }

        /// <summary>
        /// True once for each press of Start, which opens and closes the pause
        /// menu.
        ///
        /// Taken rather than read, and not a keybind like everything else,
        /// because the pause menu is not a thing the *player* does: it is a
        /// window the host platform opens, so it has to be acted on by
        /// whatever owns a window rather than by the entity. The desktop
        /// consumes this after the frame updates and Android in its own loop.
        /// </summary>
        public static bool TakeMenuPress()
        {
            if ((_pressed & GamepadButtons.Start) == 0)
            {
                return false;
            }
            // Cleared so a frame that is drawn twice, or a caller that asks
            // twice, cannot open the menu and close it again in one press.
            _pressed &= ~GamepadButtons.Start;
            return true;
        }

        /// <summary>
        /// Add the pad to what the keyboard and mouse already said, for the
        /// player this machine is driving.
        ///
        /// After <c>PlayerEntity.ProcessInput</c>, never instead of it: the
        /// binds are filled from the keyboard first and this only ever turns
        /// things on, so a player with a hand on each works, and a pad sitting
        /// on a desk contributes nothing.
        /// </summary>
        public static void Apply(PlayerEntity? player)
        {
            if (player == null || !Active || player.IsBot
                || !player.LoadFlags.TestFlag(LoadFlags.Active))
            {
                return;
            }
            PlayerControls controls = player.Controls;
            (float moveX, float moveY) = ApplyDeadZone(State.LeftX, State.LeftY);
            // Both sets, as the touch controls do: walking reads Move and the
            // morph ball reads Roll, and a player who has bound them to
            // different keys expects the stick to drive whichever form they
            // are in.
            Hold(controls.MoveUp, moveY > WalkThreshold);
            Hold(controls.RollUp, moveY > WalkThreshold);
            Hold(controls.MoveDown, moveY < -WalkThreshold);
            Hold(controls.RollDown, moveY < -WalkThreshold);
            Hold(controls.MoveLeft, moveX < -WalkThreshold);
            Hold(controls.RolltLeft, moveX < -WalkThreshold);
            Hold(controls.MoveRight, moveX > WalkThreshold);
            Hold(controls.RollRight, moveX > WalkThreshold);

            // FIRE is both attacks, for the reason the touch button is: the DS
            // had one attack button, and the game's own defaults still bind
            // the gun and the alt form's attack to the same one.
            Hold(controls.Shoot, GamepadButtons.RightTrigger);
            Hold(controls.AltAttack, GamepadButtons.RightTrigger);
            Hold(controls.Zoom, GamepadButtons.LeftTrigger);
            Hold(controls.Jump, GamepadButtons.A);
            Hold(controls.Boost, GamepadButtons.A);
            Hold(controls.Morph, GamepadButtons.B);
            Hold(controls.Scan, GamepadButtons.X);
            Hold(controls.ScanVisor, GamepadButtons.Y);
            // No weapon wheel on a pad, deliberately: PlayerHud's weapon
            // select reads the *absolute* pointer position, because on the DS
            // it was a touch screen and the slot under the stylus is the one
            // that gets picked. A stick has no position, so driving it would
            // mean warping the mouse cursor about to fake one -- which fights
            // whoever also has a hand on the mouse, and is a surprising thing
            // for a controller to do to a desktop. The bumpers and the d-pad
            // below reach every weapon without it.
            //
            // The scoreboard is the DS's own pause button, which is what Back
            // is shaped like on every pad.
            Hold(controls.Pause, GamepadButtons.Back);

            Hold(controls.NextWeapon, GamepadButtons.RightBumper | GamepadButtons.DpadRight);
            Hold(controls.PrevWeapon, GamepadButtons.LeftBumper | GamepadButtons.DpadLeft);
            Hold(controls.Missile, GamepadButtons.DpadUp);
            Hold(controls.PowerBeam, GamepadButtons.DpadDown);
        }

        private static void Hold(Keybind bind, GamepadButtons buttons)
        {
            Hold(bind, (State.Buttons & buttons) != 0, (_pressed & buttons) != 0);
        }

        private static void Hold(Keybind bind, bool down)
        {
            // No edge of its own: a stick direction is a state, and the things
            // that read IsPressed are all buttons.
            Hold(bind, down, pressed: false);
        }

        /// <summary>
        /// Turn a bind on, never off.
        ///
        /// The or is the whole point: <c>ProcessInput</c> has already written
        /// what the keyboard and mouse are doing, and a pad that assigned
        /// instead of adding would release a key somebody is holding every
        /// frame it did not have that button pressed.
        /// </summary>
        private static void Hold(Keybind bind, bool down, bool pressed)
        {
            if (down)
            {
                bind.IsDown = true;
            }
            if (pressed)
            {
                bind.IsPressed = true;
            }
        }
    }
}
