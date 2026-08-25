using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Android.App;
using Android.Content.PM;
using Android.OS;
using Android.Views;
using Android.Widget;
using Avalonia;
using Avalonia.Android;
using MphRead.Mods;
using MphRead.Mods.Launcher;
using MphRead.Mods.Network;

namespace MphRead.Droid
{
    /// <summary>
    /// The Android entry point. There is no <c>Main</c> here: Android starts an
    /// activity, and Avalonia's own base class stands the toolkit up on the UI
    /// thread it is given.
    ///
    /// That is the whole difference from the desktop heads, where
    /// <c>GuiLauncher</c> sets Avalonia up on the game's thread and drives a
    /// loop of launcher-then-match. Here the two halves are two views in one
    /// activity: the launcher is Avalonia, the match is a
    /// <see cref="GameView"/> with its own GL thread, and starting one hides
    /// the other. Two activities would have meant handing a
    /// <see cref="LaunchPlan"/> across a process boundary for no gain.
    /// </summary>
    [Activity(
        Label = "Fruity Prime",
        // Must be an AppCompat descendant: Avalonia's activity is an AndroidX
        // AppCompatActivity and throws out of onCreate under anything else.
        // See Resources/values/styles.xml.
        Theme = "@style/FruityPrime",
        MainLauncher = true,
        LaunchMode = LaunchMode.SingleTop,
        ConfigurationChanges = ConfigChanges.Orientation | ConfigChanges.ScreenSize
            | ConfigChanges.UiMode | ConfigChanges.Density | ConfigChanges.KeyboardHidden)]
    public class MainActivity : AvaloniaMainActivity<AndroidApp>
    {
        internal static MainActivity? Instance { get; private set; }

        private ViewGroup? _content;
        private View? _launcherView;
        private GameView? _gameView;
        private TouchOverlayView? _overlay;
        private TextView? _notice;
        private volatile bool _renderingPreviews;
        private volatile bool _renderingHere;
        private readonly TouchControls _controls = new TouchControls();
        private ScreenOrientation _orientationBefore = ScreenOrientation.Unspecified;

        internal bool InMatch => _gameView != null;

        protected override AppBuilder CustomizeAppBuilder(AppBuilder builder)
        {
            // The package's own directory is read-only on Android, so both the
            // preferences and paths.txt move. They move to *external* files
            // rather than internal ones because the extracted game files are
            // hundreds of megabytes a player has to copy onto the device
            // themselves, and this is the directory they can reach over USB
            // without the app asking for a storage permission.
            string root = GetExternalFilesDir(null)?.AbsolutePath
                ?? FilesDir?.AbsolutePath ?? "";
            if (root.Length > 0)
            {
                LauncherPrefs.Directory = root;
                GameFiles.Root = root;
                try
                {
                    // Upstream's Paths reads paths.txt relative to the working
                    // directory, so the two have to agree.
                    Directory.SetCurrentDirectory(root);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[android] could not use {root} as the working directory: {ex.Message}");
                }
            }
            // Before the front screen, which lists the rooms: the custom maps
            // have to be out of the package and their directory named before
            // anything reads the room tables, since that list is built once.
            AndroidMaps.Install(Assets, root, PackageTime());
            // Before base.OnCreate, which is what builds the front screen:
            // the screen asks whether previews can be rendered while it is
            // being constructed, and on the desktop the same seam is left empty
            // so the batch of worker processes answers instead.
            ThumbnailHost.Current = new AndroidThumbnailHost(this);
            ScreenCapture.PngWriter = AndroidPng.Write;
            return base.CustomizeAppBuilder(builder).WithInterFont();
        }

        /// <summary>
        /// When this package was last installed or updated, so the bundled
        /// maps are unpacked again after an update and left alone otherwise.
        /// </summary>
        private long PackageTime()
        {
            try
            {
                return PackageManager?.GetPackageInfo(PackageName!, 0)?.LastUpdateTime ?? 0;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[android] could not read the package time: {ex.Message}");
                return 0;
            }
        }

