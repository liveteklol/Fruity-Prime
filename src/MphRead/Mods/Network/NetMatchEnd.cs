using MphRead.Entities;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// The end of a networked match, and the move to the next map.
    ///
    /// Offline, a match that ends is a match that is over: GameState runs the
    /// winner's camera, then the scoreboard, then fades to black and quits to
    /// the launcher. On a server that is the wrong ending in two ways at
    /// once. The server has a rotation and never heard that anybody won, so
    /// it kept counting down the old map's clock; and every client dropped
    /// back to its own front screen, so a match that ended scattered the
    /// people playing it instead of taking them somewhere together.
    ///
    /// What happens instead: the authority tells the server the match is
    /// over, the server holds a short intermission while every client shows
    /// its results, and then it rotates. Clients follow the new room key the
    /// way they already follow a rotation that came from the clock -- through
    /// <see cref="NetRoomChange"/>, which fades to black and loads it. The
    /// fade a player sees at the end of a match is therefore the fade into
    /// the next one.
    ///
    /// The server is told rather than asked to work it out. It has no
    /// simulation and no scoreboard: only the authority knows a player
    /// reached the goal, and the shortest honest path is for it to say so.
    /// </summary>
    public static class NetMatchEnd
    {
        /// <summary>Frames between repeats of the report while it goes unacknowledged.</summary>
        private const uint ReportInterval = 15;

        private static uint _lastReport;
        private static bool _reported;
        /// <summary>
        /// The server has heard, and nothing more should be said until a new
        /// match is actually running.
        ///
        /// Without this the report outlived the match it was about. A client
        /// stays in its results sequence for a second or two after the server
        /// has rotated -- it has the new map to load first -- and during that
        /// window MatchState still says "not in progress" while the server's
        /// state has stopped saying "ending". So the authority reported the
        /// end of the *new* match the instant the rotation landed and the
        /// server dutifully ended it: a whole map skipped every time, visible
        /// in the server log as two "match over" lines in the same second.
        ///
        /// Cleared when the match state goes back to in progress, which is
        /// what loading the next room does.
        /// </summary>
        private static bool _acknowledged;

        public static void Reset()
        {
            _lastReport = 0;
            _reported = false;
            _acknowledged = false;
        }

        /// <summary>
        /// True while this client is showing the results of a networked match
        /// and waiting for the server to move everyone on. The match clock is
        /// not adopted during it -- see <see cref="NetMatchSync"/> -- because
        /// the results sequence runs on the same counter.
        /// </summary>
        public static bool InIntermission => NetSession.Active
            && (GameState.MatchState != MatchState.InProgress
                || NetSession.ServerMatch?.Ending == true);

        public static void Sync()
        {
            if (!NetSession.Active)
            {
                return;
            }
            bool serverEnding = NetSession.ServerMatch?.Ending == true;
            if (serverEnding && GameState.MatchState == MatchState.InProgress)
            {
                // The server ended the match for a reason this client has not
                // reached on its own -- the clock, or a score whose last kill
                // has not been replayed here yet. Zeroing the timer is how a
                // match ends everywhere in this engine, so the results play
                // out normally rather than being cut to.
                GameState.MatchTime = 0;
            }
            if (!NetSession.IsAuthority && !NetSession.IsHost)
            {
                return;
            }
            if (GameState.MatchState == MatchState.InProgress)
            {
                _reported = false;
                _acknowledged = false;
                return;
            }
            if (serverEnding)
            {
                _reported = true;
                _acknowledged = true;
                return;
            }
            if (_acknowledged)
            {
                return;
            }
            // Repeated until the server's own state comes back with the flag
            // set. One lost datagram would otherwise leave the whole session
            // sitting on a finished match until the old map's clock ran out.
            if (_reported && NetSession.NetFrame - _lastReport < ReportInterval)
            {
                return;
            }
            _reported = true;
            _lastReport = NetSession.NetFrame;
            NetSession.SendMatchEnd();
        }

        /// <summary>
        /// Whether the engine should quit to the launcher now that the
        /// results have finished.
        ///
        /// Offline, yes -- that is what the end of a match means. Connected,
        /// no: the server is about to say which map is next, and quitting
        /// would take this player out of a session that is still running.
        /// </summary>
        public static bool ShouldLeaveAfterMatch => !NetSession.Active;
    }
}
