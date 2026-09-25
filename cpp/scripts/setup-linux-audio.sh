#!/usr/bin/env bash
# libpulse for the Linux build under WSLg, without sudo: the packages are
# unpacked into ~/.local/sysroot and their run paths pointed next to
# themselves (patchelf from the fp-tools venv), so the RPATH build-linux.sh
# gives the binary finds them when miniaudio loads libpulse at run time.
set -euo pipefail
SYSROOT="$HOME/.local/sysroot"
LIB="$SYSROOT/usr/lib/x86_64-linux-gnu"
VENV="$HOME/.local/share/fp-tools"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
apt download libpulse0 libsndfile1 libasyncns0 libflac14 libvorbis0a libvorbisenc2 libopus0 libogg0 libmpg123-0t64 libmp3lame0
for deb in *.deb; do dpkg -x "$deb" "$SYSROOT"; done
"$VENV/bin/pip" install -q patchelf
"$VENV/bin/patchelf" --set-rpath '$ORIGIN:$ORIGIN/pulseaudio' "$(readlink -f "$LIB/libpulse.so.0")"
"$VENV/bin/patchelf" --set-rpath '$ORIGIN/..' "$LIB"/pulseaudio/libpulsecommon-*.so
echo "libpulse ready in $LIB"
