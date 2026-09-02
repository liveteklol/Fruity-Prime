using System;
using System.Collections.Generic;

namespace MphRead.Droid
{
    /// <summary>What a thumb can press. Each maps to one of the player's binds.</summary>
    internal enum TouchAction
    {
        /// <summary>
        /// FIRE, which is both attacks: the gun on foot and the alt form's
        /// attack in the ball. There is no separate ALT action, because there
        /// was never a separate button -- the DS had one, and the game's
        /// defaults still bind both to it.
        /// </summary>
        Shoot,
        Jump,
        Morph,
        ScanVisor,
        WeaponMenu,
        Zoom,
        Pause,
        /// <summary>
        /// The DS's own pause button, which is the map and status screen on
        /// foot and the scoreboard while it is held in a match. MENU stopped
        /// being that when it became the way to the app's own menu, and it is
        /// still worth reaching.
        /// </summary>
        Scoreboard
    }

    internal sealed class TouchButton
    {
        public TouchAction Action { get; }
        public string Label { get; }
        public float CentreX { get; set; }
        public float CentreY { get; set; }
        public float Radius { get; set; }

        public TouchButton(TouchAction action, string label)
        {
            Action = action;
            Label = label;
        }

        public bool Contains(float x, float y)
        {
            float dx = x - CentreX;
            float dy = y - CentreY;
            // a little forgiveness: thumbs are not precise and the gap between
            // these is bigger than the slop
            float reach = Radius * 1.15f;
            return dx * dx + dy * dy <= reach * reach;
        }
    }

    /// <summary>
    /// The on-screen controls: where they are, what is being held, and how far
    /// the aiming finger has moved since the last frame read it.
    ///
    /// Deliberately free of any Android type. The view draws what this
    /// describes and reports touches to it; the game loop reads it. That split
    /// is what lets the layout be reasoned about (and moved) without going
    /// through a device.
    ///
    /// Touches arrive on the UI thread and are read on the GL thread, so
    /// everything crossing that line is behind the lock.
    /// </summary>
    internal sealed class TouchControls
    {
        [Flags]
        public enum Dir
        {
            None = 0,
            Up = 1,
            Down = 2,
            Left = 4,
            Right = 8
        }

        private readonly object _lock = new object();

        public IReadOnlyList<TouchButton> Buttons => _buttons;
        private readonly List<TouchButton> _buttons = new List<TouchButton>
        {
            new TouchButton(TouchAction.Shoot, "FIRE"),
            new TouchButton(TouchAction.Jump, "JUMP"),
            new TouchButton(TouchAction.Morph, "MORPH"),
            // No ALT button. It shared MouseButton.Left with FIRE -- the
            // game's own default, the DS having had one attack button -- so
            // it was a second way to press the thing FIRE presses, and having
            // both is what stopped FIRE working at all. FIRE is both attacks
            // now; see GameView.CollectInput. SCAN now sits where ALT did.
            new TouchButton(TouchAction.ScanVisor, "SCAN"),
            new TouchButton(TouchAction.WeaponMenu, "WEAPON"),
            new TouchButton(TouchAction.Zoom, "ZOOM"),
            new TouchButton(TouchAction.Pause, "MENU"),
            new TouchButton(TouchAction.Scoreboard, "SCORE")
        };

        public float Width { get; private set; }
        public float Height { get; private set; }
        /// <summary>Screen density, so a swipe turns the same amount on any phone.</summary>
        public float Density { get; private set; } = 1f;

        // the movement stick, which appears where the thumb lands
        public bool StickActive { get; private set; }
        public float StickX { get; private set; }
        public float StickY { get; private set; }
        public float StickKnobX { get; private set; }
        public float StickKnobY { get; private set; }
        public float StickRadius { get; private set; }
        public float StickKnobRadius { get; private set; }

