#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Add missing characters to the player's UI fonts (ui-tc.ttf, ui-sc.ttf, ui-ext.ttf).

Give it text, a file, a folder of music, or the player's missing-glyph record; it finds the characters that none of the three text fonts has, takes them
from the original Noto fonts in fonts-src/ (Traditional Chinese first, then Simplified, then the fallback set: Latin/Greek/Cyrillic, Hangul, Devanagari,
symbols, maths, monochrome emoji), rebuilds the files that change, and writes them to --out, ready to copy to the card (or, with --pack, a flash image).

  python tools/fonts/add_glyphs.py --text "Привет 한글 ☃ 瘂"
  python tools/fonts/add_glyphs.py --file titles.txt
  python tools/fonts/add_glyphs.py --scan /path/to/music
  python tools/fonts/add_glyphs.py --miss fontmiss.txt --pack

Inputs (any number, combined):
  --text STR      characters typed here
  --file FILE     a UTF-8 text file (every character in it)
  --scan DIR      every file and folder name under DIR, plus the text frames of the ID3v2 tags of .mp3 files (title, artist, album ...). Names and
                  MP3 ID3v2 tags only: tags of FLAC / Ogg / M4A are NOT read (standard library only); put those titles into a --file
  --miss FILE     the player's missing-glyph record: what the console command `fontmiss` prints (`APP fontmiss total=3 list=U+7DB2,U+1F600 more=0`),
                  the `APP font miss U+XXXX` lines of the log, or the web tool's JSON {"list":[30002,...]}. The record lives in the player's RAM only (nothing is
                  stored on the card): see docs/dev/fonts.md for how to read it.
