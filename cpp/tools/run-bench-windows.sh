#!/usr/bin/env bash
# From WSL: C# (OpenGL) vs C++ (Vulkan) natively on Windows, same room, camera and size.
# CS_DIR / CPP_DIR are Windows folders reachable under /mnt/c.
set -uo pipefail
OUT="$(realpath -m "${1:-$(dirname "$0")/../build/bench-windows.jsonl}")"
RUNS="${RUNS:-3}"
CS_WIN="${CS_WIN:-C:\\fruityprime-cs}"
CPP_WIN="${CPP_WIN:-C:\\fruityprime}"
FILES_WIN="${FILES_WIN:-$CS_WIN\\files\\AMHE0}"
PS1="$CPP_WIN\\bench-windows.ps1"
ROOM="MP3 PROVING GROUND"
: > "$OUT"

run() { # label exe args [extra env assignments as PowerShell]
    local label="$1" exe="$2" args="$3" envs="${4:-}"
    local json
    json=$(timeout 180 powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "$envs & '$PS1' -Label '$label' -Exe '$exe' -Args '$args' -Warmup 8 -Seconds 22" | tr -d '\r' | tail -1)
    python3 - "$json" "$OUT" <<'PY'
import json, re, sys, pathlib, glob, os
d = json.loads(sys.argv[1])
def wsl(p): return "/mnt/" + p[0].lower() + p[2:].replace("\\", "/")
out = pathlib.Path(wsl(d["stdout"]))
if out.exists():
    for line in out.read_text(errors="replace").splitlines():
        if m := re.search(r"([0-9.]+) fps \| frame ms avg ([0-9.]+) p50 ([0-9.]+) p95 ([0-9.]+) p99 ([0-9.]+)", line):
            d["fps_rendered"] = float(m.group(1)); d["frame_ms_p50"], d["frame_ms_p95"], d["frame_ms_p99"] = map(float, m.group(3, 4, 5))
        if m := re.search(r"first frame ([0-9]+) ms", line):
            d["first_frame_ms"] = int(m.group(1))
if d["label"].startswith("cs"):
    logs = sorted(glob.glob(wsl(os.environ["CS_WIN"]) + "/logs/*.log"), key=os.path.getmtime)
    lines = open(logs[-1], errors="replace").read().splitlines()
    draws = [float(m.group(1)) for l in lines if (m := re.search(r"\[frametiming\] .*draw ([0-9.]+) Hz", l))]
    if draws: d["fps_rendered"] = draws[-1]
    for l in lines:
        if m := re.search(r"done in ([0-9]+) ms", l):
            d["room_load_ms"] = int(m.group(1))
            h, mi, s = l[1:13].split(":"); lh, lm, ls = d["launched"].split(":")
            d["process_start_to_room_loaded_ms"] = int(((int(h)-int(lh))*3600 + (int(mi)-int(lm))*60 + float(s)-float(ls)) * 1000)
            break
if d.get("fps_rendered"):
    d["cpu_ms_per_frame"] = round(1000 * d["cpu_seconds"] / (d["fps_rendered"] * d["wall_seconds"]), 3)
print(d["label"], "fps", d.get("fps_rendered"), "cpu%", d["cpu_percent_of_one_core"], "rss", d["rss_mb_avg"])
open(sys.argv[2], "a").write(json.dumps(d) + "\n")
PY
}
export CS_WIN
NODPI='$env:QT_ENABLE_HIGHDPI_SCALING="0";'
CPP_ARGS="--files \"$FILES_WIN\" --room \"$ROOM\" --cam 0,0,0,0,0 --size 1280x768 --bench 22"
for i in $(seq "$RUNS"); do
    run cs-win-gl-vsync "$CS_WIN\\FruityPrime.exe" "-room \"$ROOM\" -debuglog"
    run cpp-win-vk-vsync "$CPP_WIN\\FruityPrime.exe" "$CPP_ARGS --vsync on" "$NODPI"
    run cpp-win-vk-uncapped-window "$CPP_WIN\\FruityPrime.exe" "$CPP_ARGS --vsync off" "$NODPI"
    run cpp-win-vk-uncapped-offscreen "$CPP_WIN\\FruityPrime.exe" "$CPP_ARGS --offscreen" "$NODPI"
done
echo "results in $OUT"
