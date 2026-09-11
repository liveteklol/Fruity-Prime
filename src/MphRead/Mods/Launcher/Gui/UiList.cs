using System;
using System.Collections.Generic;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Threading;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// One line of a list: what it is on the left, what is worth knowing about
    /// it on the right.
    ///
    /// Deliberately not a tile. The map grid drew every room as a picture,
    /// which is the right answer to "which map is that" and the wrong one to
    /// "how many maps are there" -- twenty-seven pictures is a page to scroll
    /// and a column of names is a glance. The picture has not gone: it is what
    /// the chosen line shows beside the list.
    /// </summary>
    internal sealed class UiListRow : Control
    {
        public event EventHandler? Clicked;

        /// <summary>What choosing this line means, for the screen holding the list.</summary>
        public object? Choice { get; init; }

        private readonly string _title;
        private string _detail;
        private bool _hot;

        public UiListRow(string title, string detail = "")
        {
            _title = title;
            _detail = detail;
            Height = 30;
            Focusable = true;
            Cursor = new Cursor(StandardCursorType.Hand);
        }

        public string Detail
        {
            get => _detail;
            set
            {
                _detail = value;
                InvalidateVisual();
            }
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

        protected override void OnPointerReleased(PointerReleasedEventArgs e)
        {
            // See UiWord.OnPointerReleased: a finger hovers nothing, so the
            // release position is what decides, not IsPointerOver.
            Point p = e.GetPosition(this);
            if (p.X >= 0 && p.Y >= 0 && p.X <= Bounds.Width && p.Y <= Bounds.Height)
            {
                Focus();
                Clicked?.Invoke(this, EventArgs.Empty);
            }
            base.OnPointerReleased(e);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Enter || e.Key == Key.Space)
            {
                Clicked?.Invoke(this, EventArgs.Empty);
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

        protected override void OnLostFocus(RoutedEventArgs e)
        {
            InvalidateVisual();
            base.OnLostFocus(e);
        }

        public override void Render(DrawingContext context)
        {
            var full = new Rect(0, 0, Bounds.Width, Bounds.Height);
            context.FillRectangle(Brushes.Transparent, full);
            bool lit = _hot || IsFocused;
            if (lit)
            {
                // A caret rather than a fill: the list sits on a photograph,
                // and a row of panel colour over it is a box -- which is the
                // one thing none of these screens has any more.
                context.FillRectangle(GuiTheme.AccentBrush,
                    new Rect(0, 6, 3, Bounds.Height - 12));
            }
            var title = new FormattedText(_title, CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, GuiTheme.Face(bold: true), 14,
                new SolidColorBrush(lit ? GuiTheme.Accent : GuiTheme.Text));
            double right = Bounds.Width;
            if (_detail.Length > 0)
            {
                var detail = new FormattedText(_detail, CultureInfo.InvariantCulture,
                    FlowDirection.LeftToRight, GuiTheme.Face(bold: false), 12,
                    GuiTheme.TextDimBrush)
                {
                    MaxTextWidth = Math.Max(40, Bounds.Width * 0.45),
                    MaxTextHeight = 20,
                    Trimming = TextTrimming.CharacterEllipsis
                };
                context.DrawText(detail, new Point(Bounds.Width - detail.Width - 4,
                    (Bounds.Height - detail.Height) / 2));
                right = Bounds.Width - detail.Width - 14;
            }
            title.MaxTextWidth = Math.Max(40, right - 14);
            title.MaxTextHeight = 22;
            title.Trimming = TextTrimming.CharacterEllipsis;
            context.DrawText(title, new Point(14, (Bounds.Height - title.Height) / 2));
        }
    }

    /// <summary>
    /// The one list in the launcher.
    ///
    /// Servers, maps, save slots, recordings and the maps a vote can be called
    /// on were five lists with five layouts, four of which were built once and
    /// never looked at again. This is the column all five are shown in: rows
    /// are whatever control suits them -- <see cref="UiListRow"/> for a name,
    /// <see cref="ServerRow"/> for five columns that have to line up -- and
    /// what belongs here is only the part they share, which is the scrolling
    /// and the up and down keys.
    /// </summary>
    internal sealed class UiList : Panel
    {
        private readonly StackPanel _rows = new() { Spacing = 1 };
        private readonly Panel _header = new();
        private readonly ScrollViewer _scroll;
        private readonly List<Control> _focusable = new();

        /// <summary>The row the keyboard is on, or the last one pressed.</summary>
        public Control? Selected { get; private set; }

        /// <summary>Raised when a row is pressed or Enter is taken on it.</summary>
        public event EventHandler<Control>? Activated;

        /// <summary>
        /// Raised when the keyboard merely lands on a different row. What a
        /// screen showing a picture of the chosen thing listens to -- the
        /// picture should follow the arrow keys, not wait for a press.
        /// </summary>
        public event EventHandler<Control>? SelectionChanged;

        public UiList()
        {
            _scroll = new ScrollViewer
            {
                Content = _rows,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto
            };
            var dock = new DockPanel { LastChildFill = true };
            DockPanel.SetDock(_header, Dock.Top);
            dock.Children.Add(_header);
            dock.Children.Add(_scroll);
            Children.Add(dock);
        }

        /// <summary>
        /// Column headings, outside the scroller so they stay put while it
        /// scrolls -- which is the whole point of having them.
        /// </summary>
        public void SetHeader(Control? header)
        {
            _header.Children.Clear();
            if (header != null)
            {
                _header.Children.Add(header);
            }
        }

        public void Clear()
        {
            _rows.Children.Clear();
            _focusable.Clear();
            Selected = null;
        }

        /// <summary>
        /// Add a row and say what pressing it means. The row keeps its own
        /// drawing and its own click; what is added here is that pressing it
        /// also makes it the selection, so the tick in the corner and the
        /// options beside the list are talking about the same line.
        /// </summary>
        public void Add(Control row, Action<Control>? activate = null)
        {
            _rows.Children.Add(row);
            if (!row.Focusable)
            {
                return;
            }
            _focusable.Add(row);
            Selected ??= row;
            row.GotFocus += (_, _) => Select(row);
            if (row is UiListRow line)
            {
                line.Clicked += (_, _) => Fire(row, activate);
            }
            else if (row is ServerRow server)
            {
                server.Clicked += (_, _) => Fire(row, activate);
            }
        }

        /// <summary>A line that is not a choice: an empty list saying so.</summary>
        public void AddNote(string text, Color? colour = null)
        {
            _rows.Children.Add(new Note(text, colour));
        }

        private void Fire(Control row, Action<Control>? activate)
        {
            Select(row);
            activate?.Invoke(row);
            Activated?.Invoke(this, row);
        }

        private void Select(Control row)
        {
            if (ReferenceEquals(Selected, row))
            {
                return;
            }
            Selected = row;
            SelectionChanged?.Invoke(this, row);
        }

        /// <summary>
        /// Up and down, wherever they were pressed on the screen.
        ///
        /// Offered to the host rather than handled here for the same reason
        /// <see cref="UiTabs.HandleKey"/> is: the keyboard may be on a setting
        /// beside the list, and up and down should still walk the list --
        /// which is the thing the screen is for.
        /// </summary>
        public bool HandleKey(Key key)
        {
            if (_focusable.Count == 0)
            {
                return false;
            }
            int step = key == Key.Down ? 1 : key == Key.Up ? -1 : 0;
            if (step == 0)
            {
                return false;
            }
            int at = Selected == null ? -1 : _focusable.IndexOf(Selected);
            int next = at < 0 ? (step > 0 ? 0 : _focusable.Count - 1)
                : Math.Clamp(at + step, 0, _focusable.Count - 1);
            Focus(_focusable[next]);
            return true;
        }

        /// <summary>Put the keyboard on a row, and scroll it into view.</summary>
        public void Focus(Control row)
        {
            Select(row);
            row.Focus();
            row.BringIntoView();
        }

        /// <summary>The row the list opens on, once the tree exists to focus in.</summary>
        public void FocusFirst()
        {
            if (_focusable.Count == 0)
            {
                return;
            }
            Control row = Selected != null && _focusable.Contains(Selected)
                ? Selected : _focusable[0];
            Dispatcher.UIThread.Post(() => Focus(row), DispatcherPriority.Background);
        }

        /// <summary>Choose by what a row stands for, rather than by which control it is.</summary>
        public void SelectTag(object? choice)
        {
            if (choice == null)
            {
                return;
            }
            foreach (Control row in _focusable)
            {
                if (row is UiListRow line && Equals(line.Choice, choice))
                {
                    Select(row);
                    return;
                }
            }
        }
    }
}
