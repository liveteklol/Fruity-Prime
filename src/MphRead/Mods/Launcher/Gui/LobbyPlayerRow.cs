using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher.Gui
{
    internal sealed class LobbyPlayerRow : Border
    {
        public LobbyPlayerRow(RosterPacket roster, int index, byte owner)
        {
            int slot = roster.Slots[index];
            string team = roster.Teams[index] < 0 ? "FFA" : $"Team {roster.Teams[index] + 1}";
            var lines = new StackPanel();
            lines.Children.Add(new TextBlock
            {
                Text = $"{(roster.LobbyReady[index] ? "READY" : "WAITING")}  {roster.Names[index]}"
                    + (slot == owner ? "  [OWNER]" : ""),
                FontFamily = GuiTheme.Display, FontSize = 14,
                Foreground = roster.LobbyReady[index] ? GuiTheme.WarmBrush : GuiTheme.TextBrush,
                TextTrimming = TextTrimming.CharacterEllipsis
            });
            lines.Children.Add(new Note($"{(Hunter)roster.Hunters[index]} · Suit {roster.Colors[index] + 1} · {team} · {roster.Pings[index]} ms"));
            Padding = new Thickness(5); Child = lines;
        }
    }
}
