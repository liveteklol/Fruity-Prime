using System;
using System.Drawing;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// A line of text with the tracking the stock Label cannot do.
    /// Used for card titles and the small headings above a control.
    /// </summary>
    [System.Runtime.Versioning.SupportedOSPlatform("windows")]
    public sealed class Caption : Control
    {
        private readonly LauncherTheme _theme;
        private readonly float _size;
        private readonly Color _color;
        private readonly float _tracking;
        private readonly bool _display;

        public Caption(LauncherTheme theme, string text, float size, Color color,
            float tracking, bool display)
        {
            _theme = theme;
            _size = size;
            _color = color;
            _tracking = tracking;
            _display = display;
            Text = text;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            BackColor = LauncherTheme.Panel;
            Height = theme.S((int)size) + theme.S(display ? 10 : 6);
            TabStop = false;
        }

        public void SetText(string text)
        {
            Text = text;
            Invalidate();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            LauncherTheme.Smooth(e.Graphics);
            Font font = _display
                ? _theme.Display(_theme.S((int)_size))
                : _theme.Body(_theme.S((int)_size), FontStyle.Bold);
            LauncherTheme.DrawTracked(e.Graphics, Text.ToUpperInvariant(), font, _color,
                0, (Height - font.GetHeight(e.Graphics)) / 2f, _theme.S((int)_tracking) + 0.5f);
        }
    }
}
