using System;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace MphRead.Mods.Input
{
    /// <summary>
    /// The desktop's pad, read from GLFW.
    ///
    /// <c>glfwGetGamepadState</c> rather than the raw joystick API, and that
    /// is the whole reason this file is four lines of work instead of a
    /// per-pad mapping table: GLFW carries SDL's controller database, so a
    /// DualShock, a Switch Pro pad, an eight-bit-do and an Xbox pad all arrive
    /// already remapped onto one layout, over USB or over Bluetooth alike --
    /// the operating system has already decided which of those it is by the
    /// time a pad reaches here, and Bluetooth is not a different kind of
    /// device to it.
    ///
    /// Polled rather than evented, because that is the only shape GLFW offers
    /// for pads and because the game is a frame loop anyway. Sixteen slots is
    /// GLFW's own maximum; the first one that answers wins, since nothing here
    /// has a second player to give the second pad to.
    /// </summary>
    internal static class GamepadDesktop
    {
        private static int _slot = -1;
        private static int _rescanCountdown;

        /// <summary>
        /// Frames between hunts for a pad when none is connected. Once a
        /// second: <c>glfwGetGamepadState</c> on sixteen empty slots is cheap
        /// but not free, and a pad switched on mid-match should be usable
        /// without restarting anything.
        /// </summary>
        private const int RescanFrames = 60;

        // GLFW's own gamepad indices. Written out rather than taken from an
        // enum because OpenTK 4.9 binds `glfwGetGamepadState` without binding
        // the two enums that name its slots -- `GamepadState` is a pair of
        // fixed arrays and nothing else. These are GLFW_GAMEPAD_BUTTON_* and
        // GLFW_GAMEPAD_AXIS_*, which are part of its stable API.
        private const int ButtonA = 0;
        private const int ButtonB = 1;
        private const int ButtonX = 2;
        private const int ButtonY = 3;
        private const int ButtonLeftBumper = 4;
        private const int ButtonRightBumper = 5;
        private const int ButtonBack = 6;
        private const int ButtonStart = 7;
        private const int ButtonLeftThumb = 9;
        private const int ButtonRightThumb = 10;
        private const int ButtonDpadUp = 11;
        private const int ButtonDpadRight = 12;
        private const int ButtonDpadDown = 13;
        private const int ButtonDpadLeft = 14;

        private const int AxisLeftX = 0;
        private const int AxisLeftY = 1;
        private const int AxisRightX = 2;
        private const int AxisRightY = 3;
        private const int AxisLeftTrigger = 4;
        private const int AxisRightTrigger = 5;

        public static void Poll()
        {
            if (OperatingSystem.IsAndroid())
            {
                // The Android head has no GLFW at all -- its window is an
                // Android one and its pad arrives as key and motion events.
                // See GamepadBridge there.
                return;
            }
            try
            {
                PollUnsafe();
            }
            catch (Exception ex) when (ex is DllNotFoundException
                || ex is EntryPointNotFoundException || ex is BadImageFormatException)
            {
                // No GLFW in this process: the dedicated server, the map
                // audit, anything headless. Not an error -- there is no window
                // and there is nobody holding a pad.
                GamepadInput.State = default;
                _slot = -2;
            }
        }

        private static void PollUnsafe()
        {
            if (_slot == -2)
            {
                return;
            }
            if (_slot >= 0 && TryRead(_slot))
            {
                return;
            }
            _slot = -1;
            GamepadInput.State = default;
            if (_rescanCountdown-- > 0)
            {
                return;
            }
            _rescanCountdown = RescanFrames;
            for (int i = 0; i < 16; i++)
            {
                if (TryRead(i))
                {
                    _slot = i;
                    Console.WriteLine($"[input] gamepad: {GamepadInput.State.Name}");
                    return;
                }
            }
        }

        private static unsafe bool TryRead(int slot)
        {
            if (!GLFW.JoystickIsGamepad(slot)
                || !GLFW.GetGamepadState(slot, out OpenTK.Windowing.GraphicsLibraryFramework
                    .GamepadState raw))
            {
                return false;
            }
            var state = new Mods.Input.GamepadState
            {
                Connected = true,
                Name = GLFW.GetGamepadName(slot) ?? "gamepad",
                LeftX = raw.Axes[AxisLeftX],
                // Negated: GLFW reports a stick pushed forward as -1 and every
                // caller above wants forward to be positive. See GamepadState.
                LeftY = -raw.Axes[AxisLeftY],
                RightX = raw.Axes[AxisRightX],
                RightY = -raw.Axes[AxisRightY],
                // Triggers rest at -1 and go to 1, unlike the sticks, so they
                // are moved onto 0..1 here rather than at each use.
                LeftTrigger = (raw.Axes[AxisLeftTrigger] + 1) / 2,
                RightTrigger = (raw.Axes[AxisRightTrigger] + 1) / 2
            };
            GamepadButtons buttons = GamepadButtons.None;
            Add(ref buttons, raw.Buttons, ButtonA, GamepadButtons.A);
            Add(ref buttons, raw.Buttons, ButtonB, GamepadButtons.B);
            Add(ref buttons, raw.Buttons, ButtonX, GamepadButtons.X);
            Add(ref buttons, raw.Buttons, ButtonY, GamepadButtons.Y);
            Add(ref buttons, raw.Buttons, ButtonLeftBumper, GamepadButtons.LeftBumper);
            Add(ref buttons, raw.Buttons, ButtonRightBumper, GamepadButtons.RightBumper);
            Add(ref buttons, raw.Buttons, ButtonBack, GamepadButtons.Back);
            Add(ref buttons, raw.Buttons, ButtonStart, GamepadButtons.Start);
            Add(ref buttons, raw.Buttons, ButtonLeftThumb, GamepadButtons.LeftThumb);
            Add(ref buttons, raw.Buttons, ButtonRightThumb, GamepadButtons.RightThumb);
            Add(ref buttons, raw.Buttons, ButtonDpadUp, GamepadButtons.DpadUp);
            Add(ref buttons, raw.Buttons, ButtonDpadRight, GamepadButtons.DpadRight);
            Add(ref buttons, raw.Buttons, ButtonDpadDown, GamepadButtons.DpadDown);
            Add(ref buttons, raw.Buttons, ButtonDpadLeft, GamepadButtons.DpadLeft);
            if (state.LeftTrigger > TriggerPress)
            {
                buttons |= GamepadButtons.LeftTrigger;
            }
            if (state.RightTrigger > TriggerPress)
            {
                buttons |= GamepadButtons.RightTrigger;
            }
            state.Buttons = buttons;
            GamepadInput.State = state;
            return true;
        }

        /// <summary>
        /// Matches <c>GamepadInput</c>'s own threshold: the Android head sets
        /// the same two flags from its own trigger axes, so the number has to
        /// be the same on both or the same pull fires on one platform and not
        /// the other.
        /// </summary>
        private const float TriggerPress = 0.65f;

        private static unsafe void Add(ref GamepadButtons into,
            byte* buttons, int index, GamepadButtons flag)
        {
            if (buttons[index] == (byte)JoystickInputAction.Press)
            {
                into |= flag;
            }
        }
    }
}
