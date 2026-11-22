// SPDX-License-Identifier: LGPL-2.1-or-later
/* 清單 tab: edit glyph-pack lists (午餐 / 抽人 / 點子), render them here, upload over Web Serial or download the zip.
   The device never sees text: glyphpack.js renders every glyph; the device only blits (spec 2026-11-11-glyphpack-design.md).
   Uses the page's $, el, toast, dev, Conn (index.html). initLists() is called once by the page. */
const LIST_KINDS = {
  lunch: { get label() { return T('W_LUNCH_SHOP_DISH') }, get group() { return T('W_SHOP') }, get item() { return T('W_DISHES_ONE_PER_LINE') }, file: 'lunch' },
  people: { get label() { return T('W_PICK_A_PERSON_GROUP_NAME') }, get group() { return T('W_GROUP_NAME') }, get item() { return T('W_NAMES_ONE_PER_LINE') }, file: 'people' },
  columns: { get label() { return T('W_IDEAS_THREE_COLUMNS_A_B_C') }, get group() { return T('W_COLUMN_NAME') }, get item() { return T('W_PHRASES_ONE_PER_LINE') }, file: 'ideas' },
};
const LIST_CHUNK = 2048;
let lm = null, lres = null, ldirty = true;   /* model (= source.json), last compile */

/* items as text lines; "雞腿便當 ×2" (or *2 / x2) = weight 2, "×0" = left out of picks */
const itemsToText = items => items.map(i => i.text + ((i.weight ?? 1) != 1 ? ` ×${i.weight}` : '')).join('\n');
function textToItems(t, old = []) {
  return t.split('\n').map(s => s.trim()).filter(Boolean).map(s => {
    const m = s.match(/^(.*?)\s*[×x*]\s*(\d{1,3})$/), text = m ? m[1] : s, weight = m ? Math.min(255, +m[2]) : 1;
    const prev = old.find(o => o.text == text);
    return { text, ...(weight != 1 ? { weight } : {}), ...(prev?.image ? { image: prev.image } : {}) };
  });
}

function initLists() {
  $('#lkind').replaceChildren(...Object.entries(LIST_KINDS).map(([k, v]) => el('option', { value: k }, v.label)));
  $('#lnew').onclick = () => listNew($('#lkind').value, false);
  $('#ltpl').onclick = () => listNew($('#lkind').value, true);
  $('#lopen').onchange = async e => { const f = e.target.files[0]; e.target.value = ''; if (f) try { await listOpenZip(new Uint8Array(await f.arrayBuffer()), f.name) } catch (x) { toast(T('W_COULD_NOT_OPEN') + x.message, 1) } };
  $('#lprev').onclick = () => listPreview().catch(x => toast(x.message, 1));
  $('#ldl').onclick = () => listDownload().catch(x => toast(x.message, 1));
  $('#lup').onclick = () => listUpload().catch(x => toast(T('W_UPLOAD_FAILED') + x.message, 1));
  $('#lrefresh').onclick = () => loadDeviceLists();
  $('#ltitle').oninput = e => { if (lm) { lm.title = e.target.value; ldirty = true } };
  $('#lname').oninput = () => { ldirty = true };
  listDrawEditor();
}

async function listNew(kind, template) {
  if (lm && ldirty && lm.groups.some(g => g.items.length) && !confirm(T('W_THE_CURRENT_LIST_IS_NOT_SAVED_REPLACE_IT'))) return;
  if (template) { const r = await fetch(`lists/${LIST_KINDS[kind].file}.json`); if (!r.ok) return toast(T('W_THE_SAMPLE_COULD_NOT_BE_READ'), 1); lm = await r.json() }
  else lm = { format: 1, kind, title: '', groups: kind == 'columns' ? ['A', 'B', 'C'].map(n => ({ name: n, items: [] })) : [{ name: '', items: [] }] };
  $('#lname').value = LIST_KINDS[kind].file + '.zip';
  ldirty = true; lres = null; listDrawEditor();
}

async function listOpenZip(u8, name) {
  const files = await GlyphPack.unzip(u8), src = files.get('source.json');
  if (!src) throw new Error(T('W_THERE_IS_NO_SOURCE_JSON_IN_THIS_ZIP_NOT_A_LI'));
  const m = JSON.parse(new TextDecoder().decode(src));
  if (!LIST_KINDS[m.kind] || !Array.isArray(m.groups)) throw new Error(T('W_THE_SOURCE_JSON_FORMAT_IS_WRONG'));
  lm = m; $('#lname').value = name.replace(/^.*[\\/]/, ''); ldirty = false; lres = null; listDrawEditor(); toast(T('W_OPENED') + $('#lname').value);
}

