using System;
using System.Diagnostics;
using System.Threading;
using Android.Content;
using Android.Graphics;
using Android.Opengl;
using Android.Views;
using MphRead.Entities;
using MphRead.Mods.Render;
using OpenTK.Mathematics;

namespace MphRead.Droid
{
    /// <summary>
    /// The match, on a surface, with the EGL context and the thread that owns
    /// it belonging to this class rather than to <c>GLSurfaceView</c>.
    ///
    /// **That is the whole reason this is not a GLSurfaceView.** Everything the
    /// engine does with GL -- loading a room's textures, baking its geometry,
    /// compiling the shaders, drawing -- has to happen on the thread that holds
    /// the context, so the scene is *built* there, and building it takes
    /// seconds. GLSurfaceView answers a window event by handing the new size to
    /// that thread and then **waiting on the UI thread until it has been all
    /// the way round its loop**: `surfaceChanged`, `onPause` and `onResume` all
    /// do it. So any window event that landed while a room was loading froze
    /// the UI thread for the length of the load, and Android put its own "isn't
    /// responding" dialog over the loading screen -- a white box over a black
    /// one, with nothing to press, which is what starting a match from portrait
    /// did.
    ///
    /// Waiting for the window to hold still before creating the view made that
    /// rarer and could not make it impossible: a phone can resize its own
    /// window at any moment, for the system bars, for insets, for a call. Here
    /// the callbacks write a field and return, and the loop picks it up when it
    /// is next between frames. Nothing on the UI thread ever waits for a load.
    ///
    /// Two things come free from owning the context. It survives the surface
    /// going away and coming back, so a match is not lost to it (GLSurfaceView
    /// only kept it as a favour, through `PreserveEGLContextOnPause`); and
    /// pausing is a flag rather than a handshake.
    ///
    /// The loop is the desktop's, in the same order: pause, update, render.
    /// What is not the desktop's is the pacing. <c>RenderWindow</c> asks OpenTK
    /// for 60 updates a second; here the buffer swaps at the display's rate,
    /// which on a modern phone is 90 or 120, and the engine's update *is* its
    /// frame -- a render with no update in front of it draws nothing, because
    /// the render item lists are built during the update and cleared after the
    /// draw. So the thread waits for the next 60 Hz tick instead of rendering
    /// more often than the game ticks.
    /// </summary>
    internal sealed class GameView : SurfaceView, ISurfaceHolderCallback
    {
        private readonly RenderLoop _loop;

        public GameView(Context context, TouchControls controls, AndroidInput input,
            Func<AndroidInput, Vector2i, Scene> build, Action onEnd, Action onLoaded,
            Action<string> onError, Action onPauseMenu)
            : base(context)
        {
            _loop = new RenderLoop(controls, input, build, onEnd, onLoaded, onError, onPauseMenu);
            Holder?.AddCallback(this);
        }

        public Scene? Scene => _loop.Scene;

        public void Stop()
        {
            _loop.RequestStop();
        }

        public void OnPause()
        {
            _loop.SetPaused(true);
        }

        public void OnResume()
        {
            _loop.SetPaused(false);
        }

        // The three callbacks. None of them waits for the render thread; that
        // is the point of the class.

        public void SurfaceCreated(ISurfaceHolder holder)
        {
            // Nothing to do: surfaceChanged always follows, with the size.
        }

        public void SurfaceChanged(ISurfaceHolder holder, Format format, int width, int height)
        {
            _loop.SurfaceReady(holder, width, height);
        }

        public void SurfaceDestroyed(ISurfaceHolder holder)
        {
            _loop.SurfaceGone();
        }

        /// <summary>
        /// The GL context, the thread that owns it, and the game loop that
        /// runs on it.
        /// </summary>
        private sealed class RenderLoop
        {
            private const double FrameSeconds = 1.0 / 60.0;

            /// <summary>
            /// Density-independent pixels of drag per unit of mouse movement.
            /// One means a swipe turns as far as a mouse moved the same
            /// distance would, which the player's own sensitivity setting then
            /// scales the way it does everywhere else.
            /// </summary>
            private const float AimScale = 1f;

