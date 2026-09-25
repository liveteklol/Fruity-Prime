#!/usr/bin/env bash
# Configure and build the Linux binary with user-space tools (no sudo):
#   CMake/Ninja  ~/.local/share/fp-tools   (python venv: pip install cmake ninja aqtinstall)
#   Qt 6         ~/Qt/<ver>/gcc_64         (aqt install-qt linux desktop <ver> linux_gcc_64 -O ~/Qt)
#   Vulkan SDK   ~/vulkan/<ver>            (LunarG tarball)
#   GL dev files ~/.local/sysroot          (apt download libgl-dev ... && dpkg -x)
#   libpulse     ~/.local/sysroot          (scripts/setup-linux-audio.sh): loaded at run time by miniaudio
#                                           for WSLg's PulseAudio server, found through the binary's RPATH
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
QT_DIR="${QT_DIR:-$HOME/Qt/6.11.2/gcc_64}"
VK_SDK_DIR="${VK_SDK_DIR:-$(ls -d "$HOME"/vulkan/*/ 2>/dev/null | sort -V | tail -1)}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${BUILD_DIR:-$HERE/build/linux-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')}"

export PATH="$HOME/.local/share/fp-tools/bin:$PATH"
# shellcheck disable=SC1091
set +u; source "${VK_SDK_DIR%/}/setup-env.sh" >/dev/null; set -u

cmake -S "$HERE" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$QT_DIR;$HOME/.local/sysroot/usr" \
    -DCMAKE_BUILD_RPATH="$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu;$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu/pulseaudio" \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,--disable-new-dtags"
cmake --build "$BUILD_DIR"
echo "built: $BUILD_DIR/FruityPrime"
