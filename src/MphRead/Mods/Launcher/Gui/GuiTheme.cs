using Avalonia;
using Avalonia.Media;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// The front screen's palette and metrics, in Avalonia terms.
    ///
    /// The colours are <see cref="LauncherTheme"/>'s, value for value, and are
    /// meant to stay that way: the two screens are the same product on
    /// different toolkits, and a palette copied by eye would drift the first
    /// time either was adjusted. They are duplicated rather than shared because
    /// LauncherTheme is System.Drawing and does not compile off Windows;
    /// the numbers, not the types, are the thing being kept in step.
    ///
    /// No DPI scaling here, unlike the WinForms theme: Avalonia lays out in
    /// device-independent pixels and scales the whole visual tree itself, which
    /// is the one piece of per-monitor work LauncherTheme.S had to do by hand.
    /// </summary>
    internal static class GuiTheme
    {
        public static readonly Color Ink = Color.FromRgb(10, 12, 16);
        public static readonly Color Panel = Color.FromRgb(18, 21, 28);
        public static readonly Color PanelLight = Color.FromRgb(26, 31, 41);
        public static readonly Color Edge = Color.FromRgb(38, 46, 60);
        public static readonly Color Text = Color.FromRgb(230, 234, 242);
        public static readonly Color TextDim = Color.FromRgb(138, 147, 166);
        public static readonly Color Accent = Color.FromRgb(41, 197, 255);
        public static readonly Color Warm = Color.FromRgb(255, 179, 71);
        public static readonly Color Good = Color.FromRgb(110, 231, 135);
        public static readonly Color Bad = Color.FromRgb(255, 107, 107);

        public static readonly IBrush InkBrush = new SolidColorBrush(Ink);
        public static readonly IBrush PanelBrush = new SolidColorBrush(Panel);
        public static readonly IBrush PanelLightBrush = new SolidColorBrush(PanelLight);
        public static readonly IBrush EdgeBrush = new SolidColorBrush(Edge);
        public static readonly IBrush TextBrush = new SolidColorBrush(Text);
        public static readonly IBrush TextDimBrush = new SolidColorBrush(TextDim);
        public static readonly IBrush AccentBrush = new SolidColorBrush(Accent);
        public static readonly IBrush WarmBrush = new SolidColorBrush(Warm);
        public static readonly IBrush GoodBrush = new SolidColorBrush(Good);
        public static readonly IBrush BadBrush = new SolidColorBrush(Bad);

        /// <summary>
        /// The display face. Inter is embedded in the build rather than looked
        /// up on the system: the WinForms theme can ask for Bahnschrift and
        /// fall back through four more faces because Windows is known to have
        /// them, and there is no equivalent list that every Linux install has.
        /// A launcher that renders in whatever the fontconfig default happens
        /// to be is a launcher that looks different on every distribution.
        /// </summary>
        public static readonly FontFamily Display = new("avares://Avalonia.Fonts.Inter/Assets#Inter");

        public static Typeface Face(bool bold) => new(Display,
            FontStyle.Normal, bold ? FontWeight.SemiBold : FontWeight.Normal);

        /// <summary>Blend towards white or black, for hover and pressed states.</summary>
        public static Color Shade(Color color, double amount)
        {
            double t = amount < 0 ? -amount : amount;
            int target = amount >= 0 ? 255 : 0;
            return Color.FromArgb(color.A,
                (byte)(color.R + (target - color.R) * t),
                (byte)(color.G + (target - color.G) * t),
                (byte)(color.B + (target - color.B) * t));
        }

        /// <summary>
        /// A rounded rectangle as a geometry, for the card and button shapes.
        /// Avalonia has RoundedRect on DrawingContext, so this exists only for
        /// the places that need the path itself.
        /// </summary>
        public static RoundedRect Round(Rect rect, double radius)
            => new(rect, radius);
    }
}
