#!/usr/bin/env python3
"""Find, for every Quake 3 texture levels lean on without shipping it, the
cartridge texture that looks most like it.

    FruityPrime -mphtexdump DUMP                      # the cartridge's textures
    tools/q3-texture-match.py BASEQ3 DUMP \\
        src/MphRead.Native/Mods/MapGen/Q3TextureMatches.inc [--sheet SHEET.png]

BASEQ3 is a folder of .pk3 files: Quake 3's own paks and as many community
levels as you like -- the more levels, the more of the textures they borrow
get an answer. A level's own textures need none (the translator reads them
from its archive); what is matched is every shader some level uses and does
not carry, resolved the way Quake does (a .shader script's first stage, the
image at its name, its sky box, its editor image) anywhere in BASEQ3.

Each image is described by its colour (mean and spread in CIE Lab, and how
its lightness, red-green and blue-yellow are spread), its detail (how much
it changes from texel to texel, and in which directions), and compared with
every cartridge texture; the nearest wins. A sky is only matched with the
cartridge's skies, and nothing else ever is; a see-through cartridge texture
only answers glass. What is written is names, never images: the output can
be committed, id Software's art cannot.
"""
import argparse
import io
import os
import re
import struct
import sys
import zipfile

import numpy as np
from PIL import Image

IMAGE_EXTENSIONS = ('.tga', '.jpg', '.jpeg', '.png')
FOGS = {}  # shader -> [r, g, b, distance], from every script read
SIZE = 64

SURF_SKY = 0x4
SURF_NODRAW = 0x80
SURF_NOLIGHTMAP = 0x400
CONTENTS_LAVA = 0x8
CONTENTS_SLIME = 0x10
CONTENTS_WATER = 0x20

EFFECT_WORDS = ('energy', 'transparen', 'fog', 'flame', 'beam', 'flare', 'spark', 'smoke', 'portal')
SKY_WORDS = ('sky', 'space', 'star')
GLASS_WORDS = ('glass', 'window', 'crystal', 'ice')
LAVA_WORDS = ('lava', 'magma', 'fire', 'hell')
LIQUID_WORDS = ('slime', 'water', 'liquid', 'ice', 'snow', 'frost')


def strip_ext(name):
    lower = name.lower().replace('\\', '/')
    for ext in IMAGE_EXTENSIONS:
        if lower.endswith(ext):
            return lower[:-len(ext)]
    return lower


# --- archives ----------------------------------------------------------------

class Index:
    """Every image and shader script in every archive, later archives first
    as Quake loads them (pak names in order, the last wins)."""

    def __init__(self, folder):
        self.archives = sorted(
            os.path.join(folder, n) for n in os.listdir(folder) if n.lower().endswith(('.pk3', '.zip')))
        self.images = {}      # stem -> (archive, member)
        self.shaders = {}     # shader -> (archive, first map, sky env, editor image)
        self.stages = {}      # shader -> [(image, additive)] for each stage
        self.bsps = []        # (archive, member)
        self.by_archive = {}  # archive -> (image stems, shader names)
        for archive in self.archives:
            try:
                z = zipfile.ZipFile(archive)
            except (zipfile.BadZipFile, OSError) as e:
                print(f'  skipping {os.path.basename(archive)}: {e}', file=sys.stderr)
                continue
            stems, names = set(), set()
            for info in z.infolist():
                member = info.filename
                lower = member.lower()
                if lower.endswith(IMAGE_EXTENSIONS):
                    stem = strip_ext(lower)
                    self.images[stem] = (archive, member)
                    stems.add(stem)
                elif lower.startswith('scripts/') and lower.endswith('.shader'):
                    try:
                        text = z.read(member).decode('latin-1')
                    except Exception:
                        continue
                    for name, first, sky, editor, stages in parse_shaders(text):
                        self.shaders[name] = (archive, first, sky, editor)
                        self.stages[name] = stages
                        names.add(name)
                elif lower.startswith('maps/') and lower.endswith('.bsp'):
                    self.bsps.append((archive, member))
            self.by_archive[archive] = (stems, names)
            z.close()

    def resolve(self, shader):
        """The image Quake would draw a shader with, as (archive, member)."""
        script = self.shaders.get(shader)
        if script:
            _, first, sky, editor = script
            if first and strip_ext(first) in self.images:
                return self.images[strip_ext(first)]
        if shader in self.images:
            return self.images[shader]
        if script:
            _, first, sky, editor = script
            if sky:
                for side in ('ft', 'rt', 'bk', 'lf', 'up'):
                    if f'{sky}_{side}' in self.images:
                        return self.images[f'{sky}_{side}']
            if editor and strip_ext(editor) in self.images:
                return self.images[strip_ext(editor)]
        return None


