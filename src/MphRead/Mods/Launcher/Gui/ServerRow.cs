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
        /// Column left edges as a fraction of the row, except the first, which
        /// is where the name starts. Shared with the header so the two cannot
        /// drift apart.
        /// </summary>
        internal const double NameX = 8;
        internal const double MapFrac = 0.42;
        internal const double ModeFrac = 0.68;
        internal const double PlayersFrac = 0.84;
        internal const double PingFrac = 1.0;

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
            double mapX = Bounds.Width * MapFrac;
            double modeX = Bounds.Width * ModeFrac;
            double playersX = Bounds.Width * PlayersFrac;
            double pingX = Bounds.Width * PingFrac - 8;

            // The name, and the address under nothing -- there is no room for a
            // second line here, and the address is what the row does when
            // clicked rather than something to compare servers by. It goes in
            // the tooltip instead.
            IBrush nameBrush = _answered ? GuiTheme.TextBrush : GuiTheme.TextDimBrush;
            Draw(context, _name, NameX, mapX - NameX - 8, nameBrush, bold: true, rightAlign: false);
            Draw(context, _map, mapX, modeX - mapX - 8, GuiTheme.TextDimBrush,
                bold: false, rightAlign: false);
            Draw(context, _mode, modeX, playersX - modeX - 8, GuiTheme.TextDimBrush,
                bold: false, rightAlign: false);
            Draw(context, _players, playersX, pingX - playersX - 8, GuiTheme.TextBrush,
                bold: false, rightAlign: true);
            Draw(context, _ping, pingX, 44, _pingBrush, bold: false, rightAlign: true);
        }

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
                Trimming = TextTrimming.CharacterEllipsis
            };
            double left = rightAlign ? x - Math.Min(formatted.Width, width) : x;
            context.DrawText(formatted, new Point(left, 8));
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
            double mapX = Bounds.Width * ServerRow.MapFrac;
            double modeX = Bounds.Width * ServerRow.ModeFrac;
            double playersX = Bounds.Width * ServerRow.PlayersFrac;
            double pingX = Bounds.Width * ServerRow.PingFrac - 8;
            ServerRow.Draw(context, "SERVER", ServerRow.NameX, mapX - ServerRow.NameX - 8,
                GuiTheme.TextDimBrush, bold: true, rightAlign: false, size: 11);
            ServerRow.Draw(context, "MAP", mapX, modeX - mapX - 8,
                GuiTheme.TextDimBrush, bold: true, rightAlign: false, size: 11);
            ServerRow.Draw(context, "TYPE", modeX, playersX - modeX - 8,
                GuiTheme.TextDimBrush, bold: true, rightAlign: false, size: 11);
            ServerRow.Draw(context, "PLAYERS", playersX, pingX - playersX - 8,
                GuiTheme.TextDimBrush, bold: true, rightAlign: true, size: 11);
            ServerRow.Draw(context, "PING", pingX, 44,
                GuiTheme.TextDimBrush, bold: true, rightAlign: true, size: 11);
            // A hairline under the headings, so the list reads as a table.
            context.FillRectangle(GuiTheme.EdgeBrush,
                new Rect(0, Bounds.Height - 1, Bounds.Width, 1));
        }
    }
}
