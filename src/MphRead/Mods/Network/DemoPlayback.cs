using System;

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
    /// "When" is a frame number, not a moment. <see cref="PumpFrame"/> is
    /// called once per simulated frame and releases exactly the packets the
    /// recorder saw on the matching frame of its own run, so the replay has
    /// the same packets-per-frame the recording did -- however fast this
    /// machine is drawing, and however long the room took to load in the
    /// middle. See <see cref="DemoFile"/> for the three ways the stopwatch
    /// this replaces got that wrong.
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
        /// <summary>The frame of the recording about to be replayed.</summary>
        private static uint _frame;
        private static bool _started;

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
        /// How far into the recording <see cref="Join"/> will look for the
        /// match info before giving up. Twenty seconds of recorded frames:
        /// the server repeats its match state once a second, so a file that
        /// has not said what room it is by then does not contain one.
        /// </summary>
        private const uint JoinSearchFrames = 60 * 20;

        /// <summary>
        /// Frames to keep pumping after the room key is known.
        ///
        /// BuildPlayers, called right after this, reads NetSession.SlotHunter
        /// and SlotOccupied to decide every player's hunter and whether their
        /// slot is even active, and those come from Roster packets that do not
        /// necessarily land in the same burst as the MatchState that answers
        /// ServerMatch first. Returning on the room key alone showed real
        /// players with the wrong hunter, or briefly not active at all. The
        /// roster repeats once a second, so two of those.
        /// </summary>
        private const uint JoinGraceFrames = 120;

        /// <summary>
        /// Open the file and wind it forward to the first match info, the
        /// same shape as <see cref="NetLaunch.Join"/> -- true once
        /// <c>NetSession.ServerMatch</c> knows what room to load.
        ///
        /// Blocking, and called off the UI thread for that reason, but no
        /// longer *waiting*: a live join waits on a server, and this reads a
        /// file, so it costs a few hundred frames of parsing rather than the
        /// eight seconds the wall-clock version could spend.
        /// </summary>
        public static bool Join(string path, int timeoutMs = 8000)
        {
            _ = timeoutMs; // kept for the call site; nothing here waits on a clock
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
            _frame = 0;
            _started = false;
            _pending = _reader.ReadNext();
            bool hadRecords = _pending != null;
            long knownAt = -1;
            while (_frame < JoinSearchFrames)
            {
                PumpFrame();
                NetSession.Update(_frame / 60.0);
                if (NetSession.ServerMatch?.RoomKey.Length > 0)
                {
                    if (knownAt < 0)
                    {
                        knownAt = _frame;
                    }
                    else if (_frame - knownAt >= JoinGraceFrames || AtEnd)
                    {
                        return true;
                    }
                }
                else if (AtEnd)
                {
                    break;
                }
            }
            LastError = !hadRecords
                ? "That demo file is empty -- nothing was ever recorded to it."
                : "That demo has no match info in its first few seconds -- "
                    + "the recording may have started before the server said what map it was running.";
            Console.WriteLine($"[demo] \"{path}\": {LastError}");
            Stop();
            return false;
        }

        /// <summary>
        /// Called once a frame: hands over every packet the recorder saw on
        /// this frame of its own run.
        /// </summary>
        public static void PumpFrame()
        {
            if (!IsActive || _reader == null)
            {
                return;
            }
            // The first pumped frame is frame 0 of the recording; every one
            // after it is the next. Advancing before the release instead
            // would skip whatever the recorder caught on its own first frame.
            if (_started)
            {
                _frame++;
            }
            _started = true;
            while (_pending is DemoRecord record && record.Frame <= _frame)
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
            _frame = 0;
            _started = false;
        }
    }
}
