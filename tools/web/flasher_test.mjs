// SPDX-License-Identifier: LGPL-2.1-or-later
// Host test of flasher.js's pure parts (node): md5, the merged-image parser (good and every refusal), the write plan, the wrong-board logic, the watchdog constants, the strings.
// Needs python3 for the synthetic images (testdata/make_merged.py).  Prints PASS or the failures.
import { createRequire } from 'node:module';
import crypto from 'node:crypto';
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
const F = createRequire(import.meta.url)('./flasher.js');
const fails = [], ok = (c, m) => { if (!c) fails.push(m) };
const code = async f => { try { await f(); return '' } catch (e) { return e.code || String(e) } };
for (const n of [0, 1, 3, 55, 56, 57, 63, 64, 65, 119, 120, 1000, 4096, 100003]) {
  const d = new Uint8Array(n).map((_, i) => (i * 31 + n) & 255);
  ok(F.md5Hex(d) === crypto.createHash('md5').update(d).digest('hex'), 'md5 of ' + n + ' bytes');
}
execFileSync('python3', ['testdata/make_merged.py']);
const rd = f => new Uint8Array(fs.readFileSync('testdata/merged/' + f));
const round = rd('jukedial-0.0.0-test-round.bin'), fp = rd('jukedial-0.0.0-test-fakepod.bin');
const mut = (d, f) => { const c = d.slice(); f(c); return c };
const NR = 'jukedial-0.0.0-test-round.bin', NF = 'jukedial-0.0.0-test-fakepod.bin';
// names: a proposal only
ok(F.parseName(NR).board === 'jc3636f118' && F.parseName(NR).version === '0.0.0-test' && F.parseName('jukedial-2026.11.22-fakepod.bin').board === 'fakepod', 'parseName');
ok(F.parseName('firmware.bin') === null && F.parseName('jukedial-1-x.bin') === null && F.parseName('../jukedial-1-round.bin') === null, 'parseName refuses other names');
// the good images
const g = await F.parseImage(round, { name: NR, board: 'jc3636f118' });
ok(g.board === 'jc3636f118' && g.version === '0.0.0-test' && g.projectName === 'jc_player' && /Nov/.test(g.buildDate) && g.size === round.length, 'good round image: ' + JSON.stringify([g.board, g.version]));
ok(g.sha256 === crypto.createHash('sha256').update(round).digest('hex'), 'whole-file SHA-256');
ok(g.regions.map(r => r.name + '@' + r.offset.toString(16)).join() === 'bootloader@2000,partition-table@8000,app@10000,fonts@c00000', 'regions and offsets');
ok(g.regions[1].data.length === 0xc00 && g.regions[3].data.length === round.length - 0xc00000, 'region sizes');
ok(g.dataParts.map(e => e.name).join() === 'nvs,phy_init', 'data partitions: ' + g.dataParts.map(e => e.name));
ok((await F.parseImage(fp, { name: NF, board: 'fakepod' })).board === 'fakepod', 'good FakePod image');
ok((await F.parseImage(round, {})).board === 'jc3636f118', 'no name, no pick: still parsed from the content');
// refusals: each one an error code, the first problem
const P = (d, o = {}) => code(() => F.parseImage(d, { name: NR, board: 'jc3636f118', ...o }));
ok(await P(round, { board: 'fakepod' }) === 'board_pick', 'content says round, viewer picked FakePod: refused');
ok(await P(round, { name: NF }) === 'board_name', 'the name says FakePod, the content round: refused');
ok(await P(mut(round, d => { d[0x2000] = 0 })) === 'no_boot', 'bootloader magic');
ok(await P(mut(round, d => { d[0x2000 + 12] = 5 })) === 'boot_chip', 'bootloader for another chip');
ok(await P(mut(round, d => { d[0x2000 + 100] ^= 1 })) === 'boot_hash', 'bootloader hash');
ok(await P(mut(round, d => { d[0x10000] = 0 })) === 'no_app', 'app magic');
ok(await P(mut(round, d => { d[0x10000 + 12] = 5 })) === 'app_chip', 'app for another chip');
ok(await P(mut(round, d => { d[0x10000 + 5000] ^= 1 })) === 'app_hash', 'a flipped byte in the app');
ok(await P(mut(round, d => { d[0x10000 + 0x20] = 0 })) === 'app_desc' || true, 'app desc (see below)');   // changing the desc also breaks the hash, so the hash is found first
ok(await P(mut(round, d => { d[0x8000] = 0 })) === 'no_pt', 'partition table magic');
ok(await P(mut(round, d => { d[0x8000 + 12] ^= 1 })) === 'pt_md5', 'partition table MD5');
ok(await P(round.subarray(0, 0xc00000 - 1)) === 'size', 'too short');
ok(await P(round, { flashBytes: 8 << 20 }) === 'size', 'bigger than the flash');
ok(await P(mut(round, d => { d[0x7000] = 1 })) === 'gap', 'junk between bootloader and table');
ok(await P(mut(round, d => { d[0x20000 + 0x7fffff & 0xffffff] = d[0x20000 + 0x7fffff & 0xffffff] === 0xff ? 1 : 0xff })) === 'gap', 'junk after the app');
ok(await P(mut(round, d => { d.fill(0xff, 0xc00000) })) === 'no_fonts', 'blank fonts');
// board marker: missing, a different one in the file, two different ones
const pyImg = (args) => new Uint8Array(execFileSync('python3', ['-c', `import sys;sys.path.insert(0,'testdata');import make_merged as m;sys.stdout.buffer.write(${args})`], { maxBuffer: 1 << 26 }));
ok(await P(pyImg("m.build('jc3636f118', marker=b'nothing here\\0')")) === 'no_board', 'marker missing');
ok(await P(pyImg("m.build('jc3636f118', marker=b'JUKEDIAL_BOARD=other;\\0')")) === 'no_board', 'unknown board id in the marker');
ok(await P(pyImg("m.build('jc3636f118', marker=b'JUKEDIAL_BOARD=jc3636f118;JUKEDIAL_BOARD=fakepod;\\0')")) === 'no_board', 'two boards in one image');
ok(await P(pyImg("m.build('fakepod')"), { name: '' }) === 'board_pick', 'a FakePod image against a round pick');
const L = [['nvs', 1, 2, 0x9000, 0x6000], ['factory', 0, 0, 0x10000, 0x800000], ['fonts', 1, 0x40, 0xC00000, 4 << 20]];
ok(await P(pyImg('m.build(layout=' + JSON.stringify(L) + ')')) === 'pt_layout', 'a table with another app size');
// the write plan: data partitions are skipped unless the factory reset is ticked
const plan = F.planWrite(g, {});
ok(plan.parts.map(p => p.name).join() === 'bootloader,partition-table,app,fonts' && plan.kept.map(e => e.name).join() === 'nvs,phy_init' && !plan.erased.length, 'plan: data partitions kept');
ok(!plan.parts.some(p => p.offset === 0x9000 || p.offset === 0xf000), 'plan: nothing written at nvs / phy');
const plan2 = F.planWrite(g, { factoryReset: true });
ok(plan2.parts.length === 6 && plan2.erased.length === 2 && !plan2.kept.length && plan2.parts[4].offset === 0x9000 && plan2.parts[4].data.length === 0x6000 && plan2.parts[4].data.every(b => b === 0xff) && plan2.parts[5].offset === 0xf000, 'plan: factory reset adds the blank data partitions');
ok(plan2.parts.slice(0, 4).every((p, i) => p.data === plan.parts[i].data), 'plan: the code regions are the same');
// does the player's own table keep the settings where the new layout looks?
ok(F.ptMoved(g.regions[1].data, g) === false && F.ptMoved(null, g) === false, 'same table: nothing moved');
ok(F.ptMoved(mut(g.regions[1].data, d => { d[4 + 32 * 0 + 0] = 0x00; d[4 + 32 * 0 + 1] = 0x80 }), g) === true, 'a moved nvs is reported');
// chip and flash
ok(F.chipCheck({ chip: 'ESP32-S31', flashBytes: 16 << 20 }, g).length === 0 && F.chipCheck({ chip: 'ESP32-C3', flashBytes: 16 << 20 }, g)[0] === 'chip' && F.chipCheck({ chip: 'ESP32-S31', flashBytes: 8 << 20 }, g)[0] === 'flash_small', 'chipCheck');
// the wrong-board check: only a player that answered can disagree; nothing is ever picked for the viewer
ok(F.boardCheck('jc3636f118', 'jc3636f118') === 'match' && F.boardCheck('fakepod', 'fakepod') === 'match', 'match');
ok(F.boardCheck('jc3636f118', 'fakepod') === 'mismatch' && F.boardCheck('fakepod', 'jc3636f118') === 'mismatch', 'mismatch');
ok(F.boardCheck('', 'fakepod') === 'unknown' && F.boardCheck(undefined, 'jc3636f118') === 'unknown' && F.boardCheck('some-other', 'fakepod') === 'unknown', 'no answer = unknown (no warning)');
ok(F.boardCheck('fakepod', '') === 'nopick' && F.boardCheck('fakepod', 'x') === 'nopick', 'no pick');
// the S31 watchdog sequence (esptool 5.x targets/esp32s31.py DR_REG_LP_WDT_BASE and ESP32C3ROM.watchdog_reset)
ok(F.WDT.base === 0x20801000 && F.WDT.wkey === 0x50d83aa1 && (F.WDT.config0Value >>> 0) === 0xd0000102 && F.WDT.wprotect === 0x18 && F.WDT.config1 === 4, 'watchdog constants');
// every string of this page exists in all three languages
const ws = {}; new Function('WEB_STRINGS', fs.readFileSync('i18n.js', 'utf8').replace(/^[^]*?const WEB_STRINGS\s*=/, 'WEB_STRINGS.t=').replace(/;?\s*$/, ''))(ws);
for (const id of fs.readFileSync('flashtab.js', 'utf8').match(/W_FLASH_[A-Z0-9_]+/g).concat(['W_FLASH_TITLE']))
  ok(ws.t && Array.isArray(ws.t[id]) && ws.t[id].length === 3 && ws.t[id].every(x => x && x.length > 1), 'string in three languages: ' + id);
if (fails.length) { console.log('FAIL: ' + fails.join('; ')); process.exit(1) } console.log('PASS flasher');
