#!/usr/bin/env bash
# The C++ dedicated server with a client of each build: starts `--server` on
# UDP 28191 (outside the 27988-27999 range the real servers use), joins a
# headless C++ client that chases, shoots and morphs, and the C#'s own
# `-netcheck` client, which plays through every feature and reports what it
# saw of the other player. SERVER=cs runs the C#'s server instead, for the
# same report to compare against.
#   FP_FILES     the game files (default ~/mph-test/files/AMHP1)
#   FP_CSHARP    the C# build directory (default ../src/MphRead/bin/Release/net10.0)
#   SECONDS_RUN  how long the C# client plays (default 35)
#   SERVER       cpp (default) or cs
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$HERE/build/linux-release/FruityPrime"
CSHARP="${FP_CSHARP:-$HERE/../src/MphRead/bin/Release/net10.0}"
export FP_FILES="${FP_FILES:-$HOME/mph-test/files/AMHP1}"
RUN="${SECONDS_RUN:-35}"
PORT=28191
OUT="$(mktemp -d)"
export PATH="$HOME/.dotnet:$PATH" DOTNET_ROOT="$HOME/.dotnet" DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 ALSOFT_DRIVERS=null PULSE_SERVER=
export QT_QPA_PLATFORM=offscreen FP_DEBUG_NET=1

if [ "${SERVER:-cpp}" = cs ]; then
    (cd "$CSHARP" && exec ./FruityPrime -server -port $PORT -players 4 -servername cscheck -nomaster -noautoupdate) > "$OUT/server.log" 2>&1 &
else
    "$BIN" --server --port $PORT --max-players 4 --room "MP1 SANCTORUS" --no-master > "$OUT/server.log" 2>&1 &
fi
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null' EXIT
for _ in $(seq 30); do
    "$BIN" --net-status 127.0.0.1:$PORT > /dev/null 2>&1 && break
    sleep 1
done
"$BIN" --net-status 127.0.0.1:$PORT || { echo "FAIL: the server did not start (see $OUT/server.log)"; exit 1; }

# The C++ client plays for as long as the C# one takes to load and play.
"$BIN" --walk-test --connect 127.0.0.1:$PORT --name Cpp --hunter 3 --seconds $((RUN + 25)) \
    --script "$(for _ in $(seq $((RUN / 4 + 8))); do printf 'T;Tf;Tf;j;m;w;m;Tf;'; done)" > "$OUT/cpp.log" 2>&1 &
CPP=$!
(cd "$CSHARP" && timeout $((RUN + 90)) ./FruityPrime -netcheck 127.0.0.1 -port $PORT -name Sharp -hunter 1 -seconds "$RUN") > "$OUT/cs.log" 2>&1
wait $CPP

echo "== what the C# client saw of the C++ one (logs in $OUT)"
sed -n '/feature coverage/,/RESULT/p' "$OUT/cs.log" | grep -E "mine|RESULT|FAIL:"
grep -E "scoreboard at" "$OUT/cs.log" | tail -1
echo "== what the C++ client saw"
grep 'seen world' "$OUT/cpp.log"
