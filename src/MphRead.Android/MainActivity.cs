using System;
using Android.App;
using Android.Content.PM;
using Android.OS;
using Avalonia;
using Avalonia.Android;

namespace MphRead.Droid
{
    /// <summary>
    /// The Android entry point. There is no <c>Main</c> here: Android starts an
    /// activity, and Avalonia's own base class stands the toolkit up on the UI
    /// thread it is given.
    ///
    /// That is the whole difference from the desktop heads, where
    /// <c>GuiLauncher</c> sets Avalonia up on the game's thread and drives a
    /// loop of launcher-then-match. Nothing on this platform can run the match
    /// half yet -- see the note on the screen and in the csproj -- so the
    /// activity only puts up the screen.
    /// </summary>
    [Activity(
        Label = "Fruity Prime",
        Theme = "@android:style/Theme.Material.NoActionBar",
        MainLauncher = true,
        LaunchMode = LaunchMode.SingleTop,
        ConfigurationChanges = ConfigChanges.Orientation | ConfigChanges.ScreenSize
            | ConfigChanges.UiMode | ConfigChanges.Density | ConfigChanges.KeyboardHidden)]
    public class MainActivity : AvaloniaMainActivity<AndroidApp>
    {
        protected override AppBuilder CustomizeAppBuilder(AppBuilder builder)
        {
            // The package's own directory is read-only on Android, and
            // launcher.txt is written, so the preferences move to the directory
            // the system gives this app for its files. Set before the screen is
            // built, because building it reads them.
            string? data = FilesDir?.AbsolutePath;
            if (!String.IsNullOrEmpty(data))
            {
                Mods.Launcher.LauncherPrefs.Directory = data;
            }
            return base.CustomizeAppBuilder(builder).WithInterFont();
        }

        protected override void OnCreate(Bundle? savedInstanceState)
        {
            base.OnCreate(savedInstanceState);
        }
    }
}
