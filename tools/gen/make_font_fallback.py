#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Build ui-ext.ttf: the glyphs that neither Noto Sans TC (ui-tc.ttf) nor SC (ui-sc.ttf) has - Hangul, combining marks for kaomoji, symbols, maths,
monochrome emoji - cut from several Noto fonts (OFL) and merged into one TrueType font.

usage: make_font_fallback.py [--tc OTF] [--sc OTF] [--extra DIR] [--list FILE ...] [--out ui-ext.ttf] [--add TEXT]
       make_font_fallback.py --scan DIR [DIR ...]      print the characters the files / folders / ID3 titles under DIR would need that
                                                       ui-tc, ui-sc and the list do not have (as list lines; nothing is built or changed)

The character list is tools/gen/font_sets/ext.txt (one `U+XXXX` or `U+XXXX-U+YYYY` per line, `#` starts a comment): no music library is scanned.
Defaults point into fonts-src/ (the original Noto fonts, see fonts-src/README.md). The extra fonts are tried in the order of EXT_SOURCES; the first
one that has a character supplies it. Variable fonts are pinned to their default instance. Needs fontTools (tools/requirements-fonts.txt)."""
import argparse, os, re, sys, tempfile
os.environ.setdefault('SOURCE_DATE_EPOCH', '1700000000')   # same inputs -> same bytes (make fonts-check)
from fontTools import subset
from fontTools.merge import Merger
from fontTools.ttLib import TTFont
from fontTools.ttLib.scaleUpem import scale_upem
from fontTools.varLib import instancer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, 'fonts-src')
TC_DEFAULT = os.path.join(SRC, 'noto-cjk', 'NotoSansTC-Regular.otf')
SC_DEFAULT = os.path.join(SRC, 'noto-cjk', 'NotoSansSC-Regular.otf')
EXTRA_DEFAULT = os.path.join(SRC, 'noto-extra')
LIST_DEFAULT = os.path.join(HERE, 'font_sets', 'ext.txt')
# the order matters: the first font that has a character supplies it
EXT_SOURCES = [('Sans', 'NotoSans.ttf'), ('KR', 'NotoSansKR.ttf'), ('Deva', 'NotoSansDevanagari.ttf'), ('Sym', 'NotoSansSymbols.ttf'),
               ('Sym2', 'NotoSansSymbols2.ttf'), ('Math', 'NotoSansMath.ttf'), ('Emoji', 'NotoEmoji.ttf')]

sys.path.insert(0, HERE)
import make_font_subset as _subset   # id3_text, json_strings


def parse_list(path):
    """Code points of a list file: `U+XXXX`, `U+XXXX-U+YYYY` or `XXXX-YYYY`, one or more per line, `#` comments."""
    out = set()
    with open(path, encoding='utf-8') as fh:
        lines = fh.readlines()
    for n, ln in enumerate(lines, 1):
        ln = ln.split('#', 1)[0].strip()
        for tok in ln.split():
            m = re.fullmatch(r'(?:U\+)?([0-9A-Fa-f]{1,6})(?:-(?:U\+)?([0-9A-Fa-f]{1,6}))?', tok)
            if not m:
                sys.exit(f'{path}:{n}: cannot read {tok!r} (want U+XXXX or U+XXXX-U+YYYY)')
            a = int(m[1], 16)
            b = int(m[2], 16) if m[2] else a
            if b < a or b > 0x10FFFF:
                sys.exit(f'{path}:{n}: bad range {tok!r}')
            out.update(range(a, b + 1))
    return out


def list_lines(cps):
    """Code points as list-file lines (runs become ranges), each with the Unicode name as a comment."""
    import unicodedata
    runs = []
    for c in sorted(cps):
        if runs and runs[-1][1] == c - 1:
            runs[-1][1] = c
        else:
            runs.append([c, c])
    out = []
    for a, b in runs:
        nm = unicodedata.name(chr(a), '?')
        out.append(f'U+{a:04X}' + (f'-U+{b:04X}' if b > a else '') + f'   # {nm}' + (f' .. {unicodedata.name(chr(b), "?")}' if b > a else ''))
    return out


def scan(dirs):
    """Printable characters in file / folder names and ID3 text frames (.mp3) under dirs."""
    chars = set()
    for d in dirs:
        for root, subdirs, files in os.walk(d):
            for n in subdirs + files:
                chars |= set(n)
            for n in files:
                if n.lower().endswith('.mp3'):
                    for t in _subset.id3_text(os.path.join(root, n)):
                        chars |= set(t)
    return {c for c in chars if c.isprintable()}


def source_for(cp, extra_dir):
    """Name of the first extra font that has cp, or None."""
    for _, fn in EXT_SOURCES:
        p = os.path.join(extra_dir, fn)
        if os.path.isfile(p) and cp in _cmap(p):
            return fn
    return None


_cmaps = {}
def _cmap(path):
    if path not in _cmaps:
        with TTFont(path, lazy=True) as f:
            _cmaps[path] = set(f.getBestCmap())
    return _cmaps[path]


def build(tc, sc, extra_dir, out, cps, quiet=False):
    """Write out (ui-ext.ttf) with the code points cps that neither tc nor sc has. Returns (taken per font name, set still missing)."""
    have = _cmap(tc) | _cmap(sc)
    left = {c for c in cps if c not in have}
    tmp = tempfile.mkdtemp()
    parts, taken = [], {}
    for name, fn in EXT_SOURCES:
        path = os.path.join(extra_dir, fn)
        if not os.path.isfile(path):
            sys.exit(f'{path}: missing (fonts-src/README.md lists the sources)')
        f = TTFont(path)
        take = {c for c in left if c in f.getBestCmap()}
        if not take:
            continue
        if 'fvar' in f:
            f = instancer.instantiateVariableFont(f, {a.axisTag: a.defaultValue for a in f['fvar'].axes})
        o = subset.Options()
        o.layout_features = []
        o.notdef_outline = True
        o.name_IDs = ['*']
        o.drop_tables += ['GSUB', 'GPOS', 'GDEF', 'DSIG', 'STAT', 'MVAR', 'HVAR', 'COLR', 'CPAL', 'vhea', 'vmtx']
        s = subset.Subsetter(o)
        s.populate(unicodes=sorted(take))
        s.subset(f)
        if f['head'].unitsPerEm != 1000:
            scale_upem(f, 1000)
        os2 = f['OS/2']                       # the merger needs the same OS/2 / post shape everywhere
        if os2.version < 4:
            os2.version = 4
            for a, v in (('sxHeight', 500), ('sCapHeight', 700), ('usDefaultChar', 0), ('usBreakChar', 32), ('usMaxContext', 1)):
                if not hasattr(os2, a):
                    setattr(os2, a, v)
        f['post'].formatType = 3.0
        p = os.path.join(tmp, name + '.ttf')
        f.save(p)
        parts.append(p)
        taken[fn] = take
        if not quiet:
            print(f'{name}: {len(take)}')
        left -= take
    if not parts:
        sys.exit('nothing to build: no extra font has any listed character')
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    Merger().merge(parts).save(out)
    for p in parts:
        os.remove(p)
    os.rmdir(tmp)
    if not quiet:
        print(f'{out}: {os.path.getsize(out)} bytes, still missing {len(left)}: {"".join(chr(c) for c in sorted(left))}')
    return taken, left


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tc', default=TC_DEFAULT)
    ap.add_argument('--sc', default=SC_DEFAULT)
    ap.add_argument('--extra', default=EXTRA_DEFAULT, help='folder with the extra Noto fonts (default fonts-src/noto-extra)')
    ap.add_argument('--list', action='append', help=f'character list file(s) (default {os.path.relpath(LIST_DEFAULT, ROOT)})')
    ap.add_argument('--out', default=os.path.join(ROOT, 'fonts', 'ui-ext.ttf'))
    ap.add_argument('--add', default='', help='extra characters (text) added to the list for this run')
    ap.add_argument('--scan', nargs='+', metavar='DIR', help='print the list lines the files under DIR need beyond the current list; builds nothing')
    a = ap.parse_args()
    cps = set()
    for l in (a.list or [LIST_DEFAULT]):
        cps |= parse_list(l)
    if a.scan:
        have = _cmap(a.tc) | _cmap(a.sc) | cps
        need = {ord(c) for c in scan(a.scan)} - have
        found = {c for c in need if source_for(c, a.extra)}
        for ln in list_lines(found):
            print(ln)
        nos = sorted(need - found)
        print(f'# {len(found)} character(s) to append to {os.path.relpath(LIST_DEFAULT, ROOT)}; {len(nos)} that no source font has'
              + (': ' + ''.join(chr(c) for c in nos) if nos else ''), file=sys.stderr)
        return
    cps |= {ord(c) for c in a.add if c.isprintable()}
    build(a.tc, a.sc, a.extra, a.out, cps)


if __name__ == '__main__':
    main()
