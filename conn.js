// SPDX-License-Identifier: LGPL-2.1-or-later
/* The one Web Serial connection of the page (spec 3). Every tab goes through Conn: nothing else touches a SerialPort.
   state: idle -> ctl (JSON line protocol, answers "CTL {json}") -> flashing (an exclusive user owns the port, e.g. esptool-js) -> ctl -> idle.
   Conn.open(opts?)   -> Promise<bool>  ask for a port (Espressif filter) or use opts.port; false when the user cancelled; throws on open errors
   Conn.close()       -> Promise        let go of the port (user action)
   Conn.request(op,args?,ms?) -> Promise<reply>   one JSON line out, the "CTL" line with the same id back; errors carry .code
   Conn.pause(why)    -> Promise<SerialPort>  stop reading, drop the locks, close the port, fail every waiting request (code 'busy'); state flashing
   Conn.resume()      -> Promise        reopen the same port and go back to ctl (throws code 'lost' when it does not come back)
   Conn.exclusive(why, fn) -> pause, await fn(port), finally resume
   Conn.on('state'|'log'|'close', fn)   state(s, why) / log(line) / close(byUser)
   Conn.up (ctl and open), Conn.state, Conn.port, Conn.setMessages(fn(code)->text), Conn.useMock(fn(op,args)) for the page's demo mode.
   Error codes: notconn nosupport timeout lost busy device (then .detail is the device's own code, e.g. chunk_crc). */
