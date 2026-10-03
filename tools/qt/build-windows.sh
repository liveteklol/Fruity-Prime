#!/usr/bin/env bash
# Cross-compile the Qt flavour (FRUITY_UI=qt) for Windows x64 from Linux, and
# lay out a runnable folder (exe + Qt DLLs, plugins, QML imports, libc++).
# Nothing is installed on Windows. User-space tools:
#   llvm-mingw   ~/.local/fp-zwin-tc/llvm-mingw-*          (github.com/mstorsjo/llvm-mingw, ucrt)
#   Qt 6 Windows ~/.local/fp-win/Qt/<ver>/llvm-mingw_64    (aqt install-qt windows desktop <ver> win64_llvm_mingw)
#   Qt 6 host    ~/Qt/<ver>/gcc_64                          (moc, qmlcachegen, ...)
#   deps         ~/.local/fp-zwin                           (static curl, libarchive, openal, glfw, zlib)
# DEPLOY_DIR=<dir> copies the folder there (e.g. /mnt/c/fruityprime).
set -euo pipefail

HERE="$(cd "$(dirname "$0")/../.." && pwd)"
export LLVM_MINGW="${LLVM_MINGW:-$(ls -d "$HOME"/.local/fp-zwin-tc/llvm-mingw-* | sort -V | tail -1)}"
export QT_WIN="${QT_WIN:-$(ls -d "$HOME"/.local/fp-win/Qt/6.*/llvm-mingw_64 | sort -V | tail -1)}"
export FP_WIN_DEPS="${FP_WIN_DEPS:-$HOME/.local/fp-zwin}"
QT_HOST="${QT_HOST:-$(ls -d "$HOME"/Qt/6.*/gcc_64 | sort -V | tail -1)}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${BUILD_DIR:-$HERE/build/windows-qt-$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')}"
OUT="$BUILD_DIR/dist"

export PATH="$HOME/.local/share/fp-tools/bin:$PATH"

# Sources include <Windows.h> (MSVC spelling); MinGW ships windows.h and Linux
# file names are case-sensitive.
COMPAT="$BUILD_DIR/mingw-compat"
mkdir -p "$COMPAT"
for h in Windows TlHelp32; do
    ln -sf "$LLVM_MINGW/x86_64-w64-mingw32/include/${h,,}.h" "$COMPAT/$h.h"
done

cmake -S "$HERE" -B "$BUILD_DIR" -G Ninja \
    -DFRUITY_UI=qt \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$HERE/tools/qt/mingw-toolchain.cmake" \
    -DQT_HOST_PATH="$QT_HOST" \
    -DCMAKE_PREFIX_PATH="$QT_WIN;$FP_WIN_DEPS" \
    -DCMAKE_CXX_FLAGS="-isystem $COMPAT" \
    -DCMAKE_C_FLAGS="-isystem $COMPAT" \
    -DCMAKE_CXX_STANDARD_LIBRARIES="-lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32 -lwinmm -lavrt"
cmake --build "$BUILD_DIR" "$@"

# The runnable folder.
rm -rf "$OUT"
mkdir -p "$OUT/plugins/platforms" "$OUT/qml"
cp "$BUILD_DIR/FruityPrime.exe" "$OUT/"
for f in fruity-prime-logo.png fruity-prime-mark.png launcher-bg.jpg ipcountry.bin.gz; do [ -f "$BUILD_DIR/$f" ] && cp "$BUILD_DIR/$f" "$OUT/"; done
for m in Core Gui Network OpenGL Qml QmlMeta QmlModels QmlWorkerScript Quick QuickShapes QuickEffects Svg; do
    [ -f "$QT_WIN/bin/Qt6$m.dll" ] && cp "$QT_WIN/bin/Qt6$m.dll" "$OUT/"
done
cp "$QT_WIN/plugins/platforms/qwindows.dll" "$OUT/plugins/platforms/"
for p in imageformats; do
    [ -d "$QT_WIN/plugins/$p" ] && cp -r "$QT_WIN/plugins/$p" "$OUT/plugins/"
done
rm -f "$OUT"/plugins/*/*d.dll.debug 2>/dev/null || true
mkdir -p "$OUT/qml/QtQuick"
cp "$QT_WIN/qml/QtQuick/qmldir" "$QT_WIN"/qml/QtQuick/*.qmltypes "$OUT/qml/QtQuick/" 2>/dev/null || true
cp "$QT_WIN"/qml/QtQuick/*.dll "$OUT/qml/QtQuick/" 2>/dev/null || true
for d in Effects Shapes Window; do
    [ -d "$QT_WIN/qml/QtQuick/$d" ] && cp -r "$QT_WIN/qml/QtQuick/$d" "$OUT/qml/QtQuick/"
done
[ -d "$QT_WIN/qml/QtQml" ] && cp -r "$QT_WIN/qml/QtQml" "$OUT/qml/"
# Unused modules whose plugins would pull in more Qt DLLs.
rm -rf "$OUT/qml/QtQml/XmlListModel" "$OUT/qml/QtQuick/Shapes/DesignHelpers"
printf '[Paths]\nPlugins = plugins\nQmlImports = qml\n' > "$OUT/qt.conf"
for dll in libc++.dll libunwind.dll; do
    cp "$LLVM_MINGW/x86_64-w64-mingw32/bin/$dll" "$OUT/"
done
# Anything else the exe or Qt pulls in from the toolchain or deps.
find "$BUILD_DIR" -maxdepth 1 -name '*.dll' -exec cp {} "$OUT/" \;
cp -r "$BUILD_DIR"/*.png "$OUT/" 2>/dev/null || true

if [ -n "${DEPLOY_DIR:-}" ]; then
    mkdir -p "$DEPLOY_DIR"
    cp -r "$OUT"/. "$DEPLOY_DIR"/
    echo "deployed: $DEPLOY_DIR"
fi
echo "built: $OUT/FruityPrime.exe"
