#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Screenshots of the board over the console, all in one connection (does not reset the board).
usage: shot.py /dev/ttyACM0 [--wait S] item ...   item = a console command, or @file.png to save the screen.
Commands are sent 1.5 s apart; the board's 'shot' prints RGB565 rows as base64."""
import base64, struct, sys, time, zlib
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders
from boardport import open_port

def png(path, w, h, rows):
    rgb = bytearray()
    for y in range(h):
        r = rows.get(y, b'')
        r = r + b'\0' * (2 * w - len(r))         # a row cut by log output: the rest black
        for x in range(w):
            v = r[2 * x] | r[2 * x + 1] << 8
            rgb += bytes(((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31))
    raw = b''.join(b'\0' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def grab_once(s, rows):
    """one 'shot': fills rows (only complete ones); returns (w, h) or None"""
    s.reset_input_buffer()
    s.write(b'shot\n')
    w, h, buf, t0 = 0, 0, b'', time.time()
    while time.time() - t0 < 30:
        buf += s.read(65536)
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            line = line.strip().decode('latin1')
            if line.startswith('SHOT begin'):
                _, _, w, h = line.split(); w, h = int(w), int(h)
            elif line.startswith('SHOTR '):
                try:
                    _, y, d = line.split(' ', 2); d = base64.b64decode(d)
                    if len(d) == 2 * w and int(y) not in rows: rows[int(y)] = d
                except ValueError:
                    pass                          # a row mixed with log output
            elif line.startswith('SHOT end'):
                return w, h
    return None

def grab(s, path):
    """saves the screen as PNG; returns True only if every row arrived"""
    # rows get lost when the board logs meanwhile (USB buffer): up to 4 shots, missing rows taken from the next
    rows, wh = {}, None
    for _ in range(4):
        wh = grab_once(s, rows) or wh
        if wh and len(rows) >= wh[1]: break
    if not wh: print(f'{path}: no screenshot'); return False
    png(path, wh[0], wh[1], rows); print(f'{path}: {wh[0]}x{wh[1]} rows={len(rows)}')
    return len(rows) >= wh[1]                 # False: some rows are black in the file

def main():
    a = sys.argv[1:]
    port, items, wait = a[0], a[1:], 0
    if items[:1] == ['--wait']: wait, items = float(items[1]), items[2:]
    # DTR/RTS left at pyserial's default (both high): that never resets the board; setting them low here did
    s = open_port(port, unopened=True)
    time.sleep(wait)
    for it in items:
        if it.startswith('@'): grab(s, it[1:])
        else: s.write(f'{it}\n'.encode()); time.sleep(1.5)

if __name__ == "__main__":
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    main()
