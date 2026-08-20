using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// A labelled value with an arrow on each side: map, mode, how many
    /// opponents.
    ///
    /// A drop-down list would need fewer lines here, but a combo box is the
    /// one control WinForms refuses to paint dark, and stepping is the right
    /// gesture anyway when the choice is illustrated by the picture next to
    /// it -- you step and watch the map change. Clicking the value itself
    /// raises <see cref="Activated"/>, which the map row uses to open the
    /// full grid for people who would rather see all of them at once.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class ChoiceRow : Control
    {
        private readonly LauncherTheme _theme;
        private readonly string _label;
        private readonly int _labelWidth;
        private readonly List<string> _items = new();
        private int _index;
        private int _hotZone = -1; // -1 none, 0 left arrow, 1 value, 2 right arrow

        public event EventHandler? Changed;
        public event EventHandler? Activated;

        /// <summary>Draws the value as a link, for rows that open something.</summary>
        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public bool ValueClickable { get; set; }

        public ChoiceRow(LauncherTheme theme, string label, int labelWidth = 96)
        {
            _theme = theme;
            _label = label;
            _labelWidth = theme.S(labelWidth);
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw
                | ControlStyles.Selectable, true);
            TabStop = true;
            // The parent panel is a flat colour; painting it here rather than
            // asking for a transparent background avoids the parent-repaint
            // dance WinForms does for ControlStyles.SupportsTransparentBackColor.
            BackColor = LauncherTheme.Panel;
            Height = theme.S(38);
        }

        public IReadOnlyList<string> Items => _items;

        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public int Index
        {
            get => _index;
            set
            {
                int count = _items.Count;
                if (count == 0)
                {
                    _index = 0;
                    return;
                }
                // Wraps rather than clamps: stepping past the last map should
                // land on the first, not stop dead with no way to tell why.
                int wrapped = ((value % count) + count) % count;
                if (wrapped != _index)
                {
                    _index = wrapped;
                    Invalidate();
                    Changed?.Invoke(this, EventArgs.Empty);
                }
            }
        }

        public string Value => _index >= 0 && _index < _items.Count ? _items[_index] : "";

        public void SetItems(IEnumerable<string> items, int index)
        {
            _items.Clear();
            _items.AddRange(items);
            _index = _items.Count == 0 ? 0 : Math.Clamp(index, 0, _items.Count - 1);
            Invalidate();
        }

        /// <summary>Move without raising Changed, for code that is already reacting to it.</summary>
        public void SetIndexQuiet(int index)
        {
            if (_items.Count == 0)
            {
                return;
            }
            _index = ((index % _items.Count) + _items.Count) % _items.Count;
            Invalidate();
        }

        private Rectangle BoxRect => new(_labelWidth, 0, Width - _labelWidth, Height);

        private int ZoneAt(Point point)
        {
            Rectangle box = BoxRect;
            if (!box.Contains(point))
            {
                return -1;
            }
            int arrow = _theme.S(34);
            if (point.X < box.Left + arrow)
            {
                return 0;
            }
            return point.X > box.Right - arrow ? 2 : 1;
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            int zone = ZoneAt(e.Location);
            if (zone != _hotZone)
            {
                _hotZone = zone;
                Cursor = zone == 0 || zone == 2 || (zone == 1 && ValueClickable)
                    ? Cursors.Hand
                    : Cursors.Default;
                Invalidate();
            }
            base.OnMouseMove(e);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            _hotZone = -1;
            Invalidate();
            base.OnMouseLeave(e);
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            Focus();
            switch (ZoneAt(e.Location))
            {
                case 0:
                    Index--;
                    break;
                case 2:
                    Index++;
                    break;
                case 1:
                    if (ValueClickable)
                    {
                        Activated?.Invoke(this, EventArgs.Empty);
                    }
                    break;
            }
            base.OnMouseDown(e);
        }

        protected override bool IsInputKey(Keys keyData)
        {
            return keyData == Keys.Left || keyData == Keys.Right || keyData == Keys.Enter
                || base.IsInputKey(keyData);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Left)
            {
                Index--;
                e.Handled = true;
            }
            else if (e.KeyCode == Keys.Right)
            {
                Index++;
                e.Handled = true;
            }
            else if (e.KeyCode == Keys.Enter && ValueClickable)
            {
                Activated?.Invoke(this, EventArgs.Empty);
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
                LauncherTheme.TextDim, 0,
                (Height - label.GetHeight(g)) / 2f, _theme.S(1));

            Rectangle box = BoxRect;
            var inner = new RectangleF(box.X, box.Y + _theme.S(1),
                box.Width - _theme.S(1), box.Height - _theme.S(2));
            using (System.Drawing.Drawing2D.GraphicsPath path =
                LauncherTheme.RoundRect(inner, _theme.S(4)))
            {
                using var fill = new SolidBrush(LauncherTheme.PanelLight);
                g.FillPath(fill, path);
                using var pen = new Pen(Focused ? LauncherTheme.Accent : LauncherTheme.Edge,
                    _theme.S(1));
                g.DrawPath(pen, path);
            }

            Font value = _theme.Body(_theme.S(14), FontStyle.Bold);
            bool valueLit = _hotZone == 1 && ValueClickable;
            using (var brush = new SolidBrush(valueLit ? LauncherTheme.Accent : LauncherTheme.Text))
            using (var format = new StringFormat
            {
                Alignment = StringAlignment.Center,
                LineAlignment = StringAlignment.Center,
                Trimming = StringTrimming.EllipsisCharacter,
                FormatFlags = StringFormatFlags.NoWrap
            })
            {
                int arrow = _theme.S(34);
                var text = new RectangleF(box.X + arrow, box.Y,
                    box.Width - arrow * 2, box.Height);
                g.DrawString(Value, value, brush, text, format);
            }

            DrawArrow(g, box, left: true, lit: _hotZone == 0);
            DrawArrow(g, box, left: false, lit: _hotZone == 2);
        }

        private void DrawArrow(Graphics g, Rectangle box, bool left, bool lit)
        {
            float size = _theme.S(5);
            float cx = left ? box.Left + _theme.S(17) : box.Right - _theme.S(17);
            float cy = box.Y + box.Height / 2f;
            PointF[] points = left
                ? new[]
                {
                    new PointF(cx + size * 0.6f, cy - size),
                    new PointF(cx + size * 0.6f, cy + size),
                    new PointF(cx - size * 0.7f, cy)
                }
                : new[]
                {
                    new PointF(cx - size * 0.6f, cy - size),
                    new PointF(cx - size * 0.6f, cy + size),
                    new PointF(cx + size * 0.7f, cy)
                };
            using var brush = new SolidBrush(lit ? LauncherTheme.Accent : LauncherTheme.TextDim);
            g.FillPolygon(brush, points);
        }
    }
}
