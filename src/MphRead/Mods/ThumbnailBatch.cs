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
    /// executable invoked with -thumbnail &lt;room&gt;, which also isolates a
    /// crash on one room from the rest of the batch.
    /// </summary>
    public static class ThumbnailBatch
    {
        public const int DefaultParallelism = 10;

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

            var queue = new Queue<string>(rooms);
            var running = new List<(Process Proc, string Room)>();
            var failed = new List<string>();
            failedRooms = failed;
            int done = 0;
            int written = 0;

            while (queue.Count > 0 || running.Count > 0)
            {
                while (running.Count < parallelism && queue.Count > 0)
                {
                    string room = queue.Dequeue();
                    Process? proc = StartWorker(exePath, room, width, height);
                    if (proc == null)
                    {
                        done++;
                        continue;
                    }
                    running.Add((proc, room));
                }
                for (int i = running.Count - 1; i >= 0; i--)
                {
                    (Process proc, string room) = running[i];
                    if (!proc.HasExited)
                    {
                        continue;
                    }
                    running.RemoveAt(i);
                    proc.Dispose();
                    done++;
                    bool ok = ThumbnailGenerator.Exists(room);
                    if (ok)
                    {
                        written++;
                    }
                    else
                    {
                        failed.Add(room);
                    }
                    string line = $"[thumbnails] {done}/{rooms.Count}  "
                        + $"{(ok ? "ok" : "FAILED")}  {room}";
                    Console.WriteLine(line);
                    report?.Invoke(line);
                }
                if (running.Count > 0)
                {
                    Thread.Sleep(50);
                }
            }
            return written;
        }

        private static Process? StartWorker(string exePath, string room, int width, int height)
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
            info.ArgumentList.Add("-thumbnail");
            info.ArgumentList.Add(room);
            info.ArgumentList.Add("-size");
            info.ArgumentList.Add($"{width}x{height}");
            try
            {
                Process? proc = Process.Start(info);
                if (proc == null)
                {
                    Console.WriteLine($"[thumbnails] could not start worker for {room}");
                }
                return proc;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[thumbnails] worker failed for {room}: {ex.Message}");
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