        protected override void OnCreate(Bundle? savedInstanceState)
        {
            Instance = this;
            base.OnCreate(savedInstanceState);
            // The desktop builds missing map binaries from ModEntry.TryHandle;
            // this head has no Main for that to live in. Off the UI thread:
            // it reads the extracted game files and writes three binaries per
            // map, and only the first launch after a map changes does any work.
            System.Threading.Tasks.Task.Run(AndroidMaps.EnsureBuilt);
            _content = FindViewById(Android.Resource.Id.Content) as ViewGroup;
            _launcherView = _content?.GetChildAt(0);
        }

        /// <summary>
        /// Render map previews on this device and give the launcher its
        /// progress back.
        ///
        /// Nothing is shown while it runs and nothing is rotated: the work goes
        /// to <see cref="PreviewService"/> workers in processes of their own,
        /// drawing into offscreen pbuffers, and the only thing this process
        /// does is watch the cache directory fill up. Whatever they could not
        /// produce is rendered here afterwards, on a background thread with an
        /// offscreen context of its own, so a device that will not start
        /// services still gets its pictures.
        /// </summary>
        internal Task<int> RenderPreviews(IReadOnlyList<string> rooms, Action<string> report)
        {
            if (rooms.Count == 0 || _renderingPreviews)
            {
                return Task.FromResult(0);
            }
            _renderingPreviews = true;
            void Report(string line) => RunOnUiThread(() => report(line));
            // The workers are ordinary services, and a device that goes to
            // sleep throttles them; the run is long enough for that to matter.
            RunOnUiThread(() => Window?.AddFlags(WindowManagerFlags.KeepScreenOn));
            return Task.Run(() =>
            {
                try
                {
                    ThumbnailGenerator.EnsureCacheDirectory();
                    int written = PreviewWorkers.Run(this, rooms,
                        PreviewRun.Width, PreviewRun.Height, Report);
                    var left = new List<string>();
                    for (int i = 0; i < rooms.Count; i++)
                    {
                        if (!ThumbnailGenerator.Exists(rooms[i]))
                        {
                            left.Add(rooms[i]);
                        }
                    }
                    if (left.Count > 0)
                    {
                        written += RenderHere(left, Report);
                    }
                    return written;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[thumbnails] the run failed: {ex}");
                    Report($"[thumbnails] {ex.Message}");
                    return 0;
                }
                finally
                {
                    _renderingPreviews = false;
                    RunOnUiThread(() =>
                    {
                        if (!InMatch)
                        {
                            Window?.ClearFlags(WindowManagerFlags.KeepScreenOn);
                        }
                    });
                }
            });
        }

        /// <summary>
        /// The fallback: render in this process, on this thread, with an
        /// offscreen context.
        ///
        /// A match cannot run at the same time -- one process holds one world,
        /// and <see cref="Mods.ThumbnailMode"/> is on while this runs -- so
        /// <see cref="StartMatch"/> refuses while it does.
        /// </summary>
        private int RenderHere(IReadOnlyList<string> rooms, Action<string> report)
        {
            if (InMatch)
            {
                report("[thumbnails] not while a match is running");
                return 0;
            }
            _renderingHere = true;
            try
            {
                using var gl = OffscreenGl.Create(PreviewRun.Width, PreviewRun.Height);
                return PreviewRun.Render(rooms, PreviewRun.Width, PreviewRun.Height, report);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[thumbnails] the offscreen context failed: {ex}");
                report($"[thumbnails] {ex.Message}");
                return 0;
            }
            finally
            {
                _renderingHere = false;
            }
        }

        protected override void OnPause()
        {
            _gameView?.OnPause();
            base.OnPause();
        }

        protected override void OnResume()
        {
            base.OnResume();
            _gameView?.OnResume();
        }

        protected override void OnDestroy()
        {
            if (Instance == this)
            {
                Instance = null;
            }
            base.OnDestroy();
        }

        // Obsolete on 33+ in favour of OnBackPressedDispatcher, which Avalonia's
        // activity does not route; this still runs on every version we target.
#pragma warning disable CA1422
        public override void OnBackPressed()
        {
            if (InMatch)
            {
                _gameView?.Stop();
                return;
            }
            // The same question Escape asks the desktop launcher: close the
            // overlay, or go back one card. Only when the front screen has
            // nothing left to go back to does this leave the app.
            if (AndroidApp.Home?.GoBack() == true)
            {
                return;
            }
            base.OnBackPressed();
        }
#pragma warning restore CA1422

