#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""make fonts-check: the fonts built fresh from fonts-src/ must equal the tracked fonts/*.ttf.
usage: check_fonts_fresh.py <fresh dir> <tracked dir>
Byte equality is the rule (the font tools are deterministic: SOURCE_DATE_EPOCH is pinned). When the bytes differ the message says whether the cmap is still
equal (then it is a tool-version difference: rebuild and commit) or not (then a list, a source or app/main changed: make fonts-all, copy, commit).
The cmap comparison reads the tables with the standard library only; no fontTools needed."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_font_glyphs import cmap_chars   # noqa: E402

NAMES = ['ui-tc.ttf', 'ui-sc.ttf', 'ui-ext.ttf', 'ui-icon.ttf']

def main():
    fresh, tracked = sys.argv[1:3]
    bad = 0
    for n in NAMES:
        a, b = os.path.join(fresh, n), os.path.join(tracked, n)
        if not os.path.isfile(b):
            print(f'FAIL {n}: not in {tracked}'); bad += 1; continue
        if open(a, 'rb').read() == open(b, 'rb').read():
            print(f'ok   {n} identical ({os.path.getsize(b)} bytes)')
            continue
        ca, cb = cmap_chars(a), cmap_chars(b)
        if ca == cb:
            print(f'FAIL {n}: same characters but different bytes (tool versions? see tools/requirements-fonts.txt); rebuild and commit')
        else:
            print(f'FAIL {n}: fresh build has {len(ca - cb)} character(s) the tracked file lacks, tracked has {len(cb - ca)} the fresh build lacks: make fonts-all, copy to fonts/, commit')
        bad += 1
    sys.exit(1 if bad else 0)

main()
