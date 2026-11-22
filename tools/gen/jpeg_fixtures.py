#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""JPEG test pictures for test_jpeg (hires task 8d), generated at build time, never committed.

usage: jpeg_fixtures.py OUTDIR

Writes OUTDIR/<name>.jpg, OUTDIR/<name>.rgb (Pillow's own decode, RGB888, the reference) and OUTDIR/manifest.txt with one line per
picture: "<name> <expected route> <w> <h>". Sources: Pillow (4:4:4, 4:2:2, 4:2:0, gray, progressive), ffmpeg (yuvj444p, which writes 1x2
for all components - the board's Batch C2 cover -, yuvj422p: luma 2x2 with 1x2 chroma, yuvj420p) and a small baseline encoder below for
the layouts neither tool writes (4:4:0, 4:1:1, all 2x1, gray with 2x2 factors, restart markers). Needs Pillow; ffmpeg pictures are left out
(with a line on stderr) when ffmpeg is missing.
"""
import math
import os
import shutil
import subprocess
import sys

from PIL import Image

W, H = 203, 141   # not a multiple of any MCU: partial blocks on both edges


def picture(w=W, h=H):
    """smooth colour fields and a few flat rectangles (edges), so decoders that upsample chroma differently stay close"""
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            r = 128 + 90 * math.sin(x / 23.0)
            g = 128 + 90 * math.cos(y / 19.0)
            b = 128 + 90 * math.sin((x + y) / 31.0)
            if 40 <= x < 90 and 30 <= y < 70:
                r, g, b = 200, 60, 40
            if 120 <= x < 180 and 80 <= y < 125:
                r, g, b = 30, 90, 210
            px[x, y] = (int(r), int(g), int(b))
    return im


# ---- a baseline encoder: any sampling factors, gray or YCbCr, optional restart interval ----
ZZ = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
      35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63]
QL = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
      18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99]
QC = [17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99] + [99] * 32
DC_L = ([0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0], list(range(12)))
DC_C = ([0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0], list(range(12)))
AC_L = ([0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d], bytes.fromhex(
    "01020300041105122131410613516107227114328191a1082342b1c11552d1f02433627282090a161718191a25262728292a3435363738393a434445464748494a"
    "535455565758595a636465666768696a737475767778797a838485868788898a92939495969798999aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7"
    "c8c9cad2d3d4d5d6d7d8d9dae1e2e3e4e5e6e7e8e9eaf1f2f3f4f5f6f7f8f9fa"))
AC_C = ([0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77], bytes.fromhex(
    "000102031104052131061241510761711322328108144291a1b1c109233352f0156272d10a162434e125f11718191a262728292a35363738393a434445464748494a"
    "535455565758595a636465666768696a737475767778797a82838485868788898a92939495969798999aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7"
    "c8c9cad2d3d4d5d6d7d8d9dae2e3e4e5e6e7e8e9eaf2f3f4f5f6f7f8f9fa"))
COS = [[math.cos((2 * x + 1) * u * math.pi / 16) for x in range(8)] for u in range(8)]


def codes(table):
    bits, vals = table
    out, code, k = {}, 0, 0
    for l in range(1, 17):
        for _ in range(bits[l - 1]):
            out[vals[k]] = (code, l)
            code += 1
            k += 1
        code <<= 1
    return out


class Bits:
    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def put(self, v, l):
        for i in range(l - 1, -1, -1):
            self.acc = self.acc << 1 | (v >> i & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                if self.acc == 0xFF:
                    self.out.append(0)
                self.acc, self.n = 0, 0

    def flush(self):
        while self.n:
            self.put(1, 1)


def fdct(blk):
    out = [0.0] * 64
    for v in range(8):
        for u in range(8):
            s = 0.0
            for y in range(8):
                for x in range(8):
                    s += blk[y * 8 + x] * COS[u][x] * COS[v][y]
            cu = math.sqrt(0.5) if u == 0 else 1.0
            cv = math.sqrt(0.5) if v == 0 else 1.0
            out[v * 8 + u] = 0.25 * cu * cv * s
    return out


def category(v):
    a, n = abs(v), 0
    while a:
        n += 1
        a >>= 1
    return n


def encode(im, factors, restart=0, quality_div=2):
    """factors: [(H, V)] per component (one = gray); quality_div halves the standard tables (higher quality)"""
    w, h = im.size
    ycc = im.convert("L") if len(factors) == 1 else im.convert("YCbCr")
    nc = len(factors)
    hmax, vmax = max(f[0] for f in factors), max(f[1] for f in factors)
    if nc == 1:
        mcux, mcuy = (w + 7) // 8, (h + 7) // 8
        bw = [mcux]
        bh = [mcuy]
    else:
        mcux, mcuy = (w + 8 * hmax - 1) // (8 * hmax), (h + 8 * vmax - 1) // (8 * vmax)
        bw = [mcux * f[0] for f in factors]
        bh = [mcuy * f[1] for f in factors]
    planes = []
    src = ycc.load()
    for c, (hc, vc) in enumerate(factors if nc > 1 else [(1, 1)]):
        cw, ch = (w * hc + hmax - 1) // hmax if nc > 1 else w, (h * vc + vmax - 1) // vmax if nc > 1 else h
        sx, sy = (hmax // hc, vmax // vc) if nc > 1 else (1, 1)
        pw, ph = bw[c] * 8, bh[c] * 8
        pl = [0] * (pw * ph)
        for y in range(ph):
            yy = min(y, ch - 1)
            for x in range(pw):
                xx = min(x, cw - 1)
                acc, n = 0, 0
                for dy in range(sy):
                    for dx in range(sx):
                        X, Y = min(xx * sx + dx, w - 1), min(yy * sy + dy, h - 1)
                        p = src[X, Y]
                        acc += p if nc == 1 else p[c]
                        n += 1
                pl[y * pw + x] = acc / n
        planes.append((pl, pw))
    qt = [[max(1, q // quality_div) for q in QL], [max(1, q // quality_div) for q in QC]]
    tq = [0] + [1] * (nc - 1)
    dc = [codes(DC_L), codes(DC_C)]
    ac = [codes(AC_L), codes(AC_C)]
    out = bytearray(b"\xff\xd8")
    out += b"\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    for t in range(2 if nc > 1 else 1):
        out += b"\xff\xdb\x00\x43" + bytes([t]) + bytes(qt[t][ZZ[i]] for i in range(64))
    out += b"\xff\xc0" + (8 + 3 * nc).to_bytes(2, "big") + b"\x08" + h.to_bytes(2, "big") + w.to_bytes(2, "big") + bytes([nc])
    for c, (hc, vc) in enumerate(factors):
        out += bytes([c + 1, hc << 4 | vc, tq[c]])
    for cls, t, tab in ((0, 0, DC_L), (1, 0, AC_L), (0, 1, DC_C), (1, 1, AC_C)):
        if t and nc == 1:
            continue
        body = bytes([cls << 4 | t]) + bytes(tab[0]) + bytes(tab[1])
        out += b"\xff\xc4" + (2 + len(body)).to_bytes(2, "big") + body
    if restart:
        out += b"\xff\xdd\x00\x04" + restart.to_bytes(2, "big")
    out += b"\xff\xda" + (6 + 2 * nc).to_bytes(2, "big") + bytes([nc])
    for c in range(nc):
        out += bytes([c + 1, (tq[c] << 4) | tq[c]])
    out += b"\x00\x3f\x00"
    bits = Bits()
    pred = [0] * nc

    def block(c, bx, by):
        pl, pw = planes[c]
        blk = [pl[(by * 8 + y) * pw + bx * 8 + x] - 128 for y in range(8) for x in range(8)]
        co = fdct(blk)
        q = qt[tq[c]]
        zz = [int(round(co[ZZ[i]] / q[ZZ[i]])) for i in range(64)]
        d = zz[0] - pred[c]
        pred[c] = zz[0]
        cat = category(d)
        bits.put(*dc[tq[c]][cat])
        if cat:
            bits.put(d if d > 0 else d + (1 << cat) - 1, cat)
        run = 0
        for i in range(1, 64):
            v = zz[i]
            if not v:
                run += 1
                continue
            while run > 15:
                bits.put(*ac[tq[c]][0xF0])
                run -= 16
            cat = category(v)
            bits.put(*ac[tq[c]][run << 4 | cat])
            bits.put(v if v > 0 else v + (1 << cat) - 1, cat)
            run = 0
        if run:
            bits.put(*ac[tq[c]][0x00])

    mcus = mcux * mcuy
    rst = 0
    for m in range(mcus):
        if restart and m and m % restart == 0:
            bits.flush()
            bits.out += bytes([0xFF, 0xD0 + rst])
            rst = (rst + 1) & 7
            pred = [0] * nc
        mx, my = m % mcux, m // mcux
        if nc == 1:
            block(0, mx, my)
            continue
        for c, (hc, vc) in enumerate(factors):
            for v in range(vc):
                for u in range(hc):
                    block(c, mx * hc + u, my * vc + v)
    bits.flush()
    out += bits.out + b"\xff\xd9"
    return bytes(out)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    im = picture()
    src_png = os.path.join(out, "src.png")
    im.save(src_png)
    items = []   # (name, route, bytes or None when written already)

    def pil(name, route, **kw):
        p = os.path.join(out, name + ".jpg")
        (im.convert("L") if kw.pop("gray", False) else im).save(p, quality=92, **kw)
        items.append((name, route))

    pil("pil_444", "native", subsampling=0)
    pil("pil_422", "native", subsampling=1)
    pil("pil_420", "native", subsampling=2)
    pil("pil_gray", "native", gray=True)
    pil("pil_prog", "progressive", subsampling=2, progressive=True)
    ff = shutil.which("ffmpeg")
    for fmt, route in (("yuvj444p", "own"), ("yuvj422p", "own"), ("yuvj420p", "native")):
        if not ff:
            print("jpeg_fixtures: no ffmpeg, %s left out" % fmt, file=sys.stderr)
            continue
        name = "ff_" + fmt[4:7]
        subprocess.run([ff, "-loglevel", "error", "-y", "-i", src_png, "-pix_fmt", fmt, "-q:v", "2", os.path.join(out, name + ".jpg")], check=True)
        items.append((name, route))
    for name, route, factors, rst in (("enc_440", "own", [(1, 2), (1, 1), (1, 1)], 0), ("enc_411", "own", [(4, 1), (1, 1), (1, 1)], 0),
                                      ("enc_all21", "own", [(2, 1), (2, 1), (2, 1)], 0), ("enc_gray22", "own", [(2, 2)], 0),
                                      ("enc_420_rst", "native", [(2, 2), (1, 1), (1, 1)], 3), ("enc_1x2_rst", "own", [(1, 2), (1, 2), (1, 2)], 5),
                                      ("enc_444", "native", [(1, 1), (1, 1), (1, 1)], 0)):
        with open(os.path.join(out, name + ".jpg"), "wb") as f:
            f.write(encode(im, factors, rst))
        items.append((name, route))
    with open(os.path.join(out, "manifest.txt"), "w") as m:
        for name, route in items:
            p = os.path.join(out, name + ".jpg")
            ref = Image.open(p)
            ref.load()
            with open(os.path.join(out, name + ".rgb"), "wb") as f:
                f.write(ref.convert("RGB").tobytes())
            m.write("%s %s %d %d\n" % (name, route, ref.size[0], ref.size[1]))
    os.unlink(src_png)


if __name__ == "__main__":
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    main()
