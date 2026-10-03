#!/usr/bin/env bash
# Configure and build the Qt flavour (FRUITY_UI=qt) on Linux with user-space
# tools only (no sudo):
#   CMake/Ninja  ~/.local/share/fp-tools   (python venv: pip install cmake ninja aqtinstall)
#   Qt 6         ~/Qt/<ver>/gcc_64         (aqt install-qt linux desktop <ver> linux_gcc_64 -O ~/Qt)
#   Dev packages ~/.local/sysroot          (apt download <pkg> && dpkg -x <deb> ~/.local/sysroot):
#                libgl-dev libicu-dev libicu78 libcurl4-openssl-dev libarchive-dev
#                libarchive13t64 libopenal-dev libopenal1 libglfw3-dev libglfw3
# Extra arguments go to `cmake --build` (e.g. a target name).
set -euo pipefail

HERE="$(cd "$(dirname "$0")/../.." && pwd)"
QT_DIR="${QT_DIR:-$(ls -d "$HOME"/Qt/6.*/gcc_64 2>/dev/null | sort -V | tail -1)}"
SYSROOT="${SYSROOT:-$HOME/.local/sysroot/usr}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${BUILD_DIR:-$HERE/build/linux-qt-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')}"
LIBDIR="$SYSROOT/lib/x86_64-linux-gnu"

export PATH="$HOME/.local/share/fp-tools/bin:$PATH"

cmake -S "$HERE" -B "$BUILD_DIR" -G Ninja \
    -DFRUITY_UI=qt \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$QT_DIR;$SYSROOT" \
    -DCMAKE_LIBRARY_PATH="$LIBDIR" \
    -DCMAKE_INCLUDE_PATH="$SYSROOT/include" \
    -DCMAKE_BUILD_RPATH="$LIBDIR;$LIBDIR/pulseaudio;$QT_DIR/lib" \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,--disable-new-dtags -Wl,-rpath-link,$LIBDIR"
cmake --build "$BUILD_DIR" "$@"
echo "built: $BUILD_DIR/FruityPrime"
