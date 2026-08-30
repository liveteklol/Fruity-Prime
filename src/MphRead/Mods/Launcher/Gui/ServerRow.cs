using System;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher.Gui
{
    /// <summary>
    /// One server in the browser, drawn as columns rather than as a line of
    /// prose.
    ///
    /// The list used to be <see cref="MenuEntry"/>s whose subtitle was a
    /// sentence -- "1.2.3.4:27888 -- MP3 PROVING GROUND (Battle) 3/8 players,
    /// 41 ms". Everything was there and none of it was comparable: the map
    /// started at a different x on every row, so picking the emptiest server,
    /// or the nearest one, meant reading each line rather than scanning a
    /// column. Quake 3's browser is the reference the report gave and its
    /// whole trick is that the columns line up and the ping is coloured.
    ///
    /// So: fixed column positions shared with <see cref="ServerHeader"/>, the
    /// name trimmed rather than allowed to push anything, players and ping
    /// right-aligned under their headings, and the ping coloured by how good
    /// it is. Drawn rather than laid out in a Grid for the reason every other
    /// row here is: one control, one Render, no per-row visual tree.
    /// </summary>
    internal sealed class ServerRow : Control
    {
        /// <summary>
        /// Where each column starts and how wide it is, worked out from the
        /// row's width and shared with <see cref="ServerHeader"/> so the two
        /// cannot drift apart.
        ///
        /// Measured from the right, not as fractions of the whole. Fractions
        /// were what put "MP3 PROVING GROUND" into a 89-pixel map column in
        /// the launcher's 400-pixel panel, where it wrapped onto a second line
        /// of a 30-pixel row and drew over the server under it, and squeezed
        /// the PLAYERS heading into 43 pixels of a column it needs 51 for, so
        /// it ran into PING. The three columns on the right hold numbers and a
        /// mode name: what they need is a known number of pixels, not a share
        /// of however wide the window happens to be. Everything left over goes
        /// to the two that hold prose, which is where a long name should cost
        /// something.
        /// </summary>
        internal readonly struct Columns
        {
            private const double Margin = 8;
            private const double Gutter = 10;
            /// <summary>Fits "999" and the PING heading.</summary>
            private const double MaxPing = 34;
            /// <summary>Fits "8/8" and the PLAYERS heading, which is the widest of the five.</summary>
            private const double MaxPlayers = 52;
            /// <summary>Fits "Battle"; "Prime Hunter" trims, which is the right one to trim.</summary>
            private const double MaxMode = 66;
            /// <summary>The name's share of what the fixed columns leave.</summary>
            private const double NameShare = 0.44;

            public readonly double NameX, NameWidth;
            public readonly double MapX, MapWidth;
            public readonly double ModeX, ModeWidth;
            /// <summary>Right edge: the players and ping columns are right-aligned.</summary>
            public readonly double PlayersRight, PlayersWidth;
            public readonly double PingRight, PingWidth;

            public Columns(double width)
            {
                PingRight = width - Margin;
                PingWidth = MaxPing;
                PlayersRight = PingRight - MaxPing - Gutter;
                PlayersWidth = MaxPlayers;
                // The mode column gives ground first on a narrow row: a
                // trimmed mode is still readable where a trimmed server name
                // is not the server anybody was looking for.
                ModeWidth = Math.Min(MaxMode, Math.Max(0, (width - 200) * 0.4));
                ModeX = PlayersRight - MaxPlayers - Gutter - ModeWidth;
                NameX = Margin;
                double rest = Math.Max(0, ModeX - Gutter - Margin);
                NameWidth = rest * NameShare;
                MapX = NameX + NameWidth + Gutter;
                MapWidth = Math.Max(0, rest - NameWidth - Gutter);
            }
        }

        public event EventHandler? Clicked;

        private readonly string _name;
        private readonly string _endpoint;
        private string _map = "";
        private string _mode = "";
        private string _players = "";
        private string _ping = "";
        private IBrush _pingBrush = GuiTheme.TextDimBrush;
        private bool _answered;
        private bool _hot;

        public ServerRow(string name, string endpoint)
        {
            _name = name;
            _endpoint = endpoint;
            _map = "asking...";
            Height = 30;
            Focusable = true;
            Cursor = new Cursor(StandardCursorType.Hand);
        }

        /// <summary>Fill the columns in once the server has answered.</summary>
        public void SetStatus(ServerStatus status)
        {
            _answered = status.Online;
            if (!status.Online)
            {
                _map = "did not answer";
                _mode = "";
                _players = "";
                _ping = "--";
                _pingBrush = GuiTheme.BadBrush;
                InvalidateVisual();
                return;
            }
            _map = status.RoomKey;
            _mode = NetStatus.ModeName(status.Mode);
            _players = status.MaxPlayers > 0
                ? $"{status.Players}/{status.MaxPlayers}"
                : status.Players.ToString(CultureInfo.InvariantCulture);
            if (status.Latency >= 0)
            {
                _ping = status.Latency.ToString(CultureInfo.InvariantCulture);
                // The same three bands Quake 3 uses, and for the same reason:
                // the number matters far less than which of "fine", "playable"
                // and "don't" it falls in, and a colour answers that without
                // being read.
                _pingBrush = status.Latency < 80 ? GuiTheme.GoodBrush
                    : status.Latency < 160 ? GuiTheme.WarmBrush
                    : GuiTheme.BadBrush;
            }
            else
            {
                _ping = "--";
                _pingBrush = GuiTheme.TextDimBrush;
            }
            InvalidateVisual();
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

        protected override void OnPointerPressed(PointerPressedEventArgs e)
        {
            Focus();
            Clicked?.Invoke(this, EventArgs.Empty);
            base.OnPointerPressed(e);
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

        public override void Render(DrawingContext context)
        {
            var full = new Rect(0, 0, Bounds.Width, Bounds.Height);
            // Transparent fill first: an unfilled area is not hit-testable, so
            // the whole row has to be painted for the whole row to be
            // clickable. Same as MenuEntry.
            context.FillRectangle(Brushes.Transparent, full);
            if (_hot || IsFocused)
            {
                context.FillRectangle(GuiTheme.PanelLightBrush, full, 4);
            }
            var columns = new Columns(Bounds.Width);
            // The name, and the address under nothing -- there is no room for a
            // second line here, and the address is what the row does when
            // clicked rather than something to compare servers by. It goes in
            // the tooltip instead.
            IBrush nameBrush = _answered ? GuiTheme.TextBrush : GuiTheme.TextDimBrush;
            Draw(context, _name, columns.NameX, columns.NameWidth, nameBrush,
                bold: true, rightAlign: false);
            Draw(context, _map, columns.MapX, columns.MapWidth, GuiTheme.TextDimBrush,
                bold: false, rightAlign: false);
            Draw(context, _mode, columns.ModeX, columns.ModeWidth, GuiTheme.TextDimBrush,
                bold: false, rightAlign: false);
            Draw(context, _players, columns.PlayersRight, columns.PlayersWidth,
                GuiTheme.TextBrush, bold: false, rightAlign: true);
            Draw(context, _ping, columns.PingRight, columns.PingWidth, _pingBrush,
                bold: false, rightAlign: true);
        }

        /// <summary>
        /// One cell: a single line, trimmed to the column, and clipped to it
        /// whatever the trimming decides.
        ///
        /// Both halves are needed. Without a height limit Avalonia wraps at
        /// the first space rather than ellipsizing, and a wrapped cell in a
        /// 30-pixel row draws its second line over the row beneath -- which is
        /// what a two-word map name did. And trimming cannot help a single
        /// unbreakable word that is wider than its column, which is what
        /// PLAYERS is: there is no break to take, so it simply overflows into
        /// the next heading. The clip is what makes that impossible rather
        /// than unlikely.
        /// </summary>
        internal static void Draw(DrawingContext context, string text, double x, double width,
            IBrush brush, bool bold, bool rightAlign, double size = 13)
        {
            if (text.Length == 0 || width <= 4)
            {
                return;
            }
            var formatted = new FormattedText(text, CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, GuiTheme.Face(bold), size, brush)
            {
                MaxTextWidth = width,
                // One line: enough for the tallest this face gets at this
                // size, and far short of two.
                MaxTextHeight = size * 1.6,
                Trimming = TextTrimming.CharacterEllipsis
            };
            double left = rightAlign ? x - Math.Min(formatted.Width, width) : x;
            using (context.PushClip(new Rect(rightAlign ? x - width : x, 0, width, size * 1.6 + 8)))
            {
                context.DrawText(formatted, new Point(left, 8));
            }
        }

        public string Endpoint => _endpoint;
    }

    /// <summary>The column headings over a <see cref="ServerRow"/> list.</summary>
    internal sealed class ServerHeader : Control
    {
        public ServerHeader()
        {
            Height = 22;
            IsHitTestVisible = false;
        }

        public override void Render(DrawingContext context)
        {
            var columns = new ServerRow.Columns(Bounds.Width);
            ServerRow.Draw(context, "SERVER", columns.NameX, columns.NameWidth,
                GuiTheme.TextDimBrush, bold: true, rightAlign: false, size: 11);
            ServerRow.Draw(context, "MAP", columns.MapX, columns.MapWidth,
                GuiTheme.TextDimBrush, bold: true, rightAlign: false, size: 11);
            ServerRow.Draw(context, "TYPE", columns.ModeX, columns.ModeWidth,
                GuiTheme.TextDimBrush, bold: true, rightAlign: false, size: 11);
            ServerRow.Draw(context, "PLAYERS", columns.PlayersRight, columns.PlayersWidth,
                GuiTheme.TextDimBrush, bold: true, rightAlign: true, size: 11);
            ServerRow.Draw(context, "PING", columns.PingRight, columns.PingWidth,
                GuiTheme.TextDimBrush, bold: true, rightAlign: true, size: 11);
            // A hairline under the headings, so the list reads as a table.
            context.FillRectangle(GuiTheme.EdgeBrush,
                new Rect(0, Bounds.Height - 1, Bounds.Width, 1));
        }
    }
}
