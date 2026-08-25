using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Android.App;
using Android.Content;
using MphRead.Mods;

namespace MphRead.Droid
{
    /// <summary>
    /// Map previews, rendered on the phone, in the background, several at once.
    ///
    /// Three things were wrong with doing it on a <c>GLSurfaceView</c>: the
    /// player watched every room load and unload, the screen was forced to
    /// landscape while it happened, and it was one room at a time. This is the
    /// desktop's arrangement instead -- a batch of worker processes, watched
    /// through the cache directory they write into -- with
    /// <see cref="PreviewService"/> where the desktop starts copies of itself.
    ///
    /// **Nothing is shipped.** Every preview is rendered on the device from the
    /// player's own extracted files, into
    /// <see cref="ThumbnailGenerator.CacheDirectory"/> beside them.
    /// </summary>
    internal sealed class AndroidThumbnailHost : IThumbnailHost
    {
        private readonly MainActivity _activity;

        public AndroidThumbnailHost(MainActivity activity) => _activity = activity;

        public Task<int> RenderAsync(IReadOnlyList<string> rooms, Action<string> report)
        {
            // A custom map has no picture until it has binaries to render.
            AndroidMaps.EnsureBuilt();
            return _activity.RenderPreviews(rooms, report);
        }
    }

    internal static class PreviewWorkers
    {
        /// <summary>
        /// How many to start.
        ///
        /// Ten, the same as the desktop batch, unless the device says it
        /// cannot: each worker is a runtime, a GL context and a room's
        /// textures, and a phone that runs out kills them rather than slowing
        /// down. A killed worker costs its share of the rooms, which the
        /// in-process pass afterwards picks up.
        /// </summary>
        public static int Count(Context context)
        {
            int cores = Math.Max(1, Java.Lang.Runtime.GetRuntime()?.AvailableProcessors() ?? 1);
            int heapMb = 128;
            if (context.GetSystemService(Context.ActivityService) is ActivityManager manager)
            {
                heapMb = Math.Max(32, manager.MemoryClass);
            }
            return Math.Clamp(Math.Min(cores * 2, heapMb / 24), 1, PreviewWorkerTypes.All.Count);
        }

        /// <summary>
        /// Hand the rooms out, start the workers, and watch the directory until
        /// they are done. Returns how many of the asked-for rooms now have a
        /// picture.
        /// </summary>
        public static int Run(Context context, IReadOnlyList<string> rooms, int width, int height,
            Action<string> report)
        {
            int workers = Math.Min(Count(context), rooms.Count);
            var shares = new List<string>[workers];
            for (int i = 0; i < workers; i++)
            {
                shares[i] = new List<string>();
            }
            // Round robin rather than contiguous blocks: the rooms are sorted by
            // name and their sizes are not, so one worker would otherwise get
            // every big one.
            for (int i = 0; i < rooms.Count; i++)
            {
                shares[i % workers].Add(rooms[i]);
            }
            var markers = new List<string>();
            int started = 0;
            for (int i = 0; i < workers; i++)
            {
                string marker = Path.Combine(ThumbnailGenerator.CacheDirectory, $".worker{i}.done");
                TryDelete(marker);
                var intent = new Intent(context, PreviewWorkerTypes.All[i]);
                intent.PutExtra(PreviewService.RoomsExtra, shares[i].ToArray());
                intent.PutExtra(PreviewService.MarkerExtra, marker);
                intent.PutExtra(PreviewService.WidthExtra, width);
                intent.PutExtra(PreviewService.HeightExtra, height);
                try
                {
                    context.StartService(intent);
                }
                catch (Exception ex)
                {
                    // Android refuses a service start from the background, and
                    // an OEM build may refuse one for its own reasons. Either
                    // way the rooms this worker was given are simply still
                    // missing, and the caller renders them itself.
                    Console.WriteLine($"[thumbnails] worker {i} would not start: {ex.Message}");
                    continue;
                }
                markers.Add(marker);
                started++;
            }
            if (started == 0)
            {
                return 0;
            }
            report($"[thumbnails] rendering {rooms.Count} preview(s) in the background, "
                + $"{started} at a time");
            Watch(rooms, markers, report);
            return CountWritten(rooms);
        }

        /// <summary>
        /// Wait for every worker's marker, reporting progress from the
        /// directory. A worker the system kills for memory never writes one, so
        /// this cannot wait forever.
        /// </summary>
        private static void Watch(IReadOnlyList<string> rooms, List<string> markers,
            Action<string> report)
        {
            var clock = Stopwatch.StartNew();
            TimeSpan limit = TimeSpan.FromSeconds(90 + 30 * rooms.Count);
            int last = -1;
            while (clock.Elapsed < limit)
            {
                bool allDone = true;
                for (int i = 0; i < markers.Count; i++)
                {
                    if (!File.Exists(markers[i]))
                    {
                        allDone = false;
                        break;
                    }
                }
                int written = CountWritten(rooms);
                if (written != last)
                {
                    last = written;
                    report($"[thumbnails] {written}/{rooms.Count}");
                }
                if (allDone)
                {
                    return;
                }
                Thread.Sleep(500);
            }
            report("[thumbnails] the background workers ran out of time; "
                + "the rest will be rendered on the next visit");
        }

        private static int CountWritten(IReadOnlyList<string> rooms)
        {
            int written = 0;
            for (int i = 0; i < rooms.Count; i++)
            {
                if (ThumbnailGenerator.Exists(rooms[i]))
                {
                    written++;
                }
            }
            return written;
        }

        private static void TryDelete(string path)
        {
            try
            {
                File.Delete(path);
            }
            catch (Exception)
            {
                // Nothing to delete, or a directory that is about to be created.
            }
        }
    }
}
