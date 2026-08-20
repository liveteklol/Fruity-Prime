using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Runtime.Versioning;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// Colours, fonts and the two or three painting helpers the front screen
    /// needs.
    ///
    /// An instance rather than a static class because every size has to be
    /// multiplied by the display scaling of the monitor the window opened on,
    /// and DeviceDpi is a per-form value. Fonts are cached here for the same
    /// reason they are cached anywhere in GDI+: creating one per paint is a
    /// handle leak waiting to happen, and the front screen repaints on every
    /// mouse move.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class LauncherTheme : IDisposable
    {
        public static readonly Color Ink = Color.FromArgb(10, 12, 16);
        public static readonly Color Panel = Color.FromArgb(18, 21, 28);
        public static readonly Color PanelLight = Color.FromArgb(26, 31, 41);
        public static readonly Color Edge = Color.FromArgb(38, 46, 60);
        public static readonly Color Text = Color.FromArgb(230, 234, 242);
        public static readonly Color TextDim = Color.FromArgb(138, 147, 166);
        public static readonly Color Accent = Color.FromArgb(41, 197, 255);
        public static readonly Color Warm = Color.FromArgb(255, 179, 71);
        public static readonly Color Good = Color.FromArgb(110, 231, 135);
        public static readonly Color Bad = Color.FromArgb(255, 107, 107);

        private readonly Dictionary<(float, FontStyle, bool), Font> _fonts = new();
        private readonly int _dpi;

        /// <summary>
        /// Display faces, in order of preference. Bahnschrift is the condensed
        /// technical face that ships with Windows 10 and later and suits a
        /// game menu; the Segoe entries are the fallback for anything older.
        /// Names are checked against installed families rather than passed to
        /// the Font constructor blind, because GDI+ substitutes silently and
        /// the result is a menu that looks nothing like the design on a
        /// machine missing one face.
        /// </summary>
        private static readonly string[] _displayFaces =
        {
            "Bahnschrift SemiBold", "Bahnschrift", "Segoe UI Semibold", "Segoe UI", "Tahoma"
        };

        private static readonly string[] _bodyFaces =
        {
            "Segoe UI", "Tahoma", "Arial"
        };

        public LauncherTheme(int dpi)
        {
            _dpi = dpi;
        }

        /// <summary>Scale a design-time pixel value to this monitor.</summary>
        public int S(int value) => (int)Math.Round(value * _dpi / 96.0);

        public Font Display(float size, FontStyle style = FontStyle.Bold) => Get(size, style, display: true);

        public Font Body(float size, FontStyle style = FontStyle.Regular) => Get(size, style, display: false);

        private Font Get(float size, FontStyle style, bool display)
        {
            var key = (size, style, display);
            if (_fonts.TryGetValue(key, out Font? cached))
            {
                return cached;
            }
            string family = FirstInstalled(display ? _displayFaces : _bodyFaces);
            // Sizes are given in pixels so they scale with S() like every
            // other measurement here, instead of with the point size the
            // system happens to be configured for.
            var font = new Font(family, size, style, GraphicsUnit.Pixel);
            _fonts.Add(key, font);
            return font;
        }

        private static string FirstInstalled(string[] candidates)
        {
            foreach (string name in candidates)
            {
                foreach (FontFamily family in FontFamily.Families)
                {
                    if (String.Equals(family.Name, name, StringComparison.OrdinalIgnoreCase))
                    {
                        return name;
                    }
                }
            }
            return FontFamily.GenericSansSerif.Name;
        }

        /// <summary>Anti-aliased text and curves, for every custom-painted control.</summary>
        public static void Smooth(Graphics g)
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = TextRenderingHint.ClearTypeGridFit;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        }

        /// <summary>
        /// Draw text with extra space between letters.
        ///
        /// GDI+ has no letter-spacing, and the small upper-case labels this
        /// screen uses are unreadable without it. Drawing glyph by glyph is
        /// fine for the handful of short strings involved; do not use it for
        /// running text.
        /// </summary>
        public static float DrawTracked(Graphics g, string text, Font font, Color color,
            float x, float y, float tracking)
        {
            using var brush = new SolidBrush(color);
            float space = SpaceWidth(g, font);
            float pen = x;
            foreach (char c in text)
            {
                if (c == ' ')
                {
                    pen += space + tracking;
                    continue;
                }
                string glyph = c.ToString();
                g.DrawString(glyph, font, brush, pen, y, StringFormat.GenericTypographic);
                pen += g.MeasureString(glyph, font, PointF.Empty, StringFormat.GenericTypographic).Width
                    + tracking;
            }
            return pen - x;
        }

        public static float MeasureTracked(Graphics g, string text, Font font, float tracking)
        {
            float space = SpaceWidth(g, font);
            float width = 0;
            foreach (char c in text)
            {
                width += (c == ' '
                    ? space
                    : g.MeasureString(c.ToString(), font, PointF.Empty,
                        StringFormat.GenericTypographic).Width) + tracking;
            }
            return width;
        }

        /// <summary>
        /// How wide a space is in this font.
        ///
        /// Measured as the difference between two strings rather than
        /// directly: GDI+ trims trailing whitespace before measuring, so
        /// MeasureString(" ") returns zero and a title drawn glyph by glyph
        /// comes out as PLAYONLINE.
        /// </summary>
        private static float SpaceWidth(Graphics g, Font font)
        {
            float pair = g.MeasureString("nn", font, PointF.Empty,
                StringFormat.GenericTypographic).Width;
            float spaced = g.MeasureString("n n", font, PointF.Empty,
                StringFormat.GenericTypographic).Width;
            return Math.Max(spaced - pair, font.Size * 0.22f);
        }

        public static GraphicsPath RoundRect(RectangleF rect, float radius)
        {
            var path = new GraphicsPath();
            if (radius <= 0)
            {
                path.AddRectangle(rect);
                return path;
            }
            float d = radius * 2;
            path.AddArc(rect.X, rect.Y, d, d, 180, 90);
            path.AddArc(rect.Right - d, rect.Y, d, d, 270, 90);
            path.AddArc(rect.Right - d, rect.Bottom - d, d, d, 0, 90);
            path.AddArc(rect.X, rect.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }

        /// <summary>Blend towards white or black, for hover and pressed states.</summary>
        public static Color Shade(Color color, float amount)
        {
            float t = Math.Abs(amount);
            int target = amount >= 0 ? 255 : 0;
            return Color.FromArgb(color.A,
                (int)(color.R + (target - color.R) * t),
                (int)(color.G + (target - color.G) * t),
                (int)(color.B + (target - color.B) * t));
        }

        public void Dispose()
        {
            foreach (Font font in _fonts.Values)
            {
                font.Dispose();
            }
            _fonts.Clear();
        }
    }
}
