#!/usr/bin/env python3
# cmp.py REFDIR QTDIR OUTDIR [names...]: side-by-side + share of differing pixels.
import sys, os
from PIL import Image, ImageChops
import numpy as np

ref, qt, out = sys.argv[1:4]
names = sys.argv[4:] or sorted(f[:-4] for f in os.listdir(qt) if f.endswith('.png'))
os.makedirs(out, exist_ok=True)
for n in names:
    a_path, b_path = os.path.join(ref, n + '.png'), os.path.join(qt, n + '.png')
    if not os.path.exists(a_path) or not os.path.exists(b_path):
        print(f'{n:32s} missing'); continue
    a = Image.open(a_path).convert('RGB'); b = Image.open(b_path).convert('RGB')
    if a.size != b.size:
        print(f'{n:32s} size {a.size} vs {b.size}'); b = b.resize(a.size)
    d = np.abs(np.asarray(a, dtype=int) - np.asarray(b, dtype=int)).max(axis=2)
    share = (d > 24).mean() * 100
    w, h = a.size
    side = Image.new('RGB', (w * 3, h))
    side.paste(a, (0, 0)); side.paste(b, (w, 0))
    heat = Image.fromarray(np.clip(d * 4, 0, 255).astype('uint8')).convert('RGB')
    side.paste(heat, (w * 2, 0))
    side.save(os.path.join(out, 'cmp-' + n + '.png'))
    print(f'{n:32s} {share:5.1f}% differ')
