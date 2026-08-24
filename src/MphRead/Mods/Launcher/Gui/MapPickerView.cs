using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Threading;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// Every map at once, as pictures.
    ///
    /// The front screen steps through maps one at a time, which is the right
    /// gesture when the picture beside it changes as you step, and the wrong one
    /// when you know which map you want and it is twenty steps away. This is the
    /// second half of that: click the map name, see them all, pick one.
    ///
    /// A view rather than a window, for <see cref="SettingsView"/>'s reason: the
    /// desktop puts <see cref="MapPickerWindow"/> around it and Android shows it
    /// as an overlay, and neither has a copy of the grid.
    /// </summary>
    internal sealed class MapPickerView : UserControl
    {
        /// <summary>The map that was chosen, or null when it was dismissed.</summary>
        public string? RoomKey { get; private set; }

        /// <summary>Raised when this view is finished with, picked or not.</summary>
        public event EventHandler? Closed;

        private MapTile? _first;
        private MapTile? _selected;

        public MapPickerView(IReadOnlyList<string> rooms, string current)
        {
            Background = GuiTheme.InkBrush;
            Focusable = true;

            var grid = new WrapPanel { Orientation = Orientation.Horizontal };
            foreach (string room in rooms)
            {
                var tile = new MapTile(room) { Selected = room == current };
                _first ??= tile;
                if (tile.Selected)
                {
                    _selected = tile;
                }
                tile.Clicked += (sender, _) =>
                {
                    RoomKey = ((MapTile)sender!).RoomKey;
                    Closed?.Invoke(this, EventArgs.Empty);
                };
                grid.Children.Add(tile);
            }

            // A back entry as well as Escape and the title bar's close button:
            // shown as an overlay there is no title bar, and a phone's back
            // gesture is not something to rely on as the only way out.
            var back = new MenuEntry("Back", titleSize: 13)
            {
                Accent = GuiTheme.TextDim,
                Width = 120,
                HorizontalAlignment = HorizontalAlignment.Right,
                VerticalAlignment = VerticalAlignment.Center
            };
            back.Click += (_, _) => Closed?.Invoke(this, EventArgs.Empty);
            var title = new TextBlock
            {
                Text = "Choose a map",
                FontFamily = GuiTheme.Display,
                FontSize = 18,
                Foreground = GuiTheme.TextBrush,
                VerticalAlignment = VerticalAlignment.Center
            };
            var bar = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
            Grid.SetColumn(title, 0);
            Grid.SetColumn(back, 1);
            bar.Children.Add(title);
            bar.Children.Add(back);
            var header = new Border
            {
                Background = GuiTheme.PanelBrush,
                Padding = new Thickness(18, 12, 12, 12),
                Child = bar
            };
            var body = new ScrollViewer
            {
                Content = grid,
                Padding = new Thickness(14),
                HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
            };
            var dock = new DockPanel { LastChildFill = true };
            DockPanel.SetDock(header, Dock.Top);
            dock.Children.Add(header);
            dock.Children.Add(body);
            Content = dock;
        }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            // The map that is already chosen, so the grid can be walked from
            // where it is rather than from the top left.
            Dispatcher.UIThread.Post(() => (_selected ?? _first)?.Focus(),
                DispatcherPriority.Background);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                Closed?.Invoke(this, EventArgs.Empty);
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
        }
    }

    /// <summary>A frame around <see cref="MapPickerView"/>, and nothing else.</summary>
    internal sealed class MapPickerWindow : Window
    {
        public MapPickerWindow(MapPickerView view)
        {
            view.Closed += (_, _) => Close();
            Title = "Choose a map";
            Icon = GuiTheme.AppIcon.Value;
            Width = 1120;
            Height = 720;
            MinWidth = 560;
            MinHeight = 420;
            Background = GuiTheme.InkBrush;
            RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            Content = view;
        }
    }

    /// <summary>One clickable map preview.</summary>
    internal sealed class MapTile : Control
    {
        private readonly Bitmap? _image;
        private readonly string _caption;
        private bool _hover;

        public string RoomKey { get; }

        public bool Selected { get; init; }

        public event EventHandler? Clicked;

        private const double _captionHeight = 26;

        public MapTile(string roomKey, double width = 248, double height = 168)
        {
            RoomKey = roomKey;
            (RoomMetadata? meta, _) = Metadata.GetRoomByName(roomKey);
            _caption = meta?.InGameName ?? roomKey;
            _image = LoadPreview(roomKey);
            Width = width;
            Height = height;
            Margin = new Thickness(8);
            Focusable = true;
            Cursor = new Cursor(StandardCursorType.Hand);
        }

        private static Bitmap? LoadPreview(string roomKey)
        {
            string path = ThumbnailGenerator.PathFor(roomKey);
            try
            {
                if (!File.Exists(path))
                {
                    return null;
                }
                // Through a MemoryStream so the file is not held open: the
                // preview generator rewrites these while the launcher is up.
                using var stream = new MemoryStream(File.ReadAllBytes(path));
                return new Bitmap(stream);
            }
            catch (Exception)
            {
                // A truncated PNG from an interrupted batch should show a
                // placeholder, not take the window down.
                return null;
            }
        }

        protected override void OnPointerEntered(PointerEventArgs e)
        {
            _hover = true;
            InvalidateVisual();
            base.OnPointerEntered(e);
        }

        protected override void OnPointerExited(PointerEventArgs e)
        {
            _hover = false;
            InvalidateVisual();
            base.OnPointerExited(e);
        }

        protected override void OnPointerReleased(PointerReleasedEventArgs e)
        {
            // See MenuEntry.OnPointerReleased: a finger hovers nothing.
            Point p = e.GetPosition(this);
            if (p.X >= 0 && p.Y >= 0 && p.X <= Bounds.Width && p.Y <= Bounds.Height)
            {
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
            _hover = true;
            InvalidateVisual();
            base.OnGotFocus(e);
        }

        protected override void OnLostFocus(Avalonia.Interactivity.RoutedEventArgs e)
        {
            _hover = false;
            InvalidateVisual();
            base.OnLostFocus(e);
        }

        public override void Render(DrawingContext context)
        {
            var picture = new Rect(0, 0, Bounds.Width, Bounds.Height - _captionHeight);
            if (_image != null)
            {
                context.DrawImage(_image, new Rect(_image.Size), picture);
            }
            else
            {
                context.FillRectangle(GuiTheme.PanelBrush, picture);
                FormattedText none = TrackedText.Make("no preview", 12, bold: false,
                    GuiTheme.TextDimBrush);
                context.DrawText(none, new Point((picture.Width - none.Width) / 2,
                    (picture.Height - none.Height) / 2));
            }
            var caption = new Rect(0, Bounds.Height - _captionHeight,
                Bounds.Width, _captionHeight);
            context.FillRectangle(GuiTheme.PanelBrush, caption);
            var text = new FormattedText(_caption, CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, GuiTheme.Face(bold: true), 12,
                new SolidColorBrush(Selected || _hover ? GuiTheme.Accent : GuiTheme.Text))
            {
                MaxTextWidth = Bounds.Width - 8,
                MaxTextHeight = _captionHeight,
                Trimming = TextTrimming.CharacterEllipsis
            };
            context.DrawText(text, new Point((Bounds.Width - text.Width) / 2,
                caption.Y + (caption.Height - text.Height) / 2));
            if (Selected || _hover)
            {
                context.DrawRectangle(null,
                    new Pen(GuiTheme.AccentBrush, Selected ? 3 : 2),
                    new Rect(1, 1, Bounds.Width - 2, Bounds.Height - 2));
            }
        }
    }
}
