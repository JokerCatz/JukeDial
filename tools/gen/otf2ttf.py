#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Offline CFF (.otf) -> TrueType (glyf) conversion of the Noto sources for the web font builder (hb-subset keeps outlines as they are and the board's FreeType reads
glyf only).  usage: otf2ttf.py <in.otf> <out.ttf>   Needs fontTools (the interpreter that runs make fonts-build: FONT_PY); reuses make_font_subset.cff_to_glyf, the same
conversion the device fonts get.  Variable fonts are not handled here (they are TrueType already; hb-subset pins their axes)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fontTools.ttLib import TTFont
from make_font_subset import cff_to_glyf

def main():
    if len(sys.argv) != 3: sys.exit(__doc__)
    f = TTFont(sys.argv[1])
    if 'CFF ' in f: cff_to_glyf(f)
    f.flavor = None
    f.save(sys.argv[2])

if __name__ == '__main__':
    main()
