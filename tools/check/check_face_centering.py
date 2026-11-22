#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Centring check of the designed faces' labels (2026-11-18, owner: 電力 / BATTERY sat 2-3 px high, solid plates' text 1 px low).
Measures the INK of every bilingual label (registered by tools/gen/make_faces.py with box=...) and of every state plate (sheet_bi cells) against the box
that encloses it and fails when the ink's bounding-box centre is off by more than TOL (1 px; horizontal TOLH 2 px) on a checked axis ('v' = vertical only: left-anchored cell
labels; 'hv' = both). Ink = a pixel >= 96 of the final (downsampled) layer / sheet cell; solid plates: the knocked-out (dark) pixels inside the plate.
Deliberate exceptions: ALLOW below (name -> reason). Labels with no enclosing box (秒 SEC, 時 HR, 完成 DONE, 分段 SPLIT rows, weekday cells) are not checked.
usage: check_face_centering.py [-v]   (exit 1 on any failure; needs Pillow + the CJK / Plex fonts make_faces.py needs: without them it prints a notice and exits 0)"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "gen"))   # tools/gen: sibling tools live in other folders
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
TOL = 1.0      # vertical, px
TOLH = 2.0     # horizontal, px: labels are placed by their advance widths, the ink of two scripts is asymmetric by a px or two
THR = 96
ALLOW = {   # "<board>/<face> <what>" or "sheet <name>/<what>" -> reason (none yet)
}


def ink_centre(im, box, dark=False):
    """(cx, cy) of the ink inside box (x0, y0, x1, y1) of an L image, or None when there is none"""
    x0, y0, x1, y1 = [int(round(v)) for v in box]
    c = im.crop((x0, y0, x1, y1)).point(lambda v: 255 if ((v < THR) if dark else (v >= THR)) else 0)
    bb = c.getbbox()
    if not bb: return None
    return x0 + (bb[0] + bb[2]) / 2.0, y0 + (bb[1] + bb[3]) / 2.0


def run(verbose=False):
    try:
        import make_faces as mf
        mf.font('sans', 10); mf.font('cond', 10)
    except Exception as e:
        print(f"NOTICE: face centring check skipped ({str(e).splitlines()[0]})"); return 0
    faces, _ = mf.build()
    rows = []   # (name, dx, dy, axes)
    for f in faces:
        for what, role, box, axes in f.checks:
            c = ink_centre(f.L[role], box)
            rows.append((f"{f.name} {what} {tuple(round(v) for v in box)}", c, box, axes))
    for name, (im, kind, w, h, n, advs, pad) in mf.SHEETS.items():
        pl = getattr(im, 'plates', None)
        if not pl: continue
        ch = im.cell_h
        for what, i, (rx0, ry0, rx1, ry1), mode, (bx0, by0, bx1, by1) in pl:
            oy = i * ch
            c = ink_centre(im, (rx0, oy + ry0, rx1, oy + ry1), dark=(mode == 'dark'))
            rows.append((f"sheet {name}/{what}", c, (bx0, oy + by0, bx1, oy + by1), 'hv'))
    bad = 0
    for name, c, box, axes in rows:
        if c is None:
            print(f"FAIL no ink: {name}"); bad += 1; continue
        dx = c[0] - (box[0] + box[2]) / 2.0; dy = c[1] - (box[1] + box[3]) / 2.0
        off = abs(dy) > TOL or ('h' in axes and abs(dx) > TOLH)
        key = name.split(' (')[0]
        if off and key in ALLOW: off = False
        if off: print(f"FAIL off-centre dx={dx:+.1f} dy={dy:+.1f}: {name}"); bad += 1
        elif verbose: print(f"ok   dx={dx:+.1f} dy={dy:+.1f}: {name}")
    print(f"check_face_centering: {len(rows)} labels, {bad} off-centre")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(run("-v" in sys.argv))
