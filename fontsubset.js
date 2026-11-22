// SPDX-License-Identifier: LGPL-2.1-or-later
/* hb-subset in the browser: a thin wrapper over the vendored harfbuzzjs wasm (vendor/harfbuzzjs/<HB_VERSION>/harfbuzz-subset.wasm: no imports, raw hb_* exports).
   Works in a page, a Worker (importScripts) and node (tools/web/fontbuild_test.mjs).
     const S = await FontSubset.load(wasmBytes)
     S.subset(fontBytes, [codepoints]) -> Uint8Array   keeps those code points (and what layout closure needs), all layout features, notdef outline, variable fonts pinned to their default
     FontSubset.cmap(fontBytes)        -> Set of code points the font maps (cmap formats 4 and 12; no wasm needed)
     FontSubset.flavor(fontBytes)      -> 'ttf' (glyf outlines) | 'cff' | 'unknown'   (the board's FreeType reads TrueType outlines only: a CFF source is refused by the builder)
   Same choices as tools/gen/make_font_subset.py write_subset: layout_features '*', notdef_outline. */
const FontSubset = (() => {
  const HB_VERSION = '1.6.4';
  const NOTDEF_OUTLINE = 0x40, LAYOUT_FEATURE_TAG = 6;   /* HB_SUBSET_FLAGS_NOTDEF_OUTLINE, HB_SUBSET_SETS_LAYOUT_FEATURE_TAG */
  const u32 = (v, o) => v.getUint32(o), u16 = (v, o) => v.getUint16(o);
  function tables(b) {
    const v = new DataView(b.buffer, b.byteOffset, b.byteLength), n = u16(v, 4), t = {};
    for (let i = 0; i < n; i++) { const o = 12 + 16 * i; t[String.fromCharCode(b[o], b[o + 1], b[o + 2], b[o + 3])] = { off: u32(v, o + 8), len: u32(v, o + 12) } }
    return t;
  }
  function flavor(b) {
    if (b.length < 12) return 'unknown';
    const tag = String.fromCharCode(b[0], b[1], b[2], b[3]);
    if (tag === 'OTTO') return 'cff';
    if (tag === '\0\x01\0\0' || tag === 'true') return 'ttf';
    return 'unknown';
  }
  function cmap(b) {
    const t = tables(b), v = new DataView(b.buffer, b.byteOffset, b.byteLength), out = new Set();
    if (!t.cmap) return out;
    const base = t.cmap.off, n = u16(v, base + 2); let best = null;
    for (let i = 0; i < n; i++) {   /* prefer a full-repertoire (format 12) Unicode subtable, else format 4 */
      const pid = u16(v, base + 4 + 8 * i), eid = u16(v, base + 6 + 8 * i), off = base + u32(v, base + 8 + 8 * i), fmt = u16(v, off);
      if (!((pid === 3 && (eid === 1 || eid === 10)) || pid === 0)) continue;
      if (!best || fmt === 12 && best.fmt !== 12) best = { off, fmt };
    }
    if (!best) return out;
    const o = best.off;
    if (best.fmt === 12) {
      const g = u32(v, o + 12);
      for (let i = 0; i < g; i++) { const p = o + 16 + 12 * i, s = u32(v, p), e = u32(v, p + 4), gid = u32(v, p + 8); for (let c = s; c <= e; c++) if (gid + (c - s) !== 0) out.add(c) }
    } else if (best.fmt === 4) {
      const sc = u16(v, o + 6) / 2, endp = o + 14, startp = endp + 2 * sc + 2, deltap = startp + 2 * sc, rangep = deltap + 2 * sc;
      for (let i = 0; i < sc; i++) {
        const e = u16(v, endp + 2 * i), s = u16(v, startp + 2 * i), d = u16(v, deltap + 2 * i), r = u16(v, rangep + 2 * i);
        for (let c = s; c <= e && c !== 0xffff; c++) {
          let gid; if (r === 0) gid = (c + d) & 0xffff; else { const gp = rangep + 2 * i + r + 2 * (c - s); gid = u16(v, gp); if (gid) gid = (gid + d) & 0xffff }
          if (gid) out.add(c);
        }
      }
    }
    return out;
  }
  async function load(wasmBytes) {
    const { instance } = await WebAssembly.instantiate(wasmBytes, {});
    const x = instance.exports;
    const heap = () => new Uint8Array(x.memory.buffer);
    function subset(font, cps) {
      const fp = x.malloc(font.length); if (!fp) throw new Error('hb: out of memory');
      heap().set(font, fp);
      const blob = x.hb_blob_create(fp, font.length, 2 /* HB_MEMORY_MODE_WRITABLE */, 0, 0), face = x.hb_face_create(blob, 0), input = x.hb_subset_input_create_or_fail();
      if (!input) throw new Error('hb: subset input');
      try {
        const us = x.hb_subset_input_unicode_set(input); for (const c of cps) x.hb_set_add(us, c);
        x.hb_subset_input_set_flags(input, x.hb_subset_input_get_flags(input) | NOTDEF_OUTLINE);
        const feat = x.hb_subset_input_set(input, LAYOUT_FEATURE_TAG); x.hb_set_clear(feat); x.hb_set_invert(feat);   /* every layout feature, like layout_features=['*'] */
        if ('fvar' in tables(font)) x.hb_subset_input_pin_all_axes_to_default(input, face);
        const out = x.hb_subset_or_fail(face, input); if (!out) throw new Error('hb: subset failed');
        const ob = x.hb_face_reference_blob(out), len = x.hb_blob_get_length(ob), dp = x.hb_blob_get_data(ob, 0);
        const data = heap().slice(dp, dp + len);
        x.hb_blob_destroy(ob); x.hb_face_destroy(out); return data;
      } finally { x.hb_subset_input_destroy(input); x.hb_face_destroy(face); x.hb_blob_destroy(blob); x.free(fp) }
    }
    return { subset };
  }
  return { HB_VERSION, load, cmap, flavor, tables };
})();
if (typeof module != 'undefined') module.exports = FontSubset;
if (typeof globalThis != 'undefined') globalThis.FontSubset = FontSubset;