        private readonly HashSet<TouchAction> _held = new HashSet<TouchAction>();
        private readonly Dictionary<int, TouchAction> _buttonPointers = new Dictionary<int, TouchAction>();
        private int _stickPointer = -1;
        private int _aimPointer = -1;
        private float _aimLastX;
        private float _aimLastY;
        private float _aimDeltaX;
        private float _aimDeltaY;
        private float _aimAbsX;
        private float _aimAbsY;
        private bool _aimDown;
        private Dir _direction;

        // FIRE doubles as an aim drag: a thumb that presses FIRE and then
        // moves keeps firing and steers the reticle, rather than releasing
        // FIRE the instant it leaves the button's circle.
        private int _fireAimPointer = -1;
        private float _fireAimLastX;
        private float _fireAimLastY;

        // A quick flick on the right side of the screen -- the aim side,
        // where nothing else about movement is being controlled -- boosts
        // in morph ball, the way a stylus flick did on the DS. See
        // GameView.CollectInput and PlayerInput's boost handling for the
        // other half of this. Tracked on both the free-look aim pointer and
        // the FIRE-drag pointer, since either thumb might do the flick.
        private const float SwipeBoostDistanceDp = 70f;
        private const long SwipeBoostWindowMs = 120;
        private const long SwipeBoostCooldownMs = 350;
        private float _aimSwipeX;
        private float _aimSwipeY;
        private long _aimSwipeTime;
        private float _fireAimSwipeX;
        private float _fireAimSwipeY;
        private long _fireAimSwipeTime;
        private long _lastSwipeBoostTime = long.MinValue;
        private bool _swipeBoostPending;

        /// <summary>Lay the controls out for a viewport of this size.</summary>
        public void Layout(float width, float height, float density)
        {
            lock (_lock)
            {
                Width = width;
                Height = height;
                Density = density <= 0 ? 1f : density;
                float h = height;
                StickRadius = 0.15f * h;
                StickKnobRadius = 0.06f * h;
                Place(TouchAction.Shoot, width - 0.17f * h, h - 0.19f * h, 0.105f * h);
                Place(TouchAction.Jump, width - 0.40f * h, h - 0.15f * h, 0.085f * h);
                Place(TouchAction.Morph, width - 0.15f * h, h - 0.47f * h, 0.080f * h);
                Place(TouchAction.ScanVisor, width - 0.38f * h, h - 0.42f * h, 0.075f * h);
                Place(TouchAction.WeaponMenu, width - 0.12f * h, 0.15f * h, 0.075f * h);
                Place(TouchAction.Zoom, width - 0.33f * h, 0.12f * h, 0.065f * h);
                Place(TouchAction.Pause, 0.11f * h, 0.12f * h, 0.060f * h);
                Place(TouchAction.Scoreboard, 0.28f * h, 0.12f * h, 0.060f * h);
            }
        }

        private void Place(TouchAction action, float x, float y, float radius)
        {
            foreach (TouchButton button in _buttons)
            {
                if (button.Action == action)
                {
                    button.CentreX = x;
                    button.CentreY = y;
                    button.Radius = radius;
                    return;
                }
            }
        }

        public bool IsHeld(TouchAction action)
        {
            lock (_lock)
            {
                return _held.Contains(action);
            }
        }

        /// <summary>
        /// The aim movement since this was last called, in density-independent
        /// pixels, and cleared by the call -- so a frame that reads it twice
        /// does not turn twice.
        /// </summary>
        public (float X, float Y) TakeAimDelta()
        {
            lock (_lock)
            {
                float x = _aimDeltaX;
                float y = _aimDeltaY;
                _aimDeltaX = 0;
                _aimDeltaY = 0;
                return (x / Density, y / Density);
            }
        }

        /// <summary>
        /// Whether a swipe boost fired since this was last called, cleared
        /// by the call so a frame that reads it twice does not boost twice.
        /// </summary>
        public bool TakeSwipeBoost()
        {
            lock (_lock)
            {
                bool pending = _swipeBoostPending;
                _swipeBoostPending = false;
                return pending;
            }
        }

