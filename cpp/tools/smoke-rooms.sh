#!/usr/bin/env bash
# Every multiplayer room, one simulated minute with bots, without a window:
#   tools/smoke-rooms.sh [mode] [bots] [extra FruityPrime options...]
# Rooms without a player spawn (the biodefense chambers) are skipped.
# Prints each room's deaths and the end of each match; exits non-zero when a run fails.
set -uo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-$HERE/build/linux-release/FruityPrime}"
MODE="${1:-battle}"; BOTS="${2:-5}"; shift 2 2>/dev/null || shift $#
SCRIPT=$(python3 -c "print(';'.join(['.']*120))")
mapfile -t ROOMS < <(python3 - "$HERE/src/formats/RoomTable.inc" <<'PY'
import re, sys
for line in open(sys.argv[1]):
    m = re.match(r'\s*\{\d+, "([^"]+)".*?, (true|false), (true|false), (true|false), -?[\d.]+f,', line)
    if m and m.group(2) == 'true' and m.group(3) == 'false' and m.group(4) == 'false':
        print(m.group(1))
PY
)
failed=0
for room in "${ROOMS[@]}"; do
    out=$(FP_DEBUG_WORLD=1 timeout 300 "$BIN" --walk-test --room "$room" --bots "$BOTS" --mode "$MODE" --script "$SCRIPT" "$@" 2>&1)
    status=$?
    deaths=$(grep -c "killed by" <<<"$out")
    ends=$(grep -c "match over" <<<"$out")
    if grep -q "needs a player spawn" <<<"$out"; then
        printf '%-28s skipped (no player spawn)\n' "$room"
    elif [ $status -ne 0 ]; then
        failed=1
        echo "FAIL $room (exit $status)"; tail -5 <<<"$out"
    else
        printf '%-28s %3d deaths, %d match ends\n' "$room" "$deaths" "$ends"
    fi
done
exit $failed