            // EGL_OPENGL_ES3_BIT_KHR. EGL14 exposes the ES2 bit and stops
            // there, and an ES2 config will happily give an ES3 context on most
            // drivers -- but "most" is how a phone gets a context that fails
            // every call in GlEs with no message.
            private const int OpenGlEs3Bit = 0x40;

            /// <summary>
            /// How long <see cref="SurfaceGone"/> will wait for the render
            /// thread to let go. Android wants the surface unused by the time
            /// that callback returns, and this is the one place where that is
            /// worth a wait at all -- but not an unbounded one: the thread
            /// cannot answer from inside a room load, and hanging the UI thread
            /// is the thing this class exists to stop.
            /// </summary>
            private const int SurfaceReleaseMs = 2000;

            private readonly TouchControls _controls;
            private readonly AndroidInput _input;
            private readonly Func<AndroidInput, Vector2i, Scene> _build;
            private readonly Action _onEnd;
            private readonly Action _onLoaded;
            private readonly Action<string> _onError;
            private readonly Stopwatch _clock = new Stopwatch();
            private readonly object _lock = new object();
            private readonly Thread _thread;

            private ISurfaceHolder? _holder;
            private Vector2i _wanted;
            private bool _paused;
            private bool _stopping;
            private bool _holdingSurface;
            private bool _ended;
            private bool _dialogClickDown;
            private readonly Action _onPauseMenu;
            private bool _menuWasHeld;
            private bool _spectateCycleHeld;

            private EGLDisplay? _display;
            private EGLConfig? _config;
            private EGLSurface? _eglSurface;
            private EGLContext? _context;
            private ISurfaceHolder? _boundTo;
            private Vector2i _size;
            private double _nextFrame;

            public Scene? Scene { get; private set; }

            public RenderLoop(TouchControls controls, AndroidInput input,
                Func<AndroidInput, Vector2i, Scene> build, Action onEnd, Action onLoaded,
                Action<string> onError, Action onPauseMenu)
            {
                _onPauseMenu = onPauseMenu;
                _controls = controls;
                _input = input;
                _build = build;
                _onEnd = onEnd;
                _onLoaded = onLoaded;
                _onError = onError;
                _thread = new Thread(Run) { Name = "FruityPrime GL", IsBackground = true };
                _thread.Start();
            }

            public void RequestStop()
            {
                lock (_lock)
                {
                    _stopping = true;
                    Monitor.PulseAll(_lock);
                }
            }

            public void SetPaused(bool paused)
            {
                lock (_lock)
                {
                    _paused = paused;
                    Monitor.PulseAll(_lock);
                }
            }

            public void SurfaceReady(ISurfaceHolder holder, int width, int height)
            {
                if (width <= 0 || height <= 0)
                {
                    return;
                }
                lock (_lock)
                {
                    _holder = holder;
                    _wanted = new Vector2i(width, height);
                    Monitor.PulseAll(_lock);
                }
            }

            public void SurfaceGone()
            {
                lock (_lock)
                {
                    _holder = null;
                    Monitor.PulseAll(_lock);
                    long deadline = Environment.TickCount64 + SurfaceReleaseMs;
                    while (_holdingSurface)
                    {
                        int left = (int)(deadline - Environment.TickCount64);
                        if (left <= 0)
                        {
                            // Mid-load, almost certainly. The thread drops the
                            // surface the moment it looks up, and a swap
                            // against a surface the framework has taken back
                            // fails rather than crashing -- which is handled.
                            Console.WriteLine("[android] the surface went away while the GL thread "
                                + "was busy; carrying on without waiting for it");
                            break;
                        }
                        Monitor.Wait(_lock, left);
                    }
                }
            }

            private void Run()
            {
                try
                {
                    Loop();
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[android] the render thread stopped: {ex}");
                    if (!_ended)
                    {
                        _ended = true;
                        _onError(ex.Message);
                    }
                }
                finally
                {
                    ReleaseSurface();
                    DestroyContext();
                }
            }

