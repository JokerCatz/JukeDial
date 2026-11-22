// SPDX-License-Identifier: LGPL-2.1-or-later
/* Web Worker for the font builder: the wasm subsetter off the page's thread. in: { wasm: ArrayBuffer (first message only: the page fetches it, so a failed fetch is the page's error), tc, sc: ArrayBuffer, cps: Int32Array }  out: { ok, tc, sc, missing, counts } or { ok: false, code, extra, error } */
importScripts('fontsubset.js', 'fontbuild.js');
let S = null;
onmessage = async e => {
  try {
    if (!S) S = await FontSubset.load(e.data.wasm);
    const r = FontBuild.build(S, { tc: new Uint8Array(e.data.tc), sc: new Uint8Array(e.data.sc) }, Array.from(e.data.cps));
    postMessage({ ok: true, tc: r.tc, sc: r.sc, missing: r.missing, counts: r.counts }, [r.tc.buffer, r.sc.buffer]);
  } catch (x) { postMessage({ ok: false, code: x.code || '', extra: x.extra || '', error: x.message }) }
};
