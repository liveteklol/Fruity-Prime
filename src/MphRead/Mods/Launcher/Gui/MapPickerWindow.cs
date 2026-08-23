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
    /// </summary>
    internal sealed class MapPickerWindow : Window
    {
        /// <summary>The map that was chosen, or null when the window was closed.</summary>
        public string? RoomKey { get; private set; }

        private MapTile? _first;
        private MapTile? _selected;

        public MapPickerWindow(IReadOnlyList<string> rooms, string current)
        {
            Title = "Choose a map";
            Icon = GuiTheme.AppIcon.Value;
            Width = 1120;
            Height = 720;
            MinWidth = 560;
            MinHeight = 420;
            Background = GuiTheme.InkBrush;
            RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;

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
                    Close();
                };
                grid.Children.Add(tile);
            }

            var header = new Border
            {
                Background = GuiTheme.PanelBrush,
                Padding = new Thickness(18, 12, 12, 12),
                Child = new TextBlock
                {
                    Text = "Choose a map",
                    FontFamily = GuiTheme.Display,
                    FontSize = 18,
                    Foreground = GuiTheme.TextBrush
                }
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

        protected override void OnOpened(EventArgs e)
        {
            base.OnOpened(e);
            // The map that is already chosen, so the grid can be walked from
            // where it is rather than from the top left.
            Dispatcher.UIThread.Post(() => (_selected ?? _first)?.Focus(),
                DispatcherPriority.Background);
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                Close();
                e.Handled = true;
                return;
            }
            base.OnKeyDown(e);
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
            if (IsPointerOver)
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
