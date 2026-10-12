#!/usr/bin/env python3
"""Build the OpenArena texture pack the game downloads from net.livetek.fr.

    tools/openarena-pack.py BASEOA BASEQ3 OUT [--doc OPENARENA_DOC_DIR]

BASEOA is OpenArena's baseoa folder (its .pk3 files), BASEQ3 Quake 3's (only
the names of its textures and shaders are read, to know which OpenArena art
stands in for which). OUT receives what is published, as is, at
https://net.livetek.fr/fruityprime/openarena/ (OpenArenaTextures::BaseUrl):

  oa-textures.pk3  every OpenArena image at the name of a Quake 3 one, the
                   images OpenArena's shaders of Quake 3's names draw with,
                   and all of OpenArena's shader scripts -- stored, not
                   compressed, so that a client can read any one file of it
                   with an HTTP range request;
  index.json       where each file's bytes start in the pack and how many
                   there are, and for each shader the images it needs;
  LICENSE.txt      OpenArena's licence (GPL-2+) and credits, which go with
                   the art wherever it goes.

The game fetches, for a level it translates, only the files that level
lacks, and keeps them (Mods/MapGen/OpenArenaTextures.cpp).
"""
import argparse
import glob
import json
import os
import re
import struct
import sys
import zipfile

IMAGE_EXTENSIONS = ('.tga', '.jpg', '.jpeg', '.png')
FOLDERS = ('textures/', 'env/', 'gfx/colors/', 'models/mapobjects/')
SKY_SIDES = ('rt', 'bk', 'lf', 'ft', 'up', 'dn')


def paks(folder):
    found = [p for p in glob.glob(os.path.join(folder, '*')) if p.lower().endswith('.pk3')]
    # Quake's order: later paks override earlier ones.
    return sorted(found, key=lambda p: os.path.basename(p).lower())


def stem(name):
    base, ext = os.path.splitext(name.lower())
    return base if ext in IMAGE_EXTENSIONS else name.lower()


def shader_blocks(text):
    text = re.sub(r'//[^\n]*', '', text)
    tokens = re.findall(r'\{|\}|[^\s{}]+', text)
    depth, name, body = 0, None, []
    for token in tokens:
        if token == '{':
            depth += 1
            if depth > 1:
                body.append(token)
        elif token == '}':
            depth -= 1
            if depth == 0 and name is not None:
                yield name.lower(), body
                name, body = None, []
            elif depth > 0:
                body.append(token)
        elif depth == 0:
            name = token
        else:
            body.append(token)


def shader_images(body):
    images = []
    for i, token in enumerate(body):
        low = token.lower()
        if low in ('map', 'clampmap') and i + 1 < len(body):
            if not body[i + 1].startswith('$') and not body[i + 1].startswith('*'):
                images.append(stem(body[i + 1]))
        elif low == 'animmap' and i + 2 < len(body):
            k = i + 2
            while k < len(body) and stem(body[k]) != body[k].lower():
                images.append(stem(body[k]))
                k += 1
        elif low == 'skyparms' and i + 1 < len(body) and body[i + 1] != '-':
            images.extend(stem(body[i + 1]) + '_' + side for side in SKY_SIDES)
        elif low == 'qer_editorimage' and i + 1 < len(body):
            images.append(stem(body[i + 1]))
    return images


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('baseoa')
    parser.add_argument('baseq3')
    parser.add_argument('out')
    parser.add_argument('--doc', help="OpenArena's doc folder (copyright, CREDITS) for LICENSE.txt")
    args = parser.parse_args()

    # Quake 3's names: its images and its shaders.
    q3_images, q3_shaders = set(), set()
    for path in paks(args.baseq3):
        with zipfile.ZipFile(path) as z:
            for name in z.namelist():
                low = name.lower()
                if low.endswith(IMAGE_EXTENSIONS):
                    q3_images.add(stem(low))
                elif low.startswith('scripts/') and low.endswith('.shader'):
                    for shader, _ in shader_blocks(z.read(name).decode('latin1')):
                        q3_shaders.add(shader)

    # OpenArena's files, the last pak's copy of each.
    files, scripts, oa_shaders = {}, {}, {}
    for path in paks(args.baseoa):
        z = zipfile.ZipFile(path)
        for name in z.namelist():
            low = name.lower()
            if low.endswith(IMAGE_EXTENSIONS) and low.startswith(FOLDERS):
                files[stem(low)] = (z, name, os.path.splitext(low)[1])
            elif low.startswith('scripts/') and low.endswith('.shader'):
                scripts[low] = (z, name)
                for shader, body in shader_blocks(z.read(name).decode('latin1')):
                    oa_shaders.setdefault(shader, shader_images(body))

    wanted = {s for s in files if s in q3_images}
    shaders = {}
    for shader, images in oa_shaders.items():
        if shader in q3_shaders or shader in q3_images:
            present = [image for image in images if image in files]
            if present:
                shaders[shader] = sorted(set(present))
                wanted.update(present)

    os.makedirs(args.out, exist_ok=True)
    pack = os.path.join(args.out, 'oa-textures.pk3')
    with zipfile.ZipFile(pack, 'w', zipfile.ZIP_STORED) as out:
        for low in sorted(scripts):
            z, name = scripts[low]
            out.writestr(zipfile.ZipInfo(low, (2008, 8, 28, 0, 0, 0)), z.read(name))
        for image in sorted(wanted):
            z, name, ext = files[image]
            out.writestr(zipfile.ZipInfo(image + ext, (2008, 8, 28, 0, 0, 0)), z.read(name))

    # Where each file's bytes are: past its local header, whose name and
    # extra field lengths are read from the pack itself.
    entries = {}
    with open(pack, 'rb') as raw, zipfile.ZipFile(pack) as z:
        for info in z.infolist():
            raw.seek(info.header_offset)
            header = raw.read(30)
            name_length, extra_length = struct.unpack_from('<HH', header, 26)
            entries[info.filename] = [info.header_offset + 30 + name_length + extra_length, info.file_size]

    index = {
        'version': 1,
        'pack': 'oa-textures.pk3',
        'size': os.path.getsize(pack),
        'scripts': sorted(scripts),
        'files': entries,
        'shaders': shaders,
    }
    with open(os.path.join(args.out, 'index.json'), 'w', newline='\n') as out:
        json.dump(index, out, separators=(',', ':'), sort_keys=True)

    with open(os.path.join(args.out, 'LICENSE.txt'), 'w', newline='\n') as out:
        out.write('The images and shader scripts in oa-textures.pk3 are OpenArena\'s\n'
                  '(https://openarena.ws/), unmodified, distributed under the GNU General\n'
                  'Public License, version 2 or later. Its source is OpenArena\'s own:\n'
                  'https://openarena.ws/svn/source/assets/\n\n')
        if args.doc:
            for doc in ('copyright', 'CREDITS'):
                path = os.path.join(args.doc, doc)
                if os.path.exists(path):
                    out.write('=' * 72 + '\n' + doc + '\n' + '=' * 72 + '\n')
                    out.write(open(path, encoding='utf-8', errors='replace').read() + '\n')

    print(f'{len(wanted)} images ({len([w for w in wanted if w in q3_images])} at Quake 3 names), '
          f'{len(scripts)} scripts, {len(shaders)} shaders; '
          f'{os.path.getsize(pack) / 2**20:.1f} MB -> {args.out}', file=sys.stderr)


if __name__ == '__main__':
    main()
