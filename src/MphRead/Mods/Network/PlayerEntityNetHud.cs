using MphRead.Mods.Network;
using MphRead.Hud;

namespace MphRead.Entities
{
    /// <summary>
    /// The scoreboard's ping column.
    ///
    /// A partial of PlayerEntity for the same reason the aim injection is:
    /// the HUD's text drawing is private, and reaching it from here costs
    /// upstream two call sites instead of opening up the whole HUD.
    ///
    /// The numbers come from the server, which is the only party that can
    /// measure them for everybody -- clients never exchange packets with each
    /// other -- and travel in the roster it already sends every second.
    /// </summary>
    public partial class PlayerEntity
    {
        /// <summary>
        /// Column centres in the HUD's 256-wide space. The stock two sit at
        /// 160 and 215, which leaves no room for a third: "deaths" is six
        /// characters at eight pixels each and ends at 239. In a networked
        /// match the two of them move left to make room, and offline nothing
        /// moves at all.
        /// </summary>
        internal float ModScoreColumn1 => NetSession.Active ? 145 : 160;

        internal float ModScoreColumn2 => NetSession.Active ? 193 : 215;

        private const float _pingColumnX = 236;

        internal void ModDrawPingHeader(float posY)
        {
            if (!NetSession.Active)
            {
                return;
            }
            DrawText2D(_pingColumnX, posY, Align.Center, 0, "ping",
                new ColorRgba(0x3FEF), fontSpacing: 8);
        }

        internal void ModDrawPingRow(float posY, ColorRgba rowColor, int slot)
        {
            if (!NetSession.Active || slot < 0 || slot >= NetSession.SlotPing.Length)
            {
                return;
            }
            int ping = NetSession.SlotPing[slot];
            // Zero means the server has not timed this peer yet -- a dash says
            // that, where "0" would read as a perfect connection.
            string text = ping <= 0 ? "--" : (ping > 999 ? "999" : ping.ToString());
            DrawText2D(_pingColumnX, posY, Align.Center, 0, text, PingColor(ping), fontSpacing: 8);
        }

        /// <summary>
        /// Green, amber, red -- the same reading every shooter's scoreboard
        /// has used since Quake, and the reason to show the number at all: a
        /// player who is warping is asking whether it is them or the server.
        /// </summary>
        private static ColorRgba PingColor(int ping)
        {
            if (ping <= 0)
            {
                return new ColorRgba(120, 120, 140, 255);
            }
            if (ping < 80)
            {
                return new ColorRgba(110, 231, 135, 255);
            }
            if (ping < 160)
            {
                return new ColorRgba(255, 200, 80, 255);
            }
            return new ColorRgba(255, 110, 110, 255);
        }
    }
}
