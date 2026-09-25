#!/usr/bin/env bash
# C# (OpenGL) vs C++ (Vulkan) on the same room, camera and size, in WSL2.
set -uo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
OUT="$(realpath -m "${1:-$HERE/build/bench-wsl.jsonl}")"
RUNS="${RUNS:-3}"
FILES="${FILES:-$HOME/mph-test/files/AMHP1}"
ROOM="MP3 PROVING GROUND"
CS_DIR="$REPO/src/MphRead/bin/Release/net10.0"
CPP="$HERE/build/linux-release/FruityPrime"
BENCH="python3 $HERE/tools/bench.py --warmup 5 --seconds 25 --json $OUT"
: > "$OUT"

cs_env=(env DOTNET_ROOT="$HOME/.dotnet" DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 MESA_GL_VERSION_OVERRIDE=4.5COMPAT
        ALSOFT_DRIVERS=null PULSE_SERVER=)
cs_cmd=("$CS_DIR/FruityPrime" -room "$ROOM" -debuglog)
cpp_cmd=("$CPP" --files "$FILES" --room "$ROOM" --cam 0,0,0,0,0 --size 1280x768 --bench 25)

for i in $(seq "$RUNS"); do
    (cd "$CS_DIR" && $BENCH --label "cs-gl-llvmpipe-vsync" --cs-log-dir "$CS_DIR/logs" -- \
        "${cs_env[@]}" LIBGL_ALWAYS_SOFTWARE=1 "${cs_cmd[@]}" >/dev/null)
    (cd "$CS_DIR" && $BENCH --label "cs-gl-llvmpipe-uncapped" --cs-log-dir "$CS_DIR/logs" -- \
        "${cs_env[@]}" LIBGL_ALWAYS_SOFTWARE=1 vblank_mode=0 "${cs_cmd[@]}" >/dev/null)
    $BENCH --label "cpp-vk-lavapipe-window-vsync" -- env QT_QPA_PLATFORM=wayland "${cpp_cmd[@]}" >/dev/null
    $BENCH --label "cpp-vk-lavapipe-offscreen-uncapped" -- env QT_QPA_PLATFORM=wayland "${cpp_cmd[@]}" --offscreen >/dev/null
done
(cd "$CS_DIR" && $BENCH --label "cs-gl-d3d12-gpu-uncapped" --cs-log-dir "$CS_DIR/logs" -- \
    "${cs_env[@]}" GALLIUM_DRIVER=d3d12 vblank_mode=0 "${cs_cmd[@]}" >/dev/null)
echo "results in $OUT"
