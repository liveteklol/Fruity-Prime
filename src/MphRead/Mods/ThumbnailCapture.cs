using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using OpenTK.Graphics.OpenGL;
using OpenTK.Mathematics;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.Desktop;
using ReFuel.Stb;

namespace MphRead.Mods
{
    /// <summary>
    /// Renders one multiplayer room and captures its intro camera view.
    ///
    /// The picture comes from the game's own match-start sequence
    /// (CameraSequence.Intro, looped by GameState while the match has not
    /// begun): a spectator fly-through the developers framed to show the
    /// level off. That beats any bounds-derived camera this code could
    /// compute.
    ///
    /// One window per room: Scene.AddRoom must be called before OnLoad and
    /// refuses a second room, so a room per window lifetime is the shape the
    /// engine supports. Batches therefore parallelize across *processes*
    /// (see ThumbnailBatch), not threads -- GLFW requires windows to live on
    /// the main thread, so several GL windows in one process is not an option.
    ///
    /// Everything is local. Previews render from the user's own extracted
    /// files into a git-ignored cache; no game asset is ever committed.
    /// </summary>
    public sealed class ThumbnailCapture : GameWindow
    {
        private readonly string _roomKey;
        private int _settleFrames;
        private bool _captured;

        // The intro sequence needs to start and the match-start fade
        // (20/30s, set by GameState) to clear before the frame is worth
        // keeping.
        private const int SettleFrames = 45;
        private const int RetryFrames = 20;
        private const int MaxAttempts = 3;
        private int _attempts;
        private OpenTK.Graphics.OpenGL.ErrorCode _frameError;

        private static GameWindowSettings GameSettings() => new()
        {
            UpdateFrequency = 60 // let the camera sequence advance at its authored rate
        };

