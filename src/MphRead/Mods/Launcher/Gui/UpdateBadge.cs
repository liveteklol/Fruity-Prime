using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// "Update now", in the bottom-left corner of the picture.
    ///
    /// Over the splash rather than in the menu on the right, for two reasons.
    /// It is not one of the things you came to the launcher to do, so it does
    /// not belong in a list of them; and the menu is a card that gets swapped
    /// out, which meant a fresh install -- sitting on the game-files card,
    /// which is exactly where an out-of-date copy is most likely to be -- never
    /// saw it at all. The picture is behind every card, so this is on screen
    /// whatever the launcher is showing.
    ///
    /// Sized to its own text: the subtitle names the file to fetch, and that
    /// is as long as the platform's name makes it.
    /// </summary>
    internal sealed class UpdateBadge : Control
    {
        private const double _titleSize = 13;
        private const double _subtitleSize = 11;
        private const double _tracking = 1.5;
        private const double _padX = 14;
        private const double _padY = 9;

        private string _title = "Update now";
        private string _subtitle = "";
        private bool _pressed;

        public event EventHandler? Click;

        public UpdateBadge()
        {
            Focusable = true;
            Cursor = new Cursor(StandardCursorType.Hand);
            IsVisible = false;
        }

        public void Show(string subtitle)
        {
            _subtitle = subtitle;
            IsVisible = true;
            InvalidateMeasure();
            InvalidateVisual();
        }

        /// <summary>
        /// Replace the second line, for when there was no browser to open and
        /// the address has to be readable instead.
        /// </summary>
        public void Say(string subtitle)
        {
            _subtitle = subtitle;
            InvalidateMeasure();
            InvalidateVisual();
        }

        protected override Size MeasureOverride(Size available)
        {
            double title = TrackedText.Measure(_title.ToUpperInvariant(), _titleSize, _tracking);
            double subtitle = _subtitle.Length > 0
                ? TrackedText.Make(_subtitle, _subtitleSize, bold: false, GuiTheme.TextBrush).Width
                : 0;
            double width = Math.Max(title, subtitle) + _padX * 2;
            double height = TrackedText.LineHeight(_titleSize) + _padY * 2
                + (_subtitle.Length > 0 ? _subtitleSize + 5 : 0);
            // Never wider than the picture it sits on.
            return new Size(Math.Min(width, available.Width), height);
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
            bool was = _pressed;
            _pressed = false;
            InvalidateVisual();
            base.OnPointerReleased(e);
            if (was && IsPointerOver)
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
                Click?.Invoke(this, EventArgs.Empty);
                return;
            }
            base.OnKeyDown(e);
        }

        public override void Render(DrawingContext context)
        {
            bool lit = IsPointerOver || IsFocused;
            var body = new Rect(0, 0, Bounds.Width, Bounds.Height);
            Color fill = _pressed
                ? GuiTheme.Shade(GuiTheme.Warm, -0.25)
                : lit ? GuiTheme.Shade(GuiTheme.Warm, 0.15) : GuiTheme.Warm;
            context.DrawRectangle(new SolidColorBrush(fill), null, new RoundedRect(body, 5));

            // Dark text on amber: the picture behind this is arbitrary, so the
            // badge carries its own contrast rather than relying on what it
            // happens to be sitting on.
            var ink = new SolidColorBrush(Color.FromRgb(26, 18, 4));
            TrackedText.Draw(context, _title.ToUpperInvariant(), _titleSize, ink,
                _padX, _padY, _tracking);
            if (_subtitle.Length > 0)
            {
                FormattedText sub = TrackedText.Make(_subtitle, _subtitleSize, bold: false,
                    new SolidColorBrush(Color.FromArgb(200, 26, 18, 4)));
                context.DrawText(sub, new Point(_padX,
                    _padY + TrackedText.LineHeight(_titleSize) + 3));
            }
        }
    }
}
