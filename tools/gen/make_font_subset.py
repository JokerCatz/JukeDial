#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Build the app UI fonts: Noto Sans TC (ui-tc.ttf) and Noto Sans SC (ui-sc.ttf) subset to the wanted characters.

usage: make_font_subset.py <primary.otf> <out ui-tc.ttf> <fallback.otf> <out ui-sc.ttf> [export_dir | library.muk ...]

Wanted characters = ASCII, kana, CJK punctuation, full-width forms, the app/main sources, Big5 level 1 (5401 common Traditional characters),
GB2312 (6763 Simplified; FONT_GB_LEVEL2=0 in the environment keeps level 1 only, 3755), tools/gen/font_sets/library_chars.txt (what the
library needed on 2026-11-11), tools/gen/font_sets/extra_chars.txt (registered by hand) and, optionally, the export dirs / library.muk files.

primary = Noto Sans TC, fallback = Noto Sans SC: ui-sc.ttf only holds the characters ui-tc.ttf lacks
(mostly simplified Chinese); the app chains it as the LVGL fallback font.

Export dirs: every file/folder name and ID3 text frame under them. A library.muk argument adds every string in its JSON files (scene, sound and category names).
Needs fontTools (pip install fonttools)."""
import json, os, re, sys, zipfile
os.environ.setdefault('SOURCE_DATE_EPOCH', '1700000000')   # fontTools stamps head.modified with it: the same inputs give the same bytes (make fonts-check)
from fontTools import subset
from fontTools.ttLib import TTFont


def id3_text(path):
    """Text of ID3v2 text frames (T*** and TXXX), decoded; best effort."""
    out = []
    try:
        with open(path, 'rb') as f:
            h = f.read(10)
            if len(h) < 10 or h[:3] != b'ID3':
                return out
            ver = h[3]
            size = (h[6] << 21) | (h[7] << 14) | (h[8] << 7) | h[9]
            data = f.read(size)
    except OSError:
        return out
    i = 0
    while i + 10 <= len(data):
        fid = data[i:i + 4]
        if not re.fullmatch(rb'[A-Z0-9]{4}', fid):
            break
        b = data[i + 4:i + 8]
        n = ((b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3]) if ver == 4 else int.from_bytes(b, 'big')
        body = data[i + 10:i + 10 + n]
        i += 10 + n
        if not fid.startswith(b'T') or not body:
            continue
        enc, raw = body[0], body[1:]
        codec = {0: 'latin-1', 1: 'utf-16', 2: 'utf-16-be', 3: 'utf-8'}.get(enc, 'latin-1')
        try:
            out.append(raw.decode(codec, errors='ignore'))
        except Exception:
            pass
    return out


def json_strings(v):
    if isinstance(v, str):
        yield v
    elif isinstance(v, dict):
        for x in v.values():
            yield from json_strings(x)
    elif isinstance(v, list):
        for x in v:
            yield from json_strings(x)


def codec_set(codec, leads, trails, han_only=False):
    out = set()
    for a in leads:
        for b in trails:
            try:
                c = bytes([a, b]).decode(codec)
            except UnicodeDecodeError:
                continue
            if len(c) == 1 and (not han_only or '\u4e00' <= c <= '\u9fff'):
                out.add(c)
    return out


def big5_level1():
    """The 5401 common Traditional characters: lead 0xA4..0xC6, trail 0x40..0x7E and 0xA1..0xFE (Han only: 0xC6A1.. are kana-like fillers)."""
    s = codec_set('big5', range(0xa4, 0xc7), list(range(0x40, 0x7f)) + list(range(0xa1, 0xff)), han_only=True)
    assert len(s) == 5401, len(s)
    return s


def gb2312(level2=True):
    """GB2312 hanzi: level 1 lead 0xB0..0xD7 (3755), level 2 0xD8..0xF7 (3008), trail 0xA1..0xFE."""
    s = codec_set('gb2312', range(0xb0, 0xf8 if level2 else 0xd8), range(0xa1, 0xff))
    assert len(s) == (6763 if level2 else 3755), len(s)
    return s


def listed_chars(path):
    """Characters of a font_sets file: lines starting with # are comments; whitespace is not a character."""
    if not os.path.isfile(path):
        return set()
    return {c for l in open(path, encoding='utf-8') if not l.startswith('#') for c in l if not c.isspace()}