function listDrawEditor() {
  const E = $('#led');
  E.hidden = !lm; if (!lm) return;
  const K = LIST_KINDS[lm.kind];
  $('#lkindnow').textContent = K.label; $('#ltitle').value = lm.title || '';
  const box = $('#lgroups'); box.replaceChildren();
  lm.groups.forEach((g, gi) => {
    const items = el('textarea', { value: itemsToText(g.items), placeholder: K.item, spellcheck: false, style: 'min-height:120px;font:15px/1.5 system-ui,"Noto Sans TC",sans-serif;margin-top:6px', on: { input: e => { g.items = textToItems(e.target.value, g.items); ldirty = true; count.textContent = T('W_0_ITEMS',g.items.length) } } });
    const count = el('span', { className: 'dim small' }, T('W_0_ITEMS',g.items.length));
    const head = el('div', { className: 'row' }, el('input', { value: g.name, placeholder: K.group, className: 'grow', on: { input: e => { g.name = e.target.value; ldirty = true } } }), count);
    if (lm.kind != 'columns') head.append(
      el('button', { className: 'sm', disabled: gi == 0, title: T('W_MOVE_UP_2'), on: { click: () => { [lm.groups[gi - 1], lm.groups[gi]] = [lm.groups[gi], lm.groups[gi - 1]]; ldirty = true; listDrawEditor() } } }, '↑'),
      el('button', { className: 'sm', title: T('W_DELETE_THIS_GROUP'), on: { click: () => { if (lm.groups.length > 1 && confirm(T('W_DELETE_0',g.name || T('W_THIS_GROUP')))) { lm.groups.splice(gi, 1); ldirty = true; listDrawEditor() } } } }, T('W_DELETE')));
    box.append(el('div', { className: 'fx' }, head, items));
  });
  $('#laddg').hidden = lm.kind == 'columns';
  $('#laddg').onclick = () => { lm.groups.push({ name: '', items: [] }); ldirty = true; listDrawEditor() };
}

