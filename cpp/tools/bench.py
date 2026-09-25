#!/usr/bin/env python3
"""Run a program for warmup+duration seconds and report what it cost.

Samples /proc/<pid> for CPU time, resident memory and threads, and reads the
frame rate either from Mesa's Gallium HUD dump (GL programs) or from the
program's own "bench:" lines (the C++ --bench mode).

    bench.py --label NAME --warmup 5 --seconds 20 [--hud] -- CMD ARGS...
"""
import argparse
import json
import os
import signal
import statistics
import subprocess
import sys
import tempfile
import time

CLK_TCK = os.sysconf("SC_CLK_TCK")


def proc_cpu_seconds(pid):
    with open(f"/proc/{pid}/stat") as f:
        fields = f.read().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / CLK_TCK  # utime + stime


def proc_status(pid):
    out = {}
    with open(f"/proc/{pid}/status") as f:
        for line in f:
            key, _, value = line.partition(":")
            out[key] = value.strip()
    return out


def kb(value):
    return int(value.split()[0]) if value else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True)
    ap.add_argument("--warmup", type=float, default=5)
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("--hud", action="store_true", help="read fps from GALLIUM_HUD")
    ap.add_argument("--json", help="append the result to this JSON-lines file")
    ap.add_argument("--cs-log-dir", help="read the C# -debuglog frame timing and load time from this logs directory")
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    args = ap.parse_args()
    cmd = args.cmd[1:] if args.cmd and args.cmd[0] == "--" else args.cmd

    env = dict(os.environ)
    hud_dir = None
    if args.hud:
        hud_dir = tempfile.mkdtemp(prefix="hud-")
        env.update(GALLIUM_HUD="simple,fps,frametime", GALLIUM_HUD_PERIOD="0.5", GALLIUM_HUD_DUMP_DIR=hud_dir,
                   GALLIUM_HUD_VISIBLE="false")

    start = time.monotonic()
    launch_wall = time.time()
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    pid = proc.pid
    output = []

    import threading

    def pump():
        for line in proc.stdout:
            output.append(line)

    threading.Thread(target=pump, daemon=True).start()

    rss, threads, cpu_samples = [], [], []
    cpu_start = None
    t_measure_start = start + args.warmup
    t_end = t_measure_start + args.seconds
    while time.monotonic() < t_end and proc.poll() is None:
        now = time.monotonic()
        try:
            st = proc_status(pid)
            cpu = proc_cpu_seconds(pid)
        except FileNotFoundError:
            break
        peak_hwm = max(locals().get("peak_hwm", 0), kb(st.get("VmHWM")))
        if now >= t_measure_start:
            if cpu_start is None:
                cpu_start, wall_start = cpu, now
            rss.append(kb(st.get("VmRSS")))
            threads.append(int(st.get("Threads", "0")))
            cpu_samples.append((now, cpu))
        time.sleep(0.25)

    peak_rss = locals().get("peak_hwm", 0)
    try:
        peak_rss = max(peak_rss, kb(proc_status(pid).get("VmHWM")))
    except FileNotFoundError:
        pass
    if proc.poll() is None:
        # The C++ --bench mode exits on its own; give it a moment first.
        for _ in range(40):
            if proc.poll() is not None:
                break
            time.sleep(0.25)
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    result = {"label": args.label}
    if cpu_samples and len(cpu_samples) > 1:
        (t0, c0), (t1, c1) = cpu_samples[0], cpu_samples[-1]
        result["cpu_percent_of_one_core"] = round(100 * (c1 - c0) / (t1 - t0), 1)
        result["cpu_seconds"] = round(c1 - c0, 2)
        result["wall_seconds"] = round(t1 - t0, 2)
    if rss:
        result["rss_mb_avg"] = round(statistics.mean(rss) / 1024, 1)
    result["rss_mb_peak"] = round(peak_rss / 1024, 1)
    if threads:
        result["threads"] = max(threads)

    if hud_dir:
        fps_values = []
        path = os.path.join(hud_dir, "fps")
        if os.path.exists(path):
            with open(path) as f:
                rows = [line.split() for line in f if line.strip()]
            # rows: "value" per period; skip the warmup periods
            vals = [float(r[-1]) for r in rows]
            skip = int(args.warmup / 0.5)
            fps_values = vals[skip:]
        if fps_values:
            result["fps_avg"] = round(statistics.mean(fps_values), 1)
            result["fps_min_period"] = round(min(fps_values), 1)
            result["fps_max_period"] = round(max(fps_values), 1)
            if "cpu_seconds" in result:
                frames = result["fps_avg"] * result["wall_seconds"]
                result["cpu_ms_per_frame"] = round(1000 * result["cpu_seconds"] / frames, 2) if frames else None
        ft = os.path.join(hud_dir, "frametime")
        if os.path.exists(ft):
            with open(ft) as f:
                vals = [float(line.split()[-1]) for line in f if line.strip()][int(args.warmup / 0.5):]
            if vals:
                result["frametime_ms_avg"] = round(statistics.mean(vals), 2)

    for line in output:
        if line.startswith("bench:"):
            result.setdefault("app", []).append(line.strip()[7:])
            import re
            m = re.search(r"([0-9.]+) fps \| frame ms avg ([0-9.]+) p50 ([0-9.]+) p95 ([0-9.]+) p99 ([0-9.]+)", line)
            if m:
                result["fps_rendered"] = float(m.group(1))
                result["frame_ms_p50"], result["frame_ms_p95"], result["frame_ms_p99"] = map(float, m.group(3, 4, 5))
            m = re.search(r"first frame ([0-9]+) ms", line)
            if m:
                result["first_frame_ms"] = int(m.group(1))

    if args.cs_log_dir:
        import glob
        import re
        logs = [p for p in glob.glob(os.path.join(args.cs_log_dir, "*.log")) if os.path.getmtime(p) >= launch_wall - 1]
        if logs:
            path = max(logs, key=os.path.getmtime)
            lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
            draws = [float(m.group(1)) for l in lines if (m := re.search(r"\[frametiming\] .*draw ([0-9.]+) Hz", l))]
            if draws:
                result["fps_rendered"] = draws[-1]
                result["fps_rendered_windows"] = draws
            def ts(l):
                h, mnt, s = l[1:13].split(":")
                return int(h) * 3600 + int(mnt) * 60 + float(s)
            load = [l for l in lines if re.search(r"load \".*\": done in", l)]
            first = [l for l in lines if "[frametiming" in l]
            if load:
                result["room_load_ms"] = int(re.search(r"done in ([0-9]+) ms", load[0]).group(1))
                lt = time.localtime(launch_wall)
                launch_tod = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + (launch_wall % 1)
                result["process_start_to_room_loaded_ms"] = int((ts(load[0]) - launch_tod) * 1000)

    if "fps_rendered" in result and "cpu_seconds" in result:
        frames = result["fps_rendered"] * result["wall_seconds"]
        result["cpu_ms_per_frame"] = round(1000 * result["cpu_seconds"] / frames, 2)

    print(json.dumps(result, indent=2))
    if args.json:
        with open(args.json, "a") as f:
            f.write(json.dumps(result) + "\n")
    tail = [l for l in output if l.strip()][-8:]
    if "fps_avg" not in result and "app" not in result:
        sys.stderr.write("no frame-rate data; last output:\n" + "".join(tail))


if __name__ == "__main__":
    main()
