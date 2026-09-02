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
        /// <summary>Opens and closes the scan visor: the desktop's SCAN VISOR (E).</summary>
        ScanVisor,
        /// <summary>
        /// Scans whatever is targeted, held: the desktop's SCAN (Q). Its own
        /// button, because it is a second step rather than another way to
        /// press VISOR -- reading an entry is not leaving the visor, and one
        /// button trying to be both made a press mean either.
        /// </summary>
        Scan,
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
        /// <summary>
        /// Whether it is on screen at all. FIRE and SCAN share a place and
        /// take turns: the visor cannot shoot and the gun cannot scan.
        /// </summary>
        public bool Visible { get; set; } = true;

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
            // SCAN before FIRE: they sit in the same place, and the one that
            // is visible is the one a thumb should find there.
            new TouchButton(TouchAction.Scan, "SCAN") { Visible = false },
            new TouchButton(TouchAction.Shoot, "FIRE"),
            new TouchButton(TouchAction.Jump, "JUMP"),
            new TouchButton(TouchAction.Morph, "MORPH"),
            // No ALT button. It shared MouseButton.Left with FIRE -- the
            // game's own default, the DS having had one attack button -- so
            // it was a second way to press the thing FIRE presses, and having
            // both is what stopped FIRE working at all. FIRE is both attacks
            // now; see GameView.CollectInput. VISOR now sits where ALT did.
            new TouchButton(TouchAction.ScanVisor, "VISOR"),
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
        private const float SwipeBoostDistanceDp = 50f;
        private const long SwipeBoostWindowMs = 120;
        private const long SwipeBoostCooldownMs = 350;
        private readonly SwipeTracker _aimSwipe = new SwipeTracker();
        private readonly SwipeTracker _fireAimSwipe = new SwipeTracker();
        // Zero, not long.MinValue: TickCount64 minus long.MinValue overflows
        // to a large negative number, and the cooldown below would then never
        // be satisfied -- which is exactly what stopped this firing at all.
        private long _lastSwipeBoostTime;
        private bool _swipeBoostPending;
        private bool _swipeBoostEnabled;

        /// <summary>
        /// Whether a flick means anything right now -- it is the morph ball's
        /// boost, so only in the ball. Off, the flick is left alone to be the
        /// aim it looks like.
        /// </summary>
        public bool SwipeBoostEnabled
        {
            get
            {
                lock (_lock)
                {
                    return _swipeBoostEnabled;
                }
            }
            set
            {
                lock (_lock)
                {
                    _swipeBoostEnabled = value;
                }
            }
        }

        /// <summary>
        /// Whether the scan visor is open, which is what swaps FIRE for SCAN.
        /// Set from the game thread, so a change repaints through
        /// <see cref="Invalidated"/> rather than waiting for the next touch.
        /// </summary>
        public bool ScanVisorActive
        {
            get
            {
                lock (_lock)
                {
                    return _scanVisorActive;
                }
            }
            set
            {
                bool changed = false;
                lock (_lock)
                {
                    if (_scanVisorActive != value)
                    {
                        _scanVisorActive = value;
                        changed = true;
                        foreach (TouchButton button in _buttons)
                        {
                            if (button.Action == TouchAction.Scan)
                            {
                                button.Visible = value;
                            }
                            else if (button.Action == TouchAction.Shoot)
                            {
                                button.Visible = !value;
                            }
                        }
                    }
                }
                if (changed)
                {
                    Invalidated?.Invoke();
                }
            }
        }
        private bool _scanVisorActive;

        /// <summary>
        /// Asked to repaint, for the changes that do not come from a touch.
        /// The view sets this; nothing here knows what a view is.
        /// </summary>
        public Action? Invalidated { get; set; }

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
                // The same place and the same size: in the visor, that thumb
                // has nothing to shoot with and everything to scan with.
                Place(TouchAction.Scan, width - 0.17f * h, h - 0.19f * h, 0.105f * h);
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
                    if (button.Visible && button.Contains(x, y))
                    {
                        _buttonPointers[pointerId] = button.Action;
                        _held.Add(button.Action);
                        // Both of the buttons that live under the aiming
                        // thumb drag the aim as well: keeping a target
                        // centred matters as much while scanning it as it
                        // does while shooting at it.
                        if (button.Action == TouchAction.Shoot || button.Action == TouchAction.Scan)
                        {
                            _fireAimPointer = pointerId;
                            _fireAimLastX = x;
                            _fireAimLastY = y;
                            _fireAimSwipe.Reset();
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
                    _aimSwipe.Reset();
                }
            }
        }

        /// <summary>
        /// The last few positions of one finger, for telling a flick from a
        /// drag. Only the newest matter, so it is a small ring.
        /// </summary>
        private sealed class SwipeTracker
        {
            private const int Capacity = 12;
            private readonly float[] _x = new float[Capacity];
            private readonly float[] _y = new float[Capacity];
            private readonly long[] _time = new long[Capacity];
            private int _count;
            private int _newest = -1;

            public void Reset()
            {
                _count = 0;
                _newest = -1;
            }

            public void Add(float x, float y, long now)
            {
                _newest = (_newest + 1) % Capacity;
                _x[_newest] = x;
                _y[_newest] = y;
                _time[_newest] = now;
                if (_count < Capacity)
                {
                    _count++;
                }
            }

            /// <summary>
            /// The furthest the finger has come from any sample still inside
            /// the window -- and always from at least the one before this,
            /// however old it is.
            ///
            /// That last part is the whole trick. A finger holding still
            /// sends no MOVE events at all, so a flick that follows one can
            /// arrive as a single jump whose predecessor is seconds old, and
            /// a window that only trusted its own age would throw away
            /// exactly the sample the flick lives in.
            /// </summary>
            public float Displacement(long now, long windowMs)
            {
                if (_count < 2)
                {
                    return 0;
                }
                float newestX = _x[_newest];
                float newestY = _y[_newest];
                float best = 0;
                for (int i = 1; i < _count; i++)
                {
                    int index = (_newest - i + Capacity) % Capacity;
                    if (i > 1 && now - _time[index] > windowMs)
                    {
                        break;
                    }
                    float dx = newestX - _x[index];
                    float dy = newestY - _y[index];
                    float distance = dx * dx + dy * dy;
                    if (distance > best)
                    {
                        best = distance;
                    }
                }
                return MathF.Sqrt(best);
            }
        }

        /// <summary>Called with the lock already held.</summary>
        private void CheckSwipeBoost(SwipeTracker tracker, float x, float y)
        {
            long now = Environment.TickCount64;
            tracker.Add(x, y, now);
            if (!_swipeBoostEnabled || now - _lastSwipeBoostTime < SwipeBoostCooldownMs)
            {
                return;
            }
            float threshold = SwipeBoostDistanceDp * Density;
            if (tracker.Displacement(now, SwipeBoostWindowMs) > threshold)
            {
                _swipeBoostPending = true;
                _lastSwipeBoostTime = now;
                tracker.Reset();
                // The flick was the boost, not a look. Letting it through as
                // aim as well would swing the camera through the whole of it.
                _aimDeltaX = 0;
                _aimDeltaY = 0;
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
                    CheckSwipeBoost(_aimSwipe, x, y);
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
                    CheckSwipeBoost(_fireAimSwipe, x, y);
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
                    _aimSwipe.Reset();
                    return;
                }
                if (pointerId == _fireAimPointer)
                {
                    _fireAimPointer = -1;
                    _fireAimSwipe.Reset();
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
                _aimSwipe.Reset();
                _fireAimSwipe.Reset();
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
