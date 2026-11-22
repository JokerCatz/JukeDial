#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Contact sheet of simulator pictures: sim_sheet.py DIR OUT.png [--cols N] [--cell PX] [--names a,b,c | --from i --count n]
Every picture scaled to a cell with its file name under it (Pillow; a developer aid, not a gate). The FakePod sheet is `make sim-base-fp` -> .local/sim/fp_sheet.png."""
import argparse, os
from PIL import Image, ImageDraw

ap = argparse.ArgumentParser()
ap.add_argument("dir"); ap.add_argument("out")
ap.add_argument("--cols", type=int, default=8); ap.add_argument("--cell", type=int, default=230)
ap.add_argument("--names"); ap.add_argument("--from", dest="start", type=int, default=0); ap.add_argument("--count", type=int, default=0)
a = ap.parse_args()
names = a.names.split(",") if a.names else sorted(f[:-4] for f in os.listdir(a.dir) if f.endswith(".png"))
if not a.names and a.count: names = names[a.start:a.start + a.count]
elif not a.names: names = names[a.start:]
rows = (len(names) + a.cols - 1) // a.cols
lab = 16
sheet = Image.new("RGB", (a.cols * a.cell, rows * (a.cell + lab)), (40, 40, 40))
d = ImageDraw.Draw(sheet)
for i, n in enumerate(names):
    im = Image.open(os.path.join(a.dir, n + ".png")).convert("RGB").resize((a.cell - 4, a.cell - 4), Image.LANCZOS)
    x, y = (i % a.cols) * a.cell, (i // a.cols) * (a.cell + lab)
    sheet.paste(im, (x + 2, y + 2)); d.text((x + 4, y + a.cell - 1), n, fill=(255, 255, 255))
sheet.save(a.out)
print(f"{len(names)} pictures -> {a.out}")
