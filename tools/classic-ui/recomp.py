"""Drive Metroid Prime Hunters in the MPH recomp's headless debug server, one
VBlank at a time, for comparing the classic DS UI against the game.

The recomp (github.com/mstan/MetroidPrimeHuntersRecomp, v0.7.6 or later)
ships nds_runner, whose --serve mode answers line-delimited JSON on a TCP
port (ndsrecomp's TCP.md). It needs the USA rev 0 ROM -- the one its
game.toml names by SHA-1 -- and boots it with the free BIOS and a generated
firmware: no dumps. Savestates are refused while the game drives the 3D
engine, so every run boots; the title is ~2480 VBlanks in (about a minute).

    recomp.py frames OUT STEP...    pictures (OUT/fNNNNNN.png, 256 x 384)
    recomp.py polys OUT.txt STEP... every VBlank: each polygon ID's alpha
    recomp.py gx OUT.txt STEP...    the 3D command stream of the frames named

Steps: run:N (N VBlanks), cap:N[:EVERY] (N VBlanks, a picture every EVERY),
tap:X,Y[:N] (touch DS pixel X,Y on the touch screen for N VBlanks),
key:A+UP[:N] (hold buttons), shot:NAME, mark (gx: trace the next frame).

The title, settled:  run:2421 tap:128,96:4 run:275   (frame ~2700)
Main menu, settled:  ... tap:128,96:4 run:296         (frame ~3000)

Environment: MPH_RECOMP_RUNNER (nds_runner path), MPH_ROM (the USA rev 0
.nds), MPH_SAVE (a save file to use -- copy one, the runner writes it),
MPH_RECOMP_LOG (the runner's output; recomp-runner.log in the working folder).
NDS_3D_RENDERER=soft is set: the software renderer is the deterministic one.
"""
import collections
import json
import os
import socket
import subprocess
import sys
import time

from PIL import Image

KEYS = {"A": 0, "B": 1, "SELECT": 2, "START": 3, "RIGHT": 4, "LEFT": 5, "UP": 6, "DOWN": 7,
        "R": 8, "L": 9, "X": 10, "Y": 11}

# GX commands: parameter words, and names for the trace
PARAMS = {0x00: 0, 0x10: 1, 0x11: 0, 0x12: 1, 0x13: 1, 0x14: 1, 0x15: 0, 0x16: 16, 0x17: 12, 0x18: 16, 0x19: 12,
          0x1A: 9, 0x1B: 3, 0x1C: 3, 0x20: 1, 0x21: 1, 0x22: 1, 0x23: 2, 0x24: 1, 0x25: 1, 0x26: 1, 0x27: 1,
          0x28: 1, 0x29: 1, 0x2A: 1, 0x2B: 1, 0x30: 1, 0x31: 1, 0x32: 1, 0x33: 1, 0x34: 32, 0x40: 1, 0x41: 0,
          0x50: 1, 0x60: 1, 0x70: 3, 0x71: 2, 0x72: 1}
QUIET = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x22, 0x23, 0x24, 0x25,
         0x26, 0x27, 0x28, 0x41}


