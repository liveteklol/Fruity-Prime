using Android.Views;
using MphRead.Mods.Input;

namespace MphRead.Droid
{
    /// <summary>
    /// A pad on Android, turned into the same state the desktop polls.
    ///
    /// Android has no polling API for controllers: a pad arrives as ordinary
    /// key events for its buttons and <see cref="MotionEvent"/>s for its
    /// sticks and triggers, delivered to whichever view has focus. So this
    /// accumulates them into <see cref="GamepadInput.State"/>, which the
    /// shared mapping then reads exactly as it reads the desktop's -- one
    /// mapping, one set of dead zones, one answer to what a button means.
    ///
    /// Bluetooth needs nothing of its own here. A pad paired with the phone is
    /// an input device like any other by the time events reach an app; the
    /// only difference from USB is which driver produced them, and that is
    /// decided well below this.
    /// </summary>
    internal static class GamepadBridge
    {
        /// <summary>Sources that mean "this came from a pad and not a keyboard".</summary>
        private static bool IsGamepad(InputSourceType source)
        {
            return (source & InputSourceType.Gamepad) == InputSourceType.Gamepad
                || (source & InputSourceType.Joystick) == InputSourceType.Joystick
                || (source & InputSourceType.Dpad) == InputSourceType.Dpad;
        }

        /// <summary>
        /// A button, up or down. Returns false when the event was not a pad's,
        /// so the caller can carry on and offer it to the keyboard handling.
        /// </summary>
        public static bool HandleKey(Keycode keyCode, KeyEvent? e, bool down)
        {
            if (e == null || !IsGamepad(e.Source))
            {
                return false;
            }
            GamepadButtons button = Map(keyCode);
            if (button == GamepadButtons.None)
            {
                return false;
            }
            // A held button repeats: Android sends a stream of downs with a
            // rising repeat count, and letting those through would be
            // harmless for a held bind and wrong for a tapped one -- the
            // rising edge is worked out from this state, so a repeat that
            // arrived as a fresh press would fire twice.
            if (down && e.RepeatCount > 0)
            {
                return true;
            }
            GamepadState state = GamepadInput.State;
            state.Connected = true;
            state.Name ??= "gamepad";
            if (down)
            {
                state.Buttons |= button;
            }
            else
            {
                state.Buttons &= ~button;
            }
            GamepadInput.State = state;
            return true;
        }

        /// <summary>
        /// The sticks, the triggers and the hat. Returns false when the event
        /// was not a pad's.
        /// </summary>
        public static bool HandleMotion(MotionEvent? e)
        {
            if (e == null || !IsGamepad(e.Source)
                || e.Action != MotionEventActions.Move)
            {
                return false;
            }
            GamepadState state = GamepadInput.State;
            state.Connected = true;
            state.Name ??= "gamepad";
            state.LeftX = e.GetAxisValue(Axis.X);
            // Negated, as on the desktop: Android reports a stick pushed
            // forward as -1 and everything above wants forward positive.
            state.LeftY = -e.GetAxisValue(Axis.Y);
            // Z/RZ is what almost every pad reports its right stick on;
            // RX/RY is the older convention some still use, and a pad that
            // uses neither has no right stick to read.
            state.RightX = Pick(e, Axis.Z, Axis.Rx);
            state.RightY = -Pick(e, Axis.Rz, Axis.Ry);
            // Two names for one pedal: LTRIGGER/RTRIGGER is the joystick
            // convention and BRAKE/GAS is the one Android's own gamepad
            // documentation uses. Pads report one or the other.
            state.LeftTrigger = Pick(e, Axis.Ltrigger, Axis.Brake);
            state.RightTrigger = Pick(e, Axis.Rtrigger, Axis.Gas);
            GamepadButtons buttons = state.Buttons
                & ~(GamepadButtons.LeftTrigger | GamepadButtons.RightTrigger
                    | GamepadButtons.DpadUp | GamepadButtons.DpadDown
                    | GamepadButtons.DpadLeft | GamepadButtons.DpadRight);
            if (state.LeftTrigger > TriggerPress)
            {
                buttons |= GamepadButtons.LeftTrigger;
            }
            if (state.RightTrigger > TriggerPress)
            {
                buttons |= GamepadButtons.RightTrigger;
            }
            // The d-pad arrives as a hat on most pads and as key events on the
            // rest, so both paths set the same four flags. Clearing them above
            // is what makes the hat authoritative once one has been seen --
            // a pad that sends keys never moves the hat off zero, so nothing
            // is lost by it.
            float hatX = e.GetAxisValue(Axis.HatX);
            float hatY = e.GetAxisValue(Axis.HatY);
            if (hatX < -HatPress)
            {
                buttons |= GamepadButtons.DpadLeft;
            }
            else if (hatX > HatPress)
            {
                buttons |= GamepadButtons.DpadRight;
            }
            if (hatY < -HatPress)
            {
                buttons |= GamepadButtons.DpadUp;
            }
            else if (hatY > HatPress)
            {
                buttons |= GamepadButtons.DpadDown;
            }
            state.Buttons = buttons;
            GamepadInput.State = state;
            return true;
        }

        /// <summary>The same threshold the desktop reader uses. See GamepadDesktop.</summary>
        private const float TriggerPress = 0.65f;

        /// <summary>A hat is -1, 0 or 1; half is well clear of either edge.</summary>
        private const float HatPress = 0.5f;

        private static float Pick(MotionEvent e, Axis first, Axis second)
        {
            float value = e.GetAxisValue(first);
            return value != 0 ? value : e.GetAxisValue(second);
        }

        private static GamepadButtons Map(Keycode code)
        {
            return code switch
            {
                Keycode.ButtonA => GamepadButtons.A,
                Keycode.ButtonB => GamepadButtons.B,
                Keycode.ButtonX => GamepadButtons.X,
                Keycode.ButtonY => GamepadButtons.Y,
                Keycode.ButtonL1 => GamepadButtons.LeftBumper,
                Keycode.ButtonR1 => GamepadButtons.RightBumper,
                // Some pads report the triggers as buttons and never send an
                // axis for them; the axis path above sets the same flags.
                Keycode.ButtonL2 => GamepadButtons.LeftTrigger,
                Keycode.ButtonR2 => GamepadButtons.RightTrigger,
                Keycode.ButtonSelect => GamepadButtons.Back,
                Keycode.ButtonStart => GamepadButtons.Start,
                Keycode.ButtonThumbl => GamepadButtons.LeftThumb,
                Keycode.ButtonThumbr => GamepadButtons.RightThumb,
                Keycode.DpadUp => GamepadButtons.DpadUp,
                Keycode.DpadDown => GamepadButtons.DpadDown,
                Keycode.DpadLeft => GamepadButtons.DpadLeft,
                Keycode.DpadRight => GamepadButtons.DpadRight,
                _ => GamepadButtons.None
            };
        }
    }
}
