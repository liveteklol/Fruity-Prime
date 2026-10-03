#!/usr/bin/env python3
"""The RHI freeze audit (Phase 26), as a check CI runs.

1. The common RHI names no raw API type: everything under Rhi/ outside
   the OpenGL/ and Vulkan/ backend directories (comments excluded).
2. The renderer frontend (Renderer.cpp/.hpp) makes no GL or Vulkan call.
3. Native GL and Vulkan calls live only in their backend directories, the
   Skia interop, and the files below, each with the reason it is allowed.

Exit 0 = clean. Usage: check-rhi-isolation.py [REPO_ROOT]
"""
from __future__ import annotations

import pathlib
import re
import sys

RAW_TYPE = re.compile(r"\bVk[A-Z]\w*|\bvk[A-Z]\w*|\bGLuint\b|OpenGL::GL\b|\bID3D12\w*|\bMTL[A-Z]\w*")
FRONTEND_CALL = re.compile(r"\bGL::|\bVk[A-Z]\w*|\bvk[A-Z]\w*")
NATIVE_CALL = re.compile(r"\bGL::[A-Za-z]+\(|\bgl[A-Z][a-zA-Z]+\(|\bvk[A-Z][a-zA-Z]+\(")

BACKEND_DIRS = ("NativeRuntime/Rhi/OpenGL/", "NativeRuntime/Rhi/Vulkan/", "NativeRuntime/Skia/")
ALLOWED = {
    # The GL binding itself: the functions the OpenGL backend calls.
    "MphRead.Native/NativeRuntime/OpenTK/GL.cpp": "the OpenGL binding",
    "MphRead.Native/NativeRuntime/OpenTK/GLAndroid.cpp": "the OpenGL ES binding",
    "MphRead.Native/Mods/Render/GlEs.cpp": "the GLES emulation of fixed-function GL, part of the binding",
    # Android's platform layer: the EGL context and its GLES surface.
    "MphRead.Native.Android/GameView.cpp": "Android platform layer (EGL context, GLES surface)",
    "MphRead.Native.Android/AndroidUiOverlay.cpp": "Android platform layer (GLES overlay; Vulkan goes through the RHI)",
    "MphRead.Native.Android/AndroidHunterShot.cpp": "Android platform layer (GLES capture)",
    # Instruments whose subject is the OpenGL window itself.
    "MphRead.Native/Mods/Diagnostics/LauncherWindowCheck.cpp": "OpenGL window instrument",
    "MphRead.Native/Mods/Diagnostics/ThumbnailWindowCheck.cpp": "OpenGL window instrument",
}


def code(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def sources(root: pathlib.Path):
    for folder in ("MphRead.Native", "MphRead.Native.Android"):
        for path in (root / "src" / folder).rglob("*"):
            if path.suffix in (".cpp", ".hpp", ".inc", ".h") and path.is_file():
                yield path.relative_to(root / "src").as_posix(), path


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent)
    problems = []
    for name, path in sources(root):
        text = code(path.read_text(encoding="utf-8", errors="replace"))
        inside = "/".join(name.split("/")[1:])
        in_backend = inside.startswith(BACKEND_DIRS)
        if inside.startswith("NativeRuntime/Rhi/") and not in_backend:
            for match in RAW_TYPE.finditer(text):
                problems.append(f"{name}: raw API type {match.group(0)} in the common RHI")
        if inside in ("Renderer.cpp", "Renderer.hpp") and name.startswith("MphRead.Native/"):
            for match in FRONTEND_CALL.finditer(text):
                problems.append(f"{name}: {match.group(0)} in the renderer frontend")
        if not in_backend and name not in ALLOWED:
            for match in NATIVE_CALL.finditer(text):
                problems.append(f"{name}: native call {match.group(0)} outside a backend")
    for problem in problems:
        print(f"FAIL {problem}")
    print(f"{'PASS' if not problems else 'FAIL'} RHI isolation: {len(problems)} finding(s), "
          f"{len(ALLOWED)} allowed files")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