class Recomp:
    def __init__(self, port=19850):
        runner = os.environ["MPH_RECOMP_RUNNER"]
        args = [runner, "--rom", os.environ["MPH_ROM"], "--serve", "--port", str(port), "--boot", "direct",
                "--freebios", "--generated-firmware"]
        save = os.environ.get("MPH_SAVE")
        args += ["--save-path", save] if save else ["--no-save"]
        self.log = open(os.environ.get("MPH_RECOMP_LOG", "recomp-runner.log"), "w")
        self.proc = subprocess.Popen(args, cwd=os.path.dirname(runner), stdout=self.log, stderr=subprocess.STDOUT,
                                     env=dict(os.environ, NDS_3D_RENDERER="soft"))
        deadline = time.time() + 60
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=600)
                break
            except OSError:
                if time.time() > deadline or self.proc.poll() is not None:
                    raise RuntimeError("the runner did not start: see its log (MPH_RECOMP_LOG)")
                time.sleep(0.25)
        self.buf = b""
        self.vblank = self.cmd(cmd="event_counts")["vblank9"]

    def cmd(self, **kw):
        self.sock.sendall((json.dumps(kw) + "\n").encode())
        while b"\n" not in self.buf:
            chunk = self.sock.recv(1 << 20)
            if not chunk:
                raise RuntimeError("the runner closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line)

    def frames(self, n=1):
        self.vblank += n
        r = self.cmd(cmd="run_to_event", event="vblank9", count=self.vblank)
        if not r.get("reached"):
            raise RuntimeError(f"run_to_event: {r}")

    def keys(self, *names):
        mask = 0xFFF
        for n in names:
            mask &= ~(1 << KEYS[n])
        self.cmd(cmd="keys", mask=mask)

    def touch(self, x, y, down=True):
        self.cmd(cmd="touch", x=int(x), y=int(y), down=down)

    def shot(self, path):
        im = Image.new("RGB", (256, 384))
        for screen in (0, 1):
            r = self.cmd(cmd="framebuffer", engine="AB"[screen])
            if "rgb" in r:
                im.paste(Image.frombytes("RGB", (r["w"], r["h"]), bytes.fromhex(r["rgb"])), (0, 192 * screen))
        im.save(path)

    def polygons(self):
        groups = collections.OrderedDict()
        for p in self.cmd(cmd="gx_polygons")["polygons"]:
            a = p["attr"]
            g = groups.setdefault((a >> 24) & 63, [0, set()])
            g[0] += 1
            g[1].add((a >> 16) & 31)
        return " ".join(f"id{k}:n{v[0]}a{'/'.join(map(str, sorted(v[1])))}" for k, v in groups.items())

    def trace(self):
        """The 3D register writes of the next frame, decoded into commands."""
        a = self.cmd(cmd="gx_write_sample")["latest"]
        self.frames(1)
        b = self.cmd(cmd="gx_write_sample")["latest"]
        writes = []
        for c in range(max(a + 1, b - 8000), b + 1):
            e = self.cmd(cmd="gx_write_sample", count=c)
            if e.get("found"):
                writes.append((e["addr"], e["val"]))
        return decode(writes)

    def close(self):
        try:
            self.sock.close()
        finally:
            self.proc.kill()
            self.log.close()


def decode(writes):
    """ARM9 writes to the 3D registers -> [(command, [params])]: packed FIFO words and direct ports."""
    out, queue, pending = [], [], []

    def drain():
        while queue and queue[0][1] == 0:
            c = queue.pop(0)
            if c[0]:
                out.append((c[0], []))

    for addr, val in writes:
        if addr == 0x04000400:
            if not queue:
                queue.extend([c, PARAMS.get(c, 0), []] for c in ((val >> (8 * i)) & 0xFF for i in range(4)))
                drain()
            else:
                queue[0][2].append(val)
                if len(queue[0][2]) >= queue[0][1]:
                    c = queue.pop(0)
                    out.append((c[0], c[2]))
                    drain()
        elif 0x04000440 <= addr < 0x04000600:
            c = (addr - 0x04000400) >> 2
            n = PARAMS.get(c, 0)
            if n <= 1:
                out.append((c, [val] if n else []))
            else:
                pending = pending if pending and pending[0] == c else [c, n, []]
                pending[2].append(val)
                if len(pending[2]) >= n:
                    out.append((c, pending[2]))
                    pending = []
    return out


def describe(c, p):
    rgb = lambda v: f"({v & 31},{(v >> 5) & 31},{(v >> 10) & 31})"
    s10 = lambda x: (x - 1024 if x & 512 else x) / 512
    v = p[0] if p else 0
    return {0x20: lambda: f"COLOR{rgb(v)}",
            0x21: lambda: f"NORMAL({s10(v & 1023):.2f},{s10((v >> 10) & 1023):.2f},{s10((v >> 20) & 1023):.2f})",
            0x29: lambda: f"POLY_ATTR(lights={v & 15} mode={(v >> 4) & 3} alpha={(v >> 16) & 31} id={(v >> 24) & 63})",
            0x2A: lambda: f"TEXIMAGE({v:08x})",
            0x2B: lambda: f"PLTT({v:04x})",
            0x30: lambda: f"DIF_AMB(dif={rgb(v)} set={v >> 15 & 1} amb={rgb(v >> 16)})",
            0x31: lambda: f"SPE_EMI(spe={rgb(v)} emi={rgb(v >> 16)})",
            0x32: lambda: f"LIGHT_VECTOR(l{v >> 30}=({s10(v & 1023):.2f},{s10((v >> 10) & 1023):.2f},{s10((v >> 20) & 1023):.2f}))",
            0x33: lambda: f"LIGHT_COLOR(l{v >> 30}={rgb(v)})",
            0x40: lambda: f"BEGIN({v})"}.get(c, lambda: f"{c:#x}{[hex(x) for x in p]}")()


def main():
    mode, out, steps = sys.argv[1], sys.argv[2], sys.argv[3:]
    if mode == "frames":
        os.makedirs(out, exist_ok=True)
    report = open(out, "w") if mode in ("polys", "gx") else None
    r = Recomp()
    try:
        def advance(n, capture=0):
            for i in range(n):
                r.frames(1)
                if mode == "polys":
                    report.write(f"{r.vblank} {r.polygons()}\n")
                if mode == "frames" and capture and (i + 1) % capture == 0:
                    r.shot(os.path.join(out, f"f{r.vblank:06d}.png"))

        for step in steps:
            kind, _, rest = step.partition(":")
            if kind == "run":
                advance(int(rest))
            elif kind == "cap":
                n, _, every = rest.partition(":")
                advance(int(n), int(every or 1))
            elif kind in ("tap", "key"):
                what, _, n = rest.partition(":")
                if kind == "tap":
                    r.touch(*what.split(","))
                else:
                    r.keys(*what.split("+"))
                advance(int(n or 4), 1)
                r.touch(0, 0, False) if kind == "tap" else r.keys()
            elif kind == "shot" and mode == "frames":
                r.shot(os.path.join(out, rest + ".png"))
            elif kind == "mark" and mode == "gx":
                report.write(f"=== frame {r.vblank}\n")
                line, run = [], 0
                for c, p in r.trace():
                    if c in QUIET:
                        run += 1
                        continue
                    if run:
                        line.append(f"[{run}]")
                        run = 0
                    line.append(describe(c, p))
                    if c == 0x40:
                        report.write("  " + " ".join(line) + "\n")
                        line = []
                report.write("  " + " ".join(line) + "\n")
            else:
                raise SystemExit("unknown step " + step)
    finally:
        r.close()
        if report:
            report.close()
    print("vblank", r.vblank)


if __name__ == "__main__":
    main()
