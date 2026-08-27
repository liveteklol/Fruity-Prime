using System;
using System.Diagnostics;
using System.Threading;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// Watching a recorded match. A demo file is fed into
    /// <see cref="NetSession"/> exactly like a live connection would be --
    /// see <see cref="NetSession.StartPlayback"/> and
    /// <see cref="NetTransport.EnqueueForPlayback"/> -- so every packet-type
    /// handler, room transition and match-end sequence runs unchanged; this
    /// class only decides *when* each recorded packet gets handed over.
    ///
    /// There is no real local player during playback, so the viewer starts
    /// and stays in <see cref="SpectatorMode"/>; Space additionally toggles
    /// a free no-clip camera on top of that, for looking around rather than
    /// only following whoever is spectated.
    /// </summary>
    public static class DemoPlayback
    {
        private static DemoReader? _reader;
        private static DemoRecord? _pending;
        private static Stopwatch? _clock;

        public static bool IsActive { get; private set; }

        /// <summary>True once the file has no more records -- the scene holds on the last state rather than closing itself.</summary>
        public static bool AtEnd => IsActive && _pending == null;

        /// <summary>
        /// Why the last <see cref="Join"/> failed, for a screen that is
        /// still open to show it on -- Console.WriteLine is where this used
        /// to only go, which is invisible on the Windows build outside a
        /// typed command.
        /// </summary>
        public static string? LastError { get; private set; }

        /// <summary>
        /// Open the file and wait for the first match info, the same shape
        /// as <see cref="NetLaunch.Join"/> -- blocking, called off the UI
        /// thread, true once <c>NetSession.ServerMatch</c> knows what room
        /// to load.
        /// </summary>
        public static bool Join(string path, int timeoutMs = 8000)
        {
            LastError = null;
            _reader = DemoReader.Open(path);
            if (_reader == null)
            {
                LastError = "That file isn't a demo this build recognises "
                    + "(wrong extension, damaged, or from a different build).";
                Console.WriteLine($"[demo] \"{path}\": {LastError}");
                return false;
            }
            if (_reader.ProtocolVersion != NetConfig.ProtocolVersion)
            {
                Console.WriteLine($"[demo] recorded with protocol {_reader.ProtocolVersion}, "
                    + $"this build is {NetConfig.ProtocolVersion} -- it may not play back correctly");
            }
            NetSession.StartPlayback();
            IsActive = true;
            _clock = Stopwatch.StartNew();
            _pending = _reader.ReadNext();
            bool hadRecords = _pending != null;
            // Not returned the instant the room key is known: BuildPlayers
            // (called right after this) reads NetSession.SlotHunter and
            // SlotOccupied to decide every player's hunter and whether their
            // slot is even active, and those come from Roster/Snapshot
            // packets that do not necessarily land in the same burst as the
            // MatchState that answers ServerMatch first. Returning too early
            // showed real players with the wrong hunter, or briefly not
            // active at all -- see the demo playback bug notes.
            const long gracePeriodMs = 1000;
            long knownAt = -1;
            while (_clock.ElapsedMilliseconds < timeoutMs)
            {
                PumpFrame();
                NetSession.Update(_clock.Elapsed.TotalSeconds);
                if (NetSession.ServerMatch?.RoomKey.Length > 0)
                {
                    if (knownAt < 0)
                    {
                        knownAt = _clock.ElapsedMilliseconds;
                    }
                    else if (_clock.ElapsedMilliseconds - knownAt >= gracePeriodMs || AtEnd)
                    {
                        return true;
                    }
                }
                Thread.Sleep(20);
            }
            LastError = !hadRecords
                ? "That demo file is empty -- nothing was ever recorded to it."
                : "That demo has no match info in its first few seconds -- "
                    + "the recording may have started before the server said what map it was running.";
            Console.WriteLine($"[demo] \"{path}\": {LastError}");
            Stop();
            return false;
        }

        /// <summary>Called once a frame: hands over every recorded packet whose time has come.</summary>
        public static void PumpFrame()
        {
            if (!IsActive || _reader == null || _clock == null)
            {
                return;
            }
            long elapsed = _clock.ElapsedMilliseconds;
            while (_pending is DemoRecord record && record.ElapsedMs <= elapsed)
            {
                NetSession.InjectPlaybackPacket(record.Data, record.Data.Length);
                _pending = _reader.ReadNext();
            }
        }

        public static void Stop()
        {
            IsActive = false;
            _reader?.Dispose();
            _reader = null;
            _pending = null;
            _clock = null;
        }
    }
}
