using System;
using System.Drawing;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// One setting that is either on or off, drawn as a row with a switch on
    /// the right and, when it has one, a line of explanation under the name.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class ToggleRow : Control
    {
        private readonly LauncherTheme _theme;
        private readonly string _note;
        private bool _on;
        private bool _hot;

        public event EventHandler? Toggled;

        public ToggleRow(LauncherTheme theme, string label, bool on, string note = "")
        {
            _theme = theme;
            _note = note;
            _on = on;
            Text = label;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw
                | ControlStyles.Selectable, true);
            TabStop = true;
            Cursor = Cursors.Hand;
            BackColor = LauncherTheme.Panel;
            Height = theme.S(note.Length > 0 ? 42 : 28);
        }

        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public bool On
        {
            get => _on;
            set
            {
                if (_on != value)
                {
                    _on = value;
                    Invalidate();
                    Toggled?.Invoke(this, EventArgs.Empty);
                }
            }
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            Focus();
            On = !On;
            base.OnMouseDown(e);
        }

        protected override void OnMouseEnter(EventArgs e)
        {
            _hot = true;
            Invalidate();
            base.OnMouseEnter(e);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            _hot = false;
            Invalidate();
            base.OnMouseLeave(e);
        }

        protected override bool IsInputKey(Keys keyData)
        {
            return keyData == Keys.Space || keyData == Keys.Enter || base.IsInputKey(keyData);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Space || e.KeyCode == Keys.Enter)
            {
                On = !On;
                e.Handled = true;
            }
            base.OnKeyDown(e);
        }

        protected override void OnGotFocus(EventArgs e)
        {
            Invalidate();
            base.OnGotFocus(e);
        }

        protected override void OnLostFocus(EventArgs e)
        {
            Invalidate();
            base.OnLostFocus(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            LauncherTheme.Smooth(g);
            bool lit = _hot || Focused;
            Font label = _theme.Body(_theme.S(13), FontStyle.Bold);
            float top = _note.Length > 0 ? _theme.S(2) : (Height - label.GetHeight(g)) / 2f;
            using (var brush = new SolidBrush(lit ? LauncherTheme.Text : LauncherTheme.Text))
            {
                g.DrawString(Text, label, brush, 0, top);
            }
            if (_note.Length > 0)
            {
                Font note = _theme.Body(_theme.S(11));
                using var brush = new SolidBrush(LauncherTheme.TextDim);
                g.DrawString(_note, note, brush,
                    new RectangleF(0, top + label.GetHeight(g), Width - _theme.S(70), _theme.S(24)));
            }

            // The switch: a rounded track with the knob at one end or the
            // other, which reads at a glance in a column of twenty of them.
            int width = _theme.S(38);
            int height = _theme.S(18);
            var rect = new RectangleF(Width - width - _theme.S(2), (Height - height) / 2f, width, height);
            using System.Drawing.Drawing2D.GraphicsPath path =
                LauncherTheme.RoundRect(rect, height / 2f);
            using (var fill = new SolidBrush(_on
                ? (lit ? LauncherTheme.Shade(LauncherTheme.Accent, 0.15f) : LauncherTheme.Accent)
                : LauncherTheme.PanelLight))
            {
                g.FillPath(fill, path);
            }
            using (var pen = new Pen(_on ? LauncherTheme.Accent : LauncherTheme.Edge, _theme.S(1)))
            {
                g.DrawPath(pen, path);
            }
            float knob = height - _theme.S(6);
            float knobX = _on ? rect.Right - knob - _theme.S(3) : rect.Left + _theme.S(3);
            using var knobBrush = new SolidBrush(_on ? Color.FromArgb(8, 12, 18) : LauncherTheme.TextDim);
            g.FillEllipse(knobBrush, knobX, rect.Top + _theme.S(3), knob, knob);
        }
    }
}
