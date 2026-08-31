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

        /// <summary>
        /// The entry whose page is on screen, in a rail of them. Marked with
        /// the accent on the bar and the label -- but not with the hover fill,
        /// so that "this is where you are" and "this is what the pointer is
        /// over" stay two different things.
        /// </summary>
        public static readonly StyledProperty<bool> SelectedProperty =
            AvaloniaProperty.Register<MenuEntry, bool>(nameof(Selected));

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

        public bool Selected
        {
            get => GetValue(SelectedProperty);
            set => SetValue(SelectedProperty, value);
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
                SubtitleColorProperty, PrimaryProperty, SelectedProperty, IsEnabledProperty);
        }

        /// <summary>Two line heights: one for a bare label, one with a line under it.</summary>
        private const double PlainHeight = 42;
        private const double SubtitledHeight = 54;

        /// <summary>
        /// The height follows the subtitle, rather than being decided once in
        /// the constructor.
        ///
        /// The menus carry no descriptions any more -- an entry called "Join"
        /// did not need a line explaining that it joins -- but a few of them
        /// still say something the player has to see: that the game files are
        /// missing, that a demo would not open. Those arrive after the entry
        /// is built, and an entry built at the bare height would have drawn
        /// them off its own bottom edge.
        /// </summary>
        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == SubtitleProperty)
            {
                Height = Subtitle.Length > 0 ? SubtitledHeight : PlainHeight;
            }
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
            Height = subtitle.Length > 0 ? SubtitledHeight : PlainHeight;
        }

        /// <summary>
        /// How wide this wants to be when nothing says.
        ///
        /// A bare Control measures to nothing, and Height was the only size
        /// this ever set. Down a column that is correct: the parent hands the
        /// width down and the entry fills it. A *horizontal* StackPanel
        /// measures its children with an infinite width, so every entry in a
        /// row asked for nothing and the whole row was laid out on top of the
        /// first one, at the left edge.
        ///
        /// That is the settings on a phone. Below a certain width the rail of
        /// sections turns into a strip across the top, and the strip was eight
        /// section names printed over each other in the same place.
        /// </summary>
        protected override Size MeasureOverride(Size availableSize)
        {
            Size size = base.MeasureOverride(availableSize);
            double tracking = Primary ? 2 : 1;
            double width = TrackedText.Measure(Title.ToUpperInvariant(), _titleSize, tracking);
            if (Subtitle.Length > 0)
            {
                width = Math.Max(width, TrackedText.Measure(Subtitle, 12, tracking: 0));
            }
            // The bar down the left edge and the gap after it, which Render
            // uses, and as much again on the right so that two entries side by
            // side are not touching.
            const double padding = 3 + 14 + 14;
            // Asked for in every case, not only when the width is unconstrained.
            //
            // Reporting it only against an infinite width covered a horizontal
            // StackPanel and nothing else: a WrapPanel measures each child
            // against what is left of the line, which is a real number, so the
            // entries went back to asking for nothing and stacking up on each
            // other. Down a column this changes nothing -- a control that
            // stretches is stretched to the parent whatever it asked for -- and
            // it is the honest answer to the question either way.
            return new Size(Math.Min(width + padding, availableSize.Width), size.Height);
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
            // Captured, the way a stock Button does it: without it a touch
            // release is routed by a fresh hit test rather than to the row that
            // was pressed, and the press is simply lost.
            e.Pointer.Capture(this);
            InvalidateVisual();
            base.OnPointerPressed(e);
        }

        protected override void OnPointerReleased(PointerReleasedEventArgs e)
        {
            bool wasPressed = _pressed;
            _pressed = false;
            InvalidateVisual();
            if (ReferenceEquals(e.Pointer.Captured, this))
            {
                e.Pointer.Capture(null);
            }
            base.OnPointerReleased(e);
            // Where the release landed, not IsPointerOver: a finger hovers
            // nothing, so on a touchscreen the pointer is already gone from the
            // row by the time it lets go -- which is every button on Android
            // doing nothing when pressed.
            Point p = e.GetPosition(this);
            bool inside = p.X >= 0 && p.Y >= 0
                && p.X <= Bounds.Width && p.Y <= Bounds.Height;
            if (wasPressed && IsEnabled && inside)
            {
                Click?.Invoke(this, EventArgs.Empty);
            }
        }

        protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e)
        {
            // A drag that turned into a scroll takes the pointer away; the row
            // must not still think it is being pressed.
            _pressed = false;
            InvalidateVisual();
            base.OnPointerCaptureLost(e);
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
            bool marked = lit || (Selected && IsEnabled);
            var body = new Rect(0, 0, Bounds.Width, Bounds.Height);
            // Avalonia hit-tests what was drawn, not the bounds: without this
            // the row only answers the pointer over its glyphs.
            context.FillRectangle(Brushes.Transparent, body);
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
                new SolidColorBrush(marked ? Accent : Color.FromRgb(48, 56, 72)),
                new Rect(0, 0, bar, body.Height));

            double textLeft = bar + 14;
            Color titleColor = !IsEnabled ? GuiTheme.TextDim : marked ? Accent : GuiTheme.Text;
            double lineHeight = TrackedText.LineHeight(_titleSize);
            double top = Subtitle.Length > 0 ? 8 : (body.Height - lineHeight) / 2;
            TrackedText.Draw(context, Title.ToUpperInvariant(), _titleSize,
                new SolidColorBrush(titleColor), textLeft, top, tracking: 1);

            if (Subtitle.Length > 0)
            {
                FormattedText sub = TrackedText.Make(Subtitle, 12, bold: false,
                    new SolidColorBrush(IsEnabled ? SubtitleColor : GuiTheme.TextDim));
                context.DrawText(sub, new Point(textLeft - 1, top + lineHeight + 1));
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
            double width = TrackedText.Measure(text, _titleSize, tracking) - tracking;
            TrackedText.Draw(context, text, _titleSize, new SolidColorBrush(ink),
                (body.Width - width) / 2,
                (body.Height - TrackedText.LineHeight(_titleSize)) / 2, tracking);
        }

    }
}
