// SPDX-License-Identifier: LGPL-2.1-or-later
/* The library font builder's logic (spec section 6), no UI and no text: which characters, the two-font split, the size check.
   Same rules as `make fonts-build` (tools/gen/make_font_subset.py): the wanted set is ASCII / kana / full-width / marks + Big5 level 1 + GB2312 (+ level 2) + the library characters
   + tools/gen/font_sets (FONT_SETS, fontsets.js) + what the player reports (its library, fontmiss) + the viewer's extra text; ui-tc.ttf gets what Noto Sans TC has, ui-sc.ttf only what
   TC lacks and SC has; ui-ext.ttf (Hangul, symbols, emoji from several Noto families, merged with fontTools) is kept, not rebuilt: its cmap only takes characters off the missing list.
     FontBuild.wanted({ sets, device, miss, extra, gb2 })  -> sorted array of code points
     FontBuild.build(S, { tc, sc }, cps)                   -> { tc, sc, missing: [cp], counts }    (S = FontSubset.load(...); sources must have TrueType outlines: error code 'cff')
     FontBuild.packSize(sizes) / FontBuild.fits(sizes)     the fonts-partition image of tools/gen/make_fontpak.py (16-byte header, 48 per entry, 16-byte aligned data)
   Error codes: cff (a CFF/.otf source), source (not a font). */
const FontBuild = (() => {
  const PART_BYTES = 4 << 20, FREE_MIN = 204800;   /* FONTS_PART_SIZE, FONT_FREE_MIN of the Makefile */
  const err = (code, extra) => { const e = new Error(code + (extra ? ': ' + extra : '')); e.code = code; e.extra = extra; return e };
  const bad = /[\p{C}\p{Z}]/u;
  function wanted({ sets = FONT_SETS, device = '', miss = [], extra = '', gb2 = true } = {}) {
    const all = [sets.base, sets.big5, sets.gb1, gb2 ? sets.gb2 : '', sets.library, sets.extra, sets.app, device, extra].join('');
    const out = new Set(miss);
    for (const ch of all) { const c = ch.codePointAt(0); if (c === 0x20 || !bad.test(ch)) out.add(c) }   /* Python's str.isprintable() */
    return [...out].filter(c => c >= 0x20 && c !== 0x7f && !(c >= 0x80 && c < 0xa0)).sort((a, b) => a - b);
  }
  function build(S, src, cps) {
    for (const k of ['tc', 'sc']) { const f = FontSubset.flavor(src[k]); if (f === 'cff') throw err('cff', k); if (f !== 'ttf') throw err('source', k) }
    const tcMap = FontSubset.cmap(src.tc), scMap = FontSubset.cmap(src.sc);
    const tcHave = cps.filter(c => tcMap.has(c)), rest = cps.filter(c => !tcMap.has(c)), scHave = rest.filter(c => scMap.has(c));
    const missing = rest.filter(c => !scMap.has(c));
    return { tc: S.subset(src.tc, tcHave), sc: S.subset(src.sc, scHave), missing, counts: { wanted: cps.length, tc: tcHave.length, sc: scHave.length, missing: missing.length } };
  }
  function packSize(sizes) {
    let off = 16 + 48 * sizes.length;
    for (const n of sizes) { off = (off + 15) & ~15; off += n }
    return off;
  }
  const fits = sizes => packSize(sizes) <= PART_BYTES - FREE_MIN;
  return { PART_BYTES, FREE_MIN, wanted, build, packSize, fits };
})();
if (typeof module != 'undefined') module.exports = FontBuild;
if (typeof globalThis != 'undefined') globalThis.FontBuild = FontBuild;
