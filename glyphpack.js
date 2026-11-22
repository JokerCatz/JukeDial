// SPDX-License-Identifier: LGPL-2.1-or-later
/* Glyph pack writer for the settings page (format: an internal note).
   Same bytes as tools/gen/glyphpack.py. Packing and the zip code are pure (Node runs them in gp_fixture.mjs);
   render()/compile() need a browser canvas. Sets globalThis.GlyphPack. */
/* The error texts come from the page's T() (lang.js); under Node (gp_fixture.mjs) the id itself is the text. */
var T = globalThis.T || (id => id);
(() => {
const NL = 0xFFFE, NONE = 0xFFFF, VERSION = 1, HEADER = 64;
const KINDS = { plain: 0, lunch: 1, people: 2, columns: 3, vocab: 4 };
const FMT_EMPTY = 0, FMT_A8 = 1, FMT_RGB565A8 = 2, GF_SPACE = 1, GF_WORD = 2;
const bpp = f => f == FMT_A8 ? 1 : f == FMT_RGB565A8 ? 3 : 0;

/* ---------- zip (stored write; read stored + deflate) ---------- */
const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0 } return t })();
function crc32(u8) { let c = 0xFFFFFFFF; for (let i = 0; i < u8.length; i++) c = CRC[(c ^ u8[i]) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0 }
const utf8 = s => new TextEncoder().encode(s);
/* entries: [{name, data: Uint8Array}] -> Uint8Array (stored, UTF-8 names, fixed date) */
function zipStored(entries) {
  const parts = [], cds = []; let off = 0;
  const DATE = ((2026 - 1980) << 9) | (11 << 5) | 11, TIME = 0;
  for (const e of entries) {
    const nm = utf8(e.name), d = e.data, crc = crc32(d);
    const lh = new DataView(new ArrayBuffer(30));
    lh.setUint32(0, 0x04034b50, true); lh.setUint16(4, 10, true); lh.setUint16(6, 0x800, true); lh.setUint16(8, 0, true);
    lh.setUint16(10, TIME, true); lh.setUint16(12, DATE, true); lh.setUint32(14, crc, true); lh.setUint32(18, d.length, true);
    lh.setUint32(22, d.length, true); lh.setUint16(26, nm.length, true); lh.setUint16(28, 0, true);
    const ch = new DataView(new ArrayBuffer(46));
    ch.setUint32(0, 0x02014b50, true); ch.setUint16(4, 20, true); ch.setUint16(6, 10, true); ch.setUint16(8, 0x800, true);
    ch.setUint16(12, TIME, true); ch.setUint16(14, DATE, true); ch.setUint32(16, crc, true); ch.setUint32(20, d.length, true);
    ch.setUint32(24, d.length, true); ch.setUint16(28, nm.length, true); ch.setUint32(42, off, true);
    parts.push(new Uint8Array(lh.buffer), nm, d); cds.push(new Uint8Array(ch.buffer), nm);
    off += 30 + nm.length + d.length;
  }
  const cdSize = cds.reduce((a, b) => a + b.length, 0), eo = new DataView(new ArrayBuffer(22));
  eo.setUint32(0, 0x06054b50, true); eo.setUint16(8, entries.length, true); eo.setUint16(10, entries.length, true);
  eo.setUint32(12, cdSize, true); eo.setUint32(16, off, true);
  return concat([...parts, ...cds, new Uint8Array(eo.buffer)]);
}
function concat(arrs) { const n = arrs.reduce((a, b) => a + b.length, 0), o = new Uint8Array(n); let p = 0; for (const a of arrs) { o.set(a, p); p += a.length } return o }
/* -> Map name -> Uint8Array (deflate through DecompressionStream; throws on a bad zip) */
async function unzip(u8) {
  const v = new DataView(u8.buffer, u8.byteOffset, u8.byteLength); let at = -1;
  for (let i = u8.length - 22; i >= Math.max(0, u8.length - 65557); i--) if (v.getUint32(i, true) == 0x06054b50) { at = i; break }
  if (at < 0) throw new Error(T('W_NOT_A_ZIP_FILE'));
  const n = v.getUint16(at + 10, true), cdOff = v.getUint32(at + 16, true), out = new Map(); let p = cdOff;
  for (let k = 0; k < n; k++) {
    if (v.getUint32(p, true) != 0x02014b50) throw new Error(T('W_THE_ZIP_DIRECTORY_IS_DAMAGED'));
    const method = v.getUint16(p + 10, true), cs = v.getUint32(p + 20, true), nl = v.getUint16(p + 28, true), xl = v.getUint16(p + 30, true), cl = v.getUint16(p + 32, true), lo = v.getUint32(p + 42, true);
    const name = new TextDecoder().decode(u8.subarray(p + 46, p + 46 + nl));
    const ds = lo + 30 + v.getUint16(lo + 26, true) + v.getUint16(lo + 28, true), raw = u8.subarray(ds, ds + cs);
    if (method == 0) out.set(name, raw);
    else if (method == 8) out.set(name, new Uint8Array(await new Response(new Blob([raw]).stream().pipeThrough(new DecompressionStream('deflate-raw'))).arrayBuffer()));
    p += 46 + nl + xl + cl;
  }
  return out;
}

/* ---------- pack.bin ---------- */
/* root: {text: [ids], image, weight, flags, children: [...]}; breadth first so children are contiguous and after */
function flatten(root) { const order = [root], first = new Map(), q = [root]; while (q.length) { const n = q.shift(); first.set(n, order.length); for (const c of n.children || []) order.push(c); q.push(...(n.children || [])) } return { order, first } }
/* sizes: [[px, lineH]]; glyphs: [{flags, sizes: [{w,h,x,y,adv,fmt,data}]}]; images: [{w,h,fmt,data}] */
function packBin(kind, sizes, glyphs, images, root, ellipsis = NONE) {
  const ns = sizes.length; if (ns < 1 || ns > 4 || glyphs.length > 0xFFF0) throw new Error('pack limits');
  const { order, first } = flatten(root), seq = [], nodes = new DataView(new ArrayBuffer(16 * order.length));
  order.forEach((n, i) => { const nc = (n.children || []).length, o = 16 * i;
    nodes.setUint32(o, seq.length, true); nodes.setUint16(o + 4, n.text.length, true); nodes.setUint16(o + 6, n.image ?? NONE, true);
    nodes.setUint32(o + 8, nc ? first.get(n) : 0, true); nodes.setUint16(o + 12, nc, true); nodes.setUint8(o + 14, n.weight ?? 1); nodes.setUint8(o + 15, n.flags ?? 0);
    seq.push(...n.text) });
  const grec = new DataView(new ArrayBuffer(12 * ns * glyphs.length)), blobs = [];
  for (let s = 0; s < ns; s++) { const parts = []; let off = 0;
    glyphs.forEach((g, i) => { const m = g.sizes[s], o = 12 * (s * glyphs.length + i);
      if (m.data.length != m.w * m.h * bpp(m.fmt)) throw new Error('glyph data size');
      grec.setUint32(o, off, true); grec.setUint8(o + 4, m.w); grec.setUint8(o + 5, m.h); grec.setInt8(o + 6, m.x); grec.setInt8(o + 7, m.y);
      grec.setUint8(o + 8, m.adv); grec.setUint8(o + 9, m.fmt); grec.setUint8(o + 10, g.flags); parts.push(m.data); off += m.data.length });
    blobs.push(concat(parts)) }
  const irec = new DataView(new ArrayBuffer(12 * images.length)), iparts = []; let ioff = 0;
  images.forEach((im, i) => { if (im.data.length != im.w * im.h * bpp(im.fmt)) throw new Error('image data size');
    irec.setUint32(12 * i, ioff, true); irec.setUint16(12 * i + 4, im.w, true); irec.setUint16(12 * i + 6, im.h, true); irec.setUint8(12 * i + 8, im.fmt); iparts.push(im.data); ioff += im.data.length });
  const h = new DataView(new ArrayBuffer(HEADER));
  [0x4A, 0x47, 0x50, 0x4B].forEach((c, i) => h.setUint8(i, c));
  h.setUint16(4, VERSION, true); h.setUint16(6, HEADER, true); h.setUint8(8, kind); h.setUint8(9, ns); h.setUint16(10, ellipsis, true);
  h.setUint32(12, glyphs.length, true); h.setUint32(16, images.length, true); h.setUint32(20, order.length, true); h.setUint32(24, seq.length, true);
  sizes.forEach(([px, lh], s) => { h.setUint16(32 + 8 * s, px, true); h.setUint16(34 + 8 * s, lh, true) });
  const sq = new DataView(new ArrayBuffer(2 * seq.length)); seq.forEach((v, i) => sq.setUint16(2 * i, v, true));
  return { pack: concat([h, grec, irec, nodes, sq].map(x => new Uint8Array(x.buffer))), blobs, img: concat(iparts) };
}
function build(kind, sizes, glyphs, images, root, ellipsis, source) {
  const { pack, blobs, img } = packBin(kind, sizes, glyphs, images, root, ellipsis);
  const e = [{ name: 'pack.bin', data: pack }, ...blobs.map((b, i) => ({ name: `g${i}.bin`, data: b }))];
  if (img.length) e.push({ name: 'img.bin', data: img });
  if (source !== undefined) e.push({ name: 'source.json', data: utf8(JSON.stringify(source, null, 1)) });
  return zipStored(e);
}

/* ---------- rendering (browser) ---------- */
const SIZES = [[24, 30], [40, 50]];      /* px, line height: two sizes, list rows and the big result */
const FONT = '"Noto Sans TC","Noto Sans CJK TC","PingFang TC","Microsoft JhengHei",sans-serif';
const EMOJI = '"Noto Color Emoji","Apple Color Emoji","Segoe UI Emoji"';
const segmenter = typeof Intl != 'undefined' && Intl.Segmenter ? new Intl.Segmenter('zh-Hant', { granularity: 'grapheme' }) : null;
const clusters = s => segmenter ? [...segmenter.segment(s)].map(x => x.segment) : [...s];
const isSpace = c => /^\s+$/u.test(c);
const isWord = c => /^[\p{Script=Latin}\p{Script=Greek}\p{Script=Cyrillic}\p{Nd}'’.,:;!?%&@#$\-_/()+]+$/u.test(c);
const isEmoji = c => /\p{Extended_Pictographic}|\p{Regional_Indicator}/u.test(c);
let canvas = null;
function ctx2d(w, h) { if (!canvas) canvas = document.createElement('canvas'); canvas.width = w; canvas.height = h; return canvas.getContext('2d', { willReadFrequently: true }) }
const rgb565 = (r, g, b) => ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
/* one cluster at px in a line of lineH: cropped bitmap, white text -> A8 alpha; colour (emoji) -> RGB565A8 */
function render(c, px, lineH) {
  if (isSpace(c)) { const x = ctx2d(8, 8); x.font = `${px}px ${FONT}`; return { w: 0, h: 0, x: 0, y: 0, adv: Math.min(255, Math.max(1, Math.round(x.measureText(c == '\t' ? '    ' : c).width))), fmt: FMT_EMPTY, data: new Uint8Array(0) } }
  const emoji = isEmoji(c), pad = Math.floor(px / 2), W = Math.ceil(px * 3), H = lineH + 2 * pad, base = pad + Math.round((lineH - px) / 2 + px * 0.88), pen = px;   /* line top at y = pad */
  const x = ctx2d(W, H); x.clearRect(0, 0, W, H);
  x.font = `${px}px ${emoji ? EMOJI + ',' + FONT : FONT}`; x.textBaseline = 'alphabetic'; x.fillStyle = '#fff';
  x.fillText(c, pen, base);
  const adv = Math.min(255, Math.max(1, Math.round(x.measureText(c).width)));
  const d = x.getImageData(0, 0, W, H).data; let x0 = W, y0 = H, x1 = -1, y1 = -1, colour = false;
  for (let yy = 0; yy < H; yy++) for (let xx = 0; xx < W; xx++) { const i = 4 * (yy * W + xx); if (d[i + 3] > 8) { if (xx < x0) x0 = xx; if (xx > x1) x1 = xx; if (yy < y0) y0 = yy; if (yy > y1) y1 = yy; if (Math.abs(d[i] - d[i + 1]) > 24 || Math.abs(d[i + 1] - d[i + 2]) > 24) colour = true } }
  if (x1 < 0) return { w: 0, h: 0, x: 0, y: 0, adv, fmt: FMT_EMPTY, data: new Uint8Array(0) };
  x0 = Math.max(x0, pen - 128); y0 = Math.max(y0, 0); x1 = Math.min(x1, x0 + 254); y1 = Math.min(y1, y0 + 254);   /* int8 offsets, u8 sizes */
  const w = x1 - x0 + 1, h = y1 - y0 + 1, fmt = colour ? FMT_RGB565A8 : FMT_A8, out = new Uint8Array(w * h * bpp(fmt));
  for (let yy = 0; yy < h; yy++) for (let xx = 0; xx < w; xx++) { const i = 4 * ((yy + y0) * W + xx + x0), k = yy * w + xx;
    if (fmt == FMT_A8) out[k] = d[i + 3];
    else { const v = rgb565(d[i], d[i + 1], d[i + 2]); out[2 * k] = v & 255; out[2 * k + 1] = v >> 8; out[2 * w * h + k] = d[i + 3] } }   /* getImageData: straight alpha */
  return { w, h, x: Math.max(-128, Math.min(127, x0 - pen)), y: Math.max(-128, Math.min(127, y0 - pad)), adv, fmt, data: out };
}
/* an item picture: fit into max px, RGB565A8 */
async function renderImage(dataUrl, max = 96) {
  const im = new Image(); im.src = dataUrl; await im.decode();
  const k = Math.min(1, max / Math.max(im.width, im.height)), w = Math.max(1, Math.round(im.width * k)), h = Math.max(1, Math.round(im.height * k));
  const x = ctx2d(w, h); x.clearRect(0, 0, w, h); x.drawImage(im, 0, 0, w, h);
  const d = x.getImageData(0, 0, w, h).data, out = new Uint8Array(3 * w * h);
  for (let k2 = 0; k2 < w * h; k2++) { const a = d[4 * k2 + 3], v = rgb565(d[4 * k2], d[4 * k2 + 1], d[4 * k2 + 2]); out[2 * k2] = v & 255; out[2 * k2 + 1] = v >> 8; out[2 * w * h + k2] = a }
  return { w, h, fmt: FMT_RGB565A8, data: out };
}
/* model (= source.json): {format:1, kind, title, groups:[{name, items:[{text, weight, image}]}]} -> {zip, stats, glyphs} */
async function compile(model) {
  const ids = new Map(), list = [], images = [];
  const idOf = c => { if (!ids.has(c)) { ids.set(c, list.length); list.push(c) } return ids.get(c) };
  const enc = s => { const out = []; for (const line of String(s || '').split('\n')) { if (out.length) out.push(NL); for (const c of clusters(line)) out.push(idOf(c)) } return out };
  idOf(' '); const ell = idOf('…');
  const node = async (text, it, children) => { const n = { text: enc(text), weight: it?.weight ?? 1, flags: it?.flags ?? 0, children };
    if (it?.image) { n.image = images.length; images.push(await renderImage(it.image)) } return n };
  const groups = [];
  for (const g of model.groups || []) { const items = []; for (const it of g.items || []) items.push(await node(it.text, it, [])); groups.push(await node(g.name, g, items)) }
  const root = await node(model.title, null, groups);
  if (list.length > 0xFFF0) throw new Error(T('W_TOO_MANY_CHARACTERS_AT_MOST_65520_DIFFERENT'));
  const glyphs = list.map(c => ({ flags: (isSpace(c) ? GF_SPACE : 0) | (isWord(c) ? GF_WORD : 0), sizes: SIZES.map(([px, lh]) => render(c, px, lh)) }));
  const zip = build(KINDS[model.kind] ?? 0, SIZES, glyphs, images, root, ell, model);
  return { zip, glyphs, list, stats: { glyphs: list.length, nodes: 1 + groups.length + groups.reduce((a, g) => a + g.children.length, 0), bytes: zip.length } };
}
globalThis.GlyphPack = { NL, NONE, KINDS, FMT_EMPTY, FMT_A8, FMT_RGB565A8, GF_SPACE, GF_WORD, SIZES, bpp, crc32, zipStored, unzip, packBin, build, clusters, render, renderImage, compile };
})();
