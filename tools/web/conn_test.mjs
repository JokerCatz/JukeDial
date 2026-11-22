// SPDX-License-Identifier: LGPL-2.1-or-later
// Host test of conn.js against a pretend SerialPort (node, no browser, no hardware). Prints PASS or the first failing check.
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const Conn = createRequire(import.meta.url)(join(dirname(fileURLToPath(import.meta.url)), 'conn.js'));
const sleep = ms => new Promise(r => setTimeout(r, ms));
const enc = new TextEncoder();
let fails = [];
const ok = (c, m) => { if (!c) fails.push(m) };

function fakePort() {
  const p = { opens: 0, closes: 0, push: null, got: [], reply: null, isOpen: false };
  p.open = async o => { if (o.baudRate != 115200) throw new Error('bad open'); if (p.isOpen) throw new Error('already open'); p.isOpen = true; p.opens++;
    p.readable = new ReadableStream({ start(c) { p.push = s => c.enqueue(enc.encode(s)); p.end = () => c.error(new Error('gone')) } });
    let buf = '';
    p.writable = new WritableStream({ write(ch) { buf += new TextDecoder().decode(ch); let i; while ((i = buf.indexOf('\n')) >= 0) { const l = buf.slice(0, i); buf = buf.slice(i + 1); if (l.startsWith('{')) { const q = JSON.parse(l); p.got.push(q); if (p.reply) p.reply(q) } } } }); };
  p.close = async () => { if (p.readable && p.readable.locked) throw new Error('reader still locked'); if (p.writable && p.writable.locked) throw new Error('writer still locked'); p.isOpen = false; p.closes++ };
  return p;
}
const states = [], logs = []; let closedBy = null;
Conn.on('state', (s, w) => states.push(s + (w ? ':' + w : ''))); Conn.on('log', l => logs.push(l)); Conn.on('close', u => closedBy = u);
const P = fakePort();
P.reply = q => { if (q.op == 'slow') return; if (q.op == 'split') { const s = 'CTL ' + JSON.stringify({ id: q.id, ok: true, v: 1 }) + '\n'; P.push(s.slice(0, 5)); setTimeout(() => P.push(s.slice(5)), 10); return }
  if (q.op == 'bad') return P.push('CTL ' + JSON.stringify({ id: q.id, ok: false, error: 'nope' }) + '\n');
  P.push('I (1) log line\nstale frag\nCTL ' + JSON.stringify({ id: q.id, ok: true, echo: q.op, n: q.n }) + '\n'); };

ok(Conn.state == 'idle' && !Conn.up, 'starts idle');
const opening = Conn.open({ port: P }); await sleep(60); P.push('stale log fragment\n'); await sleep(20);   /* inside the quiet window: dropped, not logged */
ok(await opening === true && Conn.up && P.opens == 1, 'open');
ok(!logs.includes('stale log fragment'), 'quiet window drops stale fragment');
const [a, b] = await Promise.all([Conn.request('one', { n: 1 }), Conn.request('two', { n: 2 })]);
ok(a.echo == 'one' && a.n == 1 && b.echo == 'two' && b.n == 2, 'ids matched');
ok(P.got[0].id != P.got[1].id, 'ids differ'); ok(logs.includes('I (1) log line'), 'log line framed');
ok((await Conn.request('split')).v == 1, 'reply cut across two chunks');
let e = await Conn.request('bad').catch(x => x); ok(e.code == 'device' && e.message == 'nope', 'device error');
e = await Conn.request('slow', {}, 60).catch(x => x); ok(e.code == 'timeout', 'timeout');
P.push('CTL {"id":99999,"ok":true}\nCTL {"id":2}\n'); await sleep(10); ok(Conn.up, 'unknown id ignored');
/* a request waiting while pause() comes is failed with busy, the port is closed and free, and new requests are refused */
const w = Conn.request('slow', {}, 5000).catch(x => x);
const port = await Conn.pause('flash');
e = await w; ok(e.code == 'busy', 'waiting request failed on pause');
ok(port === P && !P.isOpen && P.closes == 1 && Conn.state == 'flashing' && !Conn.up, 'paused: port closed, state flashing');
e = await Conn.request('status').catch(x => x); ok(e.code == 'busy', 'request refused while flashing');
e = await Conn.pause().catch(x => x); ok(e.code == 'busy', 'double pause refused');
e = await Conn.open({ port: P }).catch(x => x); ok(e.code == 'busy', 'open refused while flashing');
await Conn.resume(); ok(Conn.up && P.opens == 2 && P.isOpen, 'resume reopens');
ok((await Conn.request('after', { n: 7 })).n == 7, 'request after resume');
/* exclusive(): resume also when the user throws */
e = await Conn.exclusive('x', async p => { ok(p === P && !P.isOpen, 'exclusive gets a closed port'); throw new Error('boom') }).catch(x => x);
ok(e.message == 'boom' && Conn.up && P.opens == 3, 'exclusive resumes after a failure');
/* unplug: the read side ends, waiting requests fail with lost, state idle, close(byUser=false) */
const w2 = Conn.request('slow', {}, 5000).catch(x => x); await sleep(5); P.end(); e = await w2; await sleep(20);
ok(e.code == 'lost' && Conn.state == 'idle' && closedBy === false && !P.isOpen, 'unplug: lost, idle, port released');
/* user close */
const P2 = fakePort(); P2.reply = P.reply;
ok(await Conn.open({ port: P2 }) && Conn.up, 'open again'); await Conn.close(); await sleep(10);
ok(Conn.state == 'idle' && closedBy === true && !P2.isOpen && P2.closes == 1, 'user close releases the port');
e = await Conn.request('x').catch(x => x); ok(e.code == 'notconn', 'request when closed');
/* resume that cannot reopen */
const P3 = fakePort(); P3.reply = P.reply; await Conn.open({ port: P3 }); await Conn.pause('f'); const o3 = P3.open; P3.open = async () => { throw new Error('usb gone') };
e = await Conn.resume().catch(x => x); ok(e.code == 'lost' && Conn.state == 'idle', 'resume failure -> lost, idle');
ok(states.join() == 'ctl,flashing:flash,ctl,flashing:x,ctl,idle,ctl,idle,ctl,flashing:f,idle', 'state sequence: ' + states.join());
/* mock */
Conn.useMock(async (op, a) => ({ op, ...a })); ok(Conn.up && (await Conn.request('m', { z: 1 })).z == 1, 'mock request');
if (fails.length) { console.log('FAIL: ' + fails.join('; ')); process.exit(1) } console.log('PASS conn');
