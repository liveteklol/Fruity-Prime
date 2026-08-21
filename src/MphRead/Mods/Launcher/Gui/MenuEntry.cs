using System;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// One line of the front screen's menu: a big label, an optional line of
    /// smaller text under it, and nothing else. The Avalonia counterpart of
    /// <c>MenuButton</c>, painted to the same design.
    ///
    /// Custom-rendered rather than a styled Button for the same reason the
    /// WinForms one is: what is wanted is a marker bar and tracked capitals,
    /// and expressing that as a control template is more code than drawing it.
    /// </summary>
    internal sealed class MenuEntry : Control
    {
        public static readonly StyledProperty<string> TitleProperty =
            AvaloniaProperty.Register<MenuEntry, string>(nameof(Title), "");

        public static readonly StyledProperty<string> SubtitleProperty =
            AvaloniaProperty.Register<MenuEntry, string>(nameof(Subtitle), "");

        public static readonly StyledProperty<Color> AccentProperty =
            AvaloniaProperty.Register<MenuEntry, Color>(nameof(Accent), GuiTheme.Accent);

        public static readonly StyledProperty<Color> SubtitleColorProperty =
            AvaloniaProperty.Register<MenuEntry, Color>(nameof(SubtitleColor), GuiTheme.TextDim);

        /// <summary>
        /// Draw as a filled block with centred text instead of a menu line.
        /// Reserved for the one action a card exists to perform, so that
        /// "connect" and "start" never have to be hunted for.
        /// </summary>
        public static readonly StyledProperty<bool> PrimaryProperty =
            AvaloniaProperty.Register<MenuEntry, bool>(nameof(Primary));

        public string Title
        {
            get => GetValue(TitleProperty);
            set => SetValue(TitleProperty, value);
        }

        public string Subtitle
        {
            get => GetValue(SubtitleProperty);
            set => SetValue(SubtitleProperty, value);
        }

        public Color Accent
        {
            get => GetValue(AccentProperty);
            set => SetValue(AccentProperty, value);
        }

        public Color SubtitleColor
        {
            get => GetValue(SubtitleColorProperty);
            set => SetValue(SubtitleColorProperty, value);
        }

        public bool Primary
        {
            get => GetValue(PrimaryProperty);
            set => SetValue(PrimaryProperty, value);
        }

        public event EventHandler? Click;

        private readonly double _titleSize;
        private bool _pressed;

        static MenuEntry()
        {
            // Every one of these changes what is drawn, and nothing else knows
            // to ask for a repaint -- the label is painted, not templated, so
            // "Connect" becoming "Connecting" has to invalidate here.
            AffectsRender<MenuEntry>(TitleProperty, SubtitleProperty, AccentProperty,
                SubtitleColorProperty, PrimaryProperty, IsEnabledProperty);
        }

        public MenuEntry(string title, string subtitle = "", double titleSize = 21)
        {
            Title = title;
            Subtitle = subtitle;
            _titleSize = titleSize;
            // Focusable so the whole menu can be driven from the keyboard,
            // which is how somebody who just closed a full-screen game with
            // Escape expects to be able to answer "play again?".
            Focusable = true;
            Cursor = new Cursor(StandardCursorType.Hand);
            Height = subtitle.Length > 0 ? 54 : 42;
        }

        protected override void OnPointerEntered(PointerEventArgs e)
        {
            InvalidateVisual();
            base.OnPointerEntered(e);
        }

        protected override void OnPointerExited(PointerEventArgs e)
        {
            _pressed = false;
            InvalidateVisual();
            base.OnPointerExited(e);
        }

        protected override void OnPointerPressed(PointerPressedEventArgs e)
        {
            _pressed = true;
            Focus();
            InvalidateVisual();
            base.OnPointerPressed(e);
        }

        protected override void OnPointerReleased(PointerReleasedEventArgs e)
        {
            bool wasPressed = _pressed;
            _pressed = false;
            InvalidateVisual();
            base.OnPointerReleased(e);
            if (wasPressed && IsEnabled && IsPointerOver)
            {
                Click?.Invoke(this, EventArgs.Empty);
            }
        }

        protected override void OnGotFocus(GotFocusEventArgs e)
        {
            InvalidateVisual();
            base.OnGotFocus(e);
        }

        protected override void OnLostFocus(RoutedEventArgs e)
        {
            InvalidateVisual();
            base.OnLostFocus(e);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Enter || e.Key == Key.Space)
            {
                e.Handled = true;
                if (IsEnabled)
                {
                    Click?.Invoke(this, EventArgs.Empty);
                }
                return;
            }
            base.OnKeyDown(e);
        }

        public override void Render(DrawingContext context)
        {
            bool lit = (IsPointerOver || IsFocused) && IsEnabled;
            var body = new Rect(0, 0, Bounds.Width, Bounds.Height);
            if (Primary)
            {
                RenderPrimary(context, body, lit);
                return;
            }
            if (lit)
            {
                context.FillRectangle(new SolidColorBrush(_pressed
                    ? GuiTheme.Shade(GuiTheme.PanelLight, -0.2)
                    : GuiTheme.PanelLight), body);
            }
            const double bar = 3;
            context.FillRectangle(
                new SolidColorBrush(lit ? Accent : Color.FromRgb(48, 56, 72)),
                new Rect(0, 0, bar, body.Height));

            double textLeft = bar + 14;
            Color titleColor = !IsEnabled ? GuiTheme.TextDim : lit ? Accent : GuiTheme.Text;
            FormattedText probe = Text("X", _titleSize, bold: true, titleColor);
            double top = Subtitle.Length > 0 ? 8 : (body.Height - probe.Height) / 2;
            DrawTracked(context, Title.ToUpperInvariant(), _titleSize, titleColor,
                textLeft, top, tracking: 1);

            if (Subtitle.Length > 0)
            {
                FormattedText sub = Text(Subtitle, 12, bold: false,
                    IsEnabled ? SubtitleColor : GuiTheme.TextDim);
                context.DrawText(sub, new Point(textLeft - 1, top + probe.Height + 1));
            }
        }

        private void RenderPrimary(DrawingContext context, Rect body, bool lit)
        {
            Color fill = !IsEnabled
                ? GuiTheme.PanelLight
                : _pressed ? GuiTheme.Shade(Accent, -0.25)
                : lit ? GuiTheme.Shade(Accent, 0.18) : Accent;
            context.DrawRectangle(new SolidColorBrush(fill), null,
                new RoundedRect(body, 5));
            string text = Title.ToUpperInvariant();
            Color ink = IsEnabled ? Color.FromRgb(8, 12, 18) : GuiTheme.TextDim;
            const double tracking = 2;
            double width = MeasureTracked(text, _titleSize, tracking) - tracking;
            FormattedText probe = Text("X", _titleSize, bold: true, ink);
            DrawTracked(context, text, _titleSize, ink,
                (body.Width - width) / 2, (body.Height - probe.Height) / 2, tracking);
        }

        private static FormattedText Text(string text, double size, bool bold, Color color)
            => new(text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
                GuiTheme.Face(bold), size, new SolidColorBrush(color));

        /// <summary>
        /// Draw text with extra space between letters.
        ///
        /// Avalonia has no letter-spacing any more than GDI+ does, and the
        /// small upper-case labels this screen uses are unreadable without it.
        /// Glyph by glyph is fine for the handful of short strings involved;
        /// do not use it for running text.
        /// </summary>
        private static void DrawTracked(DrawingContext context, string text, double size,
            Color color, double x, double y, double tracking)
        {
            double space = SpaceWidth(size);
            double pen = x;
            foreach (char c in text)
            {
                if (c == ' ')
                {
                    pen += space + tracking;
                    continue;
                }
                FormattedText glyph = Text(c.ToString(), size, bold: true, color);
                context.DrawText(glyph, new Point(pen, y));
                pen += glyph.Width + tracking;
            }
        }

        private static double MeasureTracked(string text, double size, double tracking)
        {
            double space = SpaceWidth(size);
            double width = 0;
            foreach (char c in text)
            {
                width += (c == ' '
                    ? space
                    : Text(c.ToString(), size, bold: true, GuiTheme.Text).Width) + tracking;
            }
            return width;
        }

        /// <summary>
        /// How wide a space is in this font.
        ///
        /// Measured as the difference between two strings rather than directly,
        /// the same way the WinForms theme does it and for the same reason: a
        /// lone space measures as very nearly nothing, and a title drawn glyph
        /// by glyph off that number comes out as PLAYONLINE.
        /// </summary>
        private static double SpaceWidth(double size)
        {
            double pair = Text("nn", size, bold: true, GuiTheme.Text).Width;
            double spaced = Text("n n", size, bold: true, GuiTheme.Text).Width;
            return Math.Max(spaced - pair, size * 0.22);
        }
    }
}
