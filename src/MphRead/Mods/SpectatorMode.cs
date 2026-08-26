using System.Collections.Generic;
using MphRead.Entities;

namespace MphRead.Mods
{
    /// <summary>
    /// Watching another connected player in first person, as if playing as
    /// them -- reached from the pause menu, multiplayer only.
    ///
    /// The whole thing is one pointer swap: <see cref="PlayerEntity.Main"/>
    /// is already what the camera, the HUD and the weapon viewmodel all key
    /// off (see <c>Mods.Network.NetPlayerSetup</c>), so pointing
    /// <see cref="PlayerEntity.MainPlayerIndex"/> at somebody else's slot
    /// makes every one of those follow them for free. The one thing that
    /// pointer does not touch is whose slot real hardware input reaches --
    /// that is <c>Network.NetHooks.LocalSlot</c>, unchanged here -- so
    /// <see cref="PlayerInput.ProcessInput"/> checks <see cref="IsSpectating"/>
    /// itself to stop applying input to the real local player while this is
    /// active, rather than this class reaching in to silence it.
    /// </summary>
    public static class SpectatorMode
    {
        public static bool IsSpectating { get; private set; }

        /// <summary>Hidden in the adventure/single-player pause menu -- there is nobody else to watch.</summary>
        public static bool CanSpectate => GameState.Multiplayer;

        public static void Start()
        {
            if (IsSpectating || !CanSpectate)
            {
                return;
            }
            int next = FindNextActiveSlot(PlayerEntity.MainPlayerIndex);
            if (next == -1)
            {
                return;
            }
            IsSpectating = true;
            PlayerEntity.MainPlayerIndex = next;
        }

        /// <summary>Left click, while spectating: move on to the next connected player.</summary>
        public static void CycleNext()
        {
            if (!IsSpectating)
            {
                return;
            }
            int next = FindNextActiveSlot(PlayerEntity.MainPlayerIndex);
            if (next != -1)
            {
                PlayerEntity.MainPlayerIndex = next;
            }
        }

        /// <summary>
        /// Back into the match. The score resets because time spent
        /// spectating was time not playing -- picking the match back up with
        /// whatever score was left standing would credit or fault a period
        /// nothing was actually being played.
        /// </summary>
        public static void Rejoin()
        {
            if (!IsSpectating)
            {
                return;
            }
            int localSlot = Network.NetHooks.LocalSlot;
            PlayerEntity.MainPlayerIndex = localSlot;
            IsSpectating = false;
            if (localSlot >= 0 && localSlot < GameState.Points.Length)
            {
                GameState.Points[localSlot] = 0;
                GameState.Kills[localSlot] = 0;
                GameState.Deaths[localSlot] = 0;
            }
        }

        /// <summary>Forget spectating without the rejoin bookkeeping -- the match itself is ending.</summary>
        public static void Reset()
        {
            IsSpectating = false;
        }

        private static int FindNextActiveSlot(int fromSlot)
        {
            int localSlot = Network.NetHooks.LocalSlot;
            IReadOnlyList<PlayerEntity> players = PlayerEntity.Players;
            for (int offset = 1; offset <= players.Count; offset++)
            {
                int index = (fromSlot + offset) % players.Count;
                if (index == localSlot)
                {
                    continue;
                }
                if (players[index].LoadFlags.TestFlag(LoadFlags.Active))
                {
                    return index;
                }
            }
            return -1;
        }
    }
}
