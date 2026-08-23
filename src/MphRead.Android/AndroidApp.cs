using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Themes.Fluent;

namespace MphRead.Droid
{
    /// <summary>
    /// The Avalonia application on Android.
    ///
    /// A phone has one view, not a desktop full of windows, so this is a single
    /// view lifetime rather than the desktop one: <c>HomeWindow</c> and the
    /// settings and map windows are all <c>Window</c>s and have no meaning
    /// here. What they share with this screen is everything below the window --
    /// the palette, the painted controls, the preferences and the network
    /// client are the same code.
    /// </summary>
    public class AndroidApp : Application
    {
        public override void Initialize()
        {
            Styles.Add(new FluentTheme());
            RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            base.Initialize();
        }

        public override void OnFrameworkInitializationCompleted()
        {
            if (ApplicationLifetime is ISingleViewApplicationLifetime single)
            {
                single.MainView = new AndroidHomeView();
            }
            base.OnFrameworkInitializationCompleted();
        }
    }
}
