using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;

namespace MphRead.Mods
{
    /// <summary>
    /// Runs thumbnail captures several at a time.
    ///
    /// Parallel across processes, not threads: GLFW requires its windows to
    /// be created and pumped on the main thread, so several GL windows in
    /// one process is not possible. Each worker is a fresh instance of this
    /// executable invoked with a share of the rooms, which also isolates a
    /// crash on one room from the rest of the batch.
    /// </summary>
    public static class ThumbnailBatch
    {
        /// <summary>
        /// How many workers to run at once.
        ///
        /// The cores, rather than the flat ten this used to be. Ten was the
        /// right shape when a worker was one room and spent a fifth of its
        /// life asleep between 60 Hz frames -- oversubscribing hid the
        /// waiting. A worker is now a share of rooms and never waits, so
        /// anything past the cores only adds context switches, and each of
        /// them is also a GL context and a room's textures: the machines that
        /// refuse those allocations (see Run) are exactly the ones with few
        /// cores. Measured on an 8-core box rendering 28 rooms: 5.2 s at ten,
        /// 4.1 s at eight.
        /// </summary>
        public static int DefaultParallelism => Math.Clamp(Environment.ProcessorCount, 2, 10);

        /// <summary>
        /// Whether previews can be rendered at all here. Every worker is a
        /// fresh instance of this executable, and Android has no executable to
        /// start -- so a phone shows map names and no pictures rather than
        /// stalling on a batch that can never produce one.
        /// </summary>
        public static bool CanRun => !OperatingSystem.IsAndroid()
            && Environment.ProcessPath != null;

        public static int Run(IReadOnlyList<string> rooms, int parallelism,
                              int width, int height, Action<string>? report = null)
        {
            ThumbnailLog.Begin(rooms.Count);
            if (rooms.Count == 0)
            {
                return 0;
            }
            parallelism = Math.Clamp(parallelism, 1, 16);
            string? exePath = Environment.ProcessPath;
            if (exePath == null)
            {
                Console.WriteLine("[thumbnails] cannot locate this executable; running serially");
                return RunSerial(rooms, width, height, report);
            }
            int written = RunWorkers(rooms, parallelism, width, height, exePath, report,
                out List<string> failed);
            if (failed.Count > 0 && parallelism > 1)
            {
                // Ten of these run at once, and each is a GL context with a
                // 1600x900 offscreen target and a room's worth of textures in
                // it. A discrete card does not notice; an integrated one
                // sharing system memory can refuse the allocations, and what
                // that looks like from inside is texture calls failing with
                // GL_INVALID_OPERATION and every frame coming out black --
                // while the same room renders perfectly in the game, which is
                // one context rather than ten.
                //
                // So a run that lost rooms tries them again one at a time
                // before giving up. Still as worker processes: GLFW wants its
                // windows on the main thread and the launcher calls this from
                // a background one, so capturing in-process here would trade
                // one fault for another.
                string note = $"[thumbnails] {failed.Count} preview(s) failed with {parallelism} "
                    + "at a time; retrying them one at a time";
                Console.WriteLine(note);
                report?.Invoke(note);
                ThumbnailLog.Write(note);
                written += RunWorkers(failed, 1, width, height, exePath, report, out _);
            }
            return written;
        }

