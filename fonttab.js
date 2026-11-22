// SPDX-License-Identifier: LGPL-2.1-or-later
/* The 字型 (Fonts) tab: the character set (from the player over Conn, or from a file), the Noto sources, the subset build in a Worker, sizes, three downloads.
   Uses the page's $, el, toast, T, Lang, Conn, GlyphPack, FontBuild (fontbuild.js), FONT_SETS. Output goes to the card's _player/fonts/ by hand (external mode); the serial upload is W7. */
(() => {
  const q = new URLSearchParams(location.search);
  const TEST = location.search.includes('mockserial') && /^testdata\/[\w\/.-]+\/$/.test(q.get('fontsrc') || '');
  const SRC = TEST ? { tc: q.get('fontsrc') + 'tc.ttf', sc: q.get('fontsrc') + 'sc.ttf', ext: q.get('fontsrc') + 'ext.ttf' }
    : { tc: 'noto/NotoSansTC-Regular.ttf', sc: 'noto/NotoSansSC-Regular.ttf', ext: 'fonts/ui-ext.ttf' };
  const WASM = 'vendor/harfbuzzjs/' + FontSubset.HB_VERSION + '/harfbuzz-subset.wasm';
  const src = { tc: null, sc: null, ext: null }, name = { tc: '', sc: '', ext: '' };
  let device = '', miss = [], fileChars = '', info = null, out = null, building = false, worker = null, wasmBytes = null;
  const kb = n => Math.round(n / 1024);
  const sample = cps => cps.slice(0, 40).map(c => String.fromCodePoint(c)).join('');
  function drawSrc() {
    for (const k of ['tc', 'sc', 'ext']) $('#fo-' + k + '-st').textContent = src[k] ? (name[k] ? T('W_FONTS_SRC_PICKED', name[k]) : T('W_FONTS_SRC_BUNDLED')) : T('W_FONTS_SRC_MISSING');
    $('#fo-build').disabled = building || !src.tc || !src.sc;
  }
  function drawChars() {
    $('#fo-chars').textContent = info || T('W_FONTS_NO_CHARS');
  }
  async function loadBundled() {
    for (const k of ['tc', 'sc', 'ext']) {
      try { const r = await fetch(SRC[k]); if (r.ok) { const b = new Uint8Array(await r.arrayBuffer()); if (FontSubset.flavor(b) !== 'unknown') src[k] = b } } catch {}
    }
    drawSrc();
  }
  function pick(k) {
    $('#fo-' + k).onchange = async e => {
      const f = e.target.files[0]; e.target.value = ''; if (!f) return;
      const b = new Uint8Array(await f.arrayBuffer());
      if (FontSubset.flavor(b) === 'unknown') return toast(T('W_FONTS_ERR_SOURCE', f.name), 1);
      src[k] = b; name[k] = f.name; drawSrc();
    };
  }
  async function readDevice() {
    if (!Conn.up) return toast(T('W_CONNECT_TO_THE_PLAYER_FIRST'), 1);
    try {
      let chars = '', total = 1, off = 0;
      while (off < total) {
        const r = await dev('font_charset', { off }, 15000); total = r.total; const got = [...r.chars].length; chars += r.chars; off += got;
        $('#fo-chars').textContent = T('W_FONTS_READING', off + '/' + total); if (!got) break;
      }
      const m = await dev('font_miss');
      device = chars; miss = m.list || []; info = T('W_FONTS_CHARS_FROM', [...device].length, miss.length + (m.more || 0)); drawChars();
    } catch (e) { toast(e.message, 1) }
  }
  async function readFile(f) {
    const b = new Uint8Array(await f.arrayBuffer()); let text = '';
    if (b[0] === 0x50 && b[1] === 0x4b) {   /* a zip (library.muk): every string in its json files */
      const walk = v => typeof v === 'string' ? v : Array.isArray(v) ? v.map(walk).join('') : v && typeof v === 'object' ? Object.values(v).map(walk).join('') : '';
      for (const [n, d] of await GlyphPack.unzip(b)) if (n.endsWith('.json')) try { text += walk(JSON.parse(new TextDecoder().decode(d))) } catch {}
    } else text = new TextDecoder().decode(b);
    fileChars = text; info = T('W_FONTS_CHARS_FILE', new Set(text).size); drawChars();
  }
  const err = e => e.code === 'cff' ? T('W_FONTS_ERR_CFF', e.extra === 'tc' ? 'Noto Sans TC' : 'Noto Sans SC') : e.code === 'source' ? T('W_FONTS_ERR_SOURCE', e.extra) : T('W_FONTS_ERR_OTHER', e.message);
  function download(bytes, fn) { const a = el('a', { href: URL.createObjectURL(new Blob([bytes], { type: 'font/ttf' })), download: fn }); document.body.append(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove() }, 4000) }
  let last = Promise.resolve();
  function build() { last = build1(); return last }
  async function build1() {
    if (building) return; building = true; drawSrc(); $('#fo-msg').textContent = T('W_FONTS_BUILDING'); $('#fo-result').replaceChildren(); $('#fo-dl').replaceChildren();
    try {
      const cps = FontBuild.wanted({ sets: FONT_SETS, device: device + fileChars, miss, extra: $('#fo-extra').value, gb2: $('#fo-gb2').checked });
      const first = !worker; worker = worker || new Worker('fontworker.js');
      if (first) wasmBytes = await (await fetch(WASM)).arrayBuffer();
      const tc = src.tc.slice().buffer, sc = src.sc.slice().buffer;
      const r = await new Promise((res, rej) => { worker.onmessage = e => e.data && e.data.ok === true ? res(e.data) : rej(Object.assign(new Error(e.data.error), { code: e.data.code, extra: e.data.extra })); worker.onerror = e => rej(new Error(e.message)); worker.postMessage({ wasm: first ? wasmBytes : null, tc, sc, cps: Int32Array.from(cps) }, [tc, sc]) });
      const extMap = src.ext ? FontSubset.cmap(src.ext) : new Set(), missing = r.missing.filter(c => !extMap.has(c));
      out = { tc: r.tc, sc: r.sc, ext: src.ext, counts: r.counts, missing };
      const sizes = [r.tc.length, r.sc.length].concat(src.ext ? [src.ext.length] : []), pack = FontBuild.packSize(sizes), room = FontBuild.PART_BYTES - FontBuild.FREE_MIN;
      const R = $('#fo-result'); R.replaceChildren(
        el('div', {}, T('W_FONTS_RESULT_LINE', 'ui-tc.ttf', kb(r.tc.length), r.counts.tc)), el('div', {}, T('W_FONTS_RESULT_LINE', 'ui-sc.ttf', kb(r.sc.length), r.counts.sc)),
        ...(src.ext ? [el('div', {}, T('W_FONTS_RESULT_LINE', 'ui-ext.ttf', kb(src.ext.length), [...extMap].length))] : []),
        el('div', { className: 'chip ' + (pack <= room ? 'ok' : 'bad'), style: 'display:block;margin:6px 0;white-space:normal', id: 'fo-pack' }, pack <= room ? T('W_FONTS_PACK_OK', kb(pack), kb(room - pack), kb(FontBuild.FREE_MIN)) : T('W_FONTS_PACK_BIG', kb(pack), kb(pack - room))),
        el('div', { className: 'dim small', id: 'fo-missing' }, missing.length ? T('W_FONTS_MISSING', missing.length, sample(missing)) : T('W_FONTS_MISSING_NONE')));
      const D = $('#fo-dl');
      for (const [k, fn] of [['tc', 'ui-tc.ttf'], ['sc', 'ui-sc.ttf'], ['ext', 'ui-ext.ttf']]) if (out[k]) D.append(el('button', { on: { click: () => download(out[k], fn) } }, T('W_FONTS_DOWNLOAD', fn)));
      drawSend(); $('#fo-msg').textContent = ''; window.__fontOut = { tc: r.tc.length, sc: r.sc.length, pack, ok: pack <= room, missing: missing.length, counts: r.counts };
    } catch (e) { console.error(e); if (worker) { worker.terminate(); worker = null }   /* a worker that failed may hold a broken wasm instance: the next build starts a new one */
      $('#fo-msg').textContent = err(e); toast(err(e), 1) }
    building = false; drawSrc();
  }
  /* send the built files to the player's card over the serial link (FontPut; the device stages them as .new and swaps them in at the next boot) */
  /* the device answers with an error code (fonts_ctl.c): words in the page's language, not the firmware's English */
  const DEV = { card_pc: () => T('W_FONTS_DEV_CARD_PC'), no_card: () => T('W_FONTS_DEV_NO_CARD'), name: () => T('W_FONTS_DEV_NAME'), size: () => T('W_FONTS_DEV_SIZE'), no_upload: () => T('W_FONTS_DEV_NO_UPLOAD'), args: () => T('W_FONTS_DEV_ARGS'), base64: () => T('W_FONTS_DEV_BASE64'), chunk_crc: () => T('W_FONTS_DEV_CHUNK_CRC'),
    order: () => T('W_FONTS_DEV_ORDER'), past_size: () => T('W_FONTS_DEV_PAST_SIZE'), write: () => T('W_FONTS_DEV_WRITE'), incomplete: () => T('W_FONTS_DEV_INCOMPLETE'), sha: () => T('W_FONTS_DEV_SHA'), not_ttf: () => T('W_FONTS_DEV_NOT_TTF'), rename: () => T('W_FONTS_DEV_RENAME'), nothing_staged: () => T('W_FONTS_DEV_NOTHING_STAGED'), unknown: () => T('W_FONTS_DEV_UNKNOWN') };
  const devErr = e => e.code === 'device' && DEV[e.detail] ? DEV[e.detail]() : e.message;
  let sending = false;
  const drawSend = () => { $('#fo-sendrow').hidden = !out; $('#fo-send').disabled = sending || !Conn.up; if (out && !Conn.up && !sending) $('#fo-sstat').textContent = T('W_FONTS_SEND_NEED_CONN') };
  async function send() {
    if (sending || !out || !Conn.up) return;
    sending = true; drawSend();
    const files = [['ui-tc.ttf', out.tc], ['ui-sc.ttf', out.sc], ['ui-ext.ttf', out.ext]].filter(f => f[1]);
    const t0 = Date.now(), bar = $('#fo-sbar'); bar.hidden = false;
    try {
      await FontPut.uploadAll((op, a, ms) => dev(op, a, ms), files, (all, total) => {
        const sec = (Date.now() - t0) / 1000 || 1, rate = all / sec;
        bar.value = Math.round(1000 * all / total); $('#fo-sstat').textContent = T('W_FONTS_SENDING', files.map(f => f[0]).join(' '), Math.round(100 * all / total), Math.round(rate / 1024), Math.round((total - all) / (rate || 1)));
      });
      $('#fo-sstat').textContent = T('W_FONTS_SENT'); window.__fontSent = true;
    } catch (e) { console.error(e); const m = T('W_FONTS_SEND_FAIL', devErr(e)); $('#fo-sstat').textContent = m; toast(m, 1) }
    sending = false; bar.hidden = true; drawSend();
  }
  $('#fo-send').onclick = send;
  Conn.on('state', drawSend);
  $('#fo-read').onclick = readDevice;
  $('#fo-file').onchange = async e => { const f = e.target.files[0]; e.target.value = ''; if (f) try { await readFile(f) } catch (x) { toast(x.message, 1) } };
  ['tc', 'sc', 'ext'].forEach(pick);
  $('#fo-build').onclick = build;
  Lang.onChange(() => { drawSrc(); drawChars() });
  drawChars(); drawSrc(); loadBundled();
  window.fontTab = { build, send, whenBuilt: () => last, readDevice, get state() { return { device, miss, out } } };
})();
