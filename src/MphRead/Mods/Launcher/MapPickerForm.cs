using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.Versioning;
using System.Windows.Forms;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// Every map at once, as pictures.
    ///
    /// The front screen steps through maps one at a time, which is the right
    /// gesture when the picture beside it changes as you step, and the wrong
    /// one when you know which map you want and it is twenty steps away. This
    /// is the second half of that: click the map name, see them all, pick one.
    /// </summary>
    [SupportedOSPlatform("windows")]
    public sealed class MapPickerForm : Form
    {
        private readonly LauncherTheme _theme;

        public string? RoomKey { get; private set; }

        public MapPickerForm(LauncherTheme theme, IReadOnlyList<string> rooms, string current)
        {
            _theme = theme;
            Text = "Choose a map";
            FormBorderStyle = FormBorderStyle.None;
            AutoScaleMode = AutoScaleMode.None;
            StartPosition = FormStartPosition.CenterParent;
            BackColor = LauncherTheme.Ink;
            ForeColor = LauncherTheme.Text;
            KeyPreview = true;
            Rectangle work = Screen.PrimaryScreen?.WorkingArea ?? new Rectangle(0, 0, 1280, 800);
            ClientSize = new Size(
                Math.Min(theme.S(1120), work.Width - theme.S(80)),
                Math.Min(theme.S(720), work.Height - theme.S(80)));

            var grid = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                AutoScroll = true,
                BackColor = LauncherTheme.Ink,
                // Clears the header, which floats above the grid so that
                // scrolled tiles pass under it rather than pushing it away.
                Padding = new Padding(theme.S(14), theme.S(62), theme.S(14), theme.S(14)),
                FlowDirection = FlowDirection.LeftToRight,
                WrapContents = true
            };
            foreach (string room in rooms)
            {
                var tile = new MapTile(room, LauncherTheme.Accent, LauncherTheme.Panel,
                    LauncherTheme.Text, DeviceDpi)
                {
                    Selected = room == current
                };
                tile.Clicked += (sender, _) =>
                {
                    RoomKey = ((MapTile)sender!).RoomKey;
                    DialogResult = DialogResult.OK;
                    Close();
                };
                grid.Controls.Add(tile);
            }

            var header = new Panel
            {
                Dock = DockStyle.Top,
                Height = theme.S(48),
                BackColor = LauncherTheme.Panel
            };
            var title = new Label
            {
                Text = "Choose a map",
                AutoSize = true,
                ForeColor = LauncherTheme.Text,
                BackColor = LauncherTheme.Panel,
                Font = theme.Display(theme.S(18)),
                Location = new Point(theme.S(18), theme.S(14))
            };
            header.Controls.Add(title);
            var close = new GlyphButton(theme, GlyphButton.Glyph.Close)
            {
                Location = new Point(ClientSize.Width - theme.S(40), theme.S(11))
            };
            close.Click += (_, _) => Close();
            header.Controls.Add(close);
            header.MouseDown += (_, e) =>
            {
                if (e.Button == MouseButtons.Left)
                {
                    WindowChrome.DragFrom(this);
                }
            };

            // The fill goes in first so the header, docked after it, takes its
            // strip off the top rather than the grid covering it.
            Controls.Add(grid);
            Controls.Add(header);
            header.BringToFront();
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            WindowChrome.RoundCorners(Handle);
        }

        protected override bool ProcessCmdKey(ref System.Windows.Forms.Message message, Keys keyData)
        {
            if (keyData == Keys.Escape)
            {
                DialogResult = DialogResult.Cancel;
                Close();
                return true;
            }
            return base.ProcessCmdKey(ref message, keyData);
        }
    }
}
