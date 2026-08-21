using System;
using System.Globalization;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform;

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
        /// <summary>
        /// The wordmark, loaded once from what the csproj embeds as an
        /// Avalonia resource. Static and lazy so that opening the launcher
        /// twice in one process -- the loop in <c>GuiLauncher</c> -- decodes
        /// it once, and a missing asset (it cannot happen in a build this
        /// project makes, but nothing stops a stripped-down one) falls back to
        /// no picture rather than a crash on the first screen.
        /// </summary>
        private static readonly Lazy<Bitmap?> _brand = new(() =>
        {
            try
            {
                using Stream stream = AssetLoader.Open(
                    new Uri("avares://FruityPrime/Assets/fruity-prime-logo.png"));
                return new Bitmap(stream);
            }
            catch (Exception)
            {
                return null;
            }
        });

        private Bitmap? _image;
        private string _caption = "";
        private string _note = "";
        private double _bottomInset;

        /// <summary>
        /// How much of the bottom of the picture is spoken for by something
        /// drawn over it. The caption sits above it rather than underneath.
        /// </summary>
        public double BottomInset
        {
            get => _bottomInset;
            set
            {
                if (Math.Abs(_bottomInset - value) > 0.5)
                {
                    _bottomInset = value;
                    InvalidateVisual();
                }
            }
        }

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
            }, new Rect(0, body.Height - 90 - _bottomInset, body.Width, 90 + _bottomInset));

            if (_caption.Length > 0)
            {
                var caption = new FormattedText(_caption, CultureInfo.InvariantCulture,
                    FlowDirection.LeftToRight, GuiTheme.Face(true), 20, GuiTheme.TextBrush);
                context.DrawText(caption, new Point(24, body.Height - 58 - _bottomInset));
            }
            if (_note.Length > 0)
            {
                var note = new FormattedText(_note, CultureInfo.InvariantCulture,
                    FlowDirection.LeftToRight, GuiTheme.Face(false), 13, GuiTheme.TextDimBrush);
                context.DrawText(note, new Point(24, body.Height - 32 - _bottomInset));
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
        /// What a fresh install sees, before there is a map to show: the
        /// picture the game-files card is answered by, which is the whole
        /// reason the wordmark has to appear here rather than only in the
        /// corner of a screen with a room already loaded.
        /// </summary>
        private static void DrawTitleCard(DrawingContext context, Rect body)
        {
            // Plain black: Render already filled the panel with GuiTheme.Ink
            // before calling here, and the logo's own background was cut
            // transparent to exactly that colour story, so nothing further is
            // painted underneath it. A gradient or a grid behind a piece of
            // real artwork reads as a placeholder competing with the thing it
            // is a placeholder for.
            double cx = body.Width / 2;
            double cy = body.Height / 2 - 20;
            Bitmap? brand = _brand.Value;
            if (brand != null)
            {
                // Fit within a band of the panel's width, never upscaled past
                // its own resolution -- a wordmark blown up past its source
                // pixels looks soft in a way a stray dropped frame does not.
                double maxWidth = Math.Min(body.Width * 0.72, brand.Size.Width);
                double scale = maxWidth / brand.Size.Width;
                double w = brand.Size.Width * scale;
                double h = brand.Size.Height * scale;
                var dest = new Rect(cx - w / 2, cy - h / 2, w, h);
                context.DrawImage(brand, new Rect(brand.Size), dest);
                return;
            }
            // Only reachable if the embedded asset failed to decode -- the
            // build always carries it. A word is still a screen.
            var title = new FormattedText(Mods.Branding.Name.ToUpperInvariant(),
                CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, GuiTheme.Face(true), 30, GuiTheme.TextBrush);
            context.DrawText(title, new Point(cx - title.Width / 2, cy));
        }
    }
}
