"""Dump the DS menu graph (frontend/metroidhunters.bin, MARM) the way MenuData.cpp reads it.

usage: menudump.py FILES_ROOT [PAGE...]

FILES_ROOT is an extracted game's files folder (paths.txt's AMHE0=..., say).
Every item: delay, place, initial state, the look for each state code (widget
model and animation, or text with its colours, duration and format bytes),
links, and actions with their touch point in DS touch-screen pixels.
"""
import os
import struct
import sys

STATES = ["none", "hidden", "idle", "sel", "focus", "dis", "s6", "any"]


class Reader:
    def __init__(self, data):
        self.d = data

    def u8(self, o): return self.d[o]
    def u16(self, o): return struct.unpack_from("<H", self.d, o)[0]
    def u32(self, o): return struct.unpack_from("<I", self.d, o)[0]
    def i32(self, o): return struct.unpack_from("<i", self.d, o)[0]
    def fx(self, o): return self.i32(o) / 4096.0

    def list(self, o):
        out = []
        while o:
            v = self.u32(o)
            if v == 0:
                break
            out.append(v)
            o += 4
        return out

    def cstr(self, o):
        return self.d[o:self.d.index(b"\0", o)].decode("latin1")


def action(r, o):
    return dict(kind=r.u16(o), f2=r.u8(o + 2), flags=r.u8(o + 3), f4=r.u32(o + 4),
                rect=struct.unpack_from("<hhhh", r.d, o + 8),
                calls=[(r.i32(p), r.i32(p + 4)) for p in r.list(r.u32(o + 16))],
                item=r.u16(o + 20), page=r.u8(o + 22), f17=r.u8(o + 23))


def item(r, o):
    states = []
    for so in r.list(r.u32(o + 12)):
        kind, code, value = r.u8(so), r.u8(so + 1), r.u32(so + 4)
        if kind == 1:
            style = r.u32(value + 4)
            states.append(dict(code=code, text=r.u32(value) & 0xFFFF, start=r.u32(style), end=r.u32(style + 4),
                               wrap=r.u16(style + 8), dur=r.fx(style + 12), format=r.d[style + 16:style + 20]))
        else:
            w = value & 0xFFFF
            states.append(dict(code=code, widget=-1 if w == 0xFFFF else w))
    return dict(index=r.u16(o + 54), delay=r.fx(o + 20), x=r.fx(o + 32), y=r.fx(o + 36), depth=r.fx(o + 40),
                init=r.u8(o + 52), states=states,
                links=[(r.u8(lo), r.u8(lo + 1), r.u16(lo + 2)) for lo in r.list(r.u32(o + 8))],
                actions=[action(r, ao) for ao in r.list(r.u32(o + 4))])


def parse(data):
    r = Reader(data)
    widgets = []
    for o in r.list(r.u32(12)):
        widgets.append([r.cstr(r.u32(f + 8)) if r.u32(f + 8) else None for f in r.list(r.u32(o + 8))])
    pages = [dict(items=[item(r, io) for io in r.list(r.u32(o + 8))],
                  actions=[action(r, ao) for ao in r.list(r.u32(o + 12))],
                  timers=[(r.fx(to), action(r, to + 4)) for to in r.list(r.u32(o + 16))])
             for o in r.list(r.u32(8))]
    return widgets, pages


def strings(root, lang="en"):
    d = open(os.path.join(root, "frontend", f"metroidhunters_text_{lang}.bin"), "rb").read()
    r = Reader(d)
    out, o = [], 0
    while r.u32(o):
        e = r.u32(o)
        out.append(d[r.u32(e):r.u32(e) + r.u16(e + 8)].split(b"\0")[0].decode("latin1"))
        o += 4
    return out


def code_name(c):
    return f"{STATES[c & 7]}->{STATES[c >> 3]}" if c >= 8 else STATES[c]


def touch_point(a, it):
    """An action's rectangle (text coordinates, Y up) as a DS touch-screen point (Y down)."""
    x, y, w, h = a["rect"]
    if (x, y, w, h) == (0, 0, 0, 0):
        return None
    ox, oy = (it["x"], it["y"]) if it else (0, 0)
    if a["f4"] & 1:
        x0, x1, y0, y1 = x - w, x + w, y - h, y + h
    else:
        x0, x1, y0, y1 = min(x, w), max(x, w), min(y, h), max(y, h)
    return round((x0 + x1) / 2 + ox), round(192 - ((y0 + y1) / 2 + oy))


def dump_page(widgets, pages, strs, n):
    p = pages[n]
    print(f"== page {n}: {len(p['items'])} items")
    for t, a in p["timers"]:
        print(f"  timer at tick {t}: page {a['page']} calls {a['calls']}")
    for a in p["actions"]:
        print(f"  page action kind {a['kind']:#x}: page {a['page']} calls {a['calls']} touch {touch_point(a, None)}")
    for i, it in enumerate(p["items"]):
        print(f"  item {i} (#{it['index']}) delay={it['delay']} at=({it['x']},{it['y']}) depth={it['depth']} "
              f"init={code_name(it['init'])}")
        for s in it["states"]:
            if "text" in s:
                words = strs[s["text"]] if s["text"] < len(strs) else s["text"]
                print(f"     {code_name(s['code']):14} text {words!r} {s['start']:08x}->{s['end']:08x} "
                      f"dur={s['dur']} wrap={s['wrap']} format={s['format'].hex(' ')}")
            else:
                print(f"     {code_name(s['code']):14} widget {s['widget']} {widgets[s['widget']] if s['widget'] >= 0 else ''}")
        for event, state, target in it["links"]:
            print(f"     link on {code_name(event)} -> item {target} {code_name(state)}")
        for a in it["actions"]:
            print(f"     action kind {a['kind']:#x} flags {a['flags']}: page {a['page']} calls {a['calls']} "
                  f"item {a['item']} f17 {a['f17']} touch {touch_point(a, it)}")


if __name__ == "__main__":
    root = sys.argv[1]
    widgets, pages = parse(open(os.path.join(root, "frontend", "metroidhunters.bin"), "rb").read())
    strs = strings(root)
    for n in [int(x) for x in sys.argv[2:]] or range(len(pages)):
        dump_page(widgets, pages, strs, n)
