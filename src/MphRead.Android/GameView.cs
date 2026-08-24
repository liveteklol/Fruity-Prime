using System;
using System.Diagnostics;
using Android.Content;
using Android.Opengl;
using Javax.Microedition.Khronos.Opengles;
using MphRead.Entities;
using MphRead.Mods.Render;
using OpenTK.Mathematics;

namespace MphRead.Droid
{
    /// <summary>
    /// The match, on a surface.
    ///
    /// <c>GLSurfaceView</c> gives what the desktop gets from GLFW: an EGL
    /// context, a thread that owns it, and a callback per frame. Everything the
    /// engine does with GL -- loading a room's textures, baking its geometry,
    /// compiling the shaders, drawing -- has to happen on that thread, so the
    /// scene is built inside <see cref="Renderer.OnSurfaceChanged"/> rather
    /// than by whoever asked for the match.
    ///
    /// The loop is the desktop's, in the same order: pause, update, render.
    /// What is not the desktop's is the pacing. <c>RenderWindow</c> asks OpenTK
    /// for 60 updates a second; here the callback arrives once per display
    /// refresh, which on a modern phone is 90 or 120, and the engine's update
    /// *is* its frame -- a render with no update in front of it draws nothing,
    /// because the render item lists are built during the update and cleared
    /// after the draw. So the thread waits for the next 60 Hz tick instead of
    /// rendering more often than the game ticks.
    /// </summary>
    internal sealed class GameView : GLSurfaceView
    {
        private readonly Renderer _renderer;

        public GameView(Context context, TouchControls controls, AndroidInput input,
            Func<AndroidInput, Vector2i, Scene> build, Action onEnd, Action onLoaded,
            Action<string> onError)
            : base(context)
        {
            SetEGLContextClientVersion(3);
            // 8/8/8 colour, 24-bit depth and 8 bits of stencil: the renderer's
            // translucency passes mark faces in the stencil buffer, and a
            // config without one draws the transparent surfaces wrong rather
            // than failing.
            SetEGLConfigChooser(8, 8, 8, 0, 24, 8);
            PreserveEGLContextOnPause = true;
            _renderer = new Renderer(controls, input, build, onEnd, onLoaded, onError);
            SetRenderer(_renderer);
            RenderMode = Rendermode.Continuously;
        }

        public Scene? Scene => _renderer.Scene;

        public void Stop()
        {
            _renderer.RequestStop();
        }

        private sealed class Renderer : Java.Lang.Object, IRenderer
        {
            private const double FrameSeconds = 1.0 / 60.0;
            /// <summary>
            /// Density-independent pixels of drag per unit of mouse movement.
            /// One means a swipe turns as far as a mouse moved the same
            /// distance would, which the player's own sensitivity setting then
            /// scales the way it does everywhere else.
            /// </summary>
            private const float AimScale = 1f;

            private readonly TouchControls _controls;
            private readonly AndroidInput _input;
            private readonly Func<AndroidInput, Vector2i, Scene> _build;
            private readonly Action _onEnd;
            private readonly Action _onLoaded;
            private readonly Action<string> _onError;
            private readonly Stopwatch _clock = new Stopwatch();

            private double _nextFrame;
            private bool _stopping;
            private bool _ended;

            public Scene? Scene { get; private set; }

            public Renderer(TouchControls controls, AndroidInput input,
                Func<AndroidInput, Vector2i, Scene> build, Action onEnd, Action onLoaded,
                Action<string> onError)
            {
                _controls = controls;
                _input = input;
                _build = build;
                _onEnd = onEnd;
                _onLoaded = onLoaded;
                _onError = onError;
            }

            public void RequestStop()
            {
                _stopping = true;
            }

            public void OnSurfaceCreated(IGL10? gl, Javax.Microedition.Khronos.Egl.EGLConfig? config)
            {
                // Function pointers are per-process, but everything GlEs is
                // holding -- buffers, textures, the shader programs' uniform
                // locations -- belonged to a context that is gone.
                EsBindings.Load();
                GlEs.Reset();
                if (Scene != null)
                {
                    // The context was lost and remade under a live match. The
                    // scene still holds the old context's texture names and
                    // list ids, and there is no honest way to rebuild them from
                    // here, so the match ends instead of carrying on drawing
                    // nothing. PreserveEGLContextOnPause makes this rare.
                    Console.WriteLine("[android] the GL context was lost; ending the match");
                    _stopping = true;
                }
            }

            public void OnSurfaceChanged(IGL10? gl, int width, int height)
            {
                if (width <= 0 || height <= 0)
                {
                    return;
                }
                GL.Viewport(0, 0, width, height);
                if (Scene == null)
                {
                    // Loading a room takes seconds and blocks this thread. That
                    // is the right thread to block: the UI thread stays free,
                    // so the loading notice on top of this view keeps drawing.
                    try
                    {
                        Scene = _build(_input, new Vector2i(width, height));
                        Scene.OnLoad();
                    }
                    catch (Exception ex)
                    {
                        // A missing room, a shader the driver would not take, a
                        // set of game files that is not there. Any of them ends
                        // the match with what went wrong on screen, rather than
                        // taking the process down from a thread nobody is
                        // watching.
                        Console.WriteLine($"[android] the match could not start: {ex}");
                        Scene = null;
                        _ended = true;
                        _onError(ex.Message);
                        return;
                    }
                    _clock.Start();
                    _nextFrame = _clock.Elapsed.TotalSeconds;
                    _onLoaded();
                    return;
                }
                Scene.Size = new Vector2i(width, height);
                Scene.OnResize();
            }

            public void OnDrawFrame(IGL10? gl)
            {
                Scene? scene = Scene;
                if (scene == null || _ended)
                {
                    return;
                }
                if (_stopping)
                {
                    End(scene);
                    return;
                }
                WaitForTick();
                ApplyInput();
                GameState.ApplyPause();
                scene.OnUpdateFrame();
                if (!scene.OnRenderFrame())
                {
                    End(scene);
                    return;
                }
                scene.AfterRenderFrame();
            }

            private void End(Scene scene)
            {
                _ended = true;
                scene.DoCleanup();
                Scene = null;
                _onEnd();
            }

            private void WaitForTick()
            {
                double now = _clock.Elapsed.TotalSeconds;
                double wait = _nextFrame - now;
                if (wait > 0.001)
                {
                    System.Threading.Thread.Sleep((int)(wait * 1000));
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

            private void ApplyInput()
            {
                PlayerEntity main = PlayerEntity.Main;
                if (main == null || !main.LoadFlags.TestFlag(LoadFlags.Active))
                {
                    return;
                }
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
                _input.Apply(controls.Shoot, _controls.IsHeld(TouchAction.Shoot));
                _input.Apply(controls.Jump, jump);
                // One button on the DS, and the same key here by default:
                // jumping on foot is boosting in the ball.
                _input.Apply(controls.Boost, jump);
                _input.Apply(controls.Morph, _controls.IsHeld(TouchAction.Morph));
                _input.Apply(controls.AltAttack, _controls.IsHeld(TouchAction.AltAttack));
                _input.Apply(controls.Zoom, _controls.IsHeld(TouchAction.Zoom));
                _input.Apply(controls.Pause, _controls.IsHeld(TouchAction.Pause));

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
