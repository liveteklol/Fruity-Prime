using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Android.Content;
using Android.Opengl;
using Javax.Microedition.Khronos.Opengles;
using MphRead.Mods;
using MphRead.Mods.Render;
using MphRead.Sound;
using OpenTK.Mathematics;

namespace MphRead.Droid
{
    /// <summary>
    /// Map previews, rendered on the phone.
    ///
    /// The desktop runs one worker process per room because GLFW wants its
    /// windows on the main thread. An app has no second process, so this does
    /// the same work the other way round: one GL thread, one room at a time,
    /// a scene built and thrown away for each. The picture comes from the
    /// game's own intro camera and is read out of the scene's offscreen target
    /// by <see cref="ScreenCapture"/> -- the same code and the same frame the
    /// desktop captures.
    ///
    /// **Nothing is shipped.** Every preview is rendered on the device from the
    /// player's own extracted files, into
    /// <see cref="ThumbnailGenerator.CacheDirectory"/> beside them.
    /// </summary>
    internal sealed class AndroidThumbnailHost : IThumbnailHost
    {
        private readonly MainActivity _activity;

        public AndroidThumbnailHost(MainActivity activity) => _activity = activity;

        public Task<int> RenderAsync(IReadOnlyList<string> rooms, Action<string> report)
        {
            return _activity.RenderPreviews(rooms, report);
        }
    }

    /// <summary>
    /// The surface the previews are drawn on.
    ///
    /// The surface is pinned to the thumbnail size with
    /// <c>Holder.SetFixedSize</c> rather than left to the screen: what
    /// <see cref="Scene.ReadSceneTarget"/> hands back is the size the scene was
    /// given, so a phone left to itself would produce portrait previews of
    /// whatever resolution it happens to be, and the launcher shows them in a
    /// landscape band.
    /// </summary>
    internal sealed class ThumbnailView : GLSurfaceView
    {
        public ThumbnailView(Context context, IReadOnlyList<string> rooms, int width, int height,
            Action<string> report, Action<int> done)
            : base(context)
        {
            SetEGLContextClientVersion(3);
            SetEGLConfigChooser(8, 8, 8, 0, 24, 8);
            Holder?.SetFixedSize(width, height);
            SetRenderer(new Renderer(rooms, report, done));
            RenderMode = Rendermode.Continuously;
        }

        private sealed class Renderer : Java.Lang.Object, IRenderer
        {
            // The intro sequence has to start and the match-start fade clear
            // before the frame is worth keeping. The desktop's numbers.
            private const int SettleFrames = 45;

            private readonly IReadOnlyList<string> _rooms;
            private readonly Action<string> _report;
            private readonly Action<int> _done;
            private readonly AndroidInput _input = new AndroidInput();

            private Scene? _scene;
            private int _index;
            private int _settle;
            private int _written;
            private int _width;
            private int _height;
            private bool _finished;

            public Renderer(IReadOnlyList<string> rooms, Action<string> report, Action<int> done)
            {
                _rooms = rooms;
                _report = report;
                _done = done;
            }

            public void OnSurfaceCreated(IGL10? gl, Javax.Microedition.Khronos.Egl.EGLConfig? config)
            {
                EsBindings.Load();
                GlEs.Reset();
                // Suppresses the HUD and mutes the sound: a preview is a
                // picture of a room, not of a match in progress.
                ThumbnailMode.Enter();
            }

            public void OnSurfaceChanged(IGL10? gl, int width, int height)
            {
                _width = width;
                _height = height;
                GL.Viewport(0, 0, width, height);
            }

            public void OnDrawFrame(IGL10? gl)
            {
                if (_finished || _width <= 0)
                {
                    return;
                }
                if (_scene == null && !StartNextRoom())
                {
                    Finish();
                    return;
                }
                Scene scene = _scene!;
                GameState.ApplyPause();
                scene.OnUpdateFrame();
                if (!scene.OnRenderFrame())
                {
                    Capture(saved: false);
                    return;
                }
                scene.AfterRenderFrame();
                if (--_settle <= 0)
                {
                    Capture(ScreenCapture.Save(scene, ThumbnailGenerator.PathFor(Current)));
                }
            }

            private string Current => _rooms[_index - 1];

            /// <summary>Load the next room, or say there is none. Blocks this thread for seconds.</summary>
            private bool StartNextRoom()
            {
                while (_index < _rooms.Count)
                {
                    string room = _rooms[_index++];
                    try
                    {
                        // A player has to exist for the multiplayer intro
                        // camera to run at all: GameState sets the sequence up
                        // against PlayerEntity.Main's camera info.
                        var scene = new Scene(new Vector2i(_width, _height),
                            _input.Keyboard, _input.Mouse, _ => { }, () => { });
                        scene.AddPlayer(Hunter.Samus, recolor: 0, team: -1);
                        scene.AddRoom(room, GameMode.Battle, playerCount: 1);
                        scene.OnLoad();
                        Silence();
                        GL.Viewport(0, 0, _width, _height);
                        scene.OnResize();
                        _scene = scene;
                        _settle = SettleFrames;
                        return true;
                    }
                    catch (Exception ex)
                    {
                        // The whole exception, not just the message: a preview
                        // run that fails on every room fails for one reason,
                        // and the message alone rarely says which.
                        Console.WriteLine($"[thumbnails] {room} failed: {ex}");
                        _report($"[thumbnails] {room}: {ex.Message}");
                        _scene = null;
                    }
                }
                return false;
            }

            /// <summary>
            /// A preview needs no sound, and loading a room starts its music.
            /// Thirty-three rooms is thirty-three streams begun and abandoned,
            /// which is what took the audio thread down with a SIGSEGV.
            /// </summary>
            private void Silence()
            {
                try
                {
                    Music.Stop();
                    MusicPlayer.Stop();
                }
                catch (Exception ex)
                {
                    _report($"[thumbnails] could not stop the music: {ex.Message}");
                }
            }

            private void Capture(bool saved)
            {
                if (saved)
                {
                    _written++;
                }
                _report($"[thumbnails] {_index}/{_rooms.Count}  {Current}"
                    + (saved ? "" : " -- nothing usable"));
                try
                {
                    _scene?.DoCleanup();
                }
                catch (Exception ex)
                {
                    _report($"[thumbnails] {Current}: cleanup failed: {ex.Message}");
                }
                _scene = null;
            }

            private void Finish()
            {
                _finished = true;
                _done(_written);
            }
        }
    }
}
