#!/usr/bin/env bash
# Prove the dedicated server and the server directory in a build actually run.
#
# The build workflow publishes a linux-x64 and a linux-arm64 binary and calls
# them "the dedicated server", but publishing something is not the same as it
# starting: the server is reached through a code path that runs before the
# game-file check, and nothing in a compile says whether that path still works
# on a machine with no game data, no display and no sound device -- which is
# exactly the machine it is meant for.
#
# So: start both, make the server announce itself to the directory, ask the
# directory who is up, and ask the server what it is running. Every one of
# those is a thing a player depends on and none of them needs a cartridge.
#
#   tools/check-dedicated-server.sh publish/linux-x64
#   tools/check-dedicated-server.sh publish/win-x64-server   # Git Bash
#
# The Windows server is the same claim and gets the same check, rather than a
# PowerShell translation of it that would then have to be kept in step. What
# differs there is spelled out where it is handled: the binary is called
# MphReadServer.exe, python3 may only be `python`, and a path this script
# makes has to be converted before it is handed to a .NET process.
set -uo pipefail

DIR="${1:-publish/linux-x64}"
# MphReadServer first: on Windows the dedicated server is its own console
# binary, and a publish directory may hold both.
BIN=""
for candidate in MphReadServer.exe MphReadServer MphRead.exe MphRead; do
  if [ -x "$DIR/$candidate" ]; then
    BIN="$DIR/$candidate"
    break
  fi
done
[ -n "$BIN" ] || BIN="dotnet $DIR/MphRead.dll"

PYTHON="python3"
command -v "$PYTHON" >/dev/null 2>&1 || PYTHON="python"

# Git Bash hands out POSIX paths; a .NET process reads one as a path on the
# current drive and writes the rotation somewhere that does not exist.
topath() {
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -w "$1"
  else
    printf '%s' "$1"
  fi
}

WORK="$(mktemp -d)"
SERVER_PORT=27888
MASTER_PORT=27889
FAILED=0

cleanup() {
  [ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null
  [ -n "${MASTER_PID:-}" ] && kill "$MASTER_PID" 2>/dev/null
  wait 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

fail() { echo "FAIL: $*"; FAILED=1; }
pass() { echo "ok:   $*"; }

echo "checking the dedicated server in $DIR"

$BIN -masterserver -port "$MASTER_PORT" >"$WORK/master.log" 2>&1 &
MASTER_PID=$!
$BIN -server -port "$SERVER_PORT" -players 8 \
     -servername "CI smoke test" \
     -master 127.0.0.1 -masterport "$MASTER_PORT" \
     -rotation "$(topath "$WORK/maprotation.txt")" >"$WORK/server.log" 2>&1 &
SERVER_PID=$!

# Both bind before they log, and the server's first heartbeat goes out on its
# first one-second tick.
for _ in $(seq 1 30); do
  grep -q "listening on UDP" "$WORK/server.log" 2>/dev/null \
    && grep -q "listening on UDP" "$WORK/master.log" 2>/dev/null && break
  sleep 0.5
done

kill -0 "$SERVER_PID" 2>/dev/null || fail "the server exited immediately"
kill -0 "$MASTER_PID" 2>/dev/null || fail "the directory exited immediately"
grep -q "listening on UDP $SERVER_PORT" "$WORK/server.log" || fail "the server never bound its port"
grep -q "listening on UDP $MASTER_PORT" "$WORK/master.log" || fail "the directory never bound its port"

for _ in $(seq 1 20); do
  grep -q '^\[.*\] \[master\] + ' "$WORK/master.log" 2>/dev/null && break
  sleep 0.5
done
grep -q '^\[.*\] \[master\] + ' "$WORK/master.log" \
  && pass "the server registered with the directory" \
  || fail "the server never registered with the directory"

"$PYTHON" - "$SERVER_PORT" "$MASTER_PORT" <<'PY' || FAILED=1
import socket, struct, sys
server_port, master_port = int(sys.argv[1]), int(sys.argv[2])
ok = True

def check(label, condition):
    global ok
    print(("ok:   " if condition else "FAIL: ") + label)
    ok = ok and condition

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(3)

# The one thing that has to be kept in step with NetProtocol.cs. Named rather
# than inlined because these offsets have been wrong before: the match state
# grew a field and the checks below started reading room names with two bytes
# of the previous field on the front.
MODE, TIME, ELAPSED, COUNT, FLAGS, GOAL, MATCH = 1, 4, 4, 1, 1, 2, 2
ROOM_BYTES, SRVNAME_BYTES = 40, 32
ROOM_AT = MODE + TIME + ELAPSED + COUNT + FLAGS + GOAL + MATCH   # 15
NEXT_AT = ROOM_AT + ROOM_BYTES                                    # 55
MAXPLAYERS_AT = NEXT_AT + ROOM_BYTES                              # 95
SRVNAME_AT = MAXPLAYERS_AT + 2                                    # 97

# StatusQuery -> StatusReply: what a launcher shows before anybody joins.
try:
    s.sendto(bytes([14, 3]), ("127.0.0.1", server_port))
    d, _ = s.recvfrom(2048)
    check("the server answered a status query", d[0] == 15)
    body = d[1:]
    room = body[ROOM_AT:ROOM_AT + ROOM_BYTES].rstrip(b"\0").decode()
    name = body[SRVNAME_AT:SRVNAME_AT + SRVNAME_BYTES].rstrip(b"\0").decode()
    check(f"it named its map ({room!r})", len(room) > 0)
    check(f"it named itself ({name!r})", name == "CI smoke test")
    check("it reported its player cap", body[MAXPLAYERS_AT] == 8)
except socket.timeout:
    check("the server answered a status query", False)

# MasterQuery -> MasterList: what the server browser shows.
try:
    s.sendto(bytes([18, 3]), ("127.0.0.1", master_port))
    d, _ = s.recvfrom(2048)
    check("the directory answered a list query", d[0] == 19)
    check("it listed one server", d[1] == 1 and d[2] == 1)
    entry = d[3:3 + 4 + 2 + 4 + SRVNAME_BYTES + ROOM_BYTES]
    port = struct.unpack("<H", entry[4:6])[0]
    listed = entry[10:10 + SRVNAME_BYTES].rstrip(b"\0").decode()
    check(f"with the right port ({port})", port == server_port)
    check(f"and the right name ({listed!r})", listed == "CI smoke test")
except socket.timeout:
    check("the directory answered a list query", False)

sys.exit(0 if ok else 1)
PY

if [ "$FAILED" -ne 0 ]; then
  echo
  echo "--- server log ---"; cat "$WORK/server.log"
  echo "--- directory log ---"; cat "$WORK/master.log"
  exit 1
fi
echo "the dedicated server and the directory both work in this build"