        private static int RunWorkers(IReadOnlyList<string> rooms, int parallelism,
                                      int width, int height, string exePath,
                                      Action<string>? report, out List<string> failedRooms)
        {
            var failed = new List<string>();
            failedRooms = failed;
            var running = new List<Process>();
            foreach (IReadOnlyList<string> share in Shares(rooms, parallelism))
            {
                Process? proc = StartWorker(exePath, share, width, height);
                if (proc != null)
                {
                    running.Add(proc);
                }
            }
            // Watched through the directory the workers write into, rather
            // than by which process exited: a worker now holds several rooms,
            // so its exit says nothing about the ones it finished minutes
            // ago. Android's batch reports the same way and for the same
            // reason -- see PreviewWorkers.Watch.
            var pending = new HashSet<string>(rooms, StringComparer.OrdinalIgnoreCase);
            int done = 0;
            int written = 0;
            while (true)
            {
                bool allExited = true;
                for (int i = 0; i < running.Count; i++)
                {
                    if (!running[i].HasExited)
                    {
                        allExited = false;
                        break;
                    }
                }
                foreach (string room in rooms)
                {
                    if (!pending.Contains(room) || !ThumbnailGenerator.Exists(room))
                    {
                        continue;
                    }
                    pending.Remove(room);
                    written++;
                    done++;
                    string ok = $"[thumbnails] {done}/{rooms.Count}  ok  {room}";
                    Console.WriteLine(ok);
                    report?.Invoke(ok);
                }
                if (allExited)
                {
                    break;
                }
                Thread.Sleep(50);
            }
            // Whatever no worker managed to write, however its process ended.
            foreach (string room in rooms)
            {
                if (!pending.Contains(room))
                {
                    continue;
                }
                failed.Add(room);
                done++;
                string line = $"[thumbnails] {done}/{rooms.Count}  FAILED  {room}";
                Console.WriteLine(line);
                report?.Invoke(line);
            }
            for (int i = 0; i < running.Count; i++)
            {
                running[i].Dispose();
            }
            return written;
        }

        /// <summary>
        /// Deal the rooms out to that many workers, round robin.
        ///
        /// Round robin rather than contiguous blocks: the rooms arrive sorted
        /// by name and their sizes are not, so blocks would hand one worker
        /// every big room. This is the split Android already uses.
        ///
        /// A room photographed after another in the same worker is caught with
        /// its pickups at a different point in their spin -- they are animated,
        /// and the phase is one of the few things a fresh Scene does not put
        /// back (ThumbnailCapture.CaptureRoom resets the rest). Nothing appears
        /// or disappears; the geometry, the camera and the lighting are
        /// identical to the pixel. The cost of avoiding it is a process per
        /// room, which is a third of the batch.
        /// </summary>
        private static List<List<string>> Shares(IReadOnlyList<string> rooms, int parallelism)
        {
            int workers = Math.Min(parallelism, rooms.Count);
            var shares = new List<List<string>>(workers);
            for (int i = 0; i < workers; i++)
            {
                shares.Add(new List<string>());
            }
            for (int i = 0; i < rooms.Count; i++)
            {
                shares[i % workers].Add(rooms[i]);
            }
            return shares;
        }

        private static Process? StartWorker(string exePath, IReadOnlyList<string> share,
                                            int width, int height)
        {
            var info = new ProcessStartInfo
            {
                FileName = exePath,
                UseShellExecute = false,
                // Workers find paths.txt through the working directory, the
                // same way a normal launch does.
                WorkingDirectory = Directory.GetCurrentDirectory(),
                RedirectStandardOutput = true,
                RedirectStandardError = true
            };
            for (int i = 0; i < share.Count; i++)
            {
                info.ArgumentList.Add("-thumbnail");
                info.ArgumentList.Add(share[i]);
            }
            info.ArgumentList.Add("-size");
            info.ArgumentList.Add($"{width}x{height}");
            try
            {
                Process? proc = Process.Start(info);
                if (proc == null)
                {
                    Console.WriteLine($"[thumbnails] could not start a worker for "
                        + $"{share.Count} room(s)");
                }
                return proc;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[thumbnails] worker failed to start: {ex.Message}");
                return null;
            }
        }

        private static int RunSerial(IReadOnlyList<string> rooms, int width, int height,
                                      Action<string>? report)
        {
            int written = 0;
            for (int i = 0; i < rooms.Count; i++)
            {
                if (ThumbnailGenerator.Exists(rooms[i]))
                {
                    continue;
                }
                if (ThumbnailCapture.CaptureRoom(rooms[i], width, height))
                {
                    written++;
                }
                string line = $"[thumbnails] {i + 1}/{rooms.Count}  {rooms[i]}";
                Console.WriteLine(line);
                report?.Invoke(line);
            }
            return written;
        }
    }
}
