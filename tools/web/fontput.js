// SPDX-License-Identifier: LGPL-2.1-or-later
/* Font upload to the player over the settings protocol (firmware: app/main/fonts_ctl.c): begin, 2 KB chunks each with its own CRC-32, resume from the device's count after a
   lost reply or a bad chunk (at most 3 tries per chunk), the whole file's SHA-256 at the end, abort on any failure (the device removes its .part file: no half file stays).
   A file lands as <name>.stg (verified, not active). A set is committed together (font_put_commit: every .stg becomes .new, all or none) and the device swaps the .new files in at the next boot,
   so an upload that fails half way (the third file, a pulled cable) never leaves a new TC with an old SC: on any failure everything staged is discarded (font_put_abort all:true).
     await FontPut.uploadAll(request, [[name, bytes], ...], onProgress)   request = (op, args, ms) => Promise<reply> (Conn.request); onProgress(sentOfAll, totalOfAll)
     await FontPut.upload(request, name, bytes, onProgress)               one file to .stg only (no commit)
   Errors are the request's own: e.detail is the device's error code (chunk_crc, sha, card_pc ...), the page turns it into words. */
const FontPut = (() => {
  const CHUNK = 2048;
  const b64 = u8 => { let s = ''; for (let i = 0; i < u8.length; i += 8192) s += String.fromCharCode.apply(null, u8.subarray(i, i + 8192)); return btoa(s) };
  async function sha256Hex(u8) { return [...new Uint8Array(await crypto.subtle.digest('SHA-256', u8))].map(b => b.toString(16).padStart(2, '0')).join('') }
  const retryable = e => e && (e.code === 'timeout' || e.detail === 'chunk_crc');
  async function upload(request, name, bytes, onProgress) {
    const sha = await sha256Hex(bytes);
    await request('font_put_begin', { name, size: bytes.length });
    try {
      let off = 0, tries = 0;
      while (off < bytes.length) {
        const part = bytes.subarray(off, off + CHUNK);
        try { const r = await request('font_put', { off, crc: GlyphPack.crc32(part) >>> 0, data: b64(part) }, 8000); off = r.got; tries = 0; if (onProgress) onProgress(off, bytes.length) }
        catch (e) {
          if (!retryable(e) || ++tries > 3) throw e;
          const s = await request('font_put_stat', {}, 8000).catch(() => null); if (s && typeof s.got === 'number') off = s.got;   /* go on from what the device really has */
        }
      }
      return await request('font_put_end', { sha256: sha }, 30000);
    } catch (e) { try { await request('font_put_abort', {}, 8000) } catch {} throw e }
  }
  async function uploadAll(request, files, onProgress) {
    const total = files.reduce((a, f) => a + f[1].length, 0); let done = 0;
    try {
      for (const [name, bytes] of files) { await upload(request, name, bytes, sent => onProgress && onProgress(done + sent, total)); done += bytes.length }
      return await request('font_put_commit', { names: files.map(f => f[0]) }, 15000);
    } catch (e) { try { await request('font_put_abort', { all: true }, 8000) } catch {} throw e }
  }
  return { CHUNK, upload, uploadAll };
})();
if (typeof module != 'undefined') module.exports = FontPut;
if (typeof globalThis != 'undefined') globalThis.FontPut = FontPut;