            private void Loop()
            {
                while (true)
                {
                    ISurfaceHolder holder;
                    Vector2i wanted;
                    lock (_lock)
                    {
                        while (!_stopping && (_holder == null || _paused))
                        {
                            // Nothing to draw into, or nobody looking. Let go
                            // of the surface first if it is the former, so
                            // SurfaceGone is not left waiting on us.
                            if (_holder == null && _holdingSurface)
                            {
                                Monitor.Exit(_lock);
                                try
                                {
                                    ReleaseSurface();
                                }
                                finally
                                {
                                    Monitor.Enter(_lock);
                                }
                                Monitor.PulseAll(_lock);
                                continue;
                            }
                            Monitor.Wait(_lock);
                        }
                        if (_stopping)
                        {
                            break;
                        }
                        holder = _holder!;
                        wanted = _wanted;
                    }
                    if (!BindSurface(holder, wanted))
                    {
                        continue;
                    }
                    if (Scene == null)
                    {
                        if (_ended)
                        {
                            break;
                        }
                        BuildScene();
                        continue;
                    }
                    if (!DrawFrame())
                    {
                        break;
                    }
                }
                Scene? scene = Scene;
                if (scene != null)
                {
                    End(scene);
                }
            }

            /// <summary>
            /// Make sure there is a context, a surface for this holder, and a
            /// viewport at the size the window last reported.
            /// </summary>
            private bool BindSurface(ISurfaceHolder holder, Vector2i wanted)
            {
                if (_display == null && !CreateContext())
                {
                    return false;
                }
                if (!ReferenceEquals(_boundTo, holder) || _eglSurface == null)
                {
                    ReleaseSurface();
                    if (!CreateSurface(holder))
                    {
                        return false;
                    }
                }
                if (wanted != _size)
                {
                    _size = wanted;
                    GL.Viewport(0, 0, _size.X, _size.Y);
                    if (Scene != null)
                    {
                        // A resize is one frame's work here and nothing on the
                        // UI thread is waiting for it.
                        Scene.Size = _size;
                        Scene.OnResize();
                    }
                }
                return true;
            }

            private bool CreateContext()
            {
                _display = EGL14.EglGetDisplay(EGL14.EglDefaultDisplay);
                if (_display == null || _display.Equals(EGL14.EglNoDisplay))
                {
                    return Fail("no EGL display");
                }
                int[] version = new int[2];
                if (!EGL14.EglInitialize(_display, version, 0, version, 1))
                {
                    return Fail($"eglInitialize failed (0x{EGL14.EglGetError():X})");
                }
                // 8/8/8 colour, 24-bit depth and 8 bits of stencil: the
                // renderer's translucency passes mark faces in the stencil
                // buffer, and a config without one draws the transparent
                // surfaces wrong rather than failing.
                int[] attributes =
                {
                    EGL14.EglRenderableType, OpenGlEs3Bit,
                    EGL14.EglSurfaceType, EGL14.EglWindowBit,
                    EGL14.EglRedSize, 8,
                    EGL14.EglGreenSize, 8,
                    EGL14.EglBlueSize, 8,
                    EGL14.EglAlphaSize, 0,
                    EGL14.EglDepthSize, 24,
                    EGL14.EglStencilSize, 8,
                    EGL14.EglNone
                };
                var configs = new EGLConfig[1];
                int[] found = new int[1];
                if (!EGL14.EglChooseConfig(_display, attributes, 0, configs, 0, 1, found, 0)
                    || found[0] < 1 || configs[0] == null)
                {
                    return Fail("no EGL config with a window, depth and stencil");
                }
                _config = configs[0];
                _context = EGL14.EglCreateContext(_display, _config, EGL14.EglNoContext,
                    new[] { EGL14.EglContextClientVersion, 3, EGL14.EglNone }, 0);
                if (_context == null || _context.Equals(EGL14.EglNoContext))
                {
                    return Fail($"eglCreateContext failed (0x{EGL14.EglGetError():X})");
                }
                return true;
            }

