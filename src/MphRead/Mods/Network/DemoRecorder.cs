using System;
using System.IO;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// Records every packet this client receives to a demo file, from
    /// whenever the player asks (the pause menu's "Record demo", online
    /// matches only) to whenever they ask again or the match ends.
    ///
    /// Fed from <see cref="NetSession.Update"/>'s own drain loop -- it sees
    /// exactly what the session sees, in the same order, so a replay through
    /// <see cref="DemoPlayback"/> reproduces exactly what this client saw.
    /// </summary>
    internal static class DemoRecorder
    {
        private static DemoWriter? _writer;
        private static long _startTimeMs;

        public static bool IsRecording => _writer != null;

        /// <summary>Where the file being written now lives, for a "saved to..." message.</summary>
        public static string? CurrentPath { get; private set; }

        public static bool Start()
        {
            if (IsRecording || !NetSession.Active || DemoPlayback.IsActive)
            {
                return false;
            }
            string room = SanitizeFileName(NetSession.ServerMatch?.RoomKey ?? "match");
            string fileName = $"{room}_{DateTime.Now:yyyy-MM-dd_HH-mm-ss}{DemoFile.Extension}";
            string path = Paths.Combine(Paths.Export, "_demos", fileName);
            try
            {
                _writer = new DemoWriter(path);
            }
            catch (IOException ex)
            {
                Console.WriteLine($"[demo] could not start recording: {ex.Message}");
                _writer = null;
                return false;
            }
            CurrentPath = path;
            _startTimeMs = Environment.TickCount64;
            return true;
        }

        public static void Stop()
        {
            _writer?.Dispose();
            _writer = null;
            CurrentPath = null;
        }

        internal static void Record(ReceivedPacket packet)
        {
            if (_writer == null)
            {
                return;
            }
            _writer.WriteRecord(ElapsedMs(), packet.Data.AsSpan(0, packet.Length));
        }

        /// <summary>
        /// Synthesizes a SlotIntent record for this client's own outgoing
        /// Intent, exactly the shape <see cref="NetSession"/> would have
        /// received one in from the server for anybody else's input
        /// (<c>[PacketType.SlotIntent][slot][IntentPacket bytes]</c>) -- see
        /// the call site in <see cref="NetSession.SendIntent"/> for why this
        /// is the only way the recording player's own shooting/morphing/
        /// alt-attack animations end up in the file at all.
        /// </summary>
        internal static void RecordOwnIntent(int slot, ReadOnlySpan<byte> intentBytes)
        {
            if (_writer == null)
            {
                return;
            }
            Span<byte> buffer = stackalloc byte[2 + intentBytes.Length];
            buffer[0] = (byte)PacketType.SlotIntent;
            buffer[1] = (byte)slot;
            intentBytes.CopyTo(buffer[2..]);
            _writer.WriteRecord(ElapsedMs(), buffer);
        }

        private static uint ElapsedMs() => (uint)Math.Max(0, Environment.TickCount64 - _startTimeMs);

        private static string SanitizeFileName(string name)
        {
            foreach (char c in Path.GetInvalidFileNameChars())
            {
                name = name.Replace(c, '_');
            }
            return name;
        }
    }
}
