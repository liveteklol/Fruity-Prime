using System;
using System.Drawing;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// Pick a hunter from a grid of eight tiles: the seven playable ones and
    /// "random".
    ///
    /// The line under the grid names the selected hunter's alt form and its
    /// affinity weapon, which is the only difference between hunters that
    /// changes how a match plays -- and the one thing a new player has no way
    /// to find out from a list of names.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class HunterPicker : Control
    {
        private readonly LauncherTheme _theme;
        private int _hot = -1;
        private Hunter _selected = Hunter.Samus;

        public event EventHandler? Changed;

        private const int _columns = 4;
        private const int _count = 8; // seven hunters plus Random

        /// <summary>
        /// Alt form names. Not in the metadata anywhere -- the game shows them
        /// in text the launcher has no reader for -- so they are written out
        /// here, in hunter order.
        /// </summary>
        private static readonly string[] _altForms =
        {
            "Morph Ball", "Stinglarva", "Triskelion", "Lockjaw",
            "Vhoscythe", "Dialanche", "Halfturret"
        };

        /// <summary>Roughly each hunter's own colour, for the selected tile.</summary>
        private static readonly Color[] _colors =
        {
            Color.FromArgb(242, 166, 59),   // Samus
            Color.FromArgb(200, 224, 74),   // Kanden
            Color.FromArgb(75, 208, 122),   // Trace
            Color.FromArgb(58, 160, 255),   // Sylux
            Color.FromArgb(154, 123, 255),  // Noxus
            Color.FromArgb(224, 112, 60),   // Spire
            Color.FromArgb(127, 214, 214),  // Weavel
            Color.FromArgb(150, 160, 180)   // Random
        };

        public HunterPicker(LauncherTheme theme)
        {
            _theme = theme;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw
                | ControlStyles.Selectable, true);
            TabStop = true;
            // The parent panel is a flat colour; painting it here rather than
            // asking for a transparent background avoids the parent-repaint
            // dance WinForms does for ControlStyles.SupportsTransparentBackColor.
            BackColor = LauncherTheme.Panel;
            Height = theme.S(42) * 2 + theme.S(6) + theme.S(22);
        }

        /// <summary>May be <see cref="Hunter.Random"/>; the caller resolves that.</summary>
        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public Hunter Selected
        {
            get => _selected;
            set
            {
                if (_selected != value)
                {
                    _selected = value;
                    Invalidate();
                    Changed?.Invoke(this, EventArgs.Empty);
                }
            }
        }

        private int TileWidth => (Width - _theme.S(6) * (_columns - 1)) / _columns;

        private Rectangle TileAt(int index)
        {
            int gap = _theme.S(6);
            int height = _theme.S(42);
            int column = index % _columns;
            int row = index / _columns;
            return new Rectangle(column * (TileWidth + gap), row * (height + gap),
                TileWidth, height);
        }

        private int IndexAt(Point point)
        {
            for (int i = 0; i < _count; i++)
            {
                if (TileAt(i).Contains(point))
                {
                    return i;
                }
            }
            return -1;
        }

        private static Hunter HunterAt(int index) => index == 7 ? Hunter.Random : (Hunter)index;

        private static string NameOf(Hunter hunter) =>
            hunter == Hunter.Random ? "Random" : hunter.ToString();

        protected override void OnMouseMove(MouseEventArgs e)
        {
            int hot = IndexAt(e.Location);
            if (hot != _hot)
            {
                _hot = hot;
                Cursor = hot >= 0 ? Cursors.Hand : Cursors.Default;
                Invalidate();
            }
            base.OnMouseMove(e);
        }

        protected override void OnMouseLeave(EventArgs e)
        {
            _hot = -1;
            Invalidate();
            base.OnMouseLeave(e);
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            Focus();
            int index = IndexAt(e.Location);
            if (index >= 0)
            {
                Selected = HunterAt(index);
            }
            base.OnMouseDown(e);
        }

        protected override bool IsInputKey(Keys keyData)
        {
            return keyData == Keys.Left || keyData == Keys.Right || keyData == Keys.Up
                || keyData == Keys.Down || base.IsInputKey(keyData);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            int index = _selected == Hunter.Random ? 7 : (int)_selected;
            int moved = e.KeyCode switch
            {
                Keys.Left => index - 1,
                Keys.Right => index + 1,
                Keys.Up => index - _columns,
                Keys.Down => index + _columns,
                _ => index
            };
            if (moved != index)
            {
                e.Handled = true;
                Selected = HunterAt(((moved % _count) + _count) % _count);
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
            Font font = _theme.Body(_theme.S(12), FontStyle.Bold);
            using var format = new StringFormat
            {
                Alignment = StringAlignment.Center,
                LineAlignment = StringAlignment.Center,
                FormatFlags = StringFormatFlags.NoWrap,
                Trimming = StringTrimming.EllipsisCharacter
            };

            for (int i = 0; i < _count; i++)
            {
                Hunter hunter = HunterAt(i);
                bool selected = hunter == _selected;
                Rectangle tile = TileAt(i);
                Color color = _colors[i];
                using System.Drawing.Drawing2D.GraphicsPath path =
                    LauncherTheme.RoundRect(tile, _theme.S(4));
                using (var fill = new SolidBrush(selected
                    ? Color.FromArgb(56, color.R, color.G, color.B)
                    : LauncherTheme.PanelLight))
                {
                    g.FillPath(fill, path);
                }
                using (var pen = new Pen(selected
                    ? color
                    : _hot == i ? LauncherTheme.Edge : Color.FromArgb(30, 36, 48),
                    _theme.S(selected ? 2 : 1)))
                {
                    g.DrawPath(pen, path);
                }
                using var text = new SolidBrush(selected
                    ? LauncherTheme.Text
                    : _hot == i ? LauncherTheme.Text : LauncherTheme.TextDim);
                g.DrawString(NameOf(hunter), font, text, tile, format);
            }

            // Focus is shown on the caption line rather than on a tile: the
            // selected tile is already outlined, and two outlines at once
            // reads as two selections.
            Font caption = _theme.Body(_theme.S(11));
            string blurb = _selected == Hunter.Random
                ? "A different hunter every time you play."
                : $"{_altForms[(int)_selected]}  \u00B7  affinity weapon: "
                    + $"{Split(Weapons.GetAffinityBeam(_selected).ToString())}";
            using var captionBrush = new SolidBrush(Focused
                ? LauncherTheme.Accent
                : LauncherTheme.TextDim);
            g.DrawString(blurb, caption, captionBrush, 0,
                _theme.S(42) * 2 + _theme.S(6) + _theme.S(5));
        }

        /// <summary>"ShockCoil" -> "Shock Coil".</summary>
        private static string Split(string name)
        {
            var builder = new System.Text.StringBuilder(name.Length + 2);
            for (int i = 0; i < name.Length; i++)
            {
                if (i > 0 && Char.IsUpper(name[i]))
                {
                    builder.Append(' ');
                }
                builder.Append(name[i]);
            }
            return builder.ToString();
        }
    }
}
