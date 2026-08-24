using System;
using System.Reflection;
using MphRead.Entities;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace MphRead.Droid
{
    /// <summary>
    /// A keyboard and a mouse that no one is holding.
    ///
    /// The engine's input path is one method -- <c>PlayerEntity.ProcessAllInput</c>
    /// -- which reads a <see cref="KeyboardState"/> and a <see cref="MouseState"/>
    /// once a frame and turns them into the <c>Keybind</c> flags the player
    /// code actually reads. The cheapest correct way to play with a touchscreen
    /// is therefore not to fork that method: it is to hand the scene a keyboard
    /// and a mouse of our own and press the keys the player has bound.
    ///
    /// That buys three things for free. Rebinding works -- a thumb on SHOOT
    /// presses whatever <c>Controls.Shoot</c> says, key or mouse button. Mouse
    /// sensitivity and inverted aim work, because aiming moves a pointer and
    /// the engine reads the same delta it always did. And the weapon wheel
    /// works, because it is a *touchscreen* mechanic on the DS: it reads the
    /// pointer's absolute position, so while the menu is held open the pointer
    /// is the finger rather than a delta.
    ///
    /// OpenTK does not let anyone else build those two types -- the
    /// constructors and the setters are internal -- so they are reached by
    /// reflection, once, into delegates. Nothing here runs per vertex; it is a
    /// handful of calls a frame.
    /// </summary>
    internal sealed class AndroidInput
    {
        public KeyboardState Keyboard { get; }
        public MouseState Mouse { get; }

        private readonly Action<KeyboardState, Keys, bool> _setKey;
        private readonly Action<MouseState, Vector2> _setPosition;
        private readonly Action<MouseState, MouseButton, bool> _setButton;

        private Vector2 _pointer;

        public AndroidInput()
        {
            Type keyboardType = typeof(KeyboardState);
            Type mouseType = typeof(MouseState);
            Keyboard = (KeyboardState)Activator.CreateInstance(keyboardType, nonPublic: true)!;
            Mouse = (MouseState)Activator.CreateInstance(mouseType, nonPublic: true)!;
            _setKey = Bind<Action<KeyboardState, Keys, bool>>(
                keyboardType.GetMethod("SetKeyState",
                    BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public),
                "KeyboardState.SetKeyState");
            _setPosition = Bind<Action<MouseState, Vector2>>(
                mouseType.GetProperty("Position",
                    BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public)?.SetMethod,
                "MouseState.Position setter");
            _setButton = Bind<Action<MouseState, MouseButton, bool>>(
                mouseType.GetProperty("Item",
                    BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public)?.SetMethod,
                "MouseState button setter");
        }

        private static T Bind<T>(MethodInfo? method, string what) where T : Delegate
        {
            if (method == null)
            {
                throw new ProgramException(
                    $"{what} is not where this build of OpenTK keeps it; touch input cannot be delivered.");
            }
            return (T)method.CreateDelegate(typeof(T));
        }

        /// <summary>Where the pointer is, in window pixels.</summary>
        public Vector2 Pointer => _pointer;

        public void SetKey(Keys key, bool down)
        {
            if (key != Keys.Unknown)
            {
                _setKey(Keyboard, key, down);
            }
        }

        public void SetButton(MouseButton button, bool down)
        {
            _setButton(Mouse, button, down);
        }

        /// <summary>Press or release whatever the player has this action bound to.</summary>
        public void Apply(Keybind bind, bool down)
        {
            if (bind.Type == ButtonType.Key)
            {
                SetKey(bind.Key, down);
            }
            else if (bind.Type == ButtonType.Mouse)
            {
                SetButton(bind.MouseButton, down);
            }
            // Scroll binds have no touch equivalent and are left alone.
        }

        /// <summary>Aiming: move the pointer, which is what the engine reads.</summary>
        public void MovePointer(float deltaX, float deltaY)
        {
            if (deltaX == 0 && deltaY == 0)
            {
                return;
            }
            _pointer.X += deltaX;
            _pointer.Y += deltaY;
            _setPosition(Mouse, _pointer);
        }

        /// <summary>
        /// Put the pointer somewhere exactly, for the parts of the game that
        /// were a touchscreen on the DS and read a position rather than a
        /// movement -- the weapon wheel and the map.
        /// </summary>
        public void PlacePointer(float x, float y)
        {
            _pointer = new Vector2(x, y);
            _setPosition(Mouse, _pointer);
        }

        /// <summary>Everything up, for a match that is starting or a view that lost focus.</summary>
        public void ReleaseAll()
        {
            PlayerControls? controls = PlayerEntity.Main?.Controls;
            if (controls == null)
            {
                return;
            }
            for (int i = 0; i < controls.All.Length; i++)
            {
                Apply(controls.All[i], down: false);
            }
        }
    }
}
