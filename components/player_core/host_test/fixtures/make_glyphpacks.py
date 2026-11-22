#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Regenerates the glyph pack fixtures for test_glyphpack.c (run from this directory).
Synthetic bitmaps (no fonts needed); tools/web/gp_fixture.mjs builds the same pack with the web page's writer."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../tools"))
import glyphpack as gp

SIZES = [(24, 30), (40, 50)]
NGLYPHS, ELLIPSIS = 12, 11


def glyph(g, s):
    if g == 0:
        return dict(w=0, h=0, x=0, y=0, adv=5 + 5 * s, fmt=gp.FMT_EMPTY, data=b"")
    fmt = gp.FMT_RGB565A8 if g % 5 == 4 else gp.FMT_A8
    w, h = 4 + (g * 3 + s * 7) % 9, 6 + (g + s * 5) % 10
    data = bytes((g * 37 + s * 11 + i * 13) & 255 for i in range(w * h * gp.bpp(fmt)))
    return dict(w=w, h=h, x=g % 3 - 1, y=2 + s, adv=w + 2, fmt=fmt, data=data)


def content():
    glyphs = [dict(flags=gp.GF_SPACE if g == 0 else (gp.GF_WORD if g % 2 else 0), sizes=[glyph(g, s) for s in range(len(SIZES))])
              for g in range(NGLYPHS)]
    images = [dict(w=10, h=8, fmt=gp.FMT_RGB565A8, data=bytes((i * 7 + 3) & 255 for i in range(10 * 8 * 3)))]
    N = gp.Node
    root = N([1, 2, 3], children=[
        N([4, 5], children=[N([6, 7, 0, 8], weight=3), N([9], image=0, flags=1)]),
        N([10, gp.NL, 2], children=[N([3, 3, 3], weight=0)]),
    ])
    return glyphs, images, root


def picks():
    """test_listpick: groups with weights (0 = never), an empty group, a weight-0 group, a big group (cap tests)"""
    N = gp.Node
    return N([1], children=[
        N([2], children=[N([3], weight=1), N([4], weight=3), N([5], weight=0), N([6], weight=2)]),   # 1: weights 1 3 0 2
        N([7], children=[N([8], weight=0), N([9], weight=0)]),                                       # 2: nothing pickable
        N([10], weight=0, children=[N([1])]),                                                        # 3: shop weight 0
        N([2, 3], children=[N([1 + i % 10]) for i in range(100)]),                                   # 4: 100 items
        N([4]),                                                                                      # 5: no children
    ])


def vocab():
    """test_vocab: kind 4 deck. Cards are the root's children: text = word, weight = band (0 = left out), flags = class;
    child 0 = meaning, child 1 = extra (optional). Classes: 1 noun 2 verb 3 adj 8 prefix 9 root."""
    N = gp.Node
    def card(word, meaning, cls, band, extra=None):
        ch = [N(meaning)] if meaning is not None else []
        if extra is not None:
            ch.append(N(extra))
        return N(word, weight=band, flags=cls, children=ch)
    return N([1, 2, 3], children=[
        card([1], [2], 1, 1),            # 0
        card([1, 1], [3], 1, 1),         # 1
        card([1, 2], [4], 1, 2),         # 2
        card([1, 3], [5], 2, 1),         # 3
        card([1, 4], [6], 2, 1),         # 4
        card([1, 5], [7], 3, 1),         # 5
        card([1, 6], [2], 1, 1),         # 6: same meaning as 0
        card([1, 7], [8], 1, 9),         # 7: far band
        card([1, 8], [9], 1, 0),         # 8: weight 0, left out
        card([1, 9], None, 1, 1),        # 9: no meaning, left out
        card([2, 1], [10], 8, 1, [11]),  # 10: prefix with examples
        card([2, 2], [3, 3], 9, 1, [11, 11]),   # 11: root
    ])


def vocab_big(n=2000):
    """test_vocab sampling: n cards, classes 1..4 in turn, bands 1..20, all meanings different"""
    N = gp.Node
    return N([1], children=[N([1 + i % 10, 1 + i // 10 % 10, 1 + i // 100 % 10], weight=1 + i % 20, flags=1 + i % 4,
                              children=[N([2 + i % 9, 2 + i // 9 % 9, 2 + i // 81 % 9, 2 + i // 729 % 9])]) for i in range(n)])


if __name__ == "__main__":
    glyphs, images, root = content()
    src = {"kind": "lunch", "title": "午餐"}
    open("gp_basic.zip", "wb").write(gp.build(gp.KINDS["lunch"], SIZES, glyphs, images, root, ELLIPSIS, source=src))
    open("gp_picks.zip", "wb").write(gp.build(gp.KINDS["people"], SIZES, glyphs, images, picks(), ELLIPSIS))
    open("gp_deflate.zip", "wb").write(gp.build(gp.KINDS["lunch"], SIZES, glyphs, images, root, ELLIPSIS, source=src, deflate=True))
    open("gp_vocab.zip", "wb").write(gp.build(gp.KINDS["vocab"], SIZES, glyphs, images, vocab(), ELLIPSIS))
    open("gp_vocab_big.zip", "wb").write(gp.build(gp.KINDS["vocab"], SIZES, glyphs, [], vocab_big(), ELLIPSIS))
