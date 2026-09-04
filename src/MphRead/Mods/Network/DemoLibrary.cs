using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;

namespace MphRead.Mods.Network
{
    /// <summary>One recording found on disk, as a screen needs to show it.</summary>
    internal readonly struct DemoRecording
    {
        public string Path { get; }
        /// <summary>The room, when the file name still carries it. Otherwise empty.</summary>
        public string Room { get; }
        public DateTime Recorded { get; }
        public long Bytes { get; }

        public string FileName => System.IO.Path.GetFileName(Path);

        public DemoRecording(string path, string room, DateTime recorded, long bytes)
        {
            Path = path;
            Room = room;
            Recorded = recorded;
            Bytes = bytes;
        }
    }

    /// <summary>
    /// The recordings this machine made, listed from the folder they are
    /// written to.
    ///
    /// It exists because the system file picker is the wrong tool for them on
    /// the platform most likely to be recording. <see cref="DemoRecorder"/>
    /// writes into the app's own directory, and since Android 11
    /// <c>Android/data</c> is excluded from the Storage Access Framework: the
    /// picker cannot be pointed at it and a player cannot navigate to it, even
    /// though the app itself reads and writes there with no permission at all.
    /// So the app lists its own folder and the picker is kept for the other
    /// case -- importing a demo somebody sent you, which really is somewhere
    /// else.
    /// </summary>
    internal static class DemoLibrary
    {
        /// <summary>
        /// Where <see cref="DemoRecorder"/> writes, as an absolute path.
        ///
        /// Absolute matters: <c>Paths.Export</c> is empty in every paths.txt a
        /// desktop extraction produces, which makes the recorder's own combine
        /// a *relative* path resolved against the working directory. That is
        /// fine for writing and useless for handing to anything else.
        /// </summary>
        public static string Directory =>
            Path.GetFullPath(Paths.Combine(Paths.Export, "_demos"));

        /// <summary>
        /// Every recording in that folder, newest first.
        ///
        /// Nothing is opened. The file name carries the room and the moment
        /// already (see <see cref="DemoRecorder.Start"/>), and reading a
        /// header out of every file on a phone to learn what the name says is
        /// a directory listing turned into a disk full of seeks.
        /// </summary>
        public static IReadOnlyList<DemoRecording> List()
        {
            var found = new List<DemoRecording>();
            try
            {
                string directory = Directory;
                if (!System.IO.Directory.Exists(directory))
                {
                    return found;
                }
                foreach (string path in System.IO.Directory
                    .EnumerateFiles(directory, "*" + DemoFile.Extension))
                {
                    var info = new FileInfo(path);
                    (string room, DateTime? stamp) = ReadName(info.Name);
                    found.Add(new DemoRecording(path, room,
                        stamp ?? info.LastWriteTime, info.Length));
                }
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                // A folder that cannot be listed is an empty list, not a
                // screen that refuses to open.
                Console.WriteLine($"[demo] could not list {Directory}: {ex.Message}");
            }
            found.Sort((a, b) => b.Recorded.CompareTo(a.Recorded));
            return found;
        }

        /// <summary>
        /// Take the room and the moment back out of "ROOM_2026-09-04_18-22-07".
        ///
        /// The stamp is a fixed nineteen characters at the end, which is what
        /// makes this safe: a room name may contain underscores of its own
        /// (SanitizeFileName puts one in for every character a file name
        /// cannot hold), so splitting on the separator would cut the wrong one.
        /// </summary>
        private static (string Room, DateTime? Stamp) ReadName(string fileName)
        {
            string name = Path.GetFileNameWithoutExtension(fileName);
            const int stampLength = 19; // yyyy-MM-dd_HH-mm-ss
            if (name.Length < stampLength + 2 || name[^(stampLength + 1)] != '_')
            {
                return (name, null);
            }
            string stamp = name[^stampLength..];
            if (!DateTime.TryParseExact(stamp, "yyyy-MM-dd_HH-mm-ss",
                CultureInfo.InvariantCulture, DateTimeStyles.None, out DateTime parsed))
            {
                return (name, null);
            }
            return (name[..^(stampLength + 1)], parsed);
        }

        /// <summary>"4 Sep 2026, 18:22 — 1.4 MB".</summary>
        public static string Describe(DemoRecording demo)
        {
            return $"{demo.Recorded:d MMM yyyy, HH:mm} — {Size(demo.Bytes)}";
        }

        private static string Size(long bytes)
        {
            if (bytes >= 1024 * 1024)
            {
                return (bytes / (1024f * 1024f)).ToString("0.0", CultureInfo.InvariantCulture) + " MB";
            }
            if (bytes >= 1024)
            {
                return (bytes / 1024) + " KB";
            }
            return bytes + " bytes";
        }
    }
}
