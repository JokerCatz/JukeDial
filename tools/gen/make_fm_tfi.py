#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Write example TFI patches for the instrument's YM2612 FM chip (stage 5): programs 16+ on the board.
TFI (TFM Music Maker / DefleMask), 42 bytes: algorithm, feedback, then per operator in register order OP1, OP3,
OP2, OP4: MUL, DT (0..6, 3 = none), TL, RS, AR, DR, SR, RR, SL, SSG-EG.
usage: make_fm_tfi.py <out_dir>   then upload to /sdcard/instrument/fm/ with tools/board/tf_put.py"""
import os, sys
if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
    print(__doc__); sys.exit(0)

# name: (alg, fb, [OP1, OP2, OP3, OP4]) with each op (mul, dt, tl, rs, ar, dr, sr, rr, sl, ssg), logical order
PATCHES = {
    "glass_pad": (5, 6, [(1, 3, 28, 0, 10, 2, 0, 5, 2, 0), (2, 4, 18, 0, 9, 1, 0, 5, 1, 0),
                         (3, 2, 22, 0, 9, 1, 0, 5, 1, 0), (1, 3, 16, 0, 10, 1, 0, 5, 1, 0)]),
    "fat_bass": (3, 6, [(0, 3, 26, 2, 31, 8, 3, 9, 4, 0), (1, 3, 34, 2, 31, 10, 4, 9, 5, 0),
                        (2, 3, 38, 2, 31, 14, 0, 9, 9, 0), (1, 3, 6, 2, 31, 5, 2, 10, 2, 0)]),
}
FILE_SLOT = [0, 2, 1, 3]                       # logical op -> file position (OP1, OP3, OP2, OP4)

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for name, (alg, fb, ops) in PATCHES.items():
    slots = [None] * 4
    for i, op in enumerate(ops):
        slots[FILE_SLOT[i]] = op
    data = bytes([alg, fb] + [v for op in slots for v in op])
    assert len(data) == 42
    with open(os.path.join(out, name + ".tfi"), "wb") as f:
        f.write(data)
    print(name + ".tfi", len(data))
