// SPDX-License-Identifier: LGPL-2.1-or-later
// Builds components/player_core/host_test/fixtures/gp_js.zip with the page's writer (glyphpack.js): the same synthetic
// content as fixtures/make_glyphpacks.py, so test_glyphpack.c checks that the web page and Python write the same pack.
// Run: node tools/web/gp_fixture.mjs
import { writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const here = dirname(fileURLToPath(import.meta.url));
createRequire(import.meta.url)(join(here, 'glyphpack.js'));
const G = globalThis.GlyphPack;

const SIZES = [[24, 30], [40, 50]], NGLYPHS = 12, ELLIPSIS = 11;
function glyph(g, s) {
  if (g == 0) return { w: 0, h: 0, x: 0, y: 0, adv: 5 + 5 * s, fmt: G.FMT_EMPTY, data: new Uint8Array(0) };
  const fmt = g % 5 == 4 ? G.FMT_RGB565A8 : G.FMT_A8, w = 4 + (g * 3 + s * 7) % 9, h = 6 + (g + s * 5) % 10;
  const data = new Uint8Array(w * h * G.bpp(fmt)).map((_, i) => (g * 37 + s * 11 + i * 13) & 255);
  return { w, h, x: g % 3 - 1, y: 2 + s, adv: w + 2, fmt, data };
}
const glyphs = Array.from({ length: NGLYPHS }, (_, g) => ({ flags: g == 0 ? G.GF_SPACE : g % 2 ? G.GF_WORD : 0, sizes: SIZES.map((_, s) => glyph(g, s)) }));
const images = [{ w: 10, h: 8, fmt: G.FMT_RGB565A8, data: new Uint8Array(240).map((_, i) => (i * 7 + 3) & 255) }];
const N = (text, o = {}, children = []) => ({ text, ...o, children });
const root = N([1, 2, 3], {}, [
  N([4, 5], {}, [N([6, 7, 0, 8], { weight: 3 }), N([9], { image: 0, flags: 1 })]),
  N([10, G.NL, 2], {}, [N([3, 3, 3], { weight: 0 })]),
]);
const out = join(here, '../../components/player_core/host_test/fixtures/gp_js.zip');
writeFileSync(out, G.build(G.KINDS.lunch, SIZES, glyphs, images, root, ELLIPSIS, { kind: 'lunch', title: '午餐' }));
console.log('wrote', out);
