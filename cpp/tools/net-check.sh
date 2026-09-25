#!/usr/bin/env bash
# Online check against the C# dedicated server, with nothing else on the line:
# starts the C#'s server on UDP 28190 (outside the 27988-27999 range the real
# servers use), joins two C++ clients as headless walk-tests -- one standing,
# one aiming at it and shooting -- and passes when a kill went through the
# server: the victim's own client replayed the hits and the death, and the
# server spawned it again.
#   FP_FILES     the game files (default ~/mph-test/files/AMHP1)
#   FP_CSHARP    the C# build directory (default ../src/MphRead/bin/Release/net10.0)
#   SECONDS_RUN  how long the clients play (default 45)
#   SERVER       cs (default) or cpp: this port's own --server instead
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$HERE/build/linux-release/FruityPrime"
CSHARP="${FP_CSHARP:-$HERE/../src/MphRead/bin/Release/net10.0}"
export FP_FILES="${FP_FILES:-$HOME/mph-test/files/AMHP1}"
RUN="${SECONDS_RUN:-45}"
PORT=28190
OUT="$(mktemp -d)"
export PATH="$HOME/.dotnet:$PATH" DOTNET_ROOT="$HOME/.dotnet" DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 ALSOFT_DRIVERS=null PULSE_SERVER=
export QT_QPA_PLATFORM=offscreen FP_DEBUG_NET=1

if [ "${SERVER:-cs}" = cpp ]; then
    "$BIN" --server --port $PORT --max-players 8 --room "MP1 SANCTORUS" --no-master > "$OUT/server.log" 2>&1 &
else
    (cd "$CSHARP" && exec ./FruityPrime -server -port $PORT -players 8 -servername cppcheck -nomaster -noautoupdate) > "$OUT/server.log" 2>&1 &
fi
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
for _ in $(seq 30); do
    "$BIN" --net-status 127.0.0.1:$PORT > /dev/null 2>&1 && break
    sleep 1
done
"$BIN" --net-status 127.0.0.1:$PORT || { echo "FAIL: the server did not start (see $OUT/server.log)"; exit 1; }

steps=$((RUN * 2 + 10))
# A fresh server starts on MP1 SANCTORUS; FP_NET_PLACE stands the two where
# each can see the other (the server takes a player's position from its report).
FP_NET_PLACE=4.57,0.49,-6.52,-90 "$BIN" --walk-test --connect 127.0.0.1:$PORT --name Victim --hunter 0 --seconds $((RUN + 5)) \
    --script "$(for _ in $(seq $steps); do printf '.;'; done)" > "$OUT/victim.log" 2>&1 &
VICTIM=$!
sleep 2
FP_NET_PLACE=10.5,0.49,-5,90 "$BIN" --walk-test --connect 127.0.0.1:$PORT --name Hunter --hunter 3 --seconds "$RUN" \
    --script "$(for _ in $(seq $((steps / 2))); do printf 'tf;t;'; done)" > "$OUT/hunter.log" 2>&1
wait $VICTIM

# The slots the server gave them (somebody else may be on the server too).
V=$(sed -n 's/.*joined as slot \([0-9]\).*/\1/p' "$OUT/victim.log" | head -1)
H=$(sed -n 's/.*joined as slot \([0-9]\).*/\1/p' "$OUT/hunter.log" | head -1)
hits=$(grep -c "slot $V hit by $H" "$OUT/victim.log")
deaths=$(grep -c "slot $V hit by $H .*(lethal)" "$OUT/victim.log")
lives=$(grep -c "slot $V life " "$OUT/victim.log")
echo "victim (slot $V) hit by the hunter (slot $H): $hits hits replayed, $deaths deaths, $lives lives; logs in $OUT"
grep 'seen world' "$OUT/hunter.log" "$OUT/victim.log"
if [ "$hits" -gt 0 ] && [ "$deaths" -gt 0 ] && [ "$lives" -gt 1 ]; then
    echo PASS
else
    echo "FAIL: no kill went through the server (the two may not have met: rerun, or raise SECONDS_RUN)"
    exit 1
fi
