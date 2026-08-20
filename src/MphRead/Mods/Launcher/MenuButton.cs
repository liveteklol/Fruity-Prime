using System;
using System.Drawing;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// One line of the front screen's menu: a big label, an optional line of
    /// smaller text under it, and nothing else.
    ///
    /// Custom-painted rather than a themed Button because the stock control
    /// draws a system-coloured rectangle that no BackColor makes look like a
    /// game menu -- the existing settings window shows what that ends up
    /// looking like on a dark background.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class MenuButton : Control
    {
        private readonly LauncherTheme _theme;
        private readonly float _titleSize;
        private bool _hover;
        private bool _pressed;
        private string _subtitle = "";
        private Color _subtitleColor = LauncherTheme.TextDim;

        /// <summary>Colour of the marker and the title while hovered or focused.</summary>
        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public Color Accent { get; set; } = LauncherTheme.Accent;

        /// <summary>
        /// Draw as a filled block with centred text instead of a menu line.
        /// Reserved for the one action a card exists to perform, so that
        /// "connect" and "start" never have to be hunted for.
        /// </summary>
        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public bool Primary { get; set; }

        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public string Subtitle
        {
            get => _subtitle;
            set
            {
                if (_subtitle != value)
                {
                    _subtitle = value ?? "";
                    Invalidate();
                }
            }
        }

        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public Color SubtitleColor
        {
            get => _subtitleColor;
            set
            {
                if (_subtitleColor != value)
                {
                    _subtitleColor = value;
                    Invalidate();
                }
            }
        }

        public MenuButton(LauncherTheme theme, string text, string subtitle = "",
            float titleSize = 21)
        {
            _theme = theme;
            _titleSize = titleSize;
            _subtitle = subtitle;
            Text = text;
            // Selectable so the whole menu can be driven from the keyboard,
            // which is how somebody who just closed a full-screen game with
            // Escape expects to be able to answer "play again?".
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw
                | ControlStyles.Selectable, true);
            TabStop = true;
            Cursor = Cursors.Hand;
            // The parent panel is a flat colour; painting it here rather than
            // asking for a transparent background avoids the parent-repaint
            // dance WinForms does for ControlStyles.SupportsTransparentBackColor.
            BackColor = LauncherTheme.Panel;
            Height = theme.S(subtitle.Length > 0 ? 54 : 42);
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
            _pressed = false;
            Invalidate();
            base.OnMouseLeave(e);
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            _pressed = true;
            Focus();
            Invalidate();
            base.OnMouseDown(e);
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            _pressed = false;
            Invalidate();
            base.OnMouseUp(e);
        }

        protected override void OnTextChanged(EventArgs e)
        {
            // The label is painted, not drawn by the framework, so changing it
            // -- "Connect" to "Connecting" -- has to ask for the repaint.
            Invalidate();
            base.OnTextChanged(e);
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

        protected override bool IsInputKey(Keys keyData)
        {
            // Enter and Space have to reach OnKeyDown; without this WinForms
            // routes them to the form's AcceptButton and the focused entry
            // never fires.
            return keyData == Keys.Enter || keyData == Keys.Space || base.IsInputKey(keyData);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space)
            {
                e.Handled = true;
                OnClick(EventArgs.Empty);
                return;
            }
            base.OnKeyDown(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            LauncherTheme.Smooth(g);
            bool lit = (_hover || Focused) && Enabled;
            var body = new Rectangle(0, 0, Width, Height);

            if (Primary)
            {
                PaintPrimary(g, body, lit);
                return;
            }
            if (lit)
            {
                using var fill = new SolidBrush(_pressed
                    ? LauncherTheme.Shade(LauncherTheme.PanelLight, -0.2f)
                    : LauncherTheme.PanelLight);
                g.FillRectangle(fill, body);
            }
            int bar = _theme.S(3);
            using (var marker = new SolidBrush(lit ? Accent : Color.FromArgb(48, 56, 72)))
            {
                g.FillRectangle(marker, 0, 0, bar, Height);
            }

            int textLeft = bar + _theme.S(14);
            Font title = _theme.Display(_theme.S((int)_titleSize));
            Color titleColor = !Enabled
                ? LauncherTheme.TextDim
                : lit ? Accent : LauncherTheme.Text;
            float tracking = _theme.S(1);
            float titleHeight = title.GetHeight(g);
            float top = _subtitle.Length > 0
                ? _theme.S(8)
                : (Height - titleHeight) / 2f;
            LauncherTheme.DrawTracked(g, Text.ToUpperInvariant(), title, titleColor,
                textLeft, top, tracking);

            if (_subtitle.Length > 0)
            {
                Font sub = _theme.Body(_theme.S(12));
                using var brush = new SolidBrush(Enabled ? _subtitleColor : LauncherTheme.TextDim);
                g.DrawString(_subtitle, sub, brush, textLeft - _theme.S(1),
                    top + titleHeight + _theme.S(1));
            }
        }

        private void PaintPrimary(Graphics g, Rectangle body, bool lit)
        {
            var rect = new RectangleF(0, 0, body.Width - 1, body.Height - 1);
            using System.Drawing.Drawing2D.GraphicsPath path =
                LauncherTheme.RoundRect(rect, _theme.S(5));
            Color fill = !Enabled
                ? LauncherTheme.PanelLight
                : _pressed ? LauncherTheme.Shade(Accent, -0.25f)
                : lit ? LauncherTheme.Shade(Accent, 0.18f) : Accent;
            using (var brush = new SolidBrush(fill))
            {
                g.FillPath(brush, path);
            }
            Font title = _theme.Display(_theme.S((int)_titleSize));
            float tracking = _theme.S(2);
            string text = Text.ToUpperInvariant();
            float width = LauncherTheme.MeasureTracked(g, text, title, tracking) - tracking;
            LauncherTheme.DrawTracked(g, text, title,
                Enabled ? Color.FromArgb(8, 12, 18) : LauncherTheme.TextDim,
                (body.Width - width) / 2f, (body.Height - title.GetHeight(g)) / 2f, tracking);
        }
    }
}
