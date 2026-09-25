#!/usr/bin/env bash
# Cross-build the Windows x86-64 binary from Linux, nothing installed on Windows.
#   Toolchain: llvm-mingw 20231128 (UCRT) -- the one Qt's win64_llvm_mingw build uses
#   Qt:        6.11.2 win64_llvm_mingw (qtbase, qtdeclarative) + the Linux Qt for host tools
# Everything is fetched into ~/.cache/fp-win and unpacked under ~/.local/fp-win.
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
QT_VERSION=6.11.2
QT_TAG=6112
QT_BUILD=202608131017
LLVM_MINGW=llvm-mingw-20231128-ucrt-ubuntu-20.04-x86_64
CACHE="$HOME/.cache/fp-win"
PREFIX="$HOME/.local/fp-win"
QT_HOST="${QT_HOST:-$HOME/Qt/$QT_VERSION/gcc_64}"
QT_WIN="$PREFIX/Qt/$QT_VERSION/llvm-mingw_64"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${BUILD_DIR:-$HERE/build/windows-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')}"
DIST="${DIST:-$HERE/build/dist/FruityPrime-windows-x64}"
VENV="$HOME/.local/share/fp-tools"
export PATH="$VENV/bin:$PATH"
mkdir -p "$CACHE" "$PREFIX"

fetch() { [ -f "$CACHE/$2" ] || curl -sfL -o "$CACHE/$2" "$1"; }

REPO="https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_$QT_TAG/qt6_${QT_TAG}_llvm_mingw/qt.qt6.$QT_TAG.win64_llvm_mingw"
ARCHIVES=(
    "$QT_VERSION-0-${QT_BUILD}qtbase-Windows-Windows_11_24H2-Clang-Windows-Windows_11_24H2-X86_64.7z"
    "$QT_VERSION-0-${QT_BUILD}qtdeclarative-Windows-Windows_11_24H2-Clang-Windows-Windows_11_24H2-X86_64.7z"
    "$QT_VERSION-0-${QT_BUILD}llvm-mingw-20231128-ucrt-x86_64-runtime.7z"
)
for a in "${ARCHIVES[@]}"; do fetch "$REPO/$a" "$a"; done
fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/20231128/$LLVM_MINGW.tar.xz" llvm-mingw.tar.xz

if [ ! -x "$PREFIX/$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++" ]; then
    echo "unpacking llvm-mingw"
    tar -xJf "$CACHE/llvm-mingw.tar.xz" -C "$PREFIX"
fi
if [ ! -f "$QT_WIN/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "unpacking Qt $QT_VERSION llvm-mingw"
    python3 - "$CACHE" "$PREFIX/qt-unpack" "${ARCHIVES[@]}" <<'PY'
import sys, py7zr, pathlib
cache, dest, *names = sys.argv[1:]
pathlib.Path(dest).mkdir(parents=True, exist_ok=True)
for n in names:
    with py7zr.SevenZipFile(f"{cache}/{n}") as z:
        z.extractall(dest)
PY
    # The archives unpack to <version>/llvm-mingw_64/ (Qt) and bin/ (runtime DLLs).
    src="$(dirname "$(find "$PREFIX/qt-unpack" -path '*/lib/cmake/Qt6/Qt6Config.cmake' | head -1)")/../../.."
    mkdir -p "$(dirname "$QT_WIN")"
    rm -rf "$QT_WIN"
    mv "$(realpath "$src")" "$QT_WIN"
    [ -d "$PREFIX/qt-unpack/bin" ] && cp -n "$PREFIX/qt-unpack/bin/"*.dll "$QT_WIN/bin/" 2>/dev/null || true
    rm -rf "$PREFIX/qt-unpack"
fi

VK_SDK_DIR="${VK_SDK_DIR:-$(ls -d "$HOME"/vulkan/*/ 2>/dev/null | sort -V | tail -1)}"
set +u; source "${VK_SDK_DIR%/}/setup-env.sh" >/dev/null; set -u

export LLVM_MINGW_ROOT="$PREFIX/$LLVM_MINGW"
cmake -S "$HERE" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$HERE/cmake/mingw-w64-x86_64.cmake" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DCMAKE_PREFIX_PATH="$QT_WIN" \
    -DVULKAN_HEADERS_DIR="$VULKAN_SDK/include" \
    -DVulkan_INCLUDE_DIR="$VULKAN_SDK/include"
cmake --build "$BUILD_DIR"

# Deploy: what windeployqt would copy for a QtGui-only app.
rm -rf "$DIST"
mkdir -p "$DIST/platforms"
cp "$BUILD_DIR/FruityPrime.exe" "$DIST/"
# Every Qt DLL the exe needs, and every one those need (Qml and Quick for the screens).
copy_deps() {
    "$LLVM_MINGW_ROOT/bin/llvm-readobj" --coff-imports "$1" 2>/dev/null | sed -n 's/^ *Name: \(.*\.dll\)$/\1/p' | while read -r dll; do
        if [ -f "$QT_WIN/bin/$dll" ] && [ ! -f "$DIST/$dll" ]; then
            cp "$QT_WIN/bin/$dll" "$DIST/"
            copy_deps "$DIST/$dll"
        fi
    done
}
copy_deps "$BUILD_DIR/FruityPrime.exe"
# The QML modules the screens import, and their plugins' own dependencies.
mkdir -p "$DIST/qml"
for module in QtQuick QtQml; do
    cp -r "$QT_WIN/qml/$module" "$DIST/qml/"
done
find "$DIST/qml" -name "*.dll" | while read -r plugin; do copy_deps "$plugin"; done
cp "$QT_WIN/plugins/platforms/qwindows.dll" "$DIST/platforms/"
for rt in libc++.dll libunwind.dll; do
    if [ -f "$QT_WIN/bin/$rt" ]; then cp "$QT_WIN/bin/$rt" "$DIST/"; else cp "$LLVM_MINGW_ROOT/x86_64-w64-mingw32/bin/$rt" "$DIST/"; fi
done
"$LLVM_MINGW_ROOT/bin/llvm-strip" "$DIST/FruityPrime.exe"
echo "built: $DIST"
ls -la "$DIST"
