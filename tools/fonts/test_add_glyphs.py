#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Host tests of tools/fonts/add_glyphs.py with tiny synthetic fonts (square glyphs; only the cmap matters). Needs fontTools; `make fonts-check` runs it.
Cases: nothing missing -> no change; a Hangul character -> ui-ext only; a character no source has -> reported, not fatal; a CJK character -> ui-tc / ui-sc;
the file size limit -> refusal (exit 2, nothing written); --pack writes fonts.bin."""
import contextlib, io, os, shutil, sys, tempfile, unittest, warnings
warnings.simplefilter('ignore', ResourceWarning)   # the vendored pack tool leaves its files to the garbage collector
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
except ImportError:
    print('test_add_glyphs: fontTools not installed (tools/requirements-fonts.txt): skipped')
    sys.exit(0)
import add_glyphs

ASCII = ''.join(chr(c) for c in range(0x20, 0x7f))


def make_font(path, chars, family):
    fb = FontBuilder(1000, isTTF=True)
    names = ['.notdef'] + [f'u{ord(c):X}' for c in chars]
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({ord(c): f'u{ord(c):X}' for c in chars})
    pen = TTGlyphPen(None)
    pen.moveTo((50, 0)); pen.lineTo((50, 700)); pen.lineTo((450, 700)); pen.lineTo((450, 0)); pen.closePath()
    g = pen.glyph()
    fb.setupGlyf({n: g for n in names})
    fb.setupHorizontalMetrics({n: (500, 50) for n in names})
    fb.setupHorizontalHeader(ascent=880, descent=-120)
    fb.setupNameTable({'familyName': family, 'styleName': 'Regular'})
    fb.setupOS2(sTypoAscender=880, sTypoDescender=-120, usWinAscent=880, usWinDescent=120, version=4)
    fb.setupPost()
    fb.save(path)


class T(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(prefix='t_addglyphs-')
        src = os.path.join(self.d, 'src')
        os.makedirs(os.path.join(src, 'noto-cjk')); os.makedirs(os.path.join(src, 'noto-extra'))
        make_font(os.path.join(src, 'noto-cjk', 'NotoSansTC-Regular.otf'), ASCII + '台灣音樂', 'TC')
        make_font(os.path.join(src, 'noto-cjk', 'NotoSansSC-Regular.otf'), ASCII + '台湾音乐网络', 'SC')
        for _, fn in add_glyphs.ext.EXT_SOURCES:
            make_font(os.path.join(src, 'noto-extra', fn), 'x' + {'NotoSansKR.ttf': '한글', 'NotoEmoji.ttf': '😀'}.get(fn, ''), fn)
        cur = os.path.join(self.d, 'fonts')
        os.makedirs(cur)
        make_font(os.path.join(cur, 'ui-tc.ttf'), ASCII + '台灣', 'TC')
        make_font(os.path.join(cur, 'ui-sc.ttf'), '湾', 'SC')
        make_font(os.path.join(cur, 'ui-ext.ttf'), '😀', 'EXT')
        make_font(os.path.join(cur, 'ui-icon.ttf'), '', 'ICON')
        lists = os.path.join(self.d, 'sets')
        os.makedirs(lists)
        for n, t in (('library_chars.txt', ''), ('extra_chars.txt', '台灣湾'), ('ext.txt', 'U+1F600\n')):
            with open(os.path.join(lists, n), 'w', encoding='utf-8') as f:
                f.write(t)
        self.base = ['--src', src, '--fonts', cur, '--out', os.path.join(self.d, 'out'), '--lists', lists, '--no-standard-set', '--no-save-lists']
        self.out = os.path.join(self.d, 'out')

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def run_tool(self, *args):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = add_glyphs.main(list(args) + self.base)
        return rc, buf.getvalue()

    def cmap(self, n):
        from fontTools.ttLib import TTFont
        with TTFont(os.path.join(self.out, n)) as f:
            return set(f.getBestCmap())

    def test_nothing_missing(self):
        rc, out = self.run_tool('--text', 'abc 台灣 😀')
        self.assertEqual(rc, 0)
        self.assertIn('nothing to add', out)
        self.assertFalse(os.path.exists(self.out))

    def test_hangul_lands_in_ext(self):
        rc, out = self.run_tool('--text', '한')
        self.assertEqual(rc, 0, out)
        self.assertEqual(sorted(os.listdir(self.out)), ['ui-ext.ttf'])
        self.assertIn(ord('한'), self.cmap('ui-ext.ttf'))
        self.assertIn(ord('😀'), self.cmap('ui-ext.ttf'))      # what was there stays

    def test_unavailable_reported_not_fatal(self):
        rc, out = self.run_tool('--text', '한☃')
        self.assertEqual(rc, 0, out)
        self.assertIn('NOT AVAILABLE', out)
        self.assertIn('U+2603', out)
        self.assertIn(ord('한'), self.cmap('ui-ext.ttf'))
        rc, out = self.run_tool('--text', '☃')
        self.assertEqual(rc, 0)
        self.assertIn('nothing can be added', out)

    def test_cjk_and_miss_record(self):
        rec = os.path.join(self.d, 'miss.txt')
        with open(rec, 'w') as f:
            f.write('APP fontmiss total=3 list=U+6A02,U+7DB2 more=0\n')     # 樂 (TC has it), 網 (SC has 网 only: not 網)
        rc, out = self.run_tool('--miss', rec)
        self.assertEqual(rc, 0, out)
        self.assertIn(0x6A02, self.cmap('ui-tc.ttf'))
        self.assertIn('U+7DB2', out)

    def test_size_limit_refused(self):
        old = add_glyphs.LIMIT_FILE
        add_glyphs.LIMIT_FILE = 100
        try:
            rc, out = self.run_tool('--text', '한')
        finally:
            add_glyphs.LIMIT_FILE = old
        self.assertEqual(rc, 2)
        self.assertIn('over the 4 MiB', out)
        self.assertFalse(os.path.exists(self.out))

    def test_pack(self):
        rc, out = self.run_tool('--text', '한', '--pack')
        self.assertEqual(rc, 0, out)
        self.assertTrue(os.path.isfile(os.path.join(self.out, 'fonts.bin')))
        self.assertIn('write-flash 0xC00000', out)


if __name__ == '__main__':
    unittest.main(verbosity=1)
