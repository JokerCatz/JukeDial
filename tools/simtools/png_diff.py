#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Pixel diff of two screenshot directories (8-bit RGB PNGs as tools/simtools/shot.py writes them; standard library only).
usage: png_diff.py DIR_A DIR_B [--allow name,name] [--mask name:x,y,w,h ...]
Compares same-named .png files, prints the number of differing pixels per file. --mask: ignore that rectangle in
that file (clock hands, battery...; repeatable). Exit 1 if a file outside --allow differs, or a file is missing/
has another size."""
import argparse, os, struct, sys, zlib


def read_png(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", f"{path}: not a PNG"
    p, idat, w = 8, b"", None
    while p < len(d):
        n, t = struct.unpack(">I4s", d[p:p + 8]); body = d[p + 8:p + 8 + n]; p += 12 + n
        if t == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype == 2, f"{path}: only 8-bit RGB"
        elif t == b"IDAT": idat += body
    raw, stride, rows, prev = zlib.decompress(idat), w * 3, [], bytearray(w * 3)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 3] if i >= 3 else 0; b = prev[i]; c = prev[i - 3] if i >= 3 else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line)); prev = line
    return w, h, rows


def diff(a, b, masks):
    (w, h, ra), (w2, h2, rb) = read_png(a), read_png(b)
    if (w, h) != (w2, h2): return None
    n = 0
    for y in range(h):
        if ra[y] == rb[y] and not masks: continue
        for x in range(w):
            if ra[y][3 * x:3 * x + 3] != rb[y][3 * x:3 * x + 3] and not any(mx <= x < mx + mw and my <= y < my + mh for mx, my, mw, mh in masks):
                n += 1
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a"); ap.add_argument("b")
    ap.add_argument("--allow", default=""); ap.add_argument("--mask", action="append", default=[])
    o = ap.parse_args()
    allow = {x for x in o.allow.split(",") if x}
    masks = {}
    for m in o.mask:
        name, _, r = m.partition(":"); masks.setdefault(name, []).append(tuple(int(v) for v in r.split(",")))
    na = {f for f in os.listdir(o.a) if f.endswith(".png")}; nb = {f for f in os.listdir(o.b) if f.endswith(".png")}
    for name in sorted(set(masks) - {f[:-4] for f in na | nb}):
        print(f"warning: --mask {name} matches no compared file", file=sys.stderr)
    bad = 0
    for f in sorted(na | nb):
        name = f[:-4]
        if f not in na or f not in nb:
            print(f"{name:24s} MISSING in {'A' if f not in na else 'B'}"); bad += 1; continue
        n = diff(os.path.join(o.a, f), os.path.join(o.b, f), masks.get(name, []))
        if n is None: print(f"{name:24s} SIZE differs"); bad += 1
        else:
            tag = "" if n == 0 else " (allowed)" if name in allow else " <-- DIFF"
            print(f"{name:24s} {n:6d}{tag}")
            if n and name not in allow: bad += 1
    print(f"{len(na | nb)} files, {bad} not ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
