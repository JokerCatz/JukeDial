#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Colour literals on pages: a page (ui/scr_*.c) takes its colours from the tokens (ui_theme.h: UI_BG UI_PANEL UI_FG UI_SEL UI_SYS UI_HELP UI_ON UI_GOLD ...,
game_style.c's field palettes). A literal (lv_color_hex / 0xRRGGBB) is a ratchet like ui_lint.allow: the count per page may only shrink (tools/check/ui_colour.allow).
  ui_colour_lint.py [--table]   (--table prints the counts as allow lines)"""
import glob, os, re, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
UI = os.path.join(ROOT, "app/main/ui")
LIT = re.compile(r'lv_color_hex\(|lv_color_make\(|\blv_palette_\w+\(|\b0x[0-9A-Fa-f]{6}\b')
PAGE_LIKE = ("game_ring.c", "game_result.c", "clock_face.c", "album_view.c")   # page-like modules that build what a page shows: scanned as pages
# a comment ends the line only outside a string ("http://..." holds no comment); strings are blanked so a literal inside one is not counted either
TOK = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//.*')
def code(ln): return TOK.sub(' ', ln)
def counts():
    out = {}
    files = glob.glob(os.path.join(UI, "scr_*.c")) + [os.path.join(UI, f) for f in PAGE_LIKE]
    for p in sorted(files):
        n = 0
        for ln in open(p, encoding="utf-8"):
            n += len(LIT.findall(code(ln)))
        if n: out[os.path.basename(p)] = n
    return out
def main():
    c = counts()
    if "--table" in sys.argv:
        for k, v in c.items(): print(k, v)
        return 0
    allow = {}
    path = os.path.join(ROOT, "tools/check/ui_colour.allow")
    for ln in open(path):
        ln = ln.split('#')[0].split()
        if len(ln) == 2: allow[ln[0]] = int(ln[1])
    bad = []
    for k in sorted(set(c) | set(allow)):
        a, b = c.get(k, 0), allow.get(k, 0)
        if a > b: bad.append("%s: %d colour literals, allowance %d (use a token of ui_theme.h)" % (k, a, b))
        elif a < b: bad.append("%s: %d colour literals, allowance %d (lower the allowance)" % (k, a, b))
    for m in bad: print("ui_colour_lint:", m)
    if not bad: print("ui_colour_lint ok")
    return 1 if bad else 0
sys.exit(main())
