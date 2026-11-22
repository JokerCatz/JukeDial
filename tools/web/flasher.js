// SPDX-License-Identifier: LGPL-2.1-or-later
/* Flashing logic: the checks, the parsing of ONE merged image file the viewer picks (jukedial-<version>-<board>.bin, written from offset 0), and one flashing session on top of esptool-js
   (vendored, pinned, lazy-loaded).  The page hosts no firmware: the file comes from the project's Releases page and never leaves the browser.
   No UI here (flashtab.js is the tab) and no text: errors carry .code and the page turns them into words with T().
   A session owns the serial port through Conn.exclusive-style pause: begin() pauses the shared connection, end() resumes it.
     const s = await Flasher.begin({ Conn, esp? })      pause Conn, open the ROM loader, read chip / flash size / MAC / partition table
     s.info                                              { chip, flashBytes, mac, ptBytes }
     await s.backup(onProgress)                          -> Blob of the whole flash (read_flash)
     await s.write(parts, { flash, onProgress })         parts [{name, offset, data}] deflate write, md5-verified by esptool-js, retried at most 2 times
     await s.end({ reset })                              hard_reset / watchdog, release the port, resume Conn ('lost' after a reset: the page asks to press Connect)
   Pure helpers (also used by tests): parseName, parseImage (async: SHA-256), planWrite, ptMoved, boardCheck, chipCheck, sha256Hex, md5Hex.
   parseImage error codes: size no_boot boot_chip boot_hash no_pt pt_md5 pt_layout no_app app_desc app_chip app_hash gap no_fonts fonts_big no_board board_name (name and content disagree). Nothing here trusts the file name. */
