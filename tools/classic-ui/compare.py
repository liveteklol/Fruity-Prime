"""Lay the port's ticks (-classicframes) against the recomp's frames (recomp.py frames).

usage: compare.py PORTDIR RECOMPDIR OUT.png [--offset=F | --fit=LO:HI] [--ticks=a,b,c] [--split=T,B] [--scale=N]

Port tick k is laid against recomp frame offset + 2k: a menu tick is two DS
frames. Without --offset, the offset that matches ticks LO..HI best is used --
pick a stretch with something moving, and mind the game's loading pauses,
which the port does not have: align each page on its own entry. Each DS
screen is drawn every other frame; --split takes the top from frame F + T and
the bottom from F + B (0,1 by default). Prints the mean difference a screen
and writes port | recomp | difference for each tick.
"""
import glob
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw


def load_dir(d, pattern, rx):
    out = {}
    for f in glob.glob(os.path.join(d, pattern)):
        m = re.search(rx, os.path.basename(f))
        if m:
            out[int(m.group(1))] = f
    return out


def arr(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def main():
    port_dir, rc_dir, out = sys.argv[1], sys.argv[2], sys.argv[3]
    opts = dict(a.split("=", 1) for a in sys.argv[4:] if "=" in a)
    port = load_dir(port_dir, "t*.png", r"t(\d+)\.png")
    rc = load_dir(rc_dir, "f*.png", r"f(\d+)")
    cache = {}

    def get(path):
        if path not in cache:
            cache[path] = arr(path)
        return cache[path]

    if "--offset" in opts:
        offset = int(opts["--offset"])
    else:
        lo, hi = (int(x) for x in opts.get("--fit", "10:60").split(":"))
        best = None
        for off in range(min(rc) - 2 * lo, max(rc) - 2 * lo + 1):
            errs = []
            for k in range(lo, hi + 1):
                if k in port and off + 2 * k in rc:
                    errs.append(np.abs(get(port[k]) - get(rc[off + 2 * k])).mean())
            if len(errs) >= (hi - lo) // 2:
                e = float(np.mean(errs))
                if best is None or e < best[0]:
                    best = (e, off)
        offset = best[1]
        print(f"best offset {offset} (mean abs diff {best[0]:.2f})")
    # each DS screen is drawn every other frame: take each from the frame that drew it
    split = opts.get("--split", "0,1")
    top_off, bottom_off = (int(x) for x in split.split(","))

    def real(f):
        a, b = rc.get(f + top_off), rc.get(f + bottom_off)
        if a is None or b is None:
            return None
        out = get(a).copy()
        out[192:] = get(b)[192:]
        return out

    ticks = [int(t) for t in opts.get("--ticks", "0,10,20,30,40,60,100,140").split(",")]
    scale = int(opts.get("--scale", "1"))
    cols = []
    for k in ticks:
        f = offset + 2 * k
        b = real(f)
        if k not in port or b is None:
            continue
        a = get(port[k])
        d = np.clip(np.abs(a - b).sum(axis=2) * 2, 0, 255).astype(np.uint8)
        print(f"tick {k:4} frame {f}: mean abs diff top {np.abs(a - b)[:192].mean():6.2f} bottom {np.abs(a - b)[192:].mean():6.2f}")
        col = Image.new("RGB", (256 * 3 + 8, 384 + 14), (40, 0, 40))
        col.paste(Image.fromarray(a.astype(np.uint8)), (0, 14))
        col.paste(Image.fromarray(b.astype(np.uint8)), (260, 14))
        col.paste(Image.fromarray(d).convert("RGB"), (520, 14))
        ImageDraw.Draw(col).text((2, 1), f"port t{k}   |   recomp f{f}   |   diff", fill=(255, 255, 255))
        cols.append(col)
    if not cols:
        return
    sheet = Image.new("RGB", (cols[0].width, sum(c.height for c in cols)))
    y = 0
    for c in cols:
        sheet.paste(c, (0, y))
        y += c.height
    if scale > 1:
        sheet = sheet.resize((sheet.width * scale, sheet.height * scale), Image.NEAREST)
    sheet.save(out)


if __name__ == "__main__":
    main()