            private bool CreateSurface(ISurfaceHolder holder)
            {
                if (_display == null || _config == null || _context == null)
                {
                    return false;
                }
                Surface? window = holder.Surface;
                if (window == null || !window.IsValid)
                {
                    return false;
                }
                _eglSurface = EGL14.EglCreateWindowSurface(_display, _config, window,
                    new[] { EGL14.EglNone }, 0);
                if (_eglSurface == null || _eglSurface.Equals(EGL14.EglNoSurface))
                {
                    _eglSurface = null;
                    // Not fatal on its own: the window can be on its way out.
                    Console.WriteLine("[android] eglCreateWindowSurface failed "
                        + $"(0x{EGL14.EglGetError():X})");
                    return false;
                }
                if (!EGL14.EglMakeCurrent(_display, _eglSurface, _eglSurface, _context))
                {
                    return Fail($"eglMakeCurrent failed (0x{EGL14.EglGetError():X})");
                }
                lock (_lock)
                {
                    _boundTo = holder;
                    _holdingSurface = true;
                }
                // Function pointers are per-process; everything GlEs holds --
                // buffers, textures, uniform locations -- belongs to a context.
                // This one outlives the surface, so the reset only belongs with
                // a *new* context, which is the first surface after one.
                if (Scene == null)
                {
                    EsBindings.Load();
                    GlEs.Reset();
                    // Something rather than whatever was in the buffer, for the
                    // seconds the room takes to load.
                    GL.ClearColor(new OpenTK.Mathematics.Color4(10 / 255f, 12 / 255f, 16 / 255f, 1f));
                    GL.Clear(OpenTK.Graphics.OpenGL.ClearBufferMask.ColorBufferBit);
                    EGL14.EglSwapBuffers(_display, _eglSurface);
                }
                _size = Vector2i.Zero;
                return true;
            }

            private void ReleaseSurface()
            {
                EGLSurface? surface;
                lock (_lock)
                {
                    surface = _eglSurface;
                    _eglSurface = null;
                    _boundTo = null;
                    _holdingSurface = false;
                    Monitor.PulseAll(_lock);
                }
                if (_display == null || surface == null)
                {
                    return;
                }
                try
                {
                    EGL14.EglMakeCurrent(_display, EGL14.EglNoSurface, EGL14.EglNoSurface,
                        EGL14.EglNoContext);
                    EGL14.EglDestroySurface(_display, surface);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[android] releasing the surface failed: {ex.Message}");
                }
            }

            private void DestroyContext()
            {
                if (_display == null)
                {
                    return;
                }
                try
                {
                    EGL14.EglMakeCurrent(_display, EGL14.EglNoSurface, EGL14.EglNoSurface,
                        EGL14.EglNoContext);
                    if (_context != null)
                    {
                        EGL14.EglDestroyContext(_display, _context);
                    }
                    EGL14.EglTerminate(_display);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[android] tearing the context down failed: {ex.Message}");
                }
                _context = null;
                _config = null;
                _display = null;
            }

            private bool Fail(string message)
            {
                Console.WriteLine($"[android] {message}");
                if (!_ended)
                {
                    _ended = true;
                    _onError(message);
                }
                lock (_lock)
                {
                    _stopping = true;
                }
                return false;
            }

            /// <summary>
            /// Load the room, on this thread. It takes seconds; nothing is
            /// waiting on it, which is the difference this class makes.
            /// </summary>
            private void BuildScene()
            {
                if (_size.X <= 0 || _size.Y <= 0)
                {
                    return;
                }
                try
                {
                    Scene = _build(_input, _size);
                    Scene.OnLoad();
                }
                catch (Exception ex)
                {
                    // A missing room, a shader the driver would not take, a set
                    // of game files that is not there. Any of them ends the
                    // match with what went wrong on screen, rather than taking
                    // the process down from a thread nobody is watching.
                    Console.WriteLine($"[android] the match could not start: {ex}");
                    Scene = null;
                    _ended = true;
                    _onError(ex.Message);
                    lock (_lock)
                    {
                        _stopping = true;
                    }
                    return;
                }
                _clock.Start();
                _nextFrame = _clock.Elapsed.TotalSeconds;
                _onLoaded();
            }

            /// <summary>One frame. False means the match is over.</summary>
            private bool DrawFrame()
            {
                Scene scene = Scene!;
                WaitForTick();
                ApplyInput();
                GameState.ApplyPause();
                scene.OnUpdateFrame();
                if (!scene.OnRenderFrame())
                {
                    End(scene);
                    return false;
                }
                scene.AfterRenderFrame();
                if (_display != null && _eglSurface != null
                    && !EGL14.EglSwapBuffers(_display, _eglSurface))
                {
                    // The framework took the surface back. Let go of it and
                    // wait for the next one rather than drawing into nothing.
                    Console.WriteLine("[android] the surface stopped accepting frames; "
                        + $"waiting for another (0x{EGL14.EglGetError():X})");
                    ReleaseSurface();
                }
                return true;
            }

