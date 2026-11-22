// SPDX-License-Identifier: LGPL-2.1-or-later
// Host test of fontput.js against a pretend device that follows app/main/fonts_ctl.c (node). Prints PASS or the failures.
import { createRequire } from 'node:module';
import crypto from 'node:crypto';
const req = createRequire(import.meta.url);
req('./glyphpack.js'); const FP = req('./fontput.js');
const fails = [], ok = (c, m) => { if (!c) fails.push(m) };
const crc32 = globalThis.GlyphPack.crc32;
function device({ lostReplyAt = -1, badCrcAt = -1, deadAt = -1 } = {}) {
  const d = { stg: {}, live: {}, abortAll: false, got: 0, size: 0, buf: [], active: false, aborted: 0, ended: null, calls: 0 };
  d.request = async (op, a) => {
    const n = d.calls++;
    const err = (m, code, detail) => { const e = new Error(m); if (code) e.code = code; if (detail) e.detail = detail; return e };
    if (op === 'font_put_begin') { d.name = a.name; d.active = true; d.size = a.size; d.got = 0; d.buf = []; return { chunk: 2048 } }
    if (op === 'font_put_stat') return { got: d.got };
    if (op === 'font_put_abort') { d.active = false; d.aborted++; if (a.all) { d.stg = {}; d.abortAll = true } return {} }
    if (op === 'font_put_commit') { for (const n of a.names) if (!d.stg[n]) throw err('nothing staged', 'device', 'nothing_staged'); for (const n of a.names) { d.live[n] = d.stg[n]; delete d.stg[n] } return { committed: a.names.length } }
    if (op === 'font_put') {
      if (n === deadAt) throw err('TF card write failed');
      const raw = Buffer.from(a.data, 'base64');
      if (n === badCrcAt) throw err('chunk CRC mismatch', 'device', 'chunk_crc');
      if (crc32(new Uint8Array(raw)) !== a.crc) throw err('chunk CRC mismatch', 'device', 'chunk_crc');
      if (a.off < d.got) return { got: d.got };          /* a resend of a chunk already written: acknowledged, not written again */
      if (a.off !== d.got) throw err('out of order');
      d.buf.push(raw); d.got += raw.length;
      if (n === lostReplyAt) throw err('no answer', 'timeout');   /* written, but the reply is lost */
      return { got: d.got };
    }
    if (op === 'font_put_end') {
      const all = Buffer.concat(d.buf), h = crypto.createHash('sha256').update(all).digest('hex');
      if (h !== a.sha256) throw err('SHA-256 mismatch: send it again');
      d.ended = all; d.stg[d.name] = all; d.active = false; return { staged: true, size: all.length, sha256: h };
    }
    throw err('unknown op');
  };
  return d;
}
const data = new Uint8Array(5000).map((_, i) => (i * 7) & 255); data.set([0, 1, 0, 0]);
const same = (a, b) => a && Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0;
let d = device(), prog = [];
let r = await FP.upload(d.request, 'ui-tc.ttf', data, (s, t) => prog.push(s));
ok(r.staged && same(d.ended, data) && prog.join() === '2048,4096,5000', 'plain upload ' + prog.join());
d = device({ lostReplyAt: 2 }); r = await FP.upload(d.request, 'ui-tc.ttf', data);
ok(r.staged && same(d.ended, data), 'lost reply: resumes from the device count');
d = device({ badCrcAt: 3 }); r = await FP.upload(d.request, 'ui-tc.ttf', data);
ok(r.staged && same(d.ended, data), 'bad chunk crc: sent again');
d = device({ deadAt: 3 }); let e = await FP.upload(d.request, 'ui-tc.ttf', data).catch(x => x);
ok(e instanceof Error && /write failed/.test(e.message) && d.aborted === 1 && !d.active, 'hard failure: abort sent');
const bad = device(); const orig = bad.request; bad.request = async (op, a) => op === 'font_put_end' ? orig(op, { ...a, sha256: '0'.repeat(64) }) : orig(op, a);
e = await FP.upload(bad.request, 'ui-sc.ttf', data).catch(x => x); ok(e instanceof Error && bad.aborted === 1, 'final hash mismatch: abort');
/* a set: all three staged then committed together; a failure in the third discards the first two (nothing live) */
d = device(); r = await FP.uploadAll(d.request, [['ui-tc.ttf', data], ['ui-sc.ttf', data], ['ui-ext.ttf', data]]);
ok(r.committed === 3 && Object.keys(d.live).length === 3 && !Object.keys(d.stg).length, 'set committed together');
d = device({ deadAt: 12 }); e = await FP.uploadAll(d.request, [['ui-tc.ttf', data], ['ui-sc.ttf', data], ['ui-ext.ttf', data]]).catch(x => x);
ok(e instanceof Error && !Object.keys(d.live).length && !Object.keys(d.stg).length && d.abortAll, 'a set that fails in the third file leaves nothing staged or live: ' + Object.keys(d.live) + Object.keys(d.stg));
if (fails.length) { console.log('FAIL: ' + fails.join('; ')); process.exit(1) } console.log('PASS fontput');
