// SPDX-License-Identifier: LGPL-2.1-or-later
// Host test of fontsubset.js + fontbuild.js (node; the vendored wasm, generated test fonts: python3 testdata/make_testfont.py first). Prints PASS or the failures.
import { createRequire } from 'node:module';
import fs from 'node:fs';
const req = createRequire(import.meta.url);
const FS = req('./fontsubset.js'); globalThis.FontSubset = FS;
globalThis.FONT_SETS = (0, eval)(fs.readFileSync('fontsets.js', 'utf8') + ';FONT_SETS');
const FB = req('./fontbuild.js');
const fails = [], ok = (c, m) => { if (!c) fails.push(m) };
const tc = new Uint8Array(fs.readFileSync('testdata/fonts/tc.ttf')), sc = new Uint8Array(fs.readFileSync('testdata/fonts/sc.ttf'));
const S = await FS.load(fs.readFileSync('vendor/harfbuzzjs/1.6.4/harfbuzz-subset.wasm'));
const cps = FB.wanted({ device: '台灣歌', miss: [0x1f600], extra: '網' });
ok(cps.length > 8000 && cps.length < 25000, 'wanted size ' + cps.length);
ok(cps.includes(0x1f600) && cps.includes(0x7db2) && cps.includes(0x20) && !cps.includes(0x7f) && !cps.includes(0x200b), 'wanted members');
ok(FB.wanted({ gb2: false }).length < FB.wanted({ gb2: true }).length, 'gb2 switch');
const r = FB.build(S, { tc, sc }, cps), tm = FS.cmap(tc), sm = FS.cmap(sc), ot = FS.cmap(r.tc), os = FS.cmap(r.sc);
const eq = (a, b) => a.size === b.size && [...a].every(x => b.has(x));
ok(eq(ot, new Set(cps.filter(c => tm.has(c)))), 'tc keeps exactly wanted & tc');
ok(eq(os, new Set(cps.filter(c => !tm.has(c) && sm.has(c)))), 'sc keeps exactly wanted & !tc & sc');
ok(![...os].some(c => ot.has(c)), 'no overlap');
ok(r.missing.length === cps.filter(c => !tm.has(c) && !sm.has(c)).length && r.missing.includes(0x1f600), 'missing list');
ok(r.tc.length < tc.length * 2 && r.sc.length > 100, 'sizes sane ' + r.tc.length + '/' + r.sc.length);
ok(FB.packSize([100, 200, 300]) === 780 && FB.fits([1000, 2000]) && !FB.fits([4 << 20]), 'pack size');
let code = ''; try { FB.build(S, { tc: new Uint8Array([0x4f, 0x54, 0x54, 0x4f, 0, 0, 0, 0, 0, 0, 0, 0]), sc }, cps) } catch (e) { code = e.code }
ok(code === 'cff', 'cff refused');
if (fails.length) { console.log('FAIL: ' + fails.join('; ')); process.exit(1) } console.log('PASS fontbuild');
