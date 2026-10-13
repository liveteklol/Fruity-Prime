#!/usr/bin/env python3
"""Classify native qualified GL references; reject unclassified dependencies.

OpenGL backend, Skia interop and named diagnostic files are the only allowed
owners. Comments and literals are reported separately and are not dependencies.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
NATIVE = ROOT / 'src/MphRead.Native'
GL = re.compile(r'\bGL::')
NON_CODE = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/'
    r'|(?:u8|u|U|L)?R"(?P<raw>[^\s()\\]{0,16})\([\s\S]*?\)(?P=raw)"'
    r'|(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"'
    r"|(?<![A-Za-z0-9_])(?:u8|u|U|L)?'(?:\\[\s\S]|[^'\\])*'"
)
WRAPPER = {'NativeRuntime/OpenTK/GL.cpp', 'NativeRuntime/OpenTK/GL.hpp',
           'NativeRuntime/OpenTK/GLAndroid.cpp'}
SKIA = {'NativeRuntime/Skia/SkiaGpu.cpp', 'Mods/Render/UiOverlay.cpp'}
DIAGNOSTICS = {
    'Mods/Diagnostics/ThumbnailWindowCheck.cpp',
    'Mods/Diagnostics/LauncherWindowCheck.cpp',
    'Mods/Diagnostics/GpuLifetimeCheck.cpp',
    'Mods/MapGen/AltFormProbe.cpp',
    'Mods/ScreenCapture.cpp', 'Mods/ThumbnailCapture.cpp', 'Mods/ThumbnailCapture.hpp',
    'Mods/Network/Testing/Maps/MapAudit.cpp', 'Mods/Network/Testing/Harness/NetCheckClient.cpp',
    'Mods/Network/Testing/Combat/WeaponDps.cpp',
}

def category(path):
    if path.startswith('NativeRuntime/Rhi/OpenGL/') or path in WRAPPER:
        return 'A'
    if path in SKIA:
        return 'B'
    if path in DIAGNOSTICS:
        return 'C'
    return 'D'

def main():
    total = {key: 0 for key in 'ABCD'}
    files = 0
    for path in sorted(NATIVE.rglob('*')):
        if path.suffix not in {'.cpp', '.hpp'}:
            continue
        files += 1
        source = path.read_text(encoding='utf-8-sig')
        raw = len(GL.findall(source))
        if not raw:
            continue
        code = NON_CODE.sub(lambda match: re.sub(r'[^\n]', ' ', match[0]), source)
        hits = list(GL.finditer(code))
        relative = path.relative_to(NATIVE).as_posix()
        kind = category(relative)
        total[kind] += len(hits)
        print(f'{kind if hits else "text-only"} {relative}: code={len(hits)} text={raw - len(hits)}')
        if kind == 'D':
            for hit in hits:
                number = code.count('\n', 0, hit.start()) + 1
                print(f'FAIL: {relative}:{number}: unclassified qualified OpenGL dependency')
    print(f'Phase 11: {files} C++ files; ' + ', '.join(f'{kind}={count}' for kind, count in total.items()))
    return 1 if total['D'] else 0

if __name__ == '__main__':
    sys.exit(main())
