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

        public static int Run(IReadOnlyList<string> rooms, int parallelism,
                              int width, int height)
        {
            if (rooms.Count == 0)
            {
                return 0;
            }
            parallelism = Math.Clamp(parallelism, 1, 16);
            string? exePath = Environment.ProcessPath;
            if (exePath == null)
            {
                Console.WriteLine("[thumbnails] cannot locate this executable; running serially");
                return RunSerial(rooms, width, height);
            }

            var queue = new Queue<string>(rooms);
            var running = new List<(Process Proc, string Room)>();
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
                    Console.WriteLine($"[thumbnails] {done}/{rooms.Count}  "
                        + $"{(ok ? "ok" : "FAILED")}  {room}");
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

        private static int RunSerial(IReadOnlyList<string> rooms, int width, int height)
        {
            int written = 0;
            for (int i = 0; i < rooms.Count; i++)
            {
                if (ThumbnailCapture.CaptureRoom(rooms[i], width, height))
                {
                    written++;
                }
                Console.WriteLine($"[thumbnails] {i + 1}/{rooms.Count}  {rooms[i]}");
            }
            return written;
        }
    }
}
