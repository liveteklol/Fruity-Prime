#!/usr/bin/env python3
"""Cross-backend parity: the same captures drawn by OpenGL and by Vulkan.

Usage: validate-cross-backend-parity.py OPENGL_DIR VULKAN_DIR [--name-filter SUBSTR]

Every PNG in OPENGL_DIR must exist in VULKAN_DIR at the same size, and the
pair must pass. The tolerance is defined once, here, and is the one the
Phase 24 gate records:

Refused outright (a broken backend, never a driver difference):
  - a black or near-black frame on either side (under 1% of pixels lit);
  - flipped output: the pair must match better than OpenGL matches Vulkan
    turned upside down, by a wide margin;
  - offset, missing or wrong geometry, textures or UVs: the mean per-channel
    difference must stay under 1 level.

Tolerated (the implementation-defined tie-breaking measured in Phases 17-18):
  - at most 0.05% of pixels differing by more than 8 levels, and
  - every connected group of those pixels (8-neighbour) at most 2 pixels
    thick, i.e. an edge or a single texel tie and never an area.
  - a differing pixel that equals the other backend's pixel one step away
    (8-neighbour) is a nearest-sampling tie on a magnified texture -- a texel
    boundary landing exactly on a pixel centre, rounded the other way -- and
    is not counted towards the 0.05%. It still counts towards the mean and
    the thickness rule, so a real shift of an area cannot hide behind it.

Requires Pillow.
"""
from __future__ import annotations

import argparse
import pathlib
import sys

from PIL import Image, ImageChops

LIT_MINIMUM = 0.01
MEAN_LEVEL_MAXIMUM = 1.0
DIFFERING_FRACTION_MAXIMUM = 0.0005
DIFFERING_LEVEL = 8
EDGE_THICKNESS_MAXIMUM = 2
FLIP_MARGIN = 4.0


def lit_fraction(image: Image.Image) -> float:
    gray = image.convert("L").get_flattened_data()
    return sum(1 for value in gray if value > 8) / max(1, len(gray))


def mean_level(a: Image.Image, b: Image.Image) -> float:
    data = ImageChops.difference(a, b).get_flattened_data()
    return sum(sum(pixel) for pixel in data) / max(1, 3 * len(data))


def components(mask: list[bool], width: int, height: int) -> list[tuple[int, int]]:
    """(thickness in x, thickness in y) of every 8-connected group."""
    seen = bytearray(len(mask))
    found = []
    for start, set_ in enumerate(mask):
        if not set_ or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        xs, ys = [], []
        while stack:
            index = stack.pop()
            x, y = index % width, index // width
            xs.append(x)
            ys.append(y)
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < width and 0 <= ny < height:
                        neighbour = ny * width + nx
                        if mask[neighbour] and not seen[neighbour]:
                            seen[neighbour] = 1
                            stack.append(neighbour)
        found.append((max(xs) - min(xs) + 1, max(ys) - min(ys) + 1, len(xs)))
    return found


def thickness(group: tuple[int, int, int]) -> int:
    """How thick a group is: its pixel count over its longer extent, rounded up."""
    width, height, count = group
    return -(-count // max(width, height))


def check(gl_path: pathlib.Path, vk_path: pathlib.Path) -> list[str]:
    a = Image.open(gl_path).convert("RGB")
    b = Image.open(vk_path).convert("RGB")
    if a.size != b.size:
        return [f"size {a.size} against {b.size}"]
    problems = []
    for side, image in (("opengl", a), ("vulkan", b)):
        if lit_fraction(image) < LIT_MINIMUM:
            problems.append(f"{side} frame is black")
    mean = mean_level(a, b)
    if mean > MEAN_LEVEL_MAXIMUM:
        problems.append(f"mean difference {mean:.3f} levels > {MEAN_LEVEL_MAXIMUM}")
    flipped = mean_level(a, b.transpose(Image.Transpose.FLIP_TOP_BOTTOM))
    if flipped < FLIP_MARGIN * max(mean, 0.05):
        problems.append(f"no better than flipped (mean {mean:.3f}, flipped {flipped:.3f})")
    width, height = a.size
    difference = ImageChops.difference(a, b).get_flattened_data()
    mask = [max(pixel) > DIFFERING_LEVEL for pixel in difference]
    pa, pb = a.load(), b.load()

    def tie(index: int) -> bool:
        x, y = index % width, index // width
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                nx, ny = x + dx, y + dy
                if (dx or dy) and 0 <= nx < width and 0 <= ny < height:
                    if max(abs(p - q) for p, q in zip(pa[x, y], pb[nx, ny])) <= DIFFERING_LEVEL:
                        return True
        return False

    ties = sum(1 for index, set_ in enumerate(mask) if set_ and tie(index))
    differing = sum(mask) - ties
    fraction = differing / (width * height)
    if fraction > DIFFERING_FRACTION_MAXIMUM:
        problems.append(f"{differing} pixels ({fraction:.4%}) differ by > {DIFFERING_LEVEL}")
    thick = [group for group in components(mask, width, height) if thickness(group) > EDGE_THICKNESS_MAXIMUM]
    if thick:
        worst = max(thick, key=lambda group: group[2])
        problems.append(f"{len(thick)} differing area(s) thicker than {EDGE_THICKNESS_MAXIMUM} px, "
                        f"largest {worst[0]}x{worst[1]} ({worst[2]} px)")
    note = f"{differing} px > {DIFFERING_LEVEL} ({fraction:.4%}) + {ties} sampling ties, mean {mean:.3f}, flipped {flipped:.2f}"
    return problems or [f"ok: {note}"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("opengl", type=pathlib.Path)
    parser.add_argument("vulkan", type=pathlib.Path)
    parser.add_argument("--name-filter", default="")
    args = parser.parse_args()
    images = sorted(path for path in args.opengl.glob("*.png") if args.name_filter in path.name)
    if not images:
        print(f"FAIL: no PNG in {args.opengl}")
        return 1
    failed = 0
    for path in images:
        other = args.vulkan / path.name
        if not other.exists():
            print(f"FAIL {path.name}: missing on the Vulkan side")
            failed += 1
            continue
        result = check(path, other)
        if result[0].startswith("ok:"):
            print(f"PASS {path.name}: {result[0][4:]}")
        else:
            failed += 1
            for problem in result:
                print(f"FAIL {path.name}: {problem}")
    print(f"{'PASS' if failed == 0 else 'FAIL'} cross-backend parity: {len(images) - failed}/{len(images)}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
