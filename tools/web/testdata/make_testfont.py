#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Tiny synthetic TrueType fonts for webcheck / fontbuild_test (no fontTools: the tables are written by hand). Every glyph is a square; the point is the cmap.
Writes testdata/fonts/{tc,sc,ext}.ttf (gitignored: generated test data, never committed).  Standard library only."""
import os, struct
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fonts')
ASCII = ''.join(chr(c) for c in range(0x20, 0x7f))
SETS = {'tc': ASCII + '台灣音樂電聽歌選單設定', 'sc': ASCII + '台湾音乐电听歌选单设定网络', 'ext': '★♪한글😀'}   # tc/sc overlap on purpose: sc must only get what tc lacks

def table_dir(tabs):
    tabs = sorted(tabs.items()); n = len(tabs)
    sr = 1
    while sr * 2 <= n: sr *= 2
    head = struct.pack('>IHHHH', 0x00010000, n, sr * 16, sr.bit_length() - 1, n * 16 - sr * 16)
    off = 12 + 16 * n; recs = b''; body = b''
    for tag, d in tabs:
        pad = d + b'\0' * (-len(d) % 4)
        s = sum(struct.unpack('>%dI' % (len(pad) // 4), pad)) & 0xffffffff
        recs += tag.encode() + struct.pack('>III', s, off + len(body), len(d)); body += pad
    return head + recs + body

def build(chars, name):
    cps = sorted({ord(c) for c in chars if ord(c) < 0xfffe})
    n = len(cps) + 1   # glyph 0 = .notdef
    sq = struct.pack('>hhhhh', 1, 50, 0, 450, 700) + struct.pack('>H', 3) + struct.pack('>H', 0) + b'\0\0'   # header(nContours,bbox), endPts[0]=3, instrLen=0
    pts = [(50, 0), (450, 0), (450, 700), (50, 700)]
    flags = b'\x01' * 4; xs = b''; ys = b''; px = py = 0
    for x, y in pts: xs += struct.pack('>h', x - px); ys += struct.pack('>h', y - py); px, py = x, y
    glyph = struct.pack('>hhhhh', 1, 50, 0, 450, 700) + struct.pack('>HH', 3, 0) + flags + xs + ys
    glyph += b'\0' * (-len(glyph) % 4)
    glyf = glyph * n; loca = b''.join(struct.pack('>I', i * len(glyph)) for i in range(n + 1))
    head = struct.pack('>IIIIHHqqhhhhHHhhh', 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0, 1000, 0, 0, 0, 0, 500, 700, 0, 8, 2, 1, 0)
    hhea = struct.pack('>IhhhHhhhhhhhhhhhH', 0x00010000, 800, -200, 0, 500, 0, 0, 500, 1, 0, 0, 0, 0, 0, 0, 0, n)
    maxp = struct.pack('>IHHHHHHHHHHHHHH', 0x00010000, n, 4, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0)
    hmtx = b''.join(struct.pack('>Hh', 500, 50) for _ in range(n))
    # cmap format 4, one segment per code point (+ the 0xFFFF end segment); BMP only (the ext test font's emoji is dropped from the BMP table, kept in format 12)
    bmp = [c for c in cps if c < 0x10000]
    segs = [(c, c, (i + 1 - c) & 0xffff) for i, c in enumerate(bmp)] + [(0xffff, 0xffff, 1)]
    sc = len(segs); sr = 1
    while sr * 2 <= sc: sr *= 2
    f4 = struct.pack('>HHHHHHH', 4, 16 + 8 * sc, 0, sc * 2, sr * 2, sr.bit_length() - 1, sc * 2 - sr * 2)
    f4 += b''.join(struct.pack('>H', e) for s, e, d in segs) + b'\0\0' + b''.join(struct.pack('>H', s) for s, e, d in segs) + b''.join(struct.pack('>H', d) for s, e, d in segs) + b'\0\0' * sc
    f4 = f4[:2] + struct.pack('>H', len(f4)) + f4[4:]
    groups = b''.join(struct.pack('>III', c, c, i + 1) for i, c in enumerate(cps))
    f12 = struct.pack('>HHIII', 12, 0, 16 + len(groups), 0, len(cps)) + groups
    cm = struct.pack('>HH', 0, 2) + struct.pack('>HHI', 3, 1, 20) + struct.pack('>HHI', 3, 10, 20 + len(f4)) + f4 + f12
    post = struct.pack('>IIhhIIIII', 0x00030000, 0, 0, 0, 0, 0, 0, 0, 0)
    nm = name.encode('utf-16-be'); nameT = struct.pack('>HHH', 0, 1, 18) + struct.pack('>HHHHHH', 3, 1, 0x409, 1, len(nm), 0) + nm
    return table_dir({'head': head, 'hhea': hhea, 'maxp': maxp, 'hmtx': hmtx, 'cmap': cm, 'loca': loca, 'glyf': glyf, 'post': post, 'name': nameT})

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for k, v in SETS.items(): open(os.path.join(OUT, k + '.ttf'), 'wb').write(build(v, 'Test ' + k))
