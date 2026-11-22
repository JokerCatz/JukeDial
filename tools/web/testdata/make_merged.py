#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Synthetic merged flash images for webcheck (NOT firmware): the same layout as a real `make release` image (bootloader 0x2000, table 0x8000, app 0x10000, fonts 0xC00000, 0xFF between),
with valid ESP image structure (header, segments, checksum byte, appended SHA-256), a valid partition table (entries + MD5 line), an esp_app_desc_t and the board marker.
Library: build(board, ...) -> bytes (also used by tools/check/check_release.py).  CLI: make_merged.py [OUTDIR]  writes OUTDIR/jukedial-0.0.0-test-{round,fakepod}.bin (default: testdata/merged/)."""
import hashlib, os, struct, sys
LAYOUT = [('nvs', 1, 2, 0x9000, 0x6000), ('phy_init', 1, 1, 0xf000, 0x1000), ('factory', 0, 0, 0x10000, 0xBF0000), ('fonts', 1, 0x40, 0xC00000, 4 << 20)]

def esp_image(payload, chip=32, app_desc=None, segments=1):
    """header 24 B + `segments` segments + pad so the checksum is the last byte of a 16 B block + SHA-256 (hash_appended = 1)"""
    hdr = bytearray(24); hdr[0] = 0xE9; hdr[1] = segments; hdr[12:14] = struct.pack('<H', chip); hdr[23] = 1
    body = bytearray()
    per = max(16, len(payload) // segments)
    pos = 0
    for i in range(segments):
        chunk = payload[pos:pos + per] if i < segments - 1 else payload[pos:]
        pos += len(chunk)
        if i == 0 and app_desc is not None: chunk = app_desc + chunk   # the first segment starts with esp_app_desc_t (at image offset 0x20 = 24 + 8)
        chunk += b'\0' * (-len(chunk) % 4)
        body += struct.pack('<II', 0x42000000 + i * 0x100000, len(chunk)) + chunk
    img = bytes(hdr) + bytes(body)
    img += b'\0' * ((-(len(img) + 1)) % 16) + b'\x5a'   # checksum byte last in its block (value not checked by the page)
    return img + hashlib.sha256(img).digest()

def app_desc(version, project='jc_player', date='Nov 22 2026', time='12:00:00', idf='v6.1'):
    d = struct.pack('<IIII', 0xABCD5432, 0, 0, 0)
    for s, n in ((version, 32), (project, 32), (time, 16), (date, 16), (idf, 32)): d += s.encode().ljust(n, b'\0')
    return d + b'\0' * (256 - len(d))

def partition_table(layout=LAYOUT):
    t = b''.join(b'\xaa\x50' + bytes([ty, sub]) + struct.pack('<II', off, size) + n.encode().ljust(16, b'\0') + b'\0\0\0\0' for n, ty, sub, off, size in layout)
    t += b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(t).digest()
    return t.ljust(0xC00, b'\xff')

def build(board='jc3636f118', version='0.0.0-test', marker=True, layout=LAYOUT, fonts_len=100000, app_len=30000, boot_len=2000):
    boot = esp_image(bytes((i * 7) & 255 for i in range(boot_len)))
    pay = bytearray((i * 13 + 5) & 255 for i in range(app_len))
    if marker is True: pay[1000:1000] = b'JUKEDIAL_BOARD=' + board.encode() + b';\0'
    elif marker: pay[1000:1000] = marker
    app = esp_image(bytes(pay), app_desc=app_desc(version), segments=2)
    flash = bytearray(b'\xff' * (0xC00000 + fonts_len))
    flash[0x2000:0x2000 + len(boot)] = boot
    flash[0x8000:0x8000 + 0xC00] = partition_table(layout)
    flash[0x10000:0x10000 + len(app)] = app
    flash[0xC00000:] = bytes((i * 3 + 1) & 255 for i in range(fonts_len))
    return bytes(flash)

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'merged')
    os.makedirs(out, exist_ok=True)
    for b, n in (('jc3636f118', 'round'), ('fakepod', 'fakepod')): open(os.path.join(out, f'jukedial-0.0.0-test-{n}.bin'), 'wb').write(build(b))