function listCheck() {
  if (!lm) throw new Error(T('W_CREATE_OR_OPEN_A_LIST_FIRST'));
  const name = $('#lname').value.trim();
  if (!/\.zip$/.test(name) || /[\/\\:*?"<>|]|\.\.|^[.\s]|\s\.zip$/.test(name) || new TextEncoder().encode(name).length > 60) throw new Error(T('W_FILE_NAME_MUST_END_IN_ZIP_NO_OR_AT_MOST_60_B'));
  if (!lm.groups.some(g => g.items.length)) throw new Error(T('W_THE_LIST_IS_EMPTY'));
  return name;
}
async function listBuild() {
  listCheck();
  if (!lres || ldirty) { $('#lstat').textContent = T('W_DRAWING_THE_GLYPHS'); lres = await GlyphPack.compile(lm); ldirty = false }
  const s = lres.stats; $('#lstat').textContent = T('W_0_GLYPHS_1_ITEMS_ZIP_2_KB',s.glyphs,s.nodes,(s.bytes / 1024).toFixed(1)) + (s.bytes > 1 << 20 ? T('W_LARGE_DOWNLOAD_IT_AND_COPY_IT_INTO_THE_LISTS') : '');
  return lres;
}

/* a round 360 px preview: the title at the big size, then the groups at the small one, as the device draws them */
async function listPreview() {
  const r = await listBuild(), c = $('#lcanvas'), x = c.getContext('2d');
  x.fillStyle = '#0f1216'; x.fillRect(0, 0, 360, 360); x.save(); x.beginPath(); x.arc(180, 180, 180, 0, 7); x.clip(); x.fillStyle = '#181d24'; x.fillRect(0, 0, 360, 360);
  const idx = new Map(r.list.map((ch, i) => [ch, i]));
  const ids = s => GlyphPack.clusters(String(s || '').replace(/\n/g, ' ')).map(ch => idx.get(ch));
  let y = 70; y = listLine(x, r, 1, ids(lm.title), y, 260, '#e8ecf1');
  y += 10;
  for (const g of lm.groups) { if (y > 290) break; y = listLine(x, r, 0, ids(g.name + '  ' + g.items.map(i => i.text).join('  '))   /* only glyphs that are in the pack */, y, 280, '#4fc3f7') }
  x.restore();
}
function listLine(x, r, size, ids, y, maxW, colour) {   /* one line, cut with … (the device wraps; this is a peek) */
  const [, lh] = GlyphPack.SIZES[size], G = r.glyphs, ell = r.list.indexOf('…');
  let w = 0, n = 0; const adv = id => G[id]?.sizes[size].adv || 0;
  while (n < ids.length && w + adv(ids[n]) <= maxW) w += adv(ids[n++]);
  const cut = n < ids.length; if (cut) while (n && w + adv(ell) > maxW) w -= adv(ids[--n]);
  const run = cut ? [...ids.slice(0, n), ell] : ids; if (cut) w += adv(ell);
  let pen = 180 - w / 2;
  for (const id of run) { const m = G[id]?.sizes[size]; if (!m) continue; if (m.w) listBlit(x, m, pen + m.x, y + m.y, colour); pen += m.adv }
  return y + lh;
}
function listBlit(x, m, px, py, colour) {
  const im = x.createImageData(m.w, m.h), [cr, cg, cb] = [1, 3, 5].map(i => parseInt(colour.slice(i, i + 2), 16));
  for (let k = 0; k < m.w * m.h; k++) {
    if (m.fmt == GlyphPack.FMT_A8) { im.data.set([cr, cg, cb, m.data[k]], 4 * k) }
    else { const v = m.data[2 * k] | m.data[2 * k + 1] << 8; im.data.set([(v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3, m.data[2 * m.w * m.h + k]], 4 * k) }
  }
  const t = document.createElement('canvas'); t.width = m.w; t.height = m.h; t.getContext('2d').putImageData(im, 0, 0); x.drawImage(t, Math.round(px), Math.round(py));
}

async function listDownload() {
  const r = await listBuild(), name = listCheck();
  const a = el('a', { href: URL.createObjectURL(new Blob([r.zip], { type: 'application/zip' })), download: name }); a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 5000);
  toast(T('W_DOWNLOADED_PUT_IT_IN_THE_LISTS_FOLDER_OF_THE'));
}

const listB64 = u8 => { let s = ''; for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode(...u8.subarray(i, i + 0x8000)); return btoa(s) };
async function listUpload() {
  if (!Conn.up) throw new Error(T('W_CONNECT_TO_THE_PLAYER_FIRST'));
  const r = await listBuild(), name = listCheck(), z = r.zip, bar = $('#lstat');
  await dev('list_put_begin', { name, size: z.length });
  try {
    /* a lost reply: ask how much the device has and go on from there (a chunk it already has is acknowledged
       without being written again) */
    for (let off = 0, fails = 0; off < z.length;) {
      try {
        off = (await dev('list_put', { off, data: listB64(z.subarray(off, off + LIST_CHUNK)) }, 4000)).got; fails = 0;
      } catch (x) {
        if (++fails > 5) throw x;
        off = await dev('list_put_stat', {}, 4000).then(r => r.got, () => off);
      }
      bar.textContent = T('W_UPLOADING_0',Math.min(100, Math.round(100 * off / z.length)));
    }
    const e = await dev('list_put_end', { crc: GlyphPack.crc32(z) }, 20000);
    bar.textContent = T('W_SAVED_ON_THE_PLAYER_0_1_GLYPHS_2_ITEMS',name,e.glyphs,e.nodes); toast(T('W_SENT_TO_THE_PLAYER'));
  } catch (x) { dev('list_put_abort').catch(() => {}); throw x }
  loadDeviceLists();
}

async function loadDeviceLists() {
  const L = $('#llist');
  if (!Conn.up) { L.textContent = T('W_THE_LISTS_IN_LISTS_ON_THE_PLAYER_S_TF_CARD_S'); return }
  try {
    const r = await dev('list_ls'); L.replaceChildren();
    if (!r.lists.length) L.textContent = T('W_THE_PLAYER_HAS_NO_LISTS_YET');
    for (const f of r.lists) L.append(el('div', { className: 'st' }, el('div', { className: 't' }, el('div', { className: 'name ell' }, f.name), el('div', { className: 'dim small' }, `${(f.size / 1024).toFixed(1)} KB`)),
      el('button', { className: 'sm', on: { click: () => listFetch(f).catch(x => toast(x.message, 1)) } }, T('W_EDIT')),
      el('button', { className: 'sm', on: { click: async () => { if (confirm(T('W_DELETE_0_FROM_THE_PLAYER',f.name))) { await dev('list_rm', { name: f.name }).catch(x => toast(x.message, 1)); loadDeviceLists() } } } }, T('W_DELETE'))));
  } catch (x) { L.textContent = T('W_COULD_NOT_READ') + x.message }
}
async function listFetch(f) {
  if (lm && ldirty && !confirm(T('W_THE_CURRENT_LIST_IS_NOT_SAVED_REPLACE_IT'))) return;
  const out = new Uint8Array(f.size);
  for (let off = 0; off < f.size; off += LIST_CHUNK) {
    const r = await dev('list_get', { name: f.name, off, len: Math.min(LIST_CHUNK, f.size - off) });
    out.set(Uint8Array.from(atob(r.data), c => c.charCodeAt(0)), off); $('#lstat').textContent = T('W_READING_0',Math.round(100 * off / f.size));
  }
  $('#lstat').textContent = '';
  await listOpenZip(out, f.name);
}
