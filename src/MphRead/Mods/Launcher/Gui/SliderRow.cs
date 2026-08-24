using System;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// A labelled 0-100 slider, shaped like <see cref="ChoiceRow"/> so that a
    /// column of rows lines up whatever each one is.
    ///
    /// Drawn rather than a styled Slider for the same reason every other row
    /// here is drawn: what is wanted is a track, a fill and a value on the
    /// right, and a control template that produces exactly that is more code
    /// than the drawing.
    /// </summary>
    internal sealed class SliderRow : Control
    {
        private readonly string _label;
        private readonly double _labelWidth;
        private readonly Func<int, string> _format;
        private int _value;
        private bool _dragging;
        private bool _hot;

        public event EventHandler? ValueChanged;

        public SliderRow(string label, int value, Func<int, string>? format = null,
            double labelWidth = 120)
        {
            _label = label;
            _labelWidth = labelWidth;
            _value = Math.Clamp(value, 0, 100);
            _format = format ?? (v => $"{v.ToString(CultureInfo.InvariantCulture)}%");
            Height = 34;
            Focusable = true;
            Cursor = new Cursor(StandardCursorType.Hand);
        }

        public int Value
        {
            get => _value;
            set
            {
                int clamped = Math.Clamp(value, 0, 100);
                if (clamped != _value)
                {
                    _value = clamped;
                    InvalidateVisual();
                    ValueChanged?.Invoke(this, EventArgs.Empty);
                }
            }
        }

        private Rect Track => new(_labelWidth, Bounds.Height / 2 - 2,
            Math.Max(40, Bounds.Width - _labelWidth - 64), 4);

        private void SetFromPointer(double x)
        {
            Rect track = Track;
            double fraction = (x - track.X) / Math.Max(1, track.Width);
            Value = (int)Math.Round(Math.Clamp(fraction, 0, 1) * 100);
        }

        protected override void OnPointerPressed(PointerPressedEventArgs e)
        {
            Focus();
            Point p = e.GetPosition(this);
            if (p.X >= _labelWidth && IsEnabled)
            {
                _dragging = true;
                // Captured so a drag that leaves the row keeps moving the
                // value: letting go of a slider two pixels above the track is
                // not a gesture anybody means.
                e.Pointer.Capture(this);
                SetFromPointer(p.X);
            }
            base.OnPointerPressed(e);
        }

        protected override void OnPointerMoved(PointerEventArgs e)
        {
            Point p = e.GetPosition(this);
            bool hot = p.X >= _labelWidth;
            if (hot != _hot)
            {
                _hot = hot;
                InvalidateVisual();
            }
            if (_dragging)
            {
                SetFromPointer(p.X);
            }
            base.OnPointerMoved(e);
        }

        protected override void OnPointerReleased(PointerReleasedEventArgs e)
        {
            _dragging = false;
            e.Pointer.Capture(null);
            base.OnPointerReleased(e);
        }

        protected override void OnPointerExited(PointerEventArgs e)
        {
            _hot = false;
            InvalidateVisual();
            base.OnPointerExited(e);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (!IsEnabled)
            {
                base.OnKeyDown(e);
                return;
            }
            if (e.Key == Key.Left)
            {
                Value -= 5;
                e.Handled = true;
                return;
            }
            if (e.Key == Key.Right)
            {
                Value += 5;
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
        }

        protected override void OnGotFocus(GotFocusEventArgs e)
        {
            InvalidateVisual();
            base.OnGotFocus(e);
        }

        protected override void OnLostFocus(Avalonia.Interactivity.RoutedEventArgs e)
        {
            InvalidateVisual();
            base.OnLostFocus(e);
        }

        public override void Render(DrawingContext context)
        {
            // See MenuEntry.Render: hit testing follows the drawing.
            context.FillRectangle(Brushes.Transparent,
                new Rect(0, 0, Bounds.Width, Bounds.Height));
            var dim = new SolidColorBrush(Color.FromRgb(70, 76, 90));
            TrackedText.Draw(context, _label.ToUpperInvariant(), 11,
                IsEnabled ? GuiTheme.TextDimBrush : dim,
                4, (Bounds.Height - TrackedText.LineHeight(11)) / 2, tracking: 1);

            Rect track = Track;
            context.FillRectangle(GuiTheme.PanelLightBrush, track);
            double filled = track.Width * (_value / 100.0);
            IBrush accent = IsEnabled
                ? new SolidColorBrush(IsFocused || _hot
                    ? GuiTheme.Shade(GuiTheme.Accent, 0.15) : GuiTheme.Accent)
                : dim;
            context.FillRectangle(accent, new Rect(track.X, track.Y, filled, track.Height));
            context.DrawEllipse(accent, null,
                new Point(track.X + filled, track.Y + track.Height / 2), 5, 5);

            FormattedText value = TrackedText.Make(_format(_value), 12, bold: true,
                IsEnabled ? GuiTheme.TextBrush : dim);
            context.DrawText(value, new Point(Bounds.Width - 4 - value.Width,
                (Bounds.Height - value.Height) / 2));
        }
    }
}
