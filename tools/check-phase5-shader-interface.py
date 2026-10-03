#!/usr/bin/env python3
"""Phase 5 static audit: explicit vertex inputs and the shader interface.

Fails when
  * any shader source string reads a GLSL built-in vertex attribute,
  * the OpenGL ES shaders' layout locations disagree with the semantic table,
  * EsShaders' recorded hashes of the desktop shaders are stale (the Android
    head throws at its first shader compile when they are),
  * a constant group that moved behind ShaderConstantSink everywhere (lights,
    fog, the cel pass, the matrix stack) is uploaded through a raw uniform
    location again from outside the OpenGL backend. Material values are not
    in the list yet: SetHudLayerUniforms still resets a subset of them (not
    the alpha) directly.
"""

from __future__ import annotations

from scene_shader_abi import read_contract, read_programs
import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
NATIVE = ROOT / "src" / "MphRead.Native"
SEMANTICS = NATIVE / "NativeRuntime" / "Rhi" / "VertexSemantics.hpp"
DESKTOP_SHADERS = NATIVE / "Shaders.cpp"
ES_SHADERS = NATIVE / "Mods" / "Render" / "EsShaders.cpp"
OPENGL_BACKEND = NATIVE / "NativeRuntime" / "Rhi" / "OpenGL"

BUILTIN = re.compile(r"\bgl_(Vertex|Normal|Color|MultiTexCoord\d|SecondaryColor|FogCoord)\b")
SHADER_BLOCK = re.compile(r'R"(shader|glsl)\((.*?)\)\1"', re.S)
CONVENTIONAL = re.compile(
    r"\b(?:GL::(?:EnableClientState|DisableClientState|VertexPointer|ColorPointer|NormalPointer|TexCoordPointer|ClientActiveTexture)"
    r"|::gl(?:EnableClientState|DisableClientState|VertexPointer|ColorPointer|NormalPointer|TexCoordPointer|ClientActiveTexture|Color[34]f|Normal3f|TexCoord[23]f))\s*\(")
SEMANTIC_ORDER = ("a_position", "a_normal", "a_color", "a_texcoord", "a_texcoord1")
MIGRATED = re.compile(
    r"_shaderLocations->(Light[12](Vector|Color)|Fog(Color|MinDistance|MaxDistance)"
    r"|Cel(TexelWidth|TexelHeight|Outline|NearPlane|FarPlane|DepthQuantum|Probe)"
    r"|MatrixStack|Shift(Table|Index|Factor)|LerpFactor|Whiteout(Table|Factor)"
    r"|View(Width|Height)|UseMask|FadeColor|LayerAlpha)\b")
# DrawMovieFrame sends LayerAlpha and FadeColor through the *integer* uniform
# calls on purpose: both are float uniforms, GL rejects the call, and the
# movie keeps the fade set earlier in the frame -- the upstream behaviour,
# reproduced. A float setter would change the picture, so these two stay.
ALLOWED = {
    ("Movie.cpp", "GL::Uniform1(_shaderLocations->LayerAlpha, 1);"),
    ("Movie.cpp", "GL::Uniform4(_shaderLocations->FadeColor, 0, 0, 0, 1);"),
}


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8").replace("\r\n", "\n")


def table(text: str, name: str) -> list[int]:
    match = re.search(name + r"\{\s*([0-9U,\s]+)\}", text)
    if match is None:
        raise SystemExit(f"FAIL: {SEMANTICS.name} has no {name}")
    return [int(v.strip().rstrip("U")) for v in match.group(1).split(",") if v.strip()]


def main() -> int:
    errors: list[str] = []
    scanned = 0
    for path in sorted(NATIVE.rglob("*.[ch]pp")):
        text = read(path)
        for block in SHADER_BLOCK.finditer(text):
            scanned += 1
            for hit in BUILTIN.finditer(block.group(2)):
                errors.append(f"{path.relative_to(ROOT)}: shader reads built-in {hit.group(0)}")
        if path.name != "GLAndroid.cpp":
            for line_no, line in enumerate(text.splitlines(), 1):
                if CONVENTIONAL.search(line):
                    errors.append(f"{path.relative_to(ROOT)}:{line_no}: desktop conventional vertex input")
        if OPENGL_BACKEND not in path.parents:
            for line_no, line in enumerate(text.splitlines(), 1):
                if (path.name, line.strip()) in ALLOWED:
                    continue
                if "GL::Uniform" in line and MIGRATED.search(line):
                    errors.append(
                        f"{path.relative_to(ROOT)}:{line_no}: uploads a ShaderConstantSink "
                        f"group through a raw uniform location")

    try:
        logical = read_contract()
        read_programs(DESKTOP_SHADERS, logical)
    except ValueError as error:
        errors.append(f"Logical scene ABI: {error}")

    semantics = read(SEMANTICS)
    if table(semantics, "OpenGlDesktopLocations") != table(semantics, "VulkanLocations"):
        errors.append("Desktop OpenGL and Vulkan vertex locations must agree")
    es_table = dict(zip(SEMANTIC_ORDER, table(semantics, "OpenGlEsLocations")))
    es = read(ES_SHADERS)
    for location, name in re.findall(r"layout\(location = (\d+)\) in \w+ (a_\w+);", es):
        if name in es_table and es_table[name] != int(location):
            errors.append(
                f"EsShaders.cpp declares {name} at {location}, the table says {es_table[name]}")

    desktop = read(DESKTOP_SHADERS)
    sources = dict(re.findall(r'const std::string Shaders::(\w+) = R"shader\((.*?)\)shader";', desktop, re.S))
    for name, expected in re.findall(r'Check\("(\w+)", Shaders::\w+,\s*"([0-9a-f]{64})"\)', es):
        actual = hashlib.sha256(sources[name].encode("utf-8")).hexdigest()
        if actual != expected:
            errors.append(f"EsShaders.cpp records a stale hash for Shaders::{name} ({actual} now)")

    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print(f"Phase 5 shader interface audit passed: {scanned} shader sources, "
          f"no built-in or conventional vertex inputs, desktop/Vulkan locations agree, ES unchanged.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