        /// <summary>Load what the plan asks for and hand the screen to it.</summary>
        internal void StartMatch(LaunchPlan plan)
        {
            if (_content == null || InMatch)
            {
                return;
            }
            if (_renderingHere)
            {
                // One process holds one world: the preview run owns the entity
                // lists and the game state until it is finished, and
                // ThumbnailMode is on while it is.
                Toast.MakeText(this, "Still rendering map previews; try again in a moment.",
                    ToastLength.Long)?.Show();
                AndroidApp.Home?.Reset();
                return;
            }
            var input = new AndroidInput();
            _controls.ReleaseEverything();
            _orientationBefore = RequestedOrientation;
            // A first-person game on a phone is landscape. The activity handles
            // its own configuration changes, so this rotates the surface rather
            // than restarting anything.
            RequestedOrientation = ScreenOrientation.SensorLandscape;
            Window?.AddFlags(WindowManagerFlags.KeepScreenOn);
            GoImmersive(true);
            if (_launcherView != null)
            {
                _launcherView.Visibility = ViewStates.Gone;
            }
            _pending = (plan, input);
            _waitingSince = SystemClock.UptimeMillis();
            _lastSize = ContentSize;
            _sizeSettledAt = _waitingSince;
            WaitForSteadyWindow();
        }

        private (LaunchPlan Plan, AndroidInput Input)? _pending;
        private (int Width, int Height) _lastSize;
        private long _waitingSince;
        private long _sizeSettledAt;

        /// <summary>How long the window has to hold still before a match starts.</summary>
        private const int SettleMs = 250;

        /// <summary>
        /// And how long to wait for it to become landscape at all before
        /// giving up and playing in whatever shape the display is. A device
        /// can refuse to rotate -- a display with one orientation, or a large
        /// screen on the Android versions that ignore an app's request.
        /// </summary>
        private const int RotateMs = 3000;

        private (int Width, int Height) ContentSize =>
            _content == null ? (0, 0) : (_content.Width, _content.Height);

        /// <summary>
        /// Do not create the <see cref="GameView"/> until the window has
        /// stopped changing shape.
        ///
        /// This is the portrait launch bug, and it is worth writing down
        /// because nothing about it looks like a bug in this file.
        /// <c>GLSurfaceView.surfaceChanged</c> hands the new size to the GL
        /// thread and then *blocks the UI thread* until that thread has
        /// finished a frame. Loading a room takes seconds on the GL thread. So
        /// a rotation -- or the system bars going away, which resizes the
        /// window just the same -- landing while the room loads freezes the UI
        /// thread for as long as the load takes, and Android puts its own "is
        /// not responding" dialog over the black loading screen. From the
        /// player's side: a white box and a black box, and nothing to press.
        ///
        /// Waiting for the rotation was the first answer to this and only
        /// covered half of it: the configuration change arrives before the
        /// window has been laid out at its new size, so the surface was still
        /// being created mid-rotation. What has to hold still is the window,
        /// not the configuration, which is what this waits for.
        /// </summary>
        private void WaitForSteadyWindow()
        {
            if (_pending == null || _content == null || InMatch)
            {
                return;
            }
            long now = SystemClock.UptimeMillis();
            (int Width, int Height) size = ContentSize;
            if (size != _lastSize)
            {
                _lastSize = size;
                _sizeSettledAt = now;
            }
            bool landscape = size.Width > size.Height;
            bool steady = size.Width > 0 && size.Height > 0 && now - _sizeSettledAt >= SettleMs;
            if (steady && landscape)
            {
                StartPending(null);
                return;
            }
            if (steady && now - _waitingSince >= RotateMs)
            {
                StartPending($"the display did not turn landscape in {RotateMs} ms");
                return;
            }
            _content.PostDelayed(WaitForSteadyWindow, 50);
        }

        private void StartPending(string? note)
        {
            if (_pending == null || InMatch)
            {
                return;
            }
            (LaunchPlan plan, AndroidInput input) = _pending.Value;
            _pending = null;
            if (note != null)
            {
                Console.WriteLine($"[android] starting the match anyway: {note}");
            }
            // Nailed down for the length of the load, for the reason above: a
            // sensor orientation lets the phone turn end for end while the
            // room is loading, which is a resize the UI thread would wait out.
            // Released again the moment the match is on screen, in
            // MatchLoaded, so the player can still hold the phone either way.
            RequestedOrientation = ScreenOrientation.Locked;
            BeginMatch(plan, input);
        }

