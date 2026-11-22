#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Writes the tiny tracker-module fixtures (tiny.mod, tiny.xm). Run from anywhere; the files land next to it.

Both play a 32-byte looped square wave (C-major arpeggio on channel 1).
tiny.mod: 4 channels, orders [0, 0], pattern 0 breaks (D00) at row 15 -> 2 x 16 rows at speed 6 / 125 BPM = 3.84 s.
tiny.xm:  2 channels, one order, a 16-row pattern -> 1.92 s.
"""
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
SQUARE = bytes([0x50] * 16 + [0xB0] * 16)          # signed 8-bit, +80 / -80


def mod():
    out = bytearray(b"jc tiny mod".ljust(20, b"\0"))
    # sample 1: name, length (words), finetune, volume, repeat start, repeat length (words)
    out += b"square".ljust(22, b"\0") + struct.pack(">HBBHH", len(SQUARE) // 2, 0, 64, 0, len(SQUARE) // 2)
    for _ in range(30):
        out += bytes(22) + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    out += bytes([2, 127]) + bytes([0, 0] + [0] * 126) + b"M.K."
    assert len(out) == 1084
    pat = bytearray(64 * 4 * 4)
    for row, period in ((0, 428), (4, 339), (8, 285), (12, 214)):   # C-2 E-2 G-2 C-3
        o = row * 16
        pat[o:o + 4] = bytes([(1 & 0xF0) | (period >> 8), period & 0xFF, (1 & 0x0F) << 4, 0])
    o = 15 * 16 + 4                                   # channel 2, row 15: D00 pattern break
    pat[o:o + 4] = bytes([0, 0, 0x0D, 0x00])
    return bytes(out + pat + SQUARE)


def xm():
    rows, chans = 16, 2
    hdr = b"Extended Module: " + b"jc tiny xm".ljust(20, b"\0") + b"\x1a" + b"make_modules.py".ljust(20, b"\0")
    hdr += struct.pack("<HI", 0x0104, 276)
    hdr += struct.pack("<HHHHHHHH", 1, 0, chans, 1, 1, 1, 6, 125) + bytes(256)
    assert len(hdr) == 336
    data = bytearray()
    for row in range(rows):
        for ch in range(chans):
            note = {0: 49, 4: 53, 8: 56, 12: 61}.get(row) if ch == 0 else None   # C-4 E-4 G-4 C-5
            data += bytes([note, 1, 0, 0, 0]) if note else bytes(5)
    pat = struct.pack("<IBHH", 9, 0, rows, len(data)) + data
    ins = struct.pack("<I", 263) + b"square".ljust(22, b"\0") + bytes([0]) + struct.pack("<H", 1)
    ins += struct.pack("<I", 40) + bytes(96) + bytes(48) + bytes(48) + bytes(14) + struct.pack("<H", 0)
    ins = ins.ljust(263, b"\0")
    smp = struct.pack("<IIIBbBBbB", len(SQUARE), 0, len(SQUARE), 64, 0, 1, 128, 0, 0) + b"square".ljust(22, b"\0")
    prev, delta = 0, bytearray()                     # XM sample data is delta coded
    for b in SQUARE:
        v = b - 256 if b > 127 else b
        delta.append((v - prev) & 0xFF)
        prev = v
    return bytes(hdr + pat + ins + smp + delta)


if __name__ == "__main__":
    for name, data in (("tiny.mod", mod()), ("tiny.xm", xm())):
        with open(os.path.join(HERE, name), "wb") as f:
            f.write(data)
        print(name, len(data))