const Flasher = (() => {
  const ESPTOOL_VERSION = '0.7.1', ESPTOOL_URL = 'vendor/esptool-js/' + ESPTOOL_VERSION + '/bundle.js';
  const CHIP = 'ESP32-S31';
  /* app/partitions.csv (tools/check/check_web_flasher.py keeps these equal to it); the ESP32-S31 bootloader sits at 0x2000 (flasher_args.json of the build, esptool BOOTLOADER_FLASH_OFFSET), the partition table at 0x8000 */
  const SLOTS = { bootloader: { off: 0x2000, size: 0x6000 }, 'partition-table': { off: 0x8000, size: 0x1000 }, app: { off: 0x10000, size: 0xbf0000 }, fonts: { off: 0xc00000, size: 4 << 20 } };
  const BOARDS = ['jc3636f118', 'fakepod'];
  /* ESP32-S31 LP watchdog (esptool 5.x targets/esp32s31.py: DR_REG_LP_WDT_BASE 0x20801000; the sequence is ESP32C3ROM.watchdog_reset, which ESP32C5ROM and ESP32S31ROM inherit).
     esptool-js 0.7.1 has no watchdogReset for the S31 target and its after() has no 'watchdog_reset' mode, so the FakePod's way out of download mode is written here. */
  const WDT = { base: 0x20801000, config0: 0x0, config1: 0x4, wprotect: 0x18, wkey: 0x50d83aa1, config0Value: 0xd0000102 | 0, timeout: 2000 };
  /* what the running player says (CTL status.board) against the board picked on the page: 'unknown' = no answer (the page never talked to a player, or it was in download mode), nothing to compare */
  function boardCheck(deviceBoard, picked) {
    if (!BOARDS.includes(picked)) return 'nopick';
    if (!BOARDS.includes(deviceBoard)) return 'unknown';
    return deviceBoard === picked ? 'match' : 'mismatch';
  }
  const err = (code, extra) => { const e = new Error(code + (extra ? ': ' + extra : '')); e.code = code; e.extra = extra; return e };
  let esp = null;
  async function loadEsptool() { return esp || (esp = await import('./' + ESPTOOL_URL)) }

  async function sha256Hex(bytes) {
    const d = await crypto.subtle.digest('SHA-256', bytes);
    return [...new Uint8Array(d)].map(b => b.toString(16).padStart(2, '0')).join('');
  }
  /* MD5 (RFC 1321), synchronous: esptool-js wants a function image -> hex to compare with the flash's own MD5 after every write (crypto.subtle has no MD5) */
  function md5Hex(u8) {
    const K = new Int32Array(64), R = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21];
    for (let i = 0; i < 64; i++) K[i] = Math.floor(Math.abs(Math.sin(i + 1)) * 4294967296) | 0;
    const n = u8.length, total = ((n + 8) >> 6) * 64 + 64, b = new Uint8Array(total); b.set(u8); b[n] = 0x80;
    const dv = new DataView(b.buffer); dv.setUint32(total - 8, (n << 3) >>> 0, true); dv.setUint32(total - 4, Math.floor(n / 536870912), true);
    let a0 = 0x67452301 | 0, b0 = 0xefcdab89 | 0, c0 = 0x98badcfe | 0, d0 = 0x10325476 | 0; const M = new Int32Array(16);
    for (let o = 0; o < total; o += 64) {
      for (let i = 0; i < 16; i++) M[i] = dv.getInt32(o + 4 * i, true);
      let A = a0, B = b0, C = c0, D = d0;
      for (let i = 0; i < 64; i++) {
        let F, g;
        if (i < 16) { F = (B & C) | (~B & D); g = i } else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) & 15 } else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) & 15 } else { F = C ^ (B | ~D); g = (7 * i) & 15 }
        F = (F + A + K[i] + M[g]) | 0; A = D; D = C; C = B; const r = R[(i >> 4) * 4 + (i & 3)]; B = (B + ((F << r) | (F >>> (32 - r)))) | 0;
      }
      a0 = (a0 + A) | 0; b0 = (b0 + B) | 0; c0 = (c0 + C) | 0; d0 = (d0 + D) | 0;
    }
    const out = new DataView(new ArrayBuffer(16)); out.setInt32(0, a0, true); out.setInt32(4, b0, true); out.setInt32(8, c0, true); out.setInt32(12, d0, true);
    return [...new Uint8Array(out.buffer)].map(x => x.toString(16).padStart(2, '0')).join('');
  }
  const NAME_BOARD = { round: 'jc3636f118', fakepod: 'fakepod' };
  /* jukedial-<version>-<board>.bin -> { version, board } or null.  Only a proposal: the content decides (parseImage). */
  function parseName(name) {
    const m = /^jukedial-([0-9A-Za-z][0-9A-Za-z._]*(?:-[0-9A-Za-z._]+)*?)-(round|fakepod)\.bin$/.exec(String(name || ''));
    return m ? { version: m[1], board: NAME_BOARD[m[2]] } : null;
  }
  const u32 = (d, o) => (d[o] | d[o + 1] << 8 | d[o + 2] << 16 | d[o + 3] << 24) >>> 0;
  const cstr = (d, o, n) => { let e = o; while (e < o + n && d[e]) e++; return String.fromCharCode(...d.subarray(o, e)) };
  const allFF = (d, a, b) => { for (let i = a; i < b; i++) if (d[i] !== 0xff) return false; return true };
  /* an ESP image (bootloader or app) starting at `off`: header 24 B (byte 0 = 0xE9, byte 1 = segment count, bytes 12..13 = chip id, byte 23 = hash appended), segments (load addr, length, data),
     padding so the checksum byte is the last of a 16-byte block, then the SHA-256 of everything before the checksum's end when the header says so.  Returns { end, hashOk } or throws a code. */
  async function espImage(d, off, codeMagic, codeChip, codeHash) {
    if (d[off] !== 0xe9 || off + 32 > d.length) throw err(codeMagic);
    if ((d[off + 12] | d[off + 13] << 8) !== 32) throw err(codeChip);   /* IMAGE_CHIP_ID of the ESP32-S31 (esptool-js S31 target IMAGE_CHIP_ID = 32) */
    let p = off + 24;
    for (let i = 0; i < d[off + 1]; i++) { if (p + 8 > d.length) throw err(codeMagic); p += 8 + u32(d, p + 4); if (p > d.length) throw err(codeMagic) }
    p = ((p - off) | 15) + off + 1;   /* checksum is the last byte of the 16-byte block */
    let end = p;
    if (d[off + 23] === 1) {
      if (end + 32 > d.length) throw err(codeHash);
      if (await sha256Hex(d.subarray(off, end)) !== [...d.subarray(end, end + 32)].map(b => b.toString(16).padStart(2, '0')).join('')) throw err(codeHash);
      end += 32;
    }
    return { start: off, end };
  }
  /* the partition table (0xc00 bytes at 0x8000): 32-byte entries (bytes AA 50, type, subtype, offset, size, name[16], flags) up to the first blank one, then the 0xEBEB line holding the MD5 of the entries */
  async function partitionTable(d) {
    const o = SLOTS['partition-table'].off, entries = [];
    let p = o, md5ok = false;
    for (; p < o + 0xc00; p += 32) {
      const m = d[p] | d[p + 1] << 8;
      if (m === 0x50aa) entries.push({ type: d[p + 2], subtype: d[p + 3], offset: u32(d, p + 4), size: u32(d, p + 8), name: cstr(d, p + 12, 16) });
      else if (m === 0xebeb) { md5ok = md5Hex(d.subarray(o, p)) === [...d.subarray(p + 16, p + 32)].map(b => b.toString(16).padStart(2, '0')).join(''); break }
      else break;
    }
    if (!entries.length) throw err('no_pt');
    if (!md5ok) throw err('pt_md5');
    return entries;
  }
  /* parse and validate a picked file; returns { name, size, sha256, nameInfo, board, version, projectName, buildDate, buildTime, idf, entries, dataParts, regions } or throws .code (first problem only).
     `d` = Uint8Array of the whole file.  expect = { board } (the viewer's choice) is compared with the content, never with the name. */
  async function parseImage(d, { name = '', board: chosen = '', flashBytes = 16 << 20 } = {}) {
    if (d.length < 0xc00000 + 0x1000 || d.length > flashBytes || d.length > 0xc00000 + SLOTS.fonts.size) throw err('size');
    if (!allFF(d, 0, SLOTS.bootloader.off)) throw err('gap', '0x0');
    const boot = await espImage(d, SLOTS.bootloader.off, 'no_boot', 'boot_chip', 'boot_hash');
    const entries = await partitionTable(d);
    const app = entries.find(e => e.type === 0 && e.offset === SLOTS.app.off && e.size === SLOTS.app.size);
    const fonts = entries.find(e => e.type === 1 && e.subtype === 0x40 && e.name === 'fonts' && e.offset === SLOTS.fonts.off && e.size === SLOTS.fonts.size);
    if (!app || !fonts) throw err('pt_layout');
    for (const e of entries) if (e.offset < 0x9000 || e.offset + e.size > flashBytes || (e !== app && e !== fonts && e.type === 0)) throw err('pt_layout', e.name);
    const ai = await espImage(d, app.offset, 'no_app', 'app_chip', 'app_hash');
    if (ai.end - ai.start > app.size) throw err('size');
    if (u32(d, app.offset + 0x20) !== 0xabcd5432) throw err('app_desc');
    const desc = { version: cstr(d, app.offset + 0x30, 32), projectName: cstr(d, app.offset + 0x50, 32), buildTime: cstr(d, app.offset + 0x70, 16), buildDate: cstr(d, app.offset + 0x80, 16), idf: cstr(d, app.offset + 0x90, 32) };
    /* the gaps between the written regions must be blank: what the viewer gets is what the build made */
    if (!allFF(d, boot.end, SLOTS['partition-table'].off)) throw err('gap', hex(boot.end));
    if (!allFF(d, SLOTS['partition-table'].off + 0xc00, app.offset)) throw err('gap', hex(SLOTS['partition-table'].off + 0xc00));
    if (!allFF(d, ai.end, fonts.offset)) throw err('gap', hex(ai.end));
    const fontsLen = d.length - fonts.offset;
    if (fontsLen <= 0 || allFF(d, fonts.offset, d.length)) throw err('no_fonts');
    if (fontsLen > fonts.size) throw err('fonts_big');
    /* the board from the CONTENT: the marker string JUKEDIAL_BOARD=<id>; in the app (app/main/guard.c).  Exactly one distinct id, or no flash. */
    const found = new Set(), dec = new TextDecoder('latin1');
    const re = /JUKEDIAL_BOARD=([a-z0-9]+);/g;
    for (let o = app.offset; o < ai.end; o += 1 << 20) {
      const chunk = dec.decode(d.subarray(o, Math.min(ai.end, o + (1 << 20) + 40)));   // 40 B overlap: a marker never straddles a chunk edge unseen
      let m; re.lastIndex = 0; while ((m = re.exec(chunk))) found.add(m[1]);
    }
    if (found.size !== 1 || !BOARDS.includes([...found][0])) throw err('no_board', [...found].join(','));
    const board = [...found][0];
    const ni = parseName(name);
    if (ni && ni.board !== board) throw err('board_name', ni.board + ' / ' + board);
    if (chosen && chosen !== board) throw err('board_pick', board);
    const regions = [
      { name: 'bootloader', offset: boot.start, data: d.subarray(boot.start, boot.end) },
      { name: 'partition-table', offset: SLOTS['partition-table'].off, data: d.subarray(SLOTS['partition-table'].off, SLOTS['partition-table'].off + 0xc00) },
      { name: 'app', offset: ai.start, data: d.subarray(ai.start, ai.end) },
      { name: 'fonts', offset: fonts.offset, data: d.subarray(fonts.offset, d.length) },
    ];
    const dataParts = entries.filter(e => e.type === 1 && e !== fonts).map(e => ({ name: e.name, offset: e.offset, size: e.size }));
    return { name, size: d.length, sha256: await sha256Hex(d), nameInfo: ni, board, ...desc, entries, dataParts, regions };
  }
  const hex = n => '0x' + n.toString(16);
  /* what to write: the four regions that carry code or fonts; the data partitions (settings, pairings: nvs, phy ...) are skipped unless factoryReset, then they are written blank */
  function planWrite(img, { factoryReset = false } = {}) {
    const parts = img.regions.map(r => ({ name: r.name, offset: r.offset, data: r.data }));
    const erased = factoryReset ? img.dataParts : [];
    for (const e of erased) parts.push({ name: e.name, offset: e.offset, data: new Uint8Array(e.size).fill(0xff) });
    return { parts, kept: factoryReset ? [] : img.dataParts, erased };
  }
  /* does the player's current table put a data partition (settings) somewhere else than the file's?  Then the old settings sit where the new layout does not look: say so. */
  function ptMoved(devicePt, img) {
    if (!devicePt) return false;
    const dev = [], d = devicePt;
    for (let p = 0; p + 32 <= d.length; p += 32) { if ((d[p] | d[p + 1] << 8) !== 0x50aa) break; if (d[p + 2] === 1 && cstr(d, p + 12, 16) !== 'fonts') dev.push(cstr(d, p + 12, 16) + ':' + u32(d, p + 4) + ':' + u32(d, p + 8)) }
    return dev.sort().join() !== img.dataParts.map(e => e.name + ':' + e.offset + ':' + e.size).sort().join();
  }
  /* the chip and the flash against what the image needs; returns the codes of what is wrong */
  function chipCheck(info, img) {
    const errors = [];
    if (info.chip !== CHIP) errors.push('chip');
    if (info.flashBytes < img.size) errors.push('flash_small');
    return errors;
  }

  async function watchdogReset(loader) {
    const w = r => WDT.base + r;
    await loader.writeReg(w(WDT.wprotect), WDT.wkey); await loader.writeReg(w(WDT.config1), WDT.timeout); await loader.writeReg(w(WDT.config0), WDT.config0Value); await loader.writeReg(w(WDT.wprotect), 0);
    await new Promise(r => setTimeout(r, 500));
  }
  async function begin({ Conn, esp: lib, log = () => {} }) {
    const E = lib || await loadEsptool();
    if (Conn.state === 'idle') { if (!await Conn.open()) return null }   /* the viewer picks the port; flashing does not need the CTL link, only a port */
    const port = await Conn.pause('flash');
    let loader = null, transport = null, ended = false;
    const release = async resume => {
      if (ended) return; ended = true; let lost = false;
      try { if (transport) await transport.disconnect() } catch {}
      if (resume) try { await Conn.resume() } catch { lost = true }
      return lost;
    };
    try {
      transport = new E.Transport(port, false);
      loader = new E.ESPLoader({ transport, baudrate: 460800, romBaudrate: 115200, terminal: { clean() {}, writeLine: log, write: log }, debugLogging: false });
      await loader.main();
      const chip = loader.chip.CHIP_NAME;
      const fs = await loader.detectFlashSize();   /* "16MB" */
      const flashBytes = fs ? parseInt(fs, 10) * (/KB/i.test(fs) ? 1024 : 1048576) : 0;
      const mac = await loader.chip.readMac(loader);
      const pt = await loader.readFlash(SLOTS['partition-table'].off, 0xc00);
      const info = { chip, flashBytes, flashSize: fs, mac, ptBytes: pt };
      return {
        info,
        async backup(onProgress) {
          const chunks = [];
          const data = await loader.readFlash(0, flashBytes, (pkt, done, total) => onProgress && onProgress(done / total));
          /* the copy in memory must be what the flash holds: the chip's own MD5 of the whole flash against ours (a short or damaged read is no backup) */
          const chip = await loader.flashMd5sum(0, flashBytes);
          if (data.length !== flashBytes || md5Hex(data) !== chip) throw err('backup_verify');
          chunks.push(data); return new Blob(chunks, { type: 'application/octet-stream' });
        },
        async write(parts, { flash, onProgress }) {
          const total = parts.reduce((a, p) => a + p.data.length, 0), sizes = parts.map(p => p.data.length);
          let lastErr;
          for (let attempt = 0; attempt < 3; attempt++) {   /* the first try and at most 2 more */
            try {
              await loader.writeFlash({
                fileArray: parts.map(p => ({ data: p.data, address: p.offset })), flashMode: flash.mode, flashFreq: flash.freq, flashSize: 'keep', eraseAll: false, compress: true,
                calculateMD5Hash: md5Hex,   /* without it esptool-js does not compare the flash with the file at all */
                reportProgress: (i, written, t) => { if (onProgress) onProgress((sizes.slice(0, i).reduce((a, b) => a + b, 0) + written * sizes[i] / t) / total) },
              });
              return;
            } catch (e) { lastErr = e; log('retry ' + (attempt + 1) + ': ' + e.message) }
          }
          throw /MD5/i.test(lastErr.message) ? err('verify', lastErr.message) : lastErr;
        },
        /* reset: false = stay in the loader; true = hard_reset (RTS); 'watchdog' = the S31 LP watchdog, for the FakePod, whose strap wiring keeps a plain reset in download mode.
           The watchdog write may fail (secure download mode, a different ROM): then the plain reset is tried and end() says so with .resetKind = 'hard' (the page tells the viewer to power-cycle) */
        async end({ reset }) {
          this.resetKind = reset ? 'hard' : 'none';
          if (reset === 'watchdog') { try { await watchdogReset(loader); this.resetKind = 'watchdog' } catch (e) { log('watchdog reset failed: ' + e.message) } }
          if (reset && this.resetKind !== 'watchdog') try { await loader.after('hard_reset') } catch {}
          return release(true);
        },
      };
    } catch (e) { await release(true); throw e }
  }
  return { ESPTOOL_VERSION, SLOTS, BOARDS, CHIP, WDT, boardCheck, parseName, parseImage, planWrite, ptMoved, chipCheck, sha256Hex, md5Hex, begin, setEsptool(m) { esp = m } };
})();
if (typeof module != 'undefined') module.exports = Flasher;
