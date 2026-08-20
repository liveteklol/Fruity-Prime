using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// One clickable map preview.
    ///
    /// Shared by the settings window's map grid and the front screen's map
    /// browser: the two used to be the same control copied twice, and the
    /// copies drifted -- one grew the missing-preview placeholder and the
    /// other kept hiding maps that had none.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class MapTile : Panel
    {
        private readonly Color _accent;
        private readonly Color _panelColor;
        private readonly Color _textColor;
        private readonly Image? _image;
        private readonly string _caption;
        private readonly int _captionHeight;
        private bool _hover;

        public string RoomKey { get; }

        [System.ComponentModel.DesignerSerializationVisibility(
            System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public bool Selected { get; set; }

        public event EventHandler? Clicked;

        public MapTile(string roomKey, Color accent, Color panel, Color text, int dpi,
            int width = 248, int height = 168)
        {
            RoomKey = roomKey;
            _accent = accent;
            _panelColor = panel;
            _textColor = text;
            (RoomMetadata? meta, _) = Metadata.GetRoomByName(roomKey);
            _caption = meta?.InGameName ?? roomKey;
            _image = LoadPreview(roomKey);

            int scale(int v) => (int)Math.Round(v * dpi / 96.0);
            _captionHeight = scale(26);
            Size = new Size(scale(width), scale(height));
            Margin = new Padding(scale(8));
            Cursor = Cursors.Hand;
            DoubleBuffered = true;
            Click += (_, _) => Clicked?.Invoke(this, EventArgs.Empty);
            MouseEnter += (_, _) => { _hover = true; Invalidate(); };
            MouseLeave += (_, _) => { _hover = false; Invalidate(); };
        }

        private static Image? LoadPreview(string roomKey)
        {
            string path = ThumbnailGenerator.PathFor(roomKey);
            if (!File.Exists(path))
            {
                return null;
            }
            try
            {
                // Copy through a MemoryStream so the file is not locked for
                // the lifetime of the form: a user may regenerate thumbnails
                // while the launcher is open.
                byte[] bytes = File.ReadAllBytes(path);
                using var stream = new MemoryStream(bytes);
                return Image.FromStream(stream);
            }
            catch (Exception)
            {
                // A truncated PNG from an interrupted batch should show a
                // placeholder, not take the launcher down.
                return null;
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            var imageRect = new Rectangle(0, 0, Width, Height - _captionHeight);
            if (_image != null)
            {
                g.DrawImage(_image, imageRect);
            }
            else
            {
                using var brush = new SolidBrush(_panelColor);
                g.FillRectangle(brush, imageRect);
                TextRenderer.DrawText(g, "no preview", Font, imageRect,
                    Color.Gray, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            }
            var captionRect = new Rectangle(0, Height - _captionHeight, Width, _captionHeight);
            using (var brush = new SolidBrush(_panelColor))
            {
                g.FillRectangle(brush, captionRect);
            }
            TextRenderer.DrawText(g, _caption, Font, captionRect,
                Selected || _hover ? _accent : _textColor,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter
                    | TextFormatFlags.EndEllipsis);
            if (Selected || _hover)
            {
                using var pen = new Pen(_accent, Selected ? 3 : 2);
                g.DrawRectangle(pen, 1, 1, Width - 3, Height - 3);
            }
            base.OnPaint(e);
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _image?.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
