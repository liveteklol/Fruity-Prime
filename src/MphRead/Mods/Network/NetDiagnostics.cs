using System;
using System.Text;
using MphRead.Entities;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// Periodic report of what the running game believes about a networked
    /// session.
    ///
    /// This exists because the interesting failure is invisible from the
    /// wire: two clients can be perfectly connected -- correct slots, agreed
    /// clock -- while each one's scene contains only its own player, so
    /// nobody sees anybody and the scoreboard lists one name. Only the game
    /// process can see that, so it reports it here rather than a test trying
    /// to reconstruct the engine's state from outside.
    ///
    /// Printed to the console once a second while NDS_NET_DEBUG is set, or
    /// whenever -netdebug is passed.
    /// </summary>
    public static class NetDiagnostics
    {
        private static double _lastReport;
        private static bool _enabled;
        private static bool _checked;

        public static bool Enabled
        {
            get
            {
                if (!_checked)
                {
                    _checked = true;
                    _enabled = Environment.GetEnvironmentVariable("MPHREAD_NET_DEBUG") != null;
                }
                return _enabled;
            }
            set
            {
                _checked = true;
                _enabled = value;
            }
        }

        public static void Report(double time)
        {
            if (!Enabled || !NetSession.Active)
            {
                return;
            }
            if (time - _lastReport < 1.0)
            {
                return;
            }
            _lastReport = time;

            var line = new StringBuilder();
            line.Append("[netdbg] role=").Append(NetSession.Role);
            line.Append(" slot=").Append(NetSession.LocalSlot);

            int active = 0;
            int created = 0;
            line.Append(" slots=[");
            for (int i = 0; i < PlayerEntity.MaxPlayers; i++)
            {
                PlayerEntity? player = PlayerEntity.Players[i];
                if (player == null)
                {
                    line.Append('-');
                    continue;
                }
                created++;
                bool isActive = player.LoadFlags.TestFlag(LoadFlags.Active);
                if (isActive)
                {
                    active++;
                }
                // A = active player, B = active but AI-driven (wrong on a
                // remote slot -- PlayerAi would overwrite network intent),
                // s = slot-active only, . = present but inactive.
                line.Append(isActive ? (player.IsBot ? 'B' : 'A')
                    : player.LoadFlags.TestFlag(LoadFlags.SlotActive) ? 's' : '.');
            }
            line.Append("] active=").Append(active);
            line.Append(" scoreboard=").Append(GameState.ActivePlayers);
            line.Append(" created=").Append(created);

            line.Append(" remoteState=[");
            for (int i = 0; i < NetSession.RemoteStateValid.Length; i++)
            {
                line.Append(NetSession.RemoteStateValid[i] ? 'y' : 'n');
            }
            line.Append("] remoteIntent=[");
            for (int i = 0; i < NetSession.RemoteIntentValid.Length; i++)
            {
                line.Append(NetSession.RemoteIntentValid[i] ? 'y' : 'n');
            }
            line.Append(']');

            // Surface the failure directly rather than leaving it to be
            // spotted in the slot map: an AI-driven remote slot is always a
            // bug, and it is the one that produced bots in a networked match.
            int botRemotes = 0;
            for (int i = 0; i < PlayerEntity.MaxPlayers; i++)
            {
                PlayerEntity? p = PlayerEntity.Players[i];
                if (p != null && i != NetSession.LocalSlot && p.IsBot
                    && p.LoadFlags.TestFlag(LoadFlags.Active))
                {
                    botRemotes++;
                }
            }
            if (botRemotes > 0)
            {
                line.Append("  !! ").Append(botRemotes).Append(" remote slot(s) still AI-driven");
            }

            MatchStatePacket? match = NetSession.ServerMatch;
            if (match != null)
            {
                line.Append(" serverMap=").Append(match.Value.RoomKey);
                line.Append(" serverPlayers=").Append(match.Value.PlayerCount);
            }
            Console.WriteLine(line.ToString());
        }
    }
}
