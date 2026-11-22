#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""viz_png.py <dir of <kind>_<var>_<w>x<h>.rgb565 from viz_dump> [out dir, default docs/images/viz]
writes host_<kind>_<var>.png per file and host_sheet.png with everything side by side (RGB565 host order = little endian).
No dependencies beyond the standard library (PNG written like tools/simtools/shot.py)."""
import os, re, struct, sys, zlib

def png(path, w, h, rgb):
    raw = b''.join(b'\0' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def to_rgb(data, n):
    out = bytearray(n * 3)
    for i in range(n):
        v = data[2 * i] | data[2 * i + 1] << 8
        out[3 * i] = (v >> 11 & 31) * 255 // 31; out[3 * i + 1] = (v >> 5 & 63) * 255 // 63; out[3 * i + 2] = (v & 31) * 255 // 31
    return out

def main():
    if len(sys.argv) < 2:
        print('usage: viz_png.py <dir with <kind>_<var>_<w>x<h>.rgb565> [out dir]', file=sys.stderr); return 2
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'docs', 'images', 'viz')
    os.makedirs(dst, exist_ok=True)
    items = []
    for f in sorted(os.listdir(src)):
        m = re.fullmatch(r'(\w+?)_(\d+)_(\d+)x(\d+)\.rgb565', f)
        if not m: continue
        kind, var, w, h = m[1], int(m[2]), int(m[3]), int(m[4])
        data = open(os.path.join(src, f), 'rb').read()
        if len(data) != 2 * w * h: print(f'{f}: size mismatch'); continue
        rgb = to_rgb(data, w * h)
        if kind == 'maps':                       # the colour-mapping sheet: one image, not part of host_sheet.png
            png(os.path.join(dst, 'host_maps.png'), w, h, rgb); print(f'host_maps.png ({w}x{h}) in {dst}'); continue
        png(os.path.join(dst, f'host_{kind}_{var}.png'), w, h, rgb)
        items.append((kind, var, w, h, rgb))
    if not items: return 0
    # sheet: one row per kind, 8 px gaps, mid-grey background
    rows = {}
    for it in items: rows.setdefault(it[0], []).append(it)
    gap = 8
    sw = max(sum(i[2] + gap for i in r) + gap for r in rows.values())
    sh = sum(max(i[3] for i in r) + gap for r in rows.values()) + gap
    sheet = bytearray(b'\x30' * (sw * sh * 3))
    y0 = gap
    for r in rows.values():
        x0 = gap
        for kind, var, w, h, rgb in r:
            for y in range(h):
                o = ((y0 + y) * sw + x0) * 3
                sheet[o:o + w * 3] = rgb[y * w * 3:(y + 1) * w * 3]
            x0 += w + gap
        y0 += max(i[3] for i in r) + gap
    png(os.path.join(dst, 'host_sheet.png'), sw, sh, sheet)
    print(f'{len(items)} images + host_sheet.png ({sw}x{sh}) in {dst}')
    return 0

if __name__ == '__main__':
    sys.exit(main())
