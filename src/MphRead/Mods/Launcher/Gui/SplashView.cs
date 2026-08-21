using System;
using System.Globalization;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Imaging;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// The picture on the left of the front screen: a preview of the map about
    /// to be played, or of the one a server says it is running.
    ///
    /// The pictures are rendered from the player's own files by
    /// <c>ThumbnailGenerator</c> and cached beside the executable, so nothing
    /// here ships any art. A <c>splash.png</c> next to the binary replaces the
    /// home picture. With neither -- a fresh install, which is exactly when
    /// this screen matters most -- it draws its own title card rather than a
    /// hole.
    /// </summary>
    internal sealed class SplashView : Control
    {
        private Bitmap? _image;
        private string _caption = "";
        private string _note = "";

        public SplashView()
        {
            _image = LoadCustom();
        }

        /// <summary>Show a map's preview, or the title card when there is none.</summary>
        public void ShowRoom(string? roomKey, string note = "")
        {
            _caption = roomKey ?? "";
            _note = note;
            Bitmap? next = null;
            if (roomKey != null && roomKey.Length > 0)
            {
                next = Load(ThumbnailGenerator.PathFor(roomKey));
            }
            next ??= LoadCustom();
            if (!ReferenceEquals(next, _image))
            {
                _image?.Dispose();
                _image = next;
            }
            InvalidateVisual();
        }

        private static Bitmap? LoadCustom()
        {
            foreach (string name in new[] { "splash.png", "splash.jpg", "splash.jpeg" })
            {
                Bitmap? custom = Load(Path.Combine(AppContext.BaseDirectory, name));
                if (custom != null)
                {
                    return custom;
                }
            }
            return null;
        }

        private static Bitmap? Load(string path)
        {
            try
            {
                if (!File.Exists(path))
                {
                    return null;
                }
                // Read into memory first: a Bitmap built straight from the path
                // keeps the file open, and the preview generator rewrites these
                // while the launcher is up.
                using var stream = new MemoryStream(File.ReadAllBytes(path));
                return new Bitmap(stream);
            }
            catch (Exception)
            {
                // A corrupt or half-written preview is not worth a crash on the
                // first screen of the program.
                return null;
            }
        }

        public override void Render(DrawingContext context)
        {
            var body = new Rect(0, 0, Bounds.Width, Bounds.Height);
            context.FillRectangle(GuiTheme.InkBrush, body);
            if (_image != null)
            {
                DrawCover(context, _image, body);
            }
            else
            {
                DrawTitleCard(context, body);
            }
            // A wash along the bottom so the caption stays readable over a
            // picture whose lower edge happens to be bright.
            context.FillRectangle(new LinearGradientBrush
            {
                StartPoint = new RelativePoint(0, 1, RelativeUnit.Relative),
                EndPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
                GradientStops =
                {
                    new GradientStop(Color.FromArgb(220, 10, 12, 16), 0),
                    new GradientStop(Color.FromArgb(0, 10, 12, 16), 1)
                }
            }, new Rect(0, body.Height - 90, body.Width, 90));

            if (_caption.Length > 0)
            {
                var caption = new FormattedText(_caption, CultureInfo.InvariantCulture,
                    FlowDirection.LeftToRight, GuiTheme.Face(true), 20, GuiTheme.TextBrush);
                context.DrawText(caption, new Point(24, body.Height - 58));
            }
            if (_note.Length > 0)
            {
                var note = new FormattedText(_note, CultureInfo.InvariantCulture,
                    FlowDirection.LeftToRight, GuiTheme.Face(false), 13, GuiTheme.TextDimBrush);
                context.DrawText(note, new Point(24, body.Height - 32));
            }
        }

        /// <summary>Fill the panel, cropping the overflow, rather than letterboxing.</summary>
        private static void DrawCover(DrawingContext context, Bitmap image, Rect body)
        {
            double scale = Math.Max(body.Width / image.Size.Width,
                body.Height / image.Size.Height);
            double width = image.Size.Width * scale;
            double height = image.Size.Height * scale;
            context.DrawImage(image,
                new Rect(0, 0, image.Size.Width, image.Size.Height),
                new Rect((body.Width - width) / 2, (body.Height - height) / 2, width, height));
        }

        /// <summary>
        /// What a fresh install sees. Drawn rather than shipped as an image,
        /// because a logo in the repository is one more file the asset guard
        /// has to be told about and one more thing to keep in two places.
        /// </summary>
        private static void DrawTitleCard(DrawingContext context, Rect body)
        {
            context.FillRectangle(new LinearGradientBrush
            {
                StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
                EndPoint = new RelativePoint(1, 1, RelativeUnit.Relative),
                GradientStops =
                {
                    new GradientStop(Color.FromRgb(16, 22, 34), 0),
                    new GradientStop(Color.FromRgb(9, 11, 15), 1)
                }
            }, body);

            // A faint grid, so the panel reads as deliberate rather than empty.
            var pen = new Pen(new SolidColorBrush(Color.FromArgb(18, 41, 197, 255)), 1);
            for (double x = 0; x < body.Width; x += 32)
            {
                context.DrawLine(pen, new Point(x, 0), new Point(x, body.Height));
            }
            for (double y = 0; y < body.Height; y += 32)
            {
                context.DrawLine(pen, new Point(0, y), new Point(body.Width, y));
            }

            double cx = body.Width / 2;
            double cy = body.Height / 2 - 20;
            context.DrawEllipse(null, new Pen(GuiTheme.AccentBrush, 2),
                new Point(cx, cy), 46, 46);
            context.DrawEllipse(GuiTheme.AccentBrush, null, new Point(cx, cy), 14, 14);

            var title = new FormattedText("MPHREAD", CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, GuiTheme.Face(true), 30, GuiTheme.TextBrush);
            context.DrawText(title, new Point(cx - title.Width / 2, cy + 62));
            var sub = new FormattedText("Metroid Prime Hunters, with multiplayer",
                CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
                GuiTheme.Face(false), 13, GuiTheme.TextDimBrush);
            context.DrawText(sub, new Point(cx - sub.Width / 2, cy + 100));
        }
    }
}
