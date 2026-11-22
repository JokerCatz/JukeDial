// SPDX-License-Identifier: LGPL-2.1-or-later
/* The 刷機 (Flash) tab: the board (no default), the firmware file the viewer picked from the project's Releases page (validated by Flasher.parseImage: the CONTENT decides, never the name),
   a session through Flasher (which pauses the shared Conn), checks, backup, write, done.  The page hosts no firmware and the file never leaves the browser.
   Uses the page's $, el, toast, T, Lang, Conn, Flasher. While Conn is 'flashing' every other tab is greyed and a banner says so. */
(() => {
  const RELEASES_URL = 'https://github.com/JokerCatz/JukeDial/releases';   /* the project's Releases page: empty until the owner decides it; then the page shows a link, until then only the words "download the release file" */
  const BOARD_NAME = { jc3636f118: () => T('W_FLASH_BOARD_ROUND'), fakepod: () => T('W_FLASH_BOARD_FAKEPOD') };
  const BOARD_DESC = { jc3636f118: () => T('W_FLASH_BOARD_ROUND_DESC'), fakepod: () => T('W_FLASH_BOARD_FAKEPOD_DESC') };
  const DL_HINT = { jc3636f118: () => T('W_FLASH_DL_ROUND'), fakepod: () => T('W_FLASH_DL_FAKEPOD') };
  /* no board is ever selected for the viewer: the menu starts empty.  deviceBoard = what the running player said (CTL status.board) before the page took the port; it is only compared, never picked from */
  let deviceBoard = '', img = null, imgErr = null, fileName = '', fileBytes = null, sess = null, res = null, running = false, lost = false, parsing = 0;
  const resetFor = b => b === 'fakepod' ? 'watchdog' : true;
  const kb = n => Math.max(1, Math.round(n / 1024));
  const hex = n => '0x' + n.toString(16);
  const setMsg = t => { $('#fl-msg').textContent = t || '' };
  const stat = t => { $('#fl-stat').textContent = t || '' };
  const bar = f => { const b = $('#fl-bar'); b.hidden = f == null; if (f != null) b.value = Math.round(f * 1000) };
  function errText(e) {
    const x = e.extra;
    switch (e.code) {
      case 'size': return T('W_FLASH_FERR_SIZE');
      case 'no_boot': return T('W_FLASH_FERR_NO_BOOT');
      case 'boot_chip': return T('W_FLASH_FERR_BOOT_CHIP');
      case 'boot_hash': return T('W_FLASH_FERR_BOOT_HASH');
      case 'no_pt': return T('W_FLASH_FERR_NO_PT');
      case 'pt_md5': return T('W_FLASH_FERR_PT_MD5');
      case 'pt_layout': return T('W_FLASH_FERR_PT_LAYOUT', x ? x + ': ' : '');
      case 'no_app': return T('W_FLASH_FERR_NO_APP');
      case 'app_desc': return T('W_FLASH_FERR_APP_DESC');
      case 'app_chip': return T('W_FLASH_FERR_APP_CHIP');
      case 'app_hash': return T('W_FLASH_FERR_APP_HASH');
      case 'gap': return T('W_FLASH_FERR_GAP', x || '');
      case 'no_fonts': return T('W_FLASH_FERR_NO_FONTS');
      case 'fonts_big': return T('W_FLASH_FERR_FONTS_BIG');
      case 'no_board': return T('W_FLASH_FERR_NO_BOARD');
      case 'board_name': return T('W_FLASH_FERR_BOARD_NAME', x || '');
      case 'board_pick': return T('W_FLASH_FERR_BOARD_PICK', BOARD_NAME[x] ? BOARD_NAME[x]() : x || '');
      case 'read': return T('W_FLASH_FERR_READ');
      case 'verify': return T('W_FLASH_ERR_VERIFY', x || '');
      case 'backup_verify': return T('W_FLASH_ERR_BACKUP_VERIFY');
      default: return T('W_FLASH_ERR_OTHER', e.message);
    }
  }
  /* our own line art of the picked board (board_<name>.svg next to the page, drawn in the page's text colour) */
  const ART = { jc3636f118: 'board_round.svg', fakepod: 'board_fakepod.svg' }, artCache = {};
  let artFor = '';
  function drawArt(n) {
    const a = ART[n]; $('#fl-art').hidden = !a; artFor = n;
    if (!a) return;
    if ($('#fl-art-img').dataset.b === n) return;
    $('#fl-art-img').replaceChildren(); $('#fl-art-img').dataset.b = n;
    (artCache[n] = artCache[n] || fetch(a).then(r => r.ok ? r.text() : Promise.reject(new Error(a)))).then(t => {
      if (artFor !== n) return;
      const d = new DOMParser().parseFromString(t, 'image/svg+xml');
      if (d.documentElement.nodeName === 'svg') $('#fl-art-img').replaceChildren(document.importNode(d.documentElement, true))
    }, () => { artCache[n] = null; $('#fl-art-img').dataset.b = '' });
  }
  const built = () => (img.buildDate + ' ' + img.buildTime).trim() || T('W_FLASH_F_NODATE');   /* a reproducible build (CONFIG_APP_REPRODUCIBLE_BUILD) leaves date and time empty in esp_app_desc_t */
  const row = (k, v, bad) => el('div', { className: 'set' }, el('div', {}, k), el('div', { className: bad ? 'chip bad' : 'dim', style: 'white-space:normal;word-break:break-all' }, v));
  function drawFile() {
    const L = $('#fl-finfo'); L.replaceChildren();
    if (imgErr) L.append(el('div', { className: 'chip bad', style: 'display:block;margin:4px 0;white-space:normal' }, errText(imgErr)));
    else if (img) {
      L.append(row(fileName, (img.size / 1048576).toFixed(1) + ' MB'));
      L.append(el('div', { className: 'dim small' }, T('W_FLASH_F_VERSION', img.version, built(), '')));
      L.append(el('div', { className: 'dim small' }, T('W_FLASH_F_BOARD', BOARD_NAME[img.board]())));
      L.append(el('div', { className: 'dim small', style: 'word-break:break-all' }, T('W_FLASH_F_SHA', img.sha256)));
      L.append(el('div', { className: 'chip ok', style: 'display:block;margin:4px 0;white-space:normal' }, T('W_FLASH_F_OK')));
    }
  }
  function drawPick() {
    const b = $('#fl-board'), cur = b.value;
    b.replaceChildren(el('option', { value: '' }, T('W_FLASH_BOARD_PICK')), ...Flasher.BOARDS.map(n => el('option', { value: n, selected: n === cur }, BOARD_NAME[n]())));
    drawArt(b.value);
    $('#fl-releases').hidden = !RELEASES_URL; if (RELEASES_URL) $('#fl-releases').href = RELEASES_URL;
    const mm = Flasher.boardCheck(deviceBoard, b.value) === 'mismatch';
    $('#fl-auto').hidden = !deviceBoard; $('#fl-auto').textContent = deviceBoard ? T('W_FLASH_DEVICE_BOARD', BOARD_NAME[deviceBoard]()) : '';
    $('#fl-mismatch').hidden = !mm; $('#fl-mismatch-t').textContent = mm ? T('W_FLASH_MISMATCH_CHIP', BOARD_NAME[deviceBoard](), BOARD_NAME[b.value]()) : '';
    $('#fl-dl').textContent = DL_HINT[b.value] ? DL_HINT[b.value]() : T('W_FLASH_DL_NONE');
    drawFile();
    $('#fl-check').disabled = running || parsing > 0 || !img || !b.value || !Conn.supported && !location.search.includes('mockserial');
    if (!Conn.supported && !location.search.includes('mockserial')) setMsg(T('W_THIS_BROWSER_HAS_NO_WEB_SERIAL_USE_DESKTOP_C'));
  }
  /* (re)validate the picked file against the picked board: the content must say that board */
  async function revalidate() {
    img = null; imgErr = null;
    if (!fileBytes) return drawPick();
    const my = ++parsing; drawPick();
    try { const r = await Flasher.parseImage(fileBytes, { name: fileName, board: $('#fl-board').value }); if (my === parsing) img = r } catch (e) { if (my === parsing) imgErr = e }
    if (my === parsing) { parsing = 0; drawPick() }
  }
  async function takeBytes(name, bytes) {   /* what takeFile does after the read: the tests hand the bytes over directly (reading a 15 MB File is real I/O that headless Chrome's virtual time does not wait for) */
    if (running) return;
    if (sess) await closeSession('');
    fileName = name; fileBytes = bytes; img = null; imgErr = null; drawFile();
    await revalidate();
  }
  async function takeFile(f) {
    if (!f || running) return;
    let bytes;
    try { bytes = new Uint8Array(await f.arrayBuffer()) } catch (e) { fileName = f.name; fileBytes = null; img = null; imgErr = { code: 'read', message: e.message }; return drawPick() }
    await takeBytes(f.name, bytes);
  }
  function planText() {
    const p = Flasher.planWrite(img, { factoryReset: $('#fl-factory').checked });
    const lines = [T('W_FLASH_PLAN_WRITE', img.regions.map(r => `${r.name} ${hex(r.offset)} (${kb(r.data.length)} KB)`).join(', '))];
    if (p.kept.length) lines.push(T('W_FLASH_PLAN_KEEP', p.kept.map(e => `${e.name} ${hex(e.offset)}`).join(', ')));
    if (p.erased.length) lines.push(T('W_FLASH_PLAN_ERASE', p.erased.map(e => `${e.name} ${hex(e.offset)}`).join(', ')));
    return { plan: p, text: lines.join('\n') };
  }
  function drawInfo() {
    if (!sess || !img) return;
    const i = sess.info;
    $('#fl-rows').replaceChildren(...[[T('W_FLASH_CHIP'), i.chip], [T('W_FLASH_FLASH_SIZE'), i.flashSize || '?'], ['MAC', i.mac]].map(([k, v]) => el('div', { className: 'set' }, el('div', {}, k), el('div', { className: 'dim' }, v))));
    const L = $('#fl-checks'); L.replaceChildren();
    for (const c of res.errors) L.append(el('div', { className: 'chip bad', style: 'display:block;margin:4px 0;white-space:normal' }, c === 'chip' ? T('W_FLASH_ERR_CHIP', i.chip) : T('W_FLASH_ERR_FLASH_SMALL', i.flashSize || '?', Math.ceil(img.size / 1048576))));
    if (!res.errors.length) L.append(el('div', { className: 'chip ok', style: 'display:block;margin:4px 0;white-space:normal' }, T('W_FLASH_CHECKS_OK')));
    $('#fl-opts').hidden = res.errors.length > 0;
    $('#fl-plan').textContent = planText().text;
    $('#fl-ptnote').hidden = !Flasher.ptMoved(i.ptBytes, img);
    $('#fl-skiprow').hidden = $('#fl-backup').checked; syncGo();
  }
  const syncGo = () => { $('#fl-go').disabled = running || !$('#fl-backup').checked && !$('#fl-skip').checked };
  async function closeSession(msg) {
    const s = sess; sess = null; res = null; $('#fl-info').hidden = true; bar(null); stat('');
    if (s) { lost = !!await s.end({ reset: resetFor($('#fl-board').value) }).catch(() => true) }   /* Cancel leaves download mode: the player boots again */
    running = false; drawPick(); if (msg) setMsg(msg);
  }
  async function check() {
    if (running) return; const board = $('#fl-board').value;
    if (!board) return toast(T('W_FLASH_BOARD_PICK'), 1);
    if (!img) return toast(T('W_FLASH_NEED_FILE'), 1);
    running = true; drawPick(); setMsg(T('W_FLASH_CONNECTING'));
    try {
      sess = await Flasher.begin({ Conn, esp: window.__esptoolMock });
      if (!sess) { running = false; drawPick(); return }
      res = { errors: Flasher.chipCheck(sess.info, img) };
      running = false; setMsg(''); $('#fl-info').hidden = false; $('#fl-backup').checked = true; $('#fl-skip').checked = false; $('#fl-factory').checked = false; drawInfo(); drawPick();
    } catch (e) { console.error(e); running = false; sess = null; drawPick(); setMsg(errText(e)); toast(errText(e), 1) }
  }
  function download(blob, name) { const a = el('a', { href: URL.createObjectURL(blob), download: name }); document.body.append(a); a.click(); setTimeout(() => { URL.revokeObjectURL(a.href); a.remove() }, 4000) }
  async function go() {
    if (running || !sess || res.errors.length || !img) return;
    const board = $('#fl-board').value;
    if (img.board !== board) return;   /* the content says another board than the pick: parseImage already said so, this is the last wall */
    running = true; syncGo(); bar(0); let phase = 'prepare';
    try {
      const { plan, text } = planText(), parts = plan.parts;
      /* a running player that names the other board: a loud extra question first; Cancel leaves everything as it is */
      if (Flasher.boardCheck(deviceBoard, board) === 'mismatch' && !confirm(T('W_FLASH_MISMATCH_WARN', BOARD_NAME[deviceBoard](), BOARD_NAME[board]()))) { running = false; bar(null); stat(''); syncGo(); return }
      if (!confirm(T('W_FLASH_CONFIRM_BOARD', BOARD_NAME[board](), BOARD_DESC[board]()) + '\n\n' + T('W_FLASH_CONFIRM_FILE', img.version, built(), fileName) + '\n\n' + T('W_FLASH_CONFIRM', parts.map(p => T('W_FLASH_PART_LINE', p.name, hex(p.offset), kb(p.data.length))).join('\n')) + '\n' + text.split('\n').slice(1).join('\n'))) { running = false; bar(null); stat(''); syncGo(); return }
      if ($('#fl-backup').checked) {
        const blob = await sess.backup(f => { bar(f); stat(T('W_FLASH_BACKING_UP', Math.round(f * 100))) });
        const d = new Date(), name = `backup_esp32s31_${sess.info.mac.replace(/[^0-9a-f]/gi, '').slice(-4)}_${d.getFullYear()}${String(d.getMonth() + 1).padStart(2, '0')}${String(d.getDate()).padStart(2, '0')}.bin`;
        download(blob, name); toast(T('W_FLASH_BACKUP_SAVED', name)); window.__lastBackup = { name, size: blob.size };
        /* nothing is written until the viewer says the file is really on the computer (a blocked or cancelled download is no backup) */
        if (!confirm(T('W_FLASH_BACKUP_CONFIRM', name, Math.round(blob.size / 1048576)))) { running = false; bar(null); stat(''); syncGo(); return }
      }
      phase = 'write';
      await sess.write(parts, { flash: { mode: 'keep', freq: 'keep' }, onProgress: f => { bar(f); stat(T('W_FLASH_WRITING', Math.round(f * 100))) } });
      stat(T('W_FLASH_FINISHING'));
      const s = sess; sess = null; lost = !!await s.end({ reset: resetFor(board) });
      running = false; $('#fl-info').hidden = true; bar(null); stat(''); $('#fl-done').hidden = false; $('#fl-done-fp').hidden = board !== 'fakepod'; drawPick(); window.__flashDone = true;
    } catch (e) {
      console.error(e); running = false; bar(null); stat(''); syncGo();
      if (phase === 'write') {   /* the flash may hold half an image: release the port (the flashing state ends), say what to do */
        const m = (e.code === 'verify' ? errText(e) + ' ' : '') + T('W_FLASH_WRITE_FAILED') + ' ' + T('W_FLASH_BOOT_HINT');
        const s = sess; sess = null; res = null; $('#fl-info').hidden = true; if (s) await s.end({ reset: false }).catch(() => {});
        drawPick(); setMsg(m); toast(m, 1); window.__flashFailed = true; return;
      }
      toast(errText(e), 1); setMsg(errText(e));
    }
  }
  /* the other tabs are greyed while Conn is 'flashing' */
  Conn.on('state', s => {
    const f = s === 'flashing';
    $('#flashbanner').hidden = !f;
    document.querySelectorAll('nav button').forEach(b => { if (b.dataset.t !== 'flash') b.disabled = f });
    document.querySelectorAll('#conn,#stopbtn,#vol').forEach(x => { x.disabled = f });
  });
  $('#fl-board').onchange = () => { revalidate() };
  $('#fl-pick').onclick = () => $('#fl-file').click();
  $('#fl-file').onchange = () => { const f = $('#fl-file').files[0]; $('#fl-file').value = ''; takeFile(f) };
  const drop = $('#fl-drop');
  drop.ondragover = e => { e.preventDefault() };
  drop.ondrop = e => { e.preventDefault(); takeFile(e.dataTransfer && e.dataTransfer.files[0]) };
  $('#fl-check').onclick = check; $('#fl-go').onclick = go; $('#fl-cancel').onclick = () => closeSession(T('W_FLASH_CANCELLED'));
  $('#fl-backup').onchange = $('#fl-skip').onchange = () => { $('#fl-skiprow').hidden = $('#fl-backup').checked; syncGo() };
  $('#fl-factory').onchange = () => { if (sess && img) $('#fl-plan').textContent = planText().text };
  Lang.onChange(() => { drawPick(); drawInfo() });
  drawPick();
  /* the player says which board it is (CTL status.board): remembered for the mismatch warning only */
  function setBoard(n) { const b = Flasher.BOARDS.includes(n) ? n : ''; if (b === deviceBoard || running) return; deviceBoard = b; drawPick() }
  Conn.on('state', s => { if (s === 'idle' && deviceBoard) { deviceBoard = ''; drawPick() } });   /* the player is gone: what it said is no longer about whatever is plugged in next */
  window.flashTab = { setBoard, get deviceBoard() { return deviceBoard }, drawPick, check, go, takeFile, takeBytes, get session() { return sess }, get image() { return img } };
})();