        /// <summary>
        /// Treat the whole screen as a place to point at, rather than the left
        /// half as a stick.
        ///
        /// The DS had a touch screen and parts of this game still read a
        /// position off it -- the dialog boxes and their OK button among them.
        /// Those parts are drawn across the middle of the screen, which is the
        /// seam between the stick and the aim, so half of an OK button lands on
        /// a thumbstick that is not being used: the game is paused behind the
        /// box. While one of those is up the split does more harm than good.
        /// </summary>
        public bool PointerIsAbsolute { get; set; }

        /// <summary>Where the aiming finger is, for the parts that read a position.</summary>
        public (bool Down, float X, float Y) AimPosition()
        {
            lock (_lock)
            {
                return (_aimDown, _aimAbsX, _aimAbsY);
            }
        }

        public Dir Direction
        {
            get
            {
                lock (_lock)
                {
                    return _direction;
                }
            }
        }

        public void PointerDown(int pointerId, float x, float y)
        {
            lock (_lock)
            {
                foreach (TouchButton button in _buttons)
                {
                    if (button.Contains(x, y))
                    {
                        _buttonPointers[pointerId] = button.Action;
                        _held.Add(button.Action);
                        if (button.Action == TouchAction.Shoot)
                        {
                            _fireAimPointer = pointerId;
                            _fireAimLastX = x;
                            _fireAimLastY = y;
                            _fireAimSwipeX = x;
                            _fireAimSwipeY = y;
                            _fireAimSwipeTime = Environment.TickCount64;
                        }
                        return;
                    }
                }
                if (!PointerIsAbsolute && x < Width / 2 && _stickPointer == -1)
                {
                    _stickPointer = pointerId;
                    StickActive = true;
                    StickX = x;
                    StickY = y;
                    StickKnobX = x;
                    StickKnobY = y;
                    _direction = Dir.None;
                    return;
                }
                if (_aimPointer == -1)
                {
                    _aimPointer = pointerId;
                    _aimLastX = x;
                    _aimLastY = y;
                    _aimAbsX = x;
                    _aimAbsY = y;
                    _aimDown = true;
                    _aimSwipeX = x;
                    _aimSwipeY = y;
                    _aimSwipeTime = Environment.TickCount64;
                }
            }
        }

        /// <summary>
        /// Distance covered from where this flick's window started, not
        /// sample-to-sample -- Android can deliver several MOVE samples
        /// during one flick, and comparing only adjacent samples would miss
        /// it. Called with the lock already held.
        /// </summary>
        private void CheckSwipeBoost(float x, float y, ref float windowX, ref float windowY, ref long windowTime)
        {
            long now = Environment.TickCount64;
            if (now - windowTime > SwipeBoostWindowMs)
            {
                windowX = x;
                windowY = y;
                windowTime = now;
                return;
            }
            float dx = x - windowX;
            float dy = y - windowY;
            float threshold = SwipeBoostDistanceDp * Density;
            if (dx * dx + dy * dy > threshold * threshold && now - _lastSwipeBoostTime >= SwipeBoostCooldownMs)
            {
                _swipeBoostPending = true;
                _lastSwipeBoostTime = now;
                windowX = x;
                windowY = y;
                windowTime = now;
            }
        }

