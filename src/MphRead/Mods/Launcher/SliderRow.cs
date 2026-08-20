using System;
using System.Drawing;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// A labelled 0-100 slider, shaped like <see cref="ChoiceRow"/> so a column
    /// of rows lines up whatever each one is.
    ///
    /// Painted rather than a TrackBar for the same reason as everything else
    /// on these screens: the stock control draws a system-coloured track that
    /// no property will darken.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class SliderRow : Control
    {
        private readonly LauncherTheme _theme;
        private readonly string _label;
        private readonly int _labelWidth;
        private readonly Func<int, string> _format;
        private int _value;
        private bool _dragging;
        private bool _hot;

        public event EventHandler? ValueChanged;

        public SliderRow(LauncherTheme theme, string label, int value,
            Func<int, string>? format = null, int labelWidth = 120)
        {
            _theme = theme;
            _label = label;
            _labelWidth = theme.S(labelWidth);
            _value = Math.Clamp(value, 0, 100);
            _format = format ?? (v => $"{v}%");
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw
                | ControlStyles.Selectable, true);
            TabStop = true;
            BackColor = LauncherTheme.Panel;
            Height = theme.S(34);
        }

        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public int Value
        {
            get => _value;
            set
            {
                int clamped = Math.Clamp(value, 0, 100);
                if (clamped != _value)
                {
                    _value = clamped;
                    Invalidate();
                    ValueChanged?.Invoke(this, EventArgs.Empty);
                }
            }
        }

        private Rectangle Track => new(_labelWidth, Height / 2 - _theme.S(2),
            Math.Max(_theme.S(40), Width - _labelWidth - _theme.S(64)), _theme.S(4));

        private void SetFromMouse(int x)
        {
            Rectangle track = Track;
            float fraction = (x - track.Left) / (float)Math.Max(1, track.Width);
            Value = (int)Math.Round(Math.Clamp(fraction, 0, 1) * 100);
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            Focus();
            if (e.X >= _labelWidth)
            {
                _dragging = true;
                SetFromMouse(e.X);
            }
            base.OnMouseDown(e);
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            bool hot = e.X >= _labelWidth;
            if (hot != _hot)
            {
                _hot = hot;
                Cursor = hot ? Cursors.Hand : Cursors.Default;
                Invalidate();
            }
            if (_dragging)
            {
                SetFromMouse(e.X);
            }
            base.OnMouseMove(e);
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            _dragging = false;
            base.OnMouseUp(e);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            _hot = false;
            Invalidate();
            base.OnMouseLeave(e);
        }

        protected override bool IsInputKey(Keys keyData)
        {
            return keyData == Keys.Left || keyData == Keys.Right || base.IsInputKey(keyData);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Left)
            {
                Value -= 5;
                e.Handled = true;
            }
            else if (e.KeyCode == Keys.Right)
            {
                Value += 5;
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
            Font label = _theme.Body(_theme.S(11), FontStyle.Bold);
            LauncherTheme.DrawTracked(g, _label.ToUpperInvariant(), label,
                Enabled ? LauncherTheme.TextDim : Color.FromArgb(70, 76, 90),
                0, (Height - label.GetHeight(g)) / 2f, _theme.S(1));

            Rectangle track = Track;
            using (var back = new SolidBrush(LauncherTheme.PanelLight))
            {
                g.FillRectangle(back, track);
            }
            int filled = (int)Math.Round(track.Width * (_value / 100f));
            Color accent = Enabled
                ? (Focused || _hot ? LauncherTheme.Shade(LauncherTheme.Accent, 0.15f) : LauncherTheme.Accent)
                : Color.FromArgb(70, 76, 90);
            using (var fill = new SolidBrush(accent))
            {
                g.FillRectangle(fill, track.Left, track.Top, filled, track.Height);
                int knob = _theme.S(5);
                g.FillEllipse(fill, track.Left + filled - knob, track.Top + track.Height / 2 - knob,
                    knob * 2, knob * 2);
            }

            Font value = _theme.Body(_theme.S(12), FontStyle.Bold);
            using var text = new SolidBrush(Enabled ? LauncherTheme.Text : Color.FromArgb(70, 76, 90));
            using var format = new StringFormat
            {
                Alignment = StringAlignment.Far,
                LineAlignment = StringAlignment.Center
            };
            g.DrawString(_format(_value), value, text,
                new RectangleF(Width - _theme.S(58), 0, _theme.S(56), Height), format);
        }
    }
}