def parse_shaders(text):
    text = re.sub(r'//[^\n]*', '', text)
    tokens = re.findall(r'"[^"]*"|\{|\}|[^\s{}]+', text)
    i = 0
    while i < len(tokens):
        name = tokens[i].strip('"').lower()
        if i + 1 >= len(tokens) or tokens[i + 1] != '{':
            i += 1
            continue
        i += 2
        depth, first, sky, editor = 1, None, None, None
        stages, current, additive = [], None, False
        fog = None
        while i < len(tokens) and depth > 0:
            t = tokens[i]
            low = t.lower()
            if t == '{':
                depth += 1
                if depth == 2:
                    current, additive = None, False
            elif t == '}':
                if depth == 2 and current:
                    stages.append((current, additive))
                depth -= 1
            elif depth == 1 and low == 'skyparms' and i + 1 < len(tokens):
                env = tokens[i + 1].strip('"').lower()
                if env != '-':
                    sky = env
            elif depth == 1 and low == 'fogparms':
                numbers = []
                k = i + 1
                while k < len(tokens) and len(numbers) < 4:
                    text = tokens[k].strip('()')
                    if text:
                        try:
                            numbers.append(float(text))
                        except ValueError:
                            break
                    k += 1
                if len(numbers) == 4:
                    fog = numbers
            elif depth == 1 and low == 'qer_editorimage' and i + 1 < len(tokens):
                editor = tokens[i + 1].strip('"').lower()
            elif depth == 2 and low in ('map', 'clampmap') and i + 1 < len(tokens):
                image = tokens[i + 1].strip('"').lower()
                if not image.startswith('$'):
                    current = image
                    if first is None:
                        first = image
            elif depth == 2 and low == 'blendfunc' and i + 1 < len(tokens):
                rest = ' '.join(tokens[i + 1:i + 3]).lower()
                additive = rest.startswith('add') or rest.startswith('gl_one gl_one')
            elif depth == 2 and first is None and low == 'animmap' and i + 2 < len(tokens):
                first = tokens[i + 2].strip('"').lower()
            i += 1
        FOGS.setdefault(name, fog) if fog else None
        yield name, first, sky, editor, stages