const Conn = (() => {
  const MSG = { notconn: 'not connected', nosupport: 'no Web Serial', timeout: 'no answer', lost: 'connection lost', busy: 'busy', device: 'failed' };
  let msgf = null, port = null, writer = null, reader = null, loop = null, state = 'idle', why = '', nextId = 1, quietUntil = 0, ready = Promise.resolve(), stopping = false, mock = null;
  const waiting = new Map(), subs = { state: [], log: [], close: [] };
  const enc = s => new TextEncoder().encode(s);
  const err = (code, text) => { const e = new Error(text || (msgf && msgf(code)) || MSG[code] || code); e.code = code; return e };
  const emit = (k, ...a) => { for (const f of subs[k]) try { f(...a) } catch (e) { console.error(e) } };
  const setState = (s, r) => { if (s === state && (r || '') === why) return; state = s; why = r || ''; emit('state', s, why) };
  const failAll = e => { for (const w of [...waiting.values()]) w.rej(e); waiting.clear() };

  /* line framing: bytes in, lines out; an answer is a line that starts with "CTL {" (ctl.c prints "\nCTL ...": a cut-off log fragment from the board's USB ring after a closed period ends at that newline) */
  function framer(onCtl, onLog) {
    const dec = new TextDecoder(); let buf = '';
    return { reset() { buf = '' }, push(bytes) { buf += dec.decode(bytes, { stream: true }); let i;
      while ((i = buf.indexOf('\n')) >= 0) { const line = buf.slice(0, i).replace(/\r$/, ''); buf = buf.slice(i + 1);
        const k = line.startsWith('CTL {') ? 0 : -1;   /* the device prints a newline in front of every answer, so a cut-off log fragment is a line of its own */
        if (k >= 0) { try { onCtl(JSON.parse(line.slice(k + 4))) } catch {} } else if (line) onLog(line) } } };
  }
  function gotCtl(r) { const w = waiting.get(r.id); if (w) { waiting.delete(r.id); r.ok ? w.res(r) : w.rej(Object.assign(err('device', r.error || (msgf && msgf('device')) || MSG.device), { detail: r.code || '' }))   /* detail: the device's error code, when it sends one */ } }

  async function readLoop() {
    const fr = framer(gotCtl, l => emit('log', l)); reader = port.readable.getReader();
    try { for (;;) { const { value, done } = await reader.read(); if (done) break; if (Date.now() < quietUntil) { fr.reset(); continue } fr.push(value) } }
    catch (e) { /* unplugged or cancelled */ }
    finally { try { reader.releaseLock() } catch {} reader = null }
    if (!stopping) teardown(false);   /* the read side ended by itself: the board rebooted or the cable came out */
  }
  async function startIO() {
    await port.open({ baudRate: 115200, bufferSize: 65536 });
    stopping = false; writer = port.writable.getWriter(); quietUntil = Date.now() + 300;   /* what arrives for 300 ms is the stale log fragment of the closed period: dropped */
    await writer.write(enc('\n')); loop = readLoop();
    ready = new Promise(r => setTimeout(r, 300)); await ready;
  }
  async function stopIO() {
    stopping = true; const l = loop;
    if (reader) try { await reader.cancel() } catch {}
    if (l) try { await l } catch {}
    loop = null; try { if (writer) writer.releaseLock() } catch {} writer = null;
    try { await port.close() } catch {}
  }
  function teardown(byUser, closed) {   /* closed: the port is already closed (stopIO ran) */
    const was = state !== 'idle';
    try { if (writer) writer.releaseLock() } catch {} writer = null;
    const p = port; port = null; loop = null; failAll(err('lost'));
    if (p && !closed) Promise.resolve().then(() => p.close()).catch(() => {});
    setState('idle'); if (was) emit('close', byUser);
  }

  async function open(o = {}) {
    if (state !== 'idle') throw err('busy');
    if (!o.port && !('serial' in navigator)) throw err('nosupport');
    let p = o.port;
    if (!p) try { p = await navigator.serial.requestPort({ filters: o.filters || [{ usbVendorId: 0x303a }] }) } catch (e) { if (e.name == 'NotFoundError') return false; throw e }   /* Espressif only: the board, not tty0 or other adapters */
    port = p;
    try { await startIO() } catch (e) { try { if (writer) writer.releaseLock() } catch {} writer = null; port = null; throw e }
    setState('ctl'); return true;
  }
  async function close() {
    if (mock) { mock = null; setState('idle'); emit('close', true); return }
    if (!port) return; if (state === 'flashing') { const p = port; port = null; setState('idle'); emit('close', true); try { await p.close() } catch {} return }
    await stopIO(); teardown(true, true);
  }
  function request(op, args = {}, ms = 8000) {
    if (mock) return Promise.resolve().then(() => mock(op, args));
    return (async () => {
      if (state === 'flashing') throw err('busy'); if (!writer) throw err('notconn');
      await ready; if (state === 'flashing') throw err('busy'); if (!writer) throw err('notconn');
      const id = nextId++;
      return new Promise((res, rej) => {
        const t = setTimeout(() => { if (waiting.delete(id)) rej(err('timeout')) }, ms);
        waiting.set(id, { res: v => { clearTimeout(t); res(v) }, rej: e => { clearTimeout(t); rej(e) } });
        writer.write(enc(JSON.stringify({ id, op, ...args }) + '\n')).catch(e => { waiting.delete(id); clearTimeout(t); rej(e) });
      });
    })();
  }
  async function pause(r = '') {
    if (state !== 'ctl' || mock) throw err(state === 'flashing' ? 'busy' : 'notconn');
    setState('flashing', r); failAll(err('busy'));   /* before the reader goes: nothing new is written, what waits is told */
    await stopIO(); return port;
  }
  async function resume() {
    if (state !== 'flashing' || !port) throw err('notconn');
    try { await startIO() } catch (e) { teardown(false); throw err('lost') }
    setState('ctl');
  }
  async function exclusive(r, fn) { const p = await pause(r); try { return await fn(p) } finally { if (state === 'flashing' && port) await resume().catch(() => {}) } }

  return {
    get state() { return state }, get why() { return why }, get port() { return port }, get up() { return state === 'ctl' && (!!writer || !!mock) },
    get io() { return !!writer }, get supported() { return 'serial' in navigator },
    open, close, request, pause, resume, exclusive,
    on(k, f) { subs[k].push(f) }, setMessages(f) { msgf = f },
    useMock(f) { mock = f; setState('ctl') },
    _framer: framer,
  };
})();
if (typeof module != 'undefined') module.exports = Conn;
