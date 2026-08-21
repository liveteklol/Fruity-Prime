using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Reflection;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// The picture half of the front screen: one big image with the wordmark
    /// over it.
    ///
    /// The image is whatever is most relevant at that moment -- the map the
    /// server is running, the map about to be played, or, on the home screen,
    /// one of the cached map previews at random. Nothing is shipped with the
    /// build: the previews are rendered from the user's own extracted files
    /// (see ThumbnailGenerator), and a splash.png dropped beside the
    /// executable replaces the home picture for anyone who wants their own.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class SplashPanel : Control
    {
        /// <summary>
        /// The wordmark, decoded once from the csproj's embedded resource.
        /// Shown on the placeholder -- a fresh install, before there is a room
        /// or a splash.png to show instead -- which is the one screen every
        /// player sees regardless of what they end up doing.
        /// </summary>
        private static readonly Lazy<Image?> _wordmark = new(() =>
        {
            try
            {
                using Stream? stream = typeof(SplashPanel).Assembly.GetManifestResourceStream(
                    "FruityPrime.Assets.fruity-prime-logo.png");
                return stream != null ? Image.FromStream(stream) : null;
            }
            catch (Exception)
            {
                return null;
            }
        });

        private readonly LauncherTheme _theme;
        private Image? _brandImage;
        private Image? _image;
        private bool _ownsImage;
        private string _caption = "";
        private string _captionNote = "";

        public SplashPanel(LauncherTheme theme, IReadOnlyList<string> rooms)
        {
            _theme = theme;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            _brandImage = LoadBrandImage(rooms);
            _image = _brandImage;
        }

        /// <summary>
        /// Recompute the home picture from the current set of rooms.
        ///
        /// Needed after a fresh extraction: the panel was built before any
        /// thumbnail existed, so its home picture came up empty and nothing
        /// short of reopening the launcher used to fix that.
        /// </summary>
        public void RefreshBrandImage(IReadOnlyList<string> rooms)
        {
            Image? next = LoadBrandImage(rooms);
            if (_ownsImage)
            {
                // A map is on screen right now (e.g. mid-match card); only the
                // fallback picture underneath it changes, not what is visible.
                _brandImage = next;
                return;
            }
            _image?.Dispose();
            _brandImage = next;
            _image = next;
            Invalidate();
        }

        /// <summary>Show a map, or pass null to go back to the home picture.</summary>
        public void ShowRoom(string? roomKey, string note = "")
        {
            Image? next = _brandImage;
            bool owns = false;
            string caption = "";
            if (!String.IsNullOrEmpty(roomKey))
            {
                (RoomMetadata? meta, _) = Metadata.GetRoomByName(roomKey);
                caption = meta?.InGameName ?? roomKey;
                Image? loaded = LoadImage(ThumbnailGenerator.PathFor(roomKey));
                if (loaded != null)
                {
                    next = loaded;
                    owns = true;
                }
            }
            if (_ownsImage)
            {
                _image?.Dispose();
            }
            _image = next;
            _ownsImage = owns;
            _caption = caption;
            _captionNote = note;
            Invalidate();
        }

        private static Image? LoadBrandImage(IReadOnlyList<string> rooms)
        {
            foreach (string name in new[] { "splash.png", "splash.jpg", "splash.jpeg" })
            {
                Image? custom = LoadImage(Path.Combine(AppContext.BaseDirectory, name));
                if (custom != null)
                {
                    return custom;
                }
            }
            // No splash of their own: borrow a map preview. Rooms with a
            // cached preview only, so the home screen never opens on the
            // placeholder gradient when 32 good pictures exist.
            var available = new List<string>();
            var arenas = new List<string>();
            foreach (string room in rooms)
            {
                if (!ThumbnailGenerator.Exists(room))
                {
                    continue;
                }
                available.Add(room);
                // The MP rooms are the purpose-built arenas; the rest are
                // adventure rooms reused for multiplayer, and several of them
                // are a corridor pointed at a wall.
                if (room.StartsWith("MP", StringComparison.OrdinalIgnoreCase))
                {
                    arenas.Add(room);
                }
            }
            if (arenas.Count > 0)
            {
                available = arenas;
            }
            if (available.Count == 0)
            {
                return null;
            }
            string pick = available[Random.Shared.Next(available.Count)];
            return LoadImage(ThumbnailGenerator.PathFor(pick));
        }

        private static Image? LoadImage(string path)
        {
            if (!File.Exists(path))
            {
                return null;
            }
            try
            {
                // Read into memory first: Image.FromFile keeps the file open
                // for the lifetime of the image, and previews can be
                // regenerated while the launcher is on screen.
                byte[] bytes = File.ReadAllBytes(path);
                using var stream = new MemoryStream(bytes);
                return Image.FromStream(stream);
            }
            catch (Exception)
            {
                return null;
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            LauncherTheme.Smooth(g);
            var body = new Rectangle(0, 0, Width, Height);
            if (_image != null)
            {
                DrawCover(g, _image, body);
                // A flat wash plus a corner gradient: the wash keeps light
                // maps from washing out white text, the gradient does the
                // real work behind the wordmark.
                using (var wash = new SolidBrush(Color.FromArgb(70, 6, 8, 12)))
                {
                    g.FillRectangle(wash, body);
                }
                var shade = new Rectangle(0, Height / 2, Width, Height - Height / 2);
                using var gradient = new LinearGradientBrush(shade,
                    Color.FromArgb(0, 6, 8, 12), Color.FromArgb(232, 6, 8, 12), 90f);
                g.FillRectangle(gradient, shade);
            }
            else
            {
                DrawPlaceholder(g, body);
            }
            DrawWordmark(g, body);
        }

        private static void DrawCover(Graphics g, Image image, Rectangle body)
        {
            // Cover, not fit: bands of background either side of a 16:9
            // picture in a differently-shaped panel look like a bug.
            float scale = Math.Max((float)body.Width / image.Width,
                (float)body.Height / image.Height);
            float width = image.Width * scale;
            float height = image.Height * scale;
            g.DrawImage(image, (body.Width - width) / 2f, (body.Height - height) / 2f,
                width, height);
        }

        private void DrawPlaceholder(Graphics g, Rectangle body)
        {
            using (var gradient = new LinearGradientBrush(body,
                Color.FromArgb(16, 20, 30), Color.FromArgb(8, 10, 14), 55f))
            {
                g.FillRectangle(gradient, body);
            }
            using var pen = new Pen(Color.FromArgb(16, 41, 197, 255), _theme.S(1));
            int step = _theme.S(28);
            for (int x = -body.Height; x < body.Width; x += step)
            {
                g.DrawLine(pen, x, body.Bottom, x + body.Height, body.Top);
            }

            Image? wordmark = _wordmark.Value;
            if (wordmark != null)
            {
                // Fit within a band of the panel, never upscaled past its own
                // resolution: a wordmark blown up past its source pixels looks
                // soft in a way a stray dropped frame does not.
                float maxWidth = Math.Min(body.Width * 0.72f, wordmark.Width);
                float scale = maxWidth / wordmark.Width;
                float w = wordmark.Width * scale;
                float h = wordmark.Height * scale;
                float x = body.X + (body.Width - w) / 2f;
                float y = body.Y + body.Height * 0.36f - h / 2f;
                g.DrawImage(wordmark, x, y, w, h);
            }

        }

        private void DrawWordmark(Graphics g, Rectangle body)
        {
            int left = _theme.S(36);
            int bottom = body.Bottom - _theme.S(30);

            Font version = _theme.Body(_theme.S(11));
            using (var dim = new SolidBrush(Color.FromArgb(150, 220, 226, 236)))
            {
                g.DrawString($"v{Program.Version.ToString(3)}", version, dim, left,
                    bottom - version.GetHeight(g));
            }
            bottom -= (int)version.GetHeight(g) + _theme.S(12);

            if (_caption.Length > 0)
            {
                Font caption = _theme.Display(_theme.S(24));
                float height = caption.GetHeight(g);
                using (var brush = new SolidBrush(LauncherTheme.Text))
                {
                    g.DrawString(_caption, caption, brush, left - _theme.S(2),
                        bottom - height);
                }
                bottom -= (int)height + _theme.S(2);
                if (_captionNote.Length > 0)
                {
                    Font note = _theme.Body(_theme.S(11), FontStyle.Bold);
                    float noteHeight = note.GetHeight(g);
                    LauncherTheme.DrawTracked(g, _captionNote.ToUpperInvariant(), note,
                        LauncherTheme.Accent, left, bottom - noteHeight, _theme.S(2));
                }
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                if (_ownsImage)
                {
                    _image?.Dispose();
                }
                _brandImage?.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
