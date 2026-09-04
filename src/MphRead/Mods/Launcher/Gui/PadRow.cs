using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Threading;
using MphRead.Mods.Input;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// One rebindable pad button: what it does on the left, which button it is
    /// on to the right, click and press to change it.
    ///
    /// <see cref="KeyRow"/>'s shape, and none of its problem: a key arrives as
    /// a toolkit event that has to be translated into GLFW's enumeration,
    /// whereas a pad is already reduced to <see cref="GamepadState"/> by the
    /// time anything here could see it. So this does not listen for an event
    /// at all -- it watches the state, which is the same state the game reads
    /// and therefore cannot disagree with it about what was pressed.
    ///
    /// Watching starts only while a row is listening. The pad is polled on the
    /// desktop and evented on Android (see <c>MainActivity.DispatchKeyEvent</c>,
    /// which is what puts a pad press into that state while a match is not
    /// running), and neither should be happening for the sake of a settings
    /// screen nobody is currently rebinding on.
    /// </summary>
    internal sealed class PadRow : Control
    {
        private readonly PadAction _action;
        private readonly double _labelWidth;
        private bool _listening;
        private bool _hot;
        private DispatcherTimer? _watch;

        /// <summary>
        /// What the pad already had held when listening began, so a button
        /// that is being held for some other reason -- or one still down from
        /// the press that opened this row -- is not read as the answer. Only a
        /// button that goes down from here counts.
        /// </summary>
        private GamepadButtons _baseline;

        public event EventHandler? Rebound;

        public PadRow(PadAction action, double labelWidth = 160)
        {
            _action = action;
            _labelWidth = labelWidth;
            Height = 32;
            Focusable = true;
            Cursor = new Cursor(StandardCursorType.Hand);
        }

        private Rect Box => new(_labelWidth, 2,
            Math.Max(60, Bounds.Width - _labelWidth - 4), Bounds.Height - 4);

        protected override void OnPointerPressed(PointerPressedEventArgs e)
        {
            Focus();
            if (!_listening && Box.Contains(e.GetPosition(this)))
            {
                Listen();
            }
            e.Handled = true;
            base.OnPointerPressed(e);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (!_listening)
            {
                if (e.Key == Key.Enter || e.Key == Key.Space)
                {
                    Listen();
                    e.Handled = true;
                }
                base.OnKeyDown(e);
                return;
            }
            e.Handled = true;
            if (e.Key == Key.Escape)
            {
                Done();
                return;
            }
            if (e.Key == Key.Back || e.Key == Key.Delete)
            {
                // Unbinding is worth having: a pad with a broken bumper is
                // better with nothing on it than with something that fires by
                // itself, and GamepadInput reads None as "never held".
                PadBindings.Set(_action, GamepadButtons.None);
                Done();
            }
        }

        private void Listen()
        {
            _listening = true;
            GamepadDesktop.PollForMenu();
            _baseline = GamepadInput.State.Buttons;
            _watch?.Stop();
            _watch = new DispatcherTimer(TimeSpan.FromMilliseconds(30),
                DispatcherPriority.Input, (_, _) => Check());
            _watch.Start();
            InvalidateVisual();
        }

        private void Check()
        {
            if (!_listening)
            {
                return;
            }
            // Android fills the state from events and needs nothing here; the
            // desktop's pad is polled, and with no game window running there
            // is nothing else pumping GLFW. Both cases are inside this call.
            GamepadDesktop.PollForMenu();
            GamepadButtons pressed = GamepadInput.State.Buttons & ~_baseline;
            // Whatever is no longer held stops shielding: a player who was
            // holding a button when the row opened can still choose it by
            // letting go and pressing it again.
            _baseline &= GamepadInput.State.Buttons;
            if (pressed == GamepadButtons.None)
            {
                return;
            }
            // One button, not the handful a trigger can set off at once: the
            // lowest bit that is up. A binding with two buttons in it is a
            // default (the bumper and the d-pad both cycle weapons), not
            // something a press should produce.
            foreach (GamepadButtons button in Enum.GetValues<GamepadButtons>())
            {
                if (button != GamepadButtons.None && (pressed & button) == button)
                {
                    PadBindings.Set(_action, button);
                    Done();
                    return;
                }
            }
        }

        private void Done()
        {
            _listening = false;
            _watch?.Stop();
            _watch = null;
            InvalidateVisual();
            Rebound?.Invoke(this, EventArgs.Empty);
        }

        protected override void OnPointerEntered(PointerEventArgs e)
        {
            _hot = true;
            InvalidateVisual();
            base.OnPointerEntered(e);
        }

        protected override void OnPointerExited(PointerEventArgs e)
        {
            _hot = false;
            InvalidateVisual();
            base.OnPointerExited(e);
        }

        protected override void OnLostFocus(Avalonia.Interactivity.RoutedEventArgs e)
        {
            if (_listening)
            {
                Done();
            }
            base.OnLostFocus(e);
        }

        protected override void OnGotFocus(GotFocusEventArgs e)
        {
            InvalidateVisual();
            base.OnGotFocus(e);
        }

        protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
        {
            // The timer outlives the view otherwise, and it polls GLFW.
            _watch?.Stop();
            _watch = null;
            _listening = false;
            base.OnDetachedFromVisualTree(e);
        }

        public override void Render(DrawingContext context)
        {
            // See MenuEntry.Render: hit testing follows the drawing.
            context.FillRectangle(Brushes.Transparent,
                new Rect(0, 0, Bounds.Width, Bounds.Height));
            FormattedText label = TrackedText.Make(PadBindings.Name(_action), 12,
                bold: true, GuiTheme.TextBrush);
            context.DrawText(label, new Point(4, (Bounds.Height - label.Height) / 2));

            Rect box = Box;
            context.DrawRectangle(GuiTheme.PanelLightBrush,
                new Pen(new SolidColorBrush(_listening ? GuiTheme.Warm
                    : IsFocused || _hot ? GuiTheme.Accent : GuiTheme.Edge), 1),
                new RoundedRect(box, 4));

            string text = _listening
                ? "press a button on the pad"
                : PadBindings.Describe(PadBindings.Get(_action));
            FormattedText value = TrackedText.Make(text, 12, bold: true,
                new SolidColorBrush(_listening ? GuiTheme.Warm : GuiTheme.Text));
            value.MaxTextWidth = Math.Max(20, box.Width - 12);
            value.MaxTextHeight = box.Height;
            value.Trimming = TextTrimming.CharacterEllipsis;
            context.DrawText(value, new Point(box.X + (box.Width - value.Width) / 2,
                box.Y + (box.Height - value.Height) / 2));
        }
    }
}