def standard_chars(sets_dir=None, standard=True):
    """The character set of the standard build: ASCII / kana / full-width / punctuation, Big5 level 1, GB2312, the app sources, font_sets/library_chars.txt and
    font_sets/extra_chars.txt (the latter two read from sets_dir, default tools/gen/font_sets). standard=False: only the two list files (host tests with tiny fonts)."""
    here = os.path.dirname(os.path.abspath(__file__))
    sets_dir = sets_dir or os.path.join(here, 'font_sets')
    chars = set()
    if standard:
        chars |= set(chr(c) for c in range(0x20, 0x7f))
        chars |= set(chr(c) for c in range(0x3000, 0x3100))      # CJK punctuation, kana
        chars |= set(chr(c) for c in range(0xff00, 0xff70))      # full-width forms
        chars |= set('…·—–‘’“”•°')
        chars |= big5_level1() | gb2312(os.environ.get('FONT_GB_LEVEL2', '1') != '0')
    for n in ('library_chars.txt', 'extra_chars.txt'):
        chars |= listed_chars(os.path.join(sets_dir, n))
    if not standard:
        return chars
    for sub in (('app', 'main'), ('components', 'player_core', 'src'), ('components', 'player_core', 'include')):
        for root, _, files in os.walk(os.path.join(here, '..', '..', *sub)):
            for fn in files:
                if fn.endswith(('.c', '.h', '.def')):   # .def: the sound bank's labels (app/main/sfx_bank.def, sfx task 2)
                    chars |= set(open(os.path.join(root, fn), encoding='utf-8', errors='ignore').read())
    return chars


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    src, dst, src2, dst2, dirs = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:]
    build(src, dst, src2, dst2, standard_chars(), dirs)


def build(src, dst, src2, dst2, chars, dirs=()):
    """Write dst (primary font subset) and dst2 (fallback font: only what the primary lacks). Returns (chars in the primary, in the fallback, in neither)."""
    chars = set(chars)
    for d in dirs:
        if os.path.isfile(d) and d.endswith('.muk'):
            with zipfile.ZipFile(d) as z:
                for n in z.namelist():
                    if n.endswith('.json'):
                        chars |= set(''.join(json_strings(json.loads(z.read(n)))))
            continue
        for root, subdirs, files in os.walk(d):
            for name in subdirs + files:
                chars |= set(name)
            for fn in files:
                if fn.lower().endswith('.mp3'):
                    for t in id3_text(os.path.join(root, fn)):
                        chars |= set(t)
    chars = {c for c in chars if c.isprintable()}
    have = write_subset(src, dst, chars)
    rest = chars - have
    have2 = write_subset(src2, dst2, rest)
    print(f'chars wanted={len(chars)} primary={len(have)} fallback={len(have2)} missing={len(rest - have2)} (in neither font, e.g. the ui-ext.ttf scripts)')
    print(f'{dst} bytes={os.path.getsize(dst)}  {dst2} bytes={os.path.getsize(dst2)}')
    return have, have2, rest - have2


def write_subset(src, dst, chars):
    font = TTFont(src)
    cmap = font.getBestCmap()
    have = {c for c in chars if ord(c) in cmap}
    opts = subset.Options()
    opts.layout_features = ['*']
    opts.notdef_outline = True
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=[ord(c) for c in have])
    sub.subset(font)
    if 'CFF ' in font:
        cff_to_glyf(font)      # the board's FreeType build reads TrueType outlines only
    font.flavor = None
    font.save(dst)
    return have


def cff_to_glyf(font):
    """CFF (cubic) outlines -> glyf (quadratic), as fontTools' otf2ttf snippet does."""
    from fontTools.pens.cu2quPen import Cu2QuPen
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib import newTable
    order = font.getGlyphOrder()
    gs = font.getGlyphSet()
    glyphs = {}
    for name in order:
        pen = TTGlyphPen(gs)
        gs[name].draw(Cu2QuPen(pen, max_err=1.0, reverse_direction=True))
        glyphs[name] = pen.glyph()
    font["loca"] = newTable("loca")
    glyf = font["glyf"] = newTable("glyf")
    glyf.glyphOrder = order
    glyf.glyphs = glyphs
    del font["CFF "]
    if "VORG" in font:
        del font["VORG"]
    font["maxp"] = maxp = newTable("maxp")
    maxp.tableVersion = 0x00010000
    for k in ("maxZones", "maxTwilightPoints", "maxStorage", "maxFunctionDefs", "maxInstructionDefs",
              "maxStackElements", "maxSizeOfInstructions", "maxComponentElements"):
        setattr(maxp, k, 0)
    maxp.maxZones = 1
    font["head"].indexToLocFormat = 0
    font["head"].glyphDataFormat = 0
    font["post"].formatType = 3.0
    font.sfntVersion = "\x00\x01\x00\x00"
    font["glyf"].compile(font)   # fills bounding boxes
    maxp.recalc(font)


if __name__ == '__main__':
    main()
