#!/usr/bin/env bash
# Turn an installed Linux build (the fruity_package target's dist/) into the
# dedicated-server package: the tarball a server box downloads when it updates
# itself, and what an operator unpacks to start one.
#
#   tools/package-server-linux.sh build/native/dist FruityPrime-v0.11.0-server-linux-x64.tar.gz
#
# The servers are not the machine the build ran on. The Azure boxes are
# Ubuntu 22.04 (glibc 2.35) and the Pi is Debian 12 (glibc 2.36); the build
# needs whatever the CI runner's glibc is. So the package carries the binary's
# whole library closure -- glibc and its dynamic loader included -- and a
# launch script starts the game through that loader. Nothing is taken from the
# box but the kernel.
#
# Layout, and why it is this one:
#
#   FruityPrime        the launch script. Every existing systemd unit and the
#                      updaters (C# and C++) look for exactly this name at the
#                      top of the directory, and it is what they run.
#   bin/FruityPrime    the game, stripped.
#   lib/               its libraries, by soname, as plain files (no symlinks:
#                      the updaters copy files one by one).
#
# paths.txt, maprotation.txt, maps/, logs/ and files/ stay at the top of the
# directory, where the server used them before; the launch script tells the
# game that this directory, not lib/, is where it lives (FRUITY_LAUNCHER).
set -euo pipefail

DIST="${1:?usage: $0 DIST_DIR OUTPUT.tar.gz}"
OUT="${2:?usage: $0 DIST_DIR OUTPUT.tar.gz}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BIN="$DIST/bin/FruityPrime"
test -x "$BIN" || { echo "no $BIN -- build the fruity_package target first" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/bin" "$STAGE/lib"

# The binary and what install() put beside it (the country table the
# directory reads, the logos). qt.conf is the player's: it points Qt at
# plugins this package does not carry, and a server loads none.
for file in "$DIST/bin/"*; do
  case "$(basename "$file")" in
    qt.conf) ;;
    *) cp -L "$file" "$STAGE/bin/" ;;
  esac
done
# CI builds carry debug info (500 MB of it); a server downloads this package.
strip --strip-debug "$STAGE/bin/FruityPrime"

INTERP="$(readelf -l "$BIN" | sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p')"
test -n "$INTERP" || { echo "cannot read the program interpreter of $BIN" >&2; exit 1; }
LOADER="$(basename "$INTERP")"

# The closure, as the build machine's loader resolves it. The binary's
# RUNPATH ($ORIGIN/../lib) finds the Qt libraries in dist/lib.
ldd "$BIN" > "$STAGE/ldd.txt"
if grep -q 'not found' "$STAGE/ldd.txt"; then
  cat "$STAGE/ldd.txt" >&2
  echo "the build's own library closure is incomplete" >&2
  exit 1
fi
while read -r name arrow path _; do
  [ "$arrow" = "=>" ] || continue
  [ -f "$path" ] || continue
  cp -L "$path" "$STAGE/lib/$name"
done < "$STAGE/ldd.txt"
rm "$STAGE/ldd.txt"
cp -L "$INTERP" "$STAGE/lib/$LOADER"
chmod 0755 "$STAGE/lib/$LOADER"

cat > "$STAGE/FruityPrime" <<EOF
#!/bin/sh
# Fruity Prime dedicated server (and server directory) launcher.
#
# The game is bin/FruityPrime. It is started through the dynamic loader and C
# library shipped in lib/, so this package runs on a machine whose own glibc
# is older than the one it was built against. Run this file, not bin/.
here=\$(dirname "\$(readlink -f "\$0")")
# Where the game believes it lives: here, beside paths.txt and the rotation,
# rather than lib/, where the loader it is running under is.
FRUITY_LAUNCHER="\$here/FruityPrime"
export FRUITY_LAUNCHER
# A server has no sound card, and OpenAL would otherwise go looking for one.
: "\${ALSOFT_DRIVERS:=null}"
export ALSOFT_DRIVERS
exec "\$here/lib/$LOADER" --library-path "\$here/lib" "\$here/bin/FruityPrime" "\$@"
EOF
chmod 0755 "$STAGE/FruityPrime"

cp "$ROOT/SERVER.md" "$STAGE/SERVER.md"
cp "$ROOT/LICENSE" "$STAGE/LICENSE"

# The package has to resolve from what it carries alone: ask the bundled
# loader the way the launch script runs it, and accept nothing it found
# outside lib/ (the loader still falls back to this machine's directories,
# which a server box will not have the same of).
"$STAGE/lib/$LOADER" --inhibit-cache --library-path "$STAGE/lib" \
  --list "$STAGE/bin/FruityPrime" > "$STAGE/resolved.txt"
if grep '=>' "$STAGE/resolved.txt" | grep -v "=> $STAGE/lib/"; then
  echo "the package's library closure is incomplete (above: resolved elsewhere)" >&2
  exit 1
fi
rm "$STAGE/resolved.txt"

# mktemp made the top 0700, and tar would hand that to whoever unpacks it.
chmod 0755 "$STAGE" "$STAGE/bin" "$STAGE/lib"
mkdir -p "$(dirname "$OUT")"
tar -C "$STAGE" --owner=0 --group=0 -czf "$OUT" .
echo "$OUT: $(du -h "$OUT" | cut -f1), loader $LOADER, $(ls "$STAGE/lib" | wc -l) libraries"