        private void BeginMatch(LaunchPlan plan, AndroidInput input)
        {
            if (_content == null)
            {
                return;
            }
            _gameView = new GameView(this, _controls, input,
                (i, size) => AndroidMatch.Build(i, size, plan, () => RunOnUiThread(EndMatch)),
                () => RunOnUiThread(EndMatch),
                () => RunOnUiThread(MatchLoaded),
                error => RunOnUiThread(() => FailMatch(error)));
            // The launcher is Avalonia, which draws on a surface of its own,
            // and two surfaces in one window have no z-order between them
            // unless one is asked for. Above the other surface and below the
            // window, so the touch controls and the loading notice -- ordinary
            // views -- still draw over the game.
            _gameView.SetZOrderMediaOverlay(true);
            _overlay = new TouchOverlayView(this, _controls);
            _notice = new TextView(this)
            {
                Text = $"Loading {plan.RoomKey}...",
                TextAlignment = Android.Views.TextAlignment.Center
            };
            _notice.SetTextColor(Android.Graphics.Color.Argb(230, 230, 234, 242));
            _notice.SetBackgroundColor(Android.Graphics.Color.Argb(255, 10, 12, 16));
            _notice.Gravity = GravityFlags.Center;
            _content.AddView(_gameView);
            _content.AddView(_overlay);
            _content.AddView(_notice);
        }

        /// <summary>The room is loaded and the match is drawing.</summary>
        private void MatchLoaded()
        {
            HideNotice();
            // The load is over, so a resize is one frame's wait rather than a
            // freeze; the phone can turn end for end again.
            RequestedOrientation = ScreenOrientation.SensorLandscape;
        }

        private void HideNotice()
        {
            if (_notice != null && _content != null)
            {
                _content.RemoveView(_notice);
                _notice = null;
            }
        }

        private void FailMatch(string message)
        {
            if (_notice != null)
            {
                _notice.Text = message;
            }
            else
            {
                Toast.MakeText(this, message, ToastLength.Long)?.Show();
            }
            // Leave the message up for long enough to read, then go back.
            _content?.PostDelayed(EndMatch, 4000);
        }

        /// <summary>Back to the front screen.</summary>
        internal void EndMatch()
        {
            if (_content == null)
            {
                return;
            }
            _pending = null;
            HideNotice();
            if (_overlay != null)
            {
                _content.RemoveView(_overlay);
                _overlay = null;
            }
            if (_gameView != null)
            {
                _content.RemoveView(_gameView);
                _gameView = null;
            }
            _controls.ReleaseEverything();
            if (_launcherView != null)
            {
                _launcherView.Visibility = ViewStates.Visible;
            }
            // The desktop builds a fresh front screen each time round its loop;
            // this one is the same object across a match, so it is told the
            // match is over rather than left believing it already answered.
            AndroidApp.Home?.Reset();
            NetSession.Stop();
            NetHostSession.Stop();
            Window?.ClearFlags(WindowManagerFlags.KeepScreenOn);
            GoImmersive(false);
            RequestedOrientation = _orientationBefore;
        }

        private void GoImmersive(bool immersive)
        {
            Window? window = Window;
            if (window == null)
            {
                return;
            }
            if (OperatingSystem.IsAndroidVersionAtLeast(30))
            {
                IWindowInsetsController? controller = window.InsetsController;
                if (controller != null)
                {
                    if (immersive)
                    {
                        controller.SystemBarsBehavior =
                            (int)WindowInsetsControllerBehavior.ShowTransientBarsBySwipe;
                        controller.Hide(WindowInsets.Type.SystemBars());
                    }
                    else
                    {
                        controller.Show(WindowInsets.Type.SystemBars());
                    }
                }
                return;
            }
#pragma warning disable CA1422, CS0618 // the pre-30 way, for the devices that need it
            window.DecorView.SystemUiVisibility = immersive
                ? (StatusBarVisibility)(SystemUiFlags.ImmersiveSticky | SystemUiFlags.HideNavigation
                    | SystemUiFlags.Fullscreen | SystemUiFlags.LayoutStable)
                : StatusBarVisibility.Visible;
#pragma warning restore CA1422, CS0618
        }
    }
}
