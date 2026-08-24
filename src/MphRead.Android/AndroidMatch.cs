using System;
using MphRead.Entities;
using MphRead.Mods.Launcher;
using OpenTK.Mathematics;

namespace MphRead.Droid
{
    /// <summary>
    /// A <see cref="LaunchPlan"/> turned into a loaded scene, on Android.
    ///
    /// The desktop's <see cref="MatchStart"/> does this and more -- it opens a
    /// <c>RenderWindow</c> and runs it to completion, which is a window and a
    /// loop this platform does not have. What is left when those two are taken
    /// away is what this file is: the same order, the same slot arithmetic and
    /// the same deference to the DS player cap, producing a
    /// <see cref="Scene"/> for <see cref="GameView"/> to drive.
    ///
    /// Kept beside the head rather than in Mods/Launcher for that reason:
    /// there is no second implementation of the *decisions* here, only of the
    /// three lines that own a window.
    /// </summary>
    internal static class AndroidMatch
    {
        /// <summary>Runs on the GL thread: everything below it touches GL.</summary>
        public static Scene Build(AndroidInput input, Vector2i size, LaunchPlan plan, Action close)
        {
            GameFiles.ApplyPaths();
            // No slot means nothing can be written, which is what a match
            // needs -- the same reason MatchStart gives.
            Menu.SaveSlot = 0;
            var scene = new Scene(size, input.Keyboard, input.Mouse, _ => { }, close);
            bool teamPlay = GameState.IsTeamMode(plan.Mode);
            AddLocalPlayers(scene, plan, teamPlay);
            scene.AddRoom(plan.RoomKey, plan.Mode);
            return scene;
        }

        private static void AddLocalPlayers(Scene scene, LaunchPlan plan, bool teamPlay)
        {
            int bots = Math.Clamp(plan.Bots, 0, PlayerEntity.SlotCapacity - 1);
            // Set rather than raise, for MatchStart's reason: the launcher comes
            // back between matches, and a seven-bot match must not leave the
            // next one at eight.
            PlayerEntity.MaxPlayers = Math.Max(4, bots + 1);
            scene.AddPlayer(plan.Hunter, recolor: 0, team: teamPlay ? 0 : -1);
            for (int i = 1; i <= bots; i++)
            {
                var hunter = (Hunter)(((int)plan.Hunter + i) % 7);
                scene.AddPlayer(hunter, recolor: 0, team: teamPlay ? i % 2 : -1);
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