Where it reads and writes:
  --fonts DIR     the fonts the player has now (default: fonts/ of this checkout; or a copy of the card's _player/fonts/). Coverage is measured on these.
  --out DIR       result folder (default out/fonts). Only changed font files are written (plus fonts.bin with --pack).
  --pack          also write fonts.bin, the image of the flash `fonts` partition (all four fonts), and print the esptool line.
  --dry-run       report only; write nothing.  --no-save-lists: do not append the new characters to tools/gen/font_sets/{extra_chars,ext}.txt
Limits it enforces (and refuses to write past, saying by how much): one font file <= 4 MiB (the firmware's upload limit; the flash partition is 4 MiB too),
and the fonts.bin image <= 4 MiB.  Needs Python 3 and fontTools (pip install -r tools/requirements-fonts.txt).  Exit status: 0 done / nothing to do, 1 usage, 2 a limit would be exceeded."""
import argparse, json, os, re, shutil, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'gen'))
try:
    import make_font_fallback as ext    # sets SOURCE_DATE_EPOCH, imports fontTools
    import make_font_subset as sub
    import make_fontpak
except ImportError as e:
    sys.exit(f'{e}\nInstall the requirements first:  python -m pip install -r tools/requirements-fonts.txt  (a virtual environment is fine)')

LIMIT_FILE = 4 << 20       # app/main/fonts_ctl.c FONT_MAX; also the size of the fonts partition (app/partitions.csv)
LIMIT_PACK = 4 << 20
FONT_NAMES = ['ui-tc.ttf', 'ui-sc.ttf', 'ui-ext.ttf', 'ui-icon.ttf']
INVISIBLE = set(range(0x200B, 0x2010)) | {0x2060, 0xFEFF, 0xA0} | set(range(0xFE00, 0xFE10))   # the firmware draws these blank; they are never "missing"


def chars_of_miss(text):
    try:
        j = json.loads(text)
        if isinstance(j, dict) and isinstance(j.get('list'), list):
            return {int(x) for x in j['list']}
    except ValueError:
        pass
    return {int(m, 16) for m in re.findall(r'U\+([0-9A-Fa-f]{4,6})', text)}


def wanted(a):
    cps = set()
    for c in a.text or '':
        cps.add(ord(c))
    for f in a.file or []:
        with open(f, encoding='utf-8-sig') as fh:
            cps |= {ord(c) for c in fh.read()}
    for d in a.scan or []:
        cps |= {ord(c) for c in ext.scan([d])}
    for f in a.miss or []:
        with open(f, encoding='utf-8-sig', errors='replace') as fh:
            cps |= chars_of_miss(fh.read())
    return {c for c in cps if chr(c).isprintable() and not chr(c).isspace() and c not in INVISIBLE}


def have_in(fonts_dir):
    cov = set()
    for n in FONT_NAMES[:3]:
        p = os.path.join(fonts_dir, n)
        if not os.path.isfile(p):
            sys.exit(f'{p}: not found (--fonts must hold the three text fonts the player uses now)')
        cov |= ext._cmap(p)
    return cov


def classify(missing, tc, sc, extra):
    tcm, scm = ext._cmap(tc), ext._cmap(sc)
    cjk, ex, none = set(), {}, set()
    for c in sorted(missing):
        if c in tcm or c in scm:
            cjk.add(c)
        else:
            f = ext.source_for(c, extra)
            if f:
                ex[c] = f
            else:
                none.add(c)
    return cjk, ex, none


def append_list(path, cps, header):
    with open(path, 'a', encoding='utf-8') as f:
        f.write(f'# {header}\n')
        if path.endswith('ext.txt'):
            f.write('\n'.join(ext.list_lines(cps)) + '\n')
        else:
            f.write(''.join(chr(c) for c in sorted(cps)) + '\n')


def show(cps):
    return ' '.join(f'{chr(c)}(U+{c:04X})' for c in sorted(cps))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__.split('\n\n', 1)[1])
    ap.add_argument('--text'); ap.add_argument('--file', action='append'); ap.add_argument('--scan', action='append'); ap.add_argument('--miss', action='append')
    ap.add_argument('--fonts', default=os.path.join(ROOT, 'fonts'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'out', 'fonts'))
    ap.add_argument('--src', default=os.path.join(ROOT, 'fonts-src'), help='the original Noto fonts (default fonts-src/)')
    ap.add_argument('--lists', default=os.path.join(ROOT, 'tools', 'gen', 'font_sets'), help=argparse.SUPPRESS)
    ap.add_argument('--no-standard-set', action='store_true', help=argparse.SUPPRESS)   # host tests with tiny fonts: only the list files, not Big5/GB2312/app strings
    ap.add_argument('--pack', action='store_true'); ap.add_argument('--dry-run', action='store_true'); ap.add_argument('--no-save-lists', action='store_true')
    a = ap.parse_args(argv)
    if not (a.text or a.file or a.scan or a.miss):
        ap.error('give at least one of --text --file --scan --miss')
    tc = os.path.join(a.src, 'noto-cjk', 'NotoSansTC-Regular.otf')
    sc = os.path.join(a.src, 'noto-cjk', 'NotoSansSC-Regular.otf')
    extra = os.path.join(a.src, 'noto-extra')
    want = wanted(a)
    missing = want - have_in(a.fonts)
    print(f'{len(want)} distinct character(s) asked for, {len(missing)} not in ui-tc / ui-sc / ui-ext of {a.fonts}')
    if not missing:
        print('nothing to add: every character is already in the fonts; no file written')
        return 0
    cjk, ex, none = classify(missing, tc, sc, extra)
    if cjk:
        print(f'-> ui-tc / ui-sc ({len(cjk)}, Noto Sans TC first, then SC): {show(cjk)}')
    for fn in sorted(set(ex.values())):
        print(f'-> ui-ext from {fn} ({sum(1 for v in ex.values() if v == fn)}): {show(c for c, v in ex.items() if v == fn)}')
    if none:
        print(f'NOT AVAILABLE: no source font in fonts-src/ has {len(none)} character(s): {show(none)}')
        print('   (they stay missing; a font that has them must be added to fonts-src/ and, for ui-ext, to EXT_SOURCES in tools/gen/make_font_fallback.py)')
    if not cjk and not ex:
        print('nothing can be added; no file written')
        return 0
    if a.dry_run:
        print('dry run: nothing written')
        return 0

    # the lists the standard build reads: appended in place (so `make fonts-all` reproduces the result) or kept in a temporary copy
    lists = a.lists
    tmp = tempfile.mkdtemp(prefix='addglyphs-')
    if a.no_save_lists:
        lists = os.path.join(tmp, 'font_sets')
        shutil.copytree(a.lists, lists)
    tag = 'added by tools/fonts/add_glyphs.py'
    if cjk:
        append_list(os.path.join(lists, 'extra_chars.txt'), cjk, tag)
    if ex:
        append_list(os.path.join(lists, 'ext.txt'), set(ex), tag)

    built = os.path.join(tmp, 'built')
    os.makedirs(built)
    if cjk:
        sub.build(tc, os.path.join(built, 'ui-tc.ttf'), sc, os.path.join(built, 'ui-sc.ttf'), sub.standard_chars(lists, standard=not a.no_standard_set))
    if ex:
        taken, left = ext.build(tc, sc, extra, os.path.join(built, 'ui-ext.ttf'), ext.parse_list(os.path.join(lists, 'ext.txt')), quiet=True)
        print(f'ui-ext rebuilt: {sum(len(v) for v in taken.values())} character(s), {len(left)} listed but in no source font')

    # limits, before anything reaches --out
    over, sizes = [], {}
    for n in FONT_NAMES:
        p = os.path.join(built, n)
        p = p if os.path.isfile(p) else os.path.join(a.fonts, n)
        sizes[n] = p
        sz = os.path.getsize(p)
        if n in os.listdir(built) and sz > LIMIT_FILE:
            over.append(f'{n} is {sz} bytes: {sz - LIMIT_FILE} over the 4 MiB (4194304) file limit')
    if a.pack:
        pack = make_fontpak.build([sizes[n] for n in FONT_NAMES])
        if len(pack) > LIMIT_PACK:
            over.append(f'fonts.bin is {len(pack)} bytes: {len(pack) - LIMIT_PACK} over the 4 MiB (4194304) fonts partition')
    if over:
        for o in over:
            print('REFUSED:', o)
        print('Nothing written. Remove characters (for ui-tc / ui-sc: edit tools/gen/font_sets/extra_chars.txt or set FONT_GB_LEVEL2=0 for the full build) and try again.')
        shutil.rmtree(tmp, ignore_errors=True)
        return 2

    os.makedirs(a.out, exist_ok=True)
    for n in os.listdir(built):
        shutil.copyfile(os.path.join(built, n), os.path.join(a.out, n))
        print(f'wrote {os.path.join(a.out, n)} ({os.path.getsize(os.path.join(a.out, n))} bytes)')
    if a.pack:
        with open(os.path.join(a.out, 'fonts.bin'), 'wb') as f:
            f.write(pack)
        print(f'wrote {os.path.join(a.out, "fonts.bin")} ({len(pack)} bytes, {100 * len(pack) // LIMIT_PACK}% of the partition)')
    shutil.rmtree(tmp, ignore_errors=True)
    print('\nNext: copy the written ui-*.ttf into the card folder _player/fonts/ (the player prefers a card file that differs from the flash copy) and reboot the player.')
    if a.pack:
        print('Flash image instead (writes the whole fonts partition at 0xC00000; download mode first, use your board\'s serial port):\n'
              f'  python -m esptool --chip esp32s31 --port <PORT> --baud 460800 --no-stub write-flash 0xC00000 {os.path.join(a.out, "fonts.bin")}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
