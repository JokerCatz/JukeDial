#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Builds the example glyph-pack lists (午餐 / 抽人 / 點子) from tools/web/lists/*.json, the same models the settings
page loads as templates. Renders like the page: grapheme clusters, two sizes, text as A8 masks (Noto Sans CJK TC),
emoji as RGB565A8 (Noto Color Emoji). Needs Pillow and those fonts (Debian/Ubuntu: fonts-noto-cjk fonts-noto-color-emoji).

usage: tools/gen/make_lists.py [out_dir]          (default tools/build-lists/; copy to /sdcard/lists/ with tools/board/tf_put.py
                                               or in USB drive mode)
"""
import json, os, sys, unicodedata
from PIL import Image, ImageFont, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import glyphpack as gp

SIZES = [(24, 30), (40, 50)]                     # same as tools/web/glyphpack.js
CJK = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
EMOJI = "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf"


def cjk_font(px):
    for i in range(10):                          # the TC face of the collection
        f = ImageFont.truetype(CJK, px, index=i)
        if f.getname()[0] == "Noto Sans CJK TC":
            return f
    return ImageFont.truetype(CJK, px)


def is_ext(c):   # joins the previous cluster: marks, variation selectors, skin tones, keycap, tags
    o = ord(c)
    return unicodedata.combining(c) or 0xFE00 <= o <= 0xFE0F or 0x1F3FB <= o <= 0x1F3FF or o == 0x20E3 or 0xE0020 <= o <= 0xE007F


def clusters(s):
    """Grapheme clusters, good enough for text + emoji (Python has no Intl.Segmenter): ZWJ sequences, flags, modifiers."""
    out = []
    for c in s:
        o = ord(c)
        if out and (is_ext(c) or c == "‍" or out[-1].endswith("‍")):
            out[-1] += c
        elif out and 0x1F1E6 <= o <= 0x1F1FF and len(out[-1]) == 1 and 0x1F1E6 <= ord(out[-1]) <= 0x1F1FF:
            out[-1] += c                         # regional indicator pair = one flag
        else:
            out.append(c)
    return out


def is_emoji(c):
    o = ord(c[0])
    return o >= 0x1F000 or 0x2600 <= o <= 0x27BF or "‍" in c or "️" in c or 0x2B00 <= o <= 0x2BFF


def is_word(c):
    if len(c) != 1:
        return False
    o = ord(c)
    return (c.isascii() and (c.isalnum() or c in "'.,:;!?%&@#$-_/()+")) or (0xC0 <= o < 0x530 and c.isalpha())


def a8(img, x0, y0, x1, y1):
    return img.crop((x0, y0, x1 + 1, y1 + 1)).tobytes()


def rgb565a8(rgba):
    w, h = rgba.size
    px = rgba.load()
    col, al = bytearray(), bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            col += bytes((v & 255, v >> 8))
            al.append(a)
    return bytes(col + al)


def render(c, px, line_h, fonts):
    font = fonts[px]
    if c.isspace():
        return dict(w=0, h=0, x=0, y=0, adv=max(1, min(255, round(font.getlength(c)))), fmt=gp.FMT_EMPTY, data=b"")
    if is_emoji(c):
        big = Image.new("RGBA", (220, 160), (0, 0, 0, 0))
        try:
            ImageDraw.Draw(big).text((10, 10), c, font=fonts["emoji"], embedded_color=True)
        except Exception:
            pass
        box = big.getbbox()
        if box:
            e = big.crop(box)
            k = px / max(e.size[1], 1) * 0.95
            w, h = max(1, round(e.size[0] * k)), max(1, round(e.size[1] * k))
            e = e.resize((min(w, 255), min(h, 255)), Image.LANCZOS)
            return dict(w=e.size[0], h=e.size[1], x=1, y=(line_h - e.size[1]) // 2, adv=min(255, e.size[0] + 2), fmt=gp.FMT_RGB565A8, data=rgb565a8(e))
    pad = px // 2
    W, H = px * 3, line_h + 2 * pad
    base = pad + round((line_h - px) / 2 + px * 0.88)
    img = Image.new("L", (W, H), 0)
    ImageDraw.Draw(img).text((px, base), c, font=font, fill=255, anchor="ls")
    adv = max(1, min(255, round(font.getlength(c))))
    box = img.point(lambda v: 255 if v > 8 else 0).getbbox()
    if not box:
        return dict(w=0, h=0, x=0, y=0, adv=adv, fmt=gp.FMT_EMPTY, data=b"")
    x0, y0, x1, y1 = box[0], box[1], min(box[2] - 1, box[0] + 254), min(box[3] - 1, box[1] + 254)
    return dict(w=x1 - x0 + 1, h=y1 - y0 + 1, x=max(-128, min(127, x0 - px)), y=max(-128, min(127, y0 - pad)), adv=adv, fmt=gp.FMT_A8, data=a8(img, x0, y0, x1, y1))


def compile_model(m):
    ids, glyph_list = {}, []

    def gid(c):
        if c not in ids:
            ids[c] = len(glyph_list)
            glyph_list.append(c)
        return ids[c]

    def enc(s):
        out = []
        for k, line in enumerate(str(s or "").split("\n")):
            if k:
                out.append(gp.NL)
            out.extend(gid(c) for c in clusters(line))
        return out

    gid(" ")
    ell = gid("…")
    groups = [gp.Node(enc(g["name"]), weight=g.get("weight", 1), children=[gp.Node(enc(it["text"]), weight=it.get("weight", 1), flags=it.get("flags", 0)) for it in g["items"]]) for g in m["groups"]]
    root = gp.Node(enc(m["title"]), children=groups)
    fonts = {px: cjk_font(px) for px, _ in SIZES}
    fonts["emoji"] = ImageFont.truetype(EMOJI, 109)
    glyphs = [dict(flags=(gp.GF_SPACE if c.isspace() else 0) | (gp.GF_WORD if is_word(c) else 0), sizes=[render(c, px, lh, fonts) for px, lh in SIZES]) for c in glyph_list]
    return gp.build(gp.KINDS[m["kind"]], SIZES, glyphs, [], root, ell, source=m), len(glyph_list)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "build-lists")
    os.makedirs(out, exist_ok=True)
    src = os.path.join(HERE, "..", "web", "lists")
    for fn in sorted(os.listdir(src)):
        if not fn.endswith(".json"):
            continue
        m = json.load(open(os.path.join(src, fn), encoding="utf-8"))
        z, n = compile_model(m)
        p = os.path.join(out, fn[:-5] + ".zip")
        open(p, "wb").write(z)
        print("%s: %d glyphs, %d bytes" % (p, n, len(z)))


if __name__ == "__main__":
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    main()
