using System;
using MphRead.Entities;
using MphRead.Mods.Network;

namespace MphRead.Mods.Launcher
{
    /// <summary>
    /// Turning a <see cref="LaunchPlan"/> into a running match.
    ///
    /// This used to sit inside <c>LauncherEntry</c>, which is WinForms and
    /// therefore Windows. Nothing in it is: it opens a <see cref="RenderWindow"/>,
    /// fills the player slots and loads a room, all of which the Linux build
    /// has always been able to do. Having it here is what lets
    /// <see cref="TextLauncher"/> start exactly the match the window would have
    /// started -- the same bot count, the same cap on the slots, the same
    /// deference to what the server says it is running -- instead of a second
    /// implementation that agrees with it until it does not.
    /// </summary>
    public static class MatchStart
    {
        /// <summary>
        /// Load what the plan asked for and run until the match ends.
        ///
        /// For an online or hosted game the front screen has already joined --
        /// hosting included, because a host runs the server in this process and
        /// joins it over the loopback like everybody else -- so all that is
        /// left is to load what the server says is running.
        /// </summary>
        public static void Launch(MenuSettings settings, LaunchPlan plan)
        {
            if (!GameFiles.Ready)
            {
                Console.WriteLine("[launcher] no game files; nothing to load");
                return;
            }
            GameFiles.ApplyPaths();
            if (plan.Kind == LaunchKind.Online || plan.Kind == LaunchKind.Host)
            {
                (string RoomKey, GameMode Mode)? room = NetLaunch.ServerRoom();
                if (room != null)
                {
                    settings.RoomKey = room.Value.RoomKey;
                }
                else
                {
                    Console.WriteLine("[net] no map reported by the server; "
                        + "loading the selected map instead");
                }
            }

            string roomKey = plan.Kind == LaunchKind.Offline
                ? plan.RoomKey
                : settings.RoomKey;
            if (roomKey.Length == 0 || roomKey == "none")
            {
                return;
            }

            using var renderer = new RenderWindow();
            bool teamPlay = settings.TeamPlay == "on"
                || plan.Mode.ToString().EndsWith("Teams", StringComparison.Ordinal);
            GameMode mode = plan.Mode;

            if (NetSession.Active)
            {
                NetLaunch.BuildPlayers(renderer.Scene, plan.Hunter, localRecolor: 0,
                    teamId: teamPlay ? 0 : -1);
                // The server's rotation decides the mode as well as the map; a
                // client that kept its own menu choice would score a different
                // game from everyone else on the same level.
                if (NetLaunch.ServerRoom() is var room && room != null)
                {
                    mode = room.Value.Mode;
                }
            }
            else
            {
                AddLocalPlayers(renderer, plan, teamPlay);
            }
            renderer.AddRoom(roomKey, mode, playerCount: NetSession.Active
                ? NetLaunch.RoomPlayerCount
                : 0);
            renderer.Run();
        }

        /// <summary>
        /// One human and however many bots were asked for.
        ///
        /// Bots are ordinary players whose Controls are written by PlayerAi,
        /// which is what Scene.AddPlayer already arranges for every player
        /// after the first -- so the only work here is choosing who they are.
        /// A different hunter each keeps a practice match from being a room
        /// full of one's own reflection, and alternating teams is what makes
        /// the Teams modes mean anything offline.
        /// </summary>
        private static void AddLocalPlayers(RenderWindow renderer, LaunchPlan plan,
            bool teamPlay)
        {
            int bots = Math.Clamp(plan.Bots, 0, PlayerEntity.SlotCapacity - 1);
            // PlayerEntity.Create refuses every slot past MaxPlayers, and the
            // offline default is still the four a DS match could hold, so
            // asking for seven opponents would silently produce three. Set
            // rather than raise: the launcher comes back between matches now,
            // and a seven-bot match must not leave the next one at eight.
            PlayerEntity.MaxPlayers = Math.Max(4, bots + 1);
            renderer.AddPlayer(plan.Hunter, recolor: 0, team: teamPlay ? 0 : -1);
            for (int i = 1; i <= bots; i++)
            {
                var hunter = (Hunter)(((int)plan.Hunter + i) % 7);
                renderer.AddPlayer(hunter, recolor: 0, team: teamPlay ? i % 2 : -1);
            }
            int level = Math.Clamp(plan.BotLevel, 0, 2);
            for (int i = 0; i < PlayerEntity.Players.Count; i++)
            {
                PlayerEntity player = PlayerEntity.Players[i];
                if (player.IsBot)
                {
                    player.BotLevel = level;
                }
            }
        }
    }
}