        public void PointerMove(int pointerId, float x, float y)
        {
            lock (_lock)
            {
                if (pointerId == _stickPointer)
                {
                    float dx = x - StickX;
                    float dy = y - StickY;
                    float length = MathF.Sqrt(dx * dx + dy * dy);
                    if (length > StickRadius)
                    {
                        // the stick follows a thumb that has slid past its edge,
                        // rather than sticking at the rim and losing the input
                        StickX += dx * (1 - StickRadius / length);
                        StickY += dy * (1 - StickRadius / length);
                        dx = x - StickX;
                        dy = y - StickY;
                        length = StickRadius;
                    }
                    StickKnobX = x;
                    StickKnobY = y;
                    _direction = Dir.None;
                    float deadzone = StickRadius * 0.28f;
                    if (length > deadzone)
                    {
                        // eight-way, like the d-pad this stands in for: the
                        // engine's movement is a set of held keys, not an axis
                        float angle = MathF.Atan2(-dy, dx) * (180f / MathF.PI);
                        if (angle < 0)
                        {
                            angle += 360f;
                        }
                        if (angle > 22.5f && angle < 157.5f)
                        {
                            _direction |= Dir.Up;
                        }
                        if (angle > 202.5f && angle < 337.5f)
                        {
                            _direction |= Dir.Down;
                        }
                        if (angle > 112.5f && angle < 247.5f)
                        {
                            _direction |= Dir.Left;
                        }
                        if (angle < 67.5f || angle > 292.5f)
                        {
                            _direction |= Dir.Right;
                        }
                    }
                    return;
                }
                if (pointerId == _aimPointer)
                {
                    CheckSwipeBoost(x, y, ref _aimSwipeX, ref _aimSwipeY, ref _aimSwipeTime);
                    _aimDeltaX += x - _aimLastX;
                    _aimDeltaY += y - _aimLastY;
                    _aimLastX = x;
                    _aimLastY = y;
                    _aimAbsX = x;
                    _aimAbsY = y;
                    return;
                }
                if (pointerId == _fireAimPointer)
                {
                    // FIRE stays held here regardless of how far the thumb
                    // drags: this pointer skips the "slides off a button
                    // releases it" rule below on purpose.
                    CheckSwipeBoost(x, y, ref _fireAimSwipeX, ref _fireAimSwipeY, ref _fireAimSwipeTime);
                    _aimDeltaX += x - _fireAimLastX;
                    _aimDeltaY += y - _fireAimLastY;
                    _fireAimLastX = x;
                    _fireAimLastY = y;
                    return;
                }
                // a thumb that slides off a button releases it, and one that
                // slides onto another does not press it: a button press is
                // where the finger landed
                if (_buttonPointers.TryGetValue(pointerId, out TouchAction action))
                {
                    foreach (TouchButton button in _buttons)
                    {
                        if (button.Action == action)
                        {
                            if (!button.Contains(x, y))
                            {
                                _buttonPointers.Remove(pointerId);
                                ReleaseAction(action);
                            }
                            return;
                        }
                    }
                }
            }
        }

        public void PointerUp(int pointerId)
        {
            lock (_lock)
            {
                if (pointerId == _stickPointer)
                {
                    _stickPointer = -1;
                    StickActive = false;
                    _direction = Dir.None;
                    return;
                }
                if (pointerId == _aimPointer)
                {
                    _aimPointer = -1;
                    _aimDown = false;
                    return;
                }
                if (pointerId == _fireAimPointer)
                {
                    _fireAimPointer = -1;
                }
                if (_buttonPointers.Remove(pointerId, out TouchAction action))
                {
                    ReleaseAction(action);
                }
            }
        }

        public void ReleaseEverything()
        {
            lock (_lock)
            {
                _buttonPointers.Clear();
                _held.Clear();
                _stickPointer = -1;
                _aimPointer = -1;
                _aimDown = false;
                _fireAimPointer = -1;
                _swipeBoostPending = false;
                StickActive = false;
                _direction = Dir.None;
                _aimDeltaX = 0;
                _aimDeltaY = 0;
            }
        }

        private void ReleaseAction(TouchAction action)
        {
            // two fingers can hold the same button; it is up when the last one is
            foreach (KeyValuePair<int, TouchAction> pair in _buttonPointers)
            {
                if (pair.Value == action)
                {
                    return;
                }
            }
            _held.Remove(action);
        }
    }
}
