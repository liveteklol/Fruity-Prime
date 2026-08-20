using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;

namespace MphRead.Mods.Network
{
    /// <summary>
    /// One entry in the server's map cycle.
    /// </summary>
    public sealed class RotationEntry
    {
        public string RoomKey { get; init; } = "MP3 PROVING GROUND";
        public GameMode Mode { get; init; } = GameMode.Battle;
        /// <summary>Match length in seconds. Zero means "no time limit".</summary>
        public float TimeLimit { get; init; } = 7 * 60;
        public int PointGoal { get; init; } = 7;

        public override string ToString()
        {
            return $"{RoomKey} ({Mode}, {TimeLimit / 60:0.#} min, {PointGoal} pts)";
        }
    }

    /// <summary>
    /// Server-side map cycle, in the shape Quake 3 admins expect: a plain
    /// text file listing maps in order, the server advancing to the next one
    /// when the time limit or point goal is reached, and wrapping at the end.
    ///
    /// Deliberately a file rather than compiled-in defaults -- a dedicated
    /// server is usually reconfigured by someone with shell access and no
    /// build toolchain.
    /// </summary>
    public sealed class MapRotation
    {
        private readonly List<RotationEntry> _entries = new();
        private int _index;

        public IReadOnlyList<RotationEntry> Entries => _entries;
        public RotationEntry Current => _entries.Count > 0 ? _entries[_index] : _fallback;
        public RotationEntry Next => _entries.Count > 0
            ? _entries[(_index + 1) % _entries.Count]
            : _fallback;
        public int Index => _index;

        private static readonly RotationEntry _fallback = new();

        /// <summary>
        /// A rotation of exactly one match, for a player hosting from the
        /// launcher: they picked a map, and a rotation file they have never
        /// heard of should not send them somewhere else after seven minutes.
        /// </summary>
        public static MapRotation SingleMatch(string roomKey, GameMode mode, float timeLimit, int pointGoal)
        {
            var rotation = new MapRotation();
            rotation._entries.Add(new RotationEntry
            {
                RoomKey = roomKey,
                Mode = mode == GameMode.None ? GameMode.Battle : mode,
                TimeLimit = timeLimit,
                PointGoal = pointGoal
            });
            return rotation;
        }

        /// <summary>Advance to the next map, wrapping at the end of the cycle.</summary>
        public RotationEntry Advance()
        {
            if (_entries.Count > 0)
            {
                _index = (_index + 1) % _entries.Count;
            }
            return Current;
        }

        /// <summary>
        /// Load a rotation file. Format, one match per line:
        ///
        ///   ROOM KEY | mode | minutes | points
        ///
        /// Only the room key is required. '#' starts a comment.
        /// </summary>
        public static MapRotation Load(string path)
        {
            var rotation = new MapRotation();
            foreach (string raw in File.ReadAllLines(path))
            {
                string line = raw;
                int comment = line.IndexOf('#');
                if (comment >= 0)
                {
                    line = line[..comment];
                }
                line = line.Trim();
                if (line.Length == 0)
                {
                    continue;
                }
                string[] parts = line.Split('|');
                string roomKey = parts[0].Trim();
                if (roomKey.Length == 0)
                {
                    continue;
                }
                GameMode mode = GameMode.Battle;
                if (parts.Length > 1 && Enum.TryParse(parts[1].Trim().Replace(" ", ""),
                    ignoreCase: true, out GameMode parsedMode))
                {
                    mode = parsedMode;
                }
                float timeLimit = 7 * 60;
                if (parts.Length > 2 && Single.TryParse(parts[2].Trim(),
                    NumberStyles.Float, CultureInfo.InvariantCulture, out float minutes))
                {
                    timeLimit = minutes * 60;
                }
                int pointGoal = 7;
                if (parts.Length > 3 && Int32.TryParse(parts[3].Trim(), out int parsedPoints))
                {
                    pointGoal = parsedPoints;
                }
                rotation._entries.Add(new RotationEntry
                {
                    RoomKey = roomKey,
                    Mode = mode,
                    TimeLimit = timeLimit,
                    PointGoal = pointGoal
                });
            }
            return rotation;
        }

        /// <summary>A starter rotation, written when no file exists yet.</summary>
        public static void WriteDefault(string path)
        {
            File.WriteAllLines(path, new[]
            {
                "# MphRead dedicated server map rotation.",
                "# One match per line:  ROOM KEY | mode | minutes | points",
                "# Mode and the numbers are optional; '#' starts a comment.",
                "# Room keys are the names MphRead uses internally -- run the",
                "# game's console menu to see the full list.",
                "",
                "MP1 SANCTORUS      | Battle | 7 | 7",
                "MP3 PROVING GROUND | Battle | 7 | 7",
                "MP4 HIGHGROUND     | Battle | 7 | 7",
                "MP2 HARVESTER      | Battle | 7 | 7",
                "MP6 HEADSHOT       | Battle | 7 | 7"
            });
        }

        public static MapRotation LoadOrCreate(string path)
        {
            if (!File.Exists(path))
            {
                WriteDefault(path);
                Console.WriteLine($"[server] wrote a starter rotation to {path}");
            }
            MapRotation rotation = Load(path);
            if (rotation._entries.Count == 0)
            {
                Console.WriteLine($"[server] {path} has no usable entries; using a single default map");
                rotation._entries.Add(_fallback);
            }
            return rotation;
        }
    }
}