            private void End(Scene scene)
            {
                _ended = true;
                scene.DoCleanup();
                Scene = null;
                // Whatever the session asked to have saved, before anything
                // else can run and before the front screen comes back. This is
                // the desktop's line after its render loop returns; nothing is
                // written unless the match was the story, so every other kind
                // of match passes straight through.
                try
                {
                    AndroidMatch.Finish();
                }
                catch (Exception ex)
                {
                    // A save that cannot be written is not a reason to leave
                    // the player on a dead view.
                    Console.WriteLine($"[android] the save could not be written: {ex}");
                }
                _onEnd();
            }

            private void WaitForTick()
            {
                double now = _clock.Elapsed.TotalSeconds;
                double wait = _nextFrame - now;
                if (wait > 0.001)
                {
                    Thread.Sleep((int)(wait * 1000));
                }
                _nextFrame += FrameSeconds;
                if (_nextFrame < now)
                {
                    // A stall (a load, a garbage collection, the app coming
                    // back) must not leave the game owing frames it would then
                    // run flat out to catch up on.
                    _nextFrame = now + FrameSeconds;
                }
            }

            /// <summary>
            /// One frame of touch, turned into key and button presses.
            ///
            /// The presses are collected and committed as a set rather than
            /// written one action at a time, because actions share binds --
            /// see <see cref="AndroidInput.Apply"/> for the FIRE/ALT bug that
            /// came of writing them through.
            /// </summary>
            private void ApplyInput()
            {
                PlayerEntity main = PlayerEntity.Main;
                if (main == null || !main.LoadFlags.TestFlag(LoadFlags.Active))
                {
                    return;
                }
                _input.BeginFrame();
                try
                {
                    CollectInput(main);
                }
                finally
                {
                    _input.CommitFrame();
                }
            }

