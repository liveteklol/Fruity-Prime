#!/usr/bin/env bash
# The Windows build into C:\fruityprime, which is where it is run from: the
# exe, its Qt DLLs and the QML, beside the game files already there. The
# extracted files and paths.txt are left alone.
set -euo pipefail
DEST="${DEST:-/mnt/c/fruityprime}"
DIST="$(cd "$(dirname "$0")/.." && pwd)/build/dist/FruityPrime-windows-x64"
[ -d "$DIST" ] || { echo "build it first: scripts/build-windows.sh" >&2; exit 1; }
mkdir -p "$DEST/screenshots"
cp -r "$DIST"/* "$DEST/"
if [ ! -f "$DEST/paths.txt" ]; then
    printf 'AMHP1=C:\\fruityprime\\files\\AMHP1\r\n' > "$DEST/paths.txt"
    echo "wrote $DEST/paths.txt -- put the extracted game files in $DEST/files/AMHP1"
fi
echo "deployed to $DEST"