def bsp_shaders(data):
    """(name, surface flags, contents) for each shader a .bsp uses."""
    if data[:4] != b'IBSP':
        return []
    offset, length = struct.unpack_from('<ii', data, 8 + 8 * 1)
    result = []
    for i in range(length // 72):
        raw, flags, contents = struct.unpack_from('<64sii', data, offset + i * 72)
        result.append((raw.split(b'\0')[0].decode('latin-1').lower(), flags, contents))
    return result


# --- describing an image -------------------------------------------------------

def to_lab(rgb):
    c = rgb / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def describe(image):
    image = image.convert('RGBA').resize((SIZE, SIZE), Image.BILINEAR)
    rgba = np.asarray(image, dtype=np.float64)
    rgb = rgba[..., :3] * (rgba[..., 3:4] / 255.0)
    lab = to_lab(rgb)
    light = lab[..., 0]
    mean = lab.reshape(-1, 3).mean(0)
    spread = lab.reshape(-1, 3).std(0)
    hist = np.concatenate([
        np.histogram(lab[..., 0], bins=8, range=(0, 100))[0],
        np.histogram(np.clip(lab[..., 1], -40, 40), bins=6, range=(-40, 40))[0],
        np.histogram(np.clip(lab[..., 2], -40, 40), bins=6, range=(-40, 40))[0],
    ]).astype(np.float64) / (SIZE * SIZE)
    # Detail: lightness against its neighbours, wrapping as a texture tiles.
    gx = np.roll(light, -1, 1) - light
    gy = np.roll(light, -1, 0) - light
    magnitude = np.hypot(gx, gy)
    angle = np.mod(np.arctan2(gy, gx), np.pi)
    orient = np.histogram(angle, bins=4, range=(0, np.pi), weights=magnitude)[0]
    orient = orient / max(orient.sum(), 1e-6)
    return {'mean': mean, 'spread': spread, 'hist': hist, 'detail': magnitude.mean(), 'orient': orient}


def distance(a, b):
    colour = np.linalg.norm(a['mean'] - b['mean']) / 100.0
    spread = np.abs(a['spread'] - b['spread']).sum() / 100.0
    hist = np.abs(a['hist'] - b['hist']).sum() / 3.0
    detail = abs(a['detail'] - b['detail']) / max(a['detail'], b['detail'], 1.0)
    orient = np.abs(a['orient'] - b['orient']).sum() / 2.0
    return 3.0 * colour + 1.5 * spread + 1.0 * hist + 0.8 * detail + 0.5 * orient


# --- the match -------------------------------------------------------------------

def has(words, text):
    return any(w in text for w in words)


def is_sky(material):
    """A cartridge texture that is a sky: one named so, not a swatch of space-age panels."""
    if 'swatch' in material or 'block' in material:
        return False
    return 'sky' in material or 'star' in material or re.search(r'(^|[:_])space($|[:_0-9])', material) is not None


def load_cartridge(dump):
    candidates = []
    with open(os.path.join(dump, 'index.tsv')) as f:
        next(f)
        for line in f:
            room, material, width, height, alpha, file = line.rstrip('\n').split('\t')
            low = material.lower()
            image = Image.open(os.path.join(dump, file))
            desc = describe(image)
            liquid = has(LIQUID_WORDS, low)
            # A flat bright colour -- a glow, a mask -- is nothing a wall looks
            # like (snow aside, for snow); a dark one may be a starfield.
            if desc['spread'][0] < 3.0 and desc['detail'] < 1.5 and desc['mean'][0] > 60 and not liquid:
                continue
            candidates.append({
                'room': room, 'material': material, 'file': os.path.join(dump, file),
                'alpha': alpha == '1', 'sky': is_sky(low),
                'glass': has(GLASS_WORDS, low), 'lava': has(LAVA_WORDS, low),
                'liquid': has(LIQUID_WORDS, low), 'desc': desc})
    return candidates


def choose(shader, flags, contents, desc, candidates):
    name = shader.lower()
    sky = (flags & SURF_SKY) != 0 or has(('skies/', 'sky'), name)
    glass = has(GLASS_WORDS, name)
    lava = (contents & CONTENTS_LAVA) != 0 or has(LAVA_WORDS, name)
    liquid = (contents & (CONTENTS_SLIME | CONTENTS_WATER)) != 0 or has(LIQUID_WORDS, name)
    best, best_score = None, None
    for c in candidates:
        if c['sky'] != sky:
            continue
        if c['alpha'] and not glass:
            continue
        # A sky is its colour: what clouds or stars it has the cartridge's
        # skies do not share.
        score = (np.linalg.norm(desc['mean'] - c['desc']['mean']) / 100.0 if sky
                 else distance(desc, c['desc']))
        # What a surface is counts as much as what it looks like.
        if lava and c['lava'] or liquid and c['liquid'] or glass and c['glass']:
            score *= 0.6
        if best_score is None or score < best_score:
            best, best_score = c, score
    return best, best_score


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('baseq3')
    parser.add_argument('dump')
    parser.add_argument('output')
    parser.add_argument('--sheet', help='a contact sheet of matches to look over')
    parser.add_argument('--report', help='every match with its score, as TSV')
    args = parser.parse_args()

    print('indexing', args.baseq3, file=sys.stderr)
    index = Index(args.baseq3)
    print(f'  {len(index.archives)} archives, {len(index.images)} images, '
          f'{len(index.shaders)} shaders, {len(index.bsps)} levels', file=sys.stderr)

    # What levels use and do not carry.
    wanted = {}  # shader -> (flags, contents, levels)
    zips = {}
    for archive, member in index.bsps:
        z = zips.setdefault(archive, zipfile.ZipFile(archive))
        try:
            shaders = bsp_shaders(z.read(member))
        except Exception as e:
            print(f'  {member}: {e}', file=sys.stderr)
            continue
        stems, names = index.by_archive[archive]
        for name, flags, contents in shaders:
            if name.startswith('textures/common/') or name == 'noshader' or flags & SURF_NODRAW:
                continue
            if has(EFFECT_WORDS, name):
                continue
            if (flags & SURF_NOLIGHTMAP) and not (flags & SURF_SKY):
                continue
            if name in stems or name in names:
                continue
            entry = wanted.setdefault(name, [flags, contents, 0])
            entry[2] += 1
    used = len(wanted)
    # Quake 3's own paks (pak0 to pak8): everything they hold, used by some
    # level here or not -- any level may lean on any of it.
    official = re.compile(r'^pak\d', re.I)
    for archive, (stems, names) in index.by_archive.items():
        if not official.match(os.path.basename(archive)):
            continue
        for name in list(names) + [st for st in stems if st.startswith(('textures/', 'models/mapobjects/'))]:
            if name.startswith('textures/common/') or has(EFFECT_WORDS, name) or name in wanted:
                continue
            script = index.shaders.get(name)
            flags = SURF_SKY if name.startswith('textures/skies/') else 0
            wanted[name] = [flags, 0, 0]
    print(f'  {used} shaders used and not carried, {len(wanted) - used} more from the game paks',
          file=sys.stderr)

    candidates = load_cartridge(args.dump)
    print(f'  {len(candidates)} cartridge textures', file=sys.stderr)

    matches = []
    unresolved = 0
    for name in sorted(wanted):
        flags, contents, levels = wanted[name]
        found = index.resolve(name)
        if not found:
            unresolved += 1
            continue
        archive, member = found
        z = zips.setdefault(archive, zipfile.ZipFile(archive))
        try:
            image = Image.open(io.BytesIO(z.read(member)))
            image.load()
        except Exception:
            unresolved += 1
            continue
        # A sky with no box is its cloud layers, the ones that add on top of
        # the first lightening it -- as Quake draws them.
        layers = index.stages.get(name, [])
        if (flags & SURF_SKY or name.startswith('textures/skies/')) and len(layers) > 1:
            total = np.asarray(image.convert('RGB').resize((SIZE, SIZE)), dtype=np.float64)
            for layer, additive in layers[1:]:
                stem = strip_ext(layer)
                if stem not in index.images:
                    continue
                la, lm = index.images[stem]
                try:
                    over = np.asarray(Image.open(io.BytesIO(zips.setdefault(la, zipfile.ZipFile(la)).read(lm)))
                                      .convert('RGB').resize((SIZE, SIZE)), dtype=np.float64)
                except Exception:
                    continue
                total = total + over if additive else (total + over) / 2.0
            image = Image.fromarray(np.clip(total, 0, 255).astype(np.uint8))
        best, score = choose(name, flags, contents, describe(image), candidates)
        if best:
            matches.append((name, best, score, levels, (archive, member)))
    print(f'  {len(matches)} matched, {unresolved} with no image anywhere', file=sys.stderr)

    def quote(text):
        return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'

    with open(args.output, 'w', newline='\n') as out:
        out.write('// Generated by tools/q3-texture-match.py: Quake 3 shader -> the cartridge\n')
        out.write('// texture most like it. Do not edit by hand.\n')
        for name, best, score, levels, _ in matches:
            out.write(f'        {{{quote(name)}, {quote(best["room"])}, {quote(best["material"])}}},\n')

    fogs_path = os.path.join(os.path.dirname(args.output), 'Q3Fogs.inc')
    with open(fogs_path, 'w', newline='\n') as out:
        out.write('// Generated by tools/q3-texture-match.py: Quake 3 fog shaders, red green\n')
        out.write('// blue and the depth they are opaque at. Do not edit by hand.\n')
        for name in sorted(FOGS):
            r, g, b, d = FOGS[name]
            out.write(f'        {{{quote(name)}, {r:.4f}F, {g:.4f}F, {b:.4f}F, {d:.1f}F}},\n')
    print(f'  {len(FOGS)} fog shaders written to {fogs_path}', file=sys.stderr)

    if args.report:
        with open(args.report, 'w') as out:
            out.write('shader\tlevels\troom\tmaterial\tscore\n')
            for name, best, score, levels, _ in matches:
                out.write(f'{name}\t{levels}\t{best["room"]}\t{best["material"]}\t{score:.3f}\n')

    if args.sheet:
        # The most used, each beside what it became.
        shown = sorted(matches, key=lambda m: -m[3])[:60]
        cell = 96
        sheet = Image.new('RGB', (cell * 2 * 6 + 5 * 8, cell * ((len(shown) + 5) // 6)), (24, 24, 24))
        for i, (name, best, score, levels, source) in enumerate(shown):
            z = zips[source[0]]
            q3 = Image.open(io.BytesIO(z.read(source[1]))).convert('RGB').resize((cell, cell))
            mph = Image.open(best['file']).convert('RGB').resize((cell, cell))
            x = (i % 6) * (cell * 2 + 8)
            y = (i // 6) * cell
            sheet.paste(q3, (x, y))
            sheet.paste(mph, (x + cell, y))
        sheet.save(args.sheet)
        print(f'  sheet of {len(shown)} written to {args.sheet}', file=sys.stderr)


if __name__ == '__main__':
    main()
