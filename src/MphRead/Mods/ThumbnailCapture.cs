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

        private static GameWindowSettings GameSettings() => new()
        {
            UpdateFrequency = 60 // let the camera sequence advance at its authored rate
        };

        private static NativeWindowSettings WindowSettings(int width, int height) => new()
        {
            ClientSize = new Vector2i(width, height),
            Title = $"{Branding.Name} thumbnails",
            Profile = ContextProfile.Compatability,
            APIVersion = new Version(3, 2),
            StartVisible = false
        };

        public Scene Scene { get; }
        public bool Succeeded => _captured;

        private ThumbnailCapture(string roomKey, int width, int height)
            : base(GameSettings(), WindowSettings(width, height))
        {
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
            Scene.OnLoad();
            base.OnLoad();
            // OnResize normally sets the viewport and resizes the offscreen
            // targets; a window that is never shown or resized never gets
            // that call.
            GL.Viewport(0, 0, ClientSize.X, ClientSize.Y);
            Scene.OnResize();
        }

        protected override void OnRenderFrame(FrameEventArgs args)
        {
            GameState.ApplyPause();
            Scene.OnUpdateFrame();
            if (!Scene.OnRenderFrame())
            {
                return;
            }
            bool capturing = !_captured && _settleFrames-- <= 0;
            if (capturing)
            {
                ThumbnailGenerator.EnsureCacheDirectory();
                ScreenCapture.Save(Scene, ThumbnailGenerator.PathFor(_roomKey));
                _captured = true;
            }
            SwapBuffers();
            Scene.AfterRenderFrame();
            base.OnRenderFrame(args);
            if (capturing)
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
                return false;
            }
        }
    }
}