            private void CollectInput(PlayerEntity main)
            {
                if (Mods.SpectatorMode.IsSpectating)
                {
                    // FIRE moves on to the next player, which is what a left
                    // click does on the desktop (Renderer.OnMouseDown). The
                    // menu button still opens the menu, and everything else is
                    // ignored: PlayerEntity.ProcessInput skips the local
                    // player's input entirely while this is on, so there is
                    // nothing else for a thumb to do.
                    bool cycle = _controls.IsHeld(TouchAction.Shoot);
                    if (cycle && !_spectateCycleHeld)
                    {
                        Mods.SpectatorMode.CycleNext();
                    }
                    _spectateCycleHeld = cycle;
                    bool menuHeld = _controls.IsHeld(TouchAction.Pause);
                    if (menuHeld && !_menuWasHeld)
                    {
                        _onPauseMenu();
                    }
                    _menuWasHeld = menuHeld;
                    _controls.TakeAimDelta();
                    _controls.TakeSwipeBoost();
                    return;
                }
                _spectateCycleHeld = false;
                PlayerControls controls = main.Controls;
                TouchControls.Dir dir = _controls.Direction;
                bool up = (dir & TouchControls.Dir.Up) != 0;
                bool down = (dir & TouchControls.Dir.Down) != 0;
                bool left = (dir & TouchControls.Dir.Left) != 0;
                bool right = (dir & TouchControls.Dir.Right) != 0;
                // Both sets: walking reads Move and the morph ball reads Roll,
                // and a player who has bound them to different keys expects
                // the stick to drive whichever form they are in.
                _input.Apply(controls.MoveUp, up);
                _input.Apply(controls.MoveDown, down);
                _input.Apply(controls.MoveLeft, left);
                _input.Apply(controls.MoveRight, right);
                _input.Apply(controls.RollUp, up);
                _input.Apply(controls.RollDown, down);
                _input.Apply(controls.RolltLeft, left);
                _input.Apply(controls.RollRight, right);

                bool jump = _controls.IsHeld(TouchAction.Jump);
                // FIRE is the only attack button, and it is both attacks.
                //
                // There used to be an ALT button beside it, which is what the
                // DS did not have: one fire button served the gun on foot and
                // the alt form's attack in the ball, and the game's own
                // defaults still say so -- shoot and altAttack are both
                // MouseButton.Left. A second button for the same bind bought
                // nothing and cost the first one (see AndroidInput.Apply), so
                // it is gone and FIRE presses whichever of the two the form
                // the player is actually in will read. Both while morphing, so
                // a thumb already down on FIRE as the ball closes is not
                // dropped on the frame the form changes.
                bool fire = _controls.IsHeld(TouchAction.Shoot);
                bool altForm = main.IsAltForm || _controls.IsHeld(TouchAction.Morph);
                _input.Apply(controls.Shoot, fire && !main.IsAltForm);
                _input.Apply(controls.AltAttack, fire && altForm);
                _input.Apply(controls.Jump, jump);
                // One button on the DS, and the same key here by default:
                // jumping on foot is boosting in the ball.
                _input.Apply(controls.Boost, jump);
                // A quick flick of the movement stick also boosts, the way a
                // stylus flick did on the DS -- see PlayerInput's boost
                // handling for how this one-shot is consumed.
                if (_controls.TakeSwipeBoost() && main.IsAltForm)
                {
                    main.SwipeBoostRequested = true;
                }
                _input.Apply(controls.Morph, _controls.IsHeld(TouchAction.Morph));
                _input.Apply(controls.ScanVisor, _controls.IsHeld(TouchAction.ScanVisor));
                _input.Apply(controls.Zoom, _controls.IsHeld(TouchAction.Zoom));
                // The DS pause button -- map and status on foot, scoreboard
                // while it is held in a match -- is SCORE now. MENU is the
                // app's own menu, which is the thing a player looks for first
                // and had no way to reach at all.
                _input.Apply(controls.Pause, _controls.IsHeld(TouchAction.Scoreboard));
                bool menu = _controls.IsHeld(TouchAction.Pause);
                if (menu && !_menuWasHeld)
                {
                    // On the press, not the release, and once per press: the
                    // menu is a view swap on the UI thread and this is the GL
                    // thread, so it is asked for rather than done here.
                    _onPauseMenu();
                }
                _menuWasHeld = menu;

                // A dialog box waiting to be dismissed reads a click position
                // and nothing else: PlayerDialog.CheckButtonPressed compares
                // Input.ClickX/Y against the button rectangle, and on this
                // platform nothing ever set them. The pointer was only ever
                // *moved*, for aiming, and no touch ever pressed the left
                // mouse button -- so the OK button could not be pressed at all,
                // and a scan or a prompt could only be left by quitting.
                //
                // While one is up, the screen is the DS's touch screen: a
                // finger is a position and holding it is holding the button.
                if (GameState.DialogPause)
                {
                    _controls.PointerIsAbsolute = true;
                    (bool Down, float X, float Y) tap = _controls.AimPosition();
                    if (tap.Down)
                    {
                        _input.PlacePointer(tap.X, tap.Y);
                    }
                    _input.ApplyButton(OpenTK.Windowing.GraphicsLibraryFramework.MouseButton.Left, tap.Down);
                    _dialogClickDown = tap.Down;
                    // Swallowed, so the aim does not lurch by however far the
                    // finger travelled once the box is gone.
                    _controls.TakeAimDelta();
                    return;
                }
                _controls.PointerIsAbsolute = false;
                if (_dialogClickDown)
                {
                    // Released explicitly rather than left to the next tap:
                    // the box can close on the same frame the finger is still
                    // down, and a mouse button stuck down outlives the dialog.
                    // Nothing to do but stop asking for it: CommitFrame
                    // releases every button no action asked for this frame.
                    _dialogClickDown = false;
                }
                bool weaponMenu = _controls.IsHeld(TouchAction.WeaponMenu);
                _input.Apply(controls.WeaponMenu, weaponMenu);
                if (weaponMenu)
                {
                    // The wheel is a touchscreen mechanic: it reads where the
                    // pointer is, not how far it moved. While it is open the
                    // pointer is the finger.
                    (bool Down, float X, float Y) aim = _controls.AimPosition();
                    if (aim.Down)
                    {
                        _input.PlacePointer(aim.X, aim.Y);
                    }
                    _controls.TakeAimDelta();
                }
                else
                {
                    (float X, float Y) delta = _controls.TakeAimDelta();
                    _input.MovePointer(delta.X * AimScale, delta.Y * AimScale);
                }
            }
        }
    }
}
