// SPDX-License-Identifier: LGPL-2.1-or-later
/* The page's language. T(id, ...args) looks id up in WEB_STRINGS (i18n.js, generated from tools/gen/i18n/web_strings.tsv) and fills {0} {1}.
   0 = 繁中, 1 = 简中, 2 = English: the numbers of the device's language setting (status.lang).
   Which language: the viewer's own choice (the menu; kept in localStorage, or ?lang=tw|cn|en for one visit) beats the device's (status.lang, once connected),
   which beats the browser's (navigator.language).  Static markup carries data-i (text), data-ih (text with <b>/<code>), data-i-placeholder / data-i-title;
   Lang.apply() fills them and calls the page's redraw hooks (Lang.onChange). */
/* cjk-allow-begin: the language names are written in their own language */
const LANG_NAMES = ['繁體中文', '简体中文', 'English'];
/* cjk-allow-end */
const Lang = (() => {
  const CODES = ['tw', 'cn', 'en'], HTML = ['zh-Hant', 'zh-Hans', 'en'];
  let cur = 0, picked = false; const subs = [];
  const store = (k, v) => { try { if (v === undefined) return localStorage.getItem(k); localStorage.setItem(k, v) } catch { return null } };
  const fromBrowser = () => /^zh-(CN|SG)|^zh-Hans/i.test(navigator.language || '') ? 1 : /^en/i.test(navigator.language || '') ? 2 : /^zh/i.test(navigator.language || '') ? 0 : 2;
  function init() {
    const q = new URLSearchParams(location.search).get('lang'), s = store('lang');
    const i = CODES.indexOf(q || s);
    if (i >= 0) { cur = i; picked = !!q || s != null; if (q) picked = true } else cur = fromBrowser();
  }
  function apply() {
    document.documentElement.lang = HTML[cur]; document.title = T('W_JC_PLAYER_SETTINGS');
    for (const e of document.querySelectorAll('[data-i]')) e.textContent = T(e.dataset.i);
    for (const e of document.querySelectorAll('[data-ih]')) e.innerHTML = T(e.dataset.ih);   /* our own table's markup, never user text */
    for (const e of document.querySelectorAll('[data-i-placeholder]')) e.placeholder = T(e.dataset.iPlaceholder);
    for (const e of document.querySelectorAll('[data-i-title]')) e.title = T(e.dataset.iTitle);
    const sel = document.getElementById('langsel'); if (sel) sel.value = cur;
  }
  function set(i, fromUser) {
    if (i === cur && !fromUser) return;
    cur = i; if (fromUser) { picked = true; store('lang', CODES[i]) }
    apply(); for (const f of subs) try { f(cur) } catch (e) { console.error(e) }
  }
  init();
  return {
    get cur() { return cur }, get picked() { return picked }, codes: CODES,
    set: i => set(i, true),   /* the viewer's choice */
    fromDevice(n) { if (!picked && (n === 0 || n === 1 || n === 2)) set(n, false) },
    onChange(f) { subs.push(f) }, apply,
  };
})();
function T(id, ...a) {
  const r = WEB_STRINGS[id]; if (!r) { console.warn('no text for', id); return id }
  return (r[Lang.cur] || r[0]).replace(/\{(\d+)\}/g, (m, i) => a[+i] ?? '');
}
if (typeof module != 'undefined') module.exports = { T, Lang };