        private static NativeWindowSettings WindowSettings(int width, int height) => new()
        {
            ClientSize = new Vector2i(width, height),
            Title = $"{Branding.Name} thumbnails",
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
        public bool Succeeded => _captured;

        private readonly Vector2i _asked;

        private ThumbnailCapture(string roomKey, int width, int height)
            : base(GameSettings(), WindowSettings(width, height))
        {
            _asked = new Vector2i(width, height);
            _roomKey = roomKey;
            _settleFrames = SettleFrames;
            Scene = new Scene(Size, KeyboardState, MouseState, _ => { }, Close);
            // A player must exist for the multiplayer intro path to run:
            // GameState sets the sequence up against PlayerEntity.Main's
            // camera info, and EnsureIntroCamSeq only fires for Multiplayer.
            Scene.AddPlayer(Hunter.Samus, recolor: 0, team: -1);
            Scene.AddRoom(roomKey, GameMode.Battle, playerCount: 1);
        }

        protected override void OnLoad()
        {
            // Scene was constructed with the *requested* size, but GLFW
            // clamps a window to what the display can hold, so asking for
            // 1920x1440 on a smaller screen yields a smaller window. The
            // render target would then be larger than the framebuffer being
            // drawn into, leaving unwritten black bands on two edges. Adopt
            // the size the window actually got before anything is allocated.
            Scene.Size = ClientSize;
            // Before the scene builds anything, so the driver's complaint
            // about the first refused call is caught rather than inferred.
            ScreenCapture.EnableDebugOutput(ThumbnailLog.Write);
            Scene.OnLoad();
            base.OnLoad();
            // OnResize normally sets the viewport and resizes the offscreen
            // targets; a window that is never shown or resized never gets
            // that call.
            GL.Viewport(0, 0, ClientSize.X, ClientSize.Y);
            Scene.OnResize();
            // Once, and only from the first worker, so a batch of thirty
            // rooms does not print it thirty times. What it answers is the
            // question a black picture cannot: whether the driver gave this
            // process a context it can actually draw in.
            if (!_describedContext)
            {
                _describedContext = true;
                string line = $"build {Update.BuildVersion.Display} / "
                    + $"{System.Reflection.Assembly.GetEntryAssembly()?.GetName().Version?.ToString() ?? "?"}, "
                    + $"{ScreenCapture.DescribeContext()}, "
                    + $"window asked {_asked.X}x{_asked.Y}, got {ClientSize.X}x{ClientSize.Y}, "
                    + $"offscreen target {Scene.FramebufferStatus}";
                Console.WriteLine($"[thumbnails] {line}");
                ThumbnailLog.Write(line);
            }
        }

        private static bool _describedContext;

        protected override void OnRenderFrame(FrameEventArgs args)
        {
            GameState.ApplyPause();
            Scene.OnUpdateFrame();
            if (!Scene.OnRenderFrame())
            {
                return;
            }
            // Drained after the scene has drawn and before anything else
            // touches GL, so what it reports belongs to the frame that was
            // just rendered.
            _frameError = Scene.DrainGlError();
            bool capturing = !_captured && _settleFrames-- <= 0;
            bool giveUp = false;
            if (capturing)
            {
                ThumbnailGenerator.EnsureCacheDirectory();
                // The result decides, rather than the attempt. Saving refuses
                // an all-black frame, and reporting that as a capture is what
                // let a run of thirty rooms announce success and leave thirty
                // black pictures behind.
                _captured = ScreenCapture.Save(Scene, ThumbnailGenerator.PathFor(_roomKey));
                if (_captured)
                {
                    ThumbnailLog.Write($"{_roomKey}: captured at {Scene.Size.X}x{Scene.Size.Y}"
                        + (_attempts > 0 ? $" on attempt {_attempts + 1}, window shown" : ""));
                }
                if (!_captured)
                {
                    ThumbnailLog.Write($"{_roomKey}: attempt {_attempts + 1} produced nothing usable "
                        + $"(target {Scene.FramebufferStatus}, first GL error this frame {_frameError})");
                    // Show the window and try again.
                    //
                    // A capture window is never displayed -- there is nothing
                    // to look at and thirty-three of them flashing would be
                    // worse than useless. But a driver is entitled to do
                    // nothing at all for a window with no visible surface,
                    // and some do: an Intel Iris Xe on a compatibility 3.2
                    // context rendered every one of thirty-three rooms at
                    // 0.00% lit while the game itself ran fine on the same
                    // machine. Mesa has the mirror image of this, recorded in
                    // CLAUDE.md -- a hidden window there has no usable back
                    // buffer, which is why captures read the offscreen target
                    // in the first place.
                    //
                    // So it stays hidden for the attempt that costs nothing,
                    // and only a machine that needs the window sees it.
                    if (!IsVisible)
                    {
                        IsVisible = true;
                        ThumbnailLog.Write($"{_roomKey}: showing the window and retrying -- "
                            + "this driver appears not to render to a hidden one");
                    }
                    // A few more frames in case the first one simply came too
                    // early, then stop: if the context cannot draw, no number
                    // of frames will change that.
                    _settleFrames = RetryFrames;
                    giveUp = ++_attempts >= MaxAttempts;
                }
            }
            SwapBuffers();
            Scene.AfterRenderFrame();
            base.OnRenderFrame(args);
            if (_captured || giveUp)
            {
                Close();
            }
        }

        protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
        {
            Scene.DoCleanup();
            base.OnClosing(e);
        }

        /// <summary>Render and save one room's preview. Returns false if it could not be captured.</summary>
        public static bool CaptureRoom(string roomKey, int width, int height)
        {
            try
            {
                ThumbnailMode.Enter();
                using var window = new ThumbnailCapture(roomKey, width, height);
                window.Run();
                return window.Succeeded;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[thumbnails] failed {roomKey}: {ex.Message}");
                ThumbnailLog.Write($"{roomKey}: threw {ex.GetType().Name}: {ex.Message}");
                return false;
            }
        }
    }
}
