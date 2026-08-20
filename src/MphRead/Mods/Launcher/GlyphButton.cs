using System;
using System.Drawing;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// A small square button drawn as a shape rather than a character:
    /// minimise and close on a window with no title bar of its own.
    ///
    /// Drawn rather than lettered because the glyph fonts that carry these
    /// symbols are not on every machine, and a missing glyph shows as a box
    /// in the one corner where it is most obvious.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class GlyphButton : Control
    {
        public enum Glyph
        {
            Minimise,
            Close
        }

        private readonly LauncherTheme _theme;
        private readonly Glyph _glyph;
        private bool _hover;

        public GlyphButton(LauncherTheme theme, Glyph glyph)
        {
            _theme = theme;
            _glyph = glyph;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            Size = new Size(theme.S(30), theme.S(26));
            Cursor = Cursors.Hand;
            // The parent panel is a flat colour; painting it here rather than
            // asking for a transparent background avoids the parent-repaint
            // dance WinForms does for ControlStyles.SupportsTransparentBackColor.
            BackColor = LauncherTheme.Panel;
            TabStop = false;
        }

        protected override void OnMouseEnter(EventArgs e)
        {
            _hover = true;
            Invalidate();
            base.OnMouseEnter(e);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            _hover = false;
            Invalidate();
            base.OnMouseLeave(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            LauncherTheme.Smooth(g);
            if (_hover)
            {
                using var fill = new SolidBrush(_glyph == Glyph.Close
                    ? Color.FromArgb(80, 255, 107, 107)
                    : LauncherTheme.PanelLight);
                g.FillRectangle(fill, 0, 0, Width, Height);
            }
            using var pen = new Pen(_hover ? LauncherTheme.Text : LauncherTheme.TextDim,
                _theme.S(1) + 0.2f);
            float cx = Width / 2f;
            float cy = Height / 2f;
            float arm = _theme.S(5);
            if (_glyph == Glyph.Minimise)
            {
                g.DrawLine(pen, cx - arm, cy + arm / 2, cx + arm, cy + arm / 2);
            }
            else
            {
                g.DrawLine(pen, cx - arm, cy - arm, cx + arm, cy + arm);
                g.DrawLine(pen, cx - arm, cy + arm, cx + arm, cy - arm);
            }
        }
    }
}
