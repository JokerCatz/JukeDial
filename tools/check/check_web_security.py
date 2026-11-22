#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Static security checks of the settings page (spec 2026-11-15-web-tool.md section 9), run by tools/web/check.sh:
 - index.html carries a Content-Security-Policy meta with the required directives, no 'unsafe-eval', no 'unsafe-inline' for scripts, no wildcard host except the radio-browser API;
 - no inline <script>, no inline event handler attribute (onclick= ...), no javascript: URL, every <script src> and <link href> is a relative file of this site;
 - no eval / new Function / document.write / insertAdjacentHTML / setTimeout("string") in the page's own scripts; innerHTML only in lang.js (our own string table);
 - the page does not register Web Serial connect/disconnect handlers (no automatic port actions) and filters ports by the Espressif vendor id;
 - every vendored library folder has LICENSE, NOTICE or VENDOR.md and a SHA256SUMS that matches (tools/check/check_web_flasher.py compares the hashes);
 - every URL the page fetches or shows is one of: same site, the radio-browser API, a station's own address (preview / probe).
Standard library only."""
import os, re, sys
WEB = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'web')
bad = []
html = open(os.path.join(WEB, 'index.html'), encoding='utf-8').read()
m = re.search(r'<meta http-equiv="Content-Security-Policy" content="([^"]*)"', html)
if not m: bad.append('no CSP meta in index.html')
else:
    d = {x.split()[0]: x.split()[1:] for x in (p.strip() for p in m.group(1).split(';')) if x}
    for k, want in {'default-src': ["'none'"], 'base-uri': ["'none'"], 'form-action': ["'none'"], 'worker-src': ["'self'"]}.items():
        if d.get(k) != want: bad.append(f'CSP {k} must be {want}, is {d.get(k)}')
    if d.get('script-src') != ["'self'", "'wasm-unsafe-eval'"]: bad.append(f"CSP script-src must be 'self' 'wasm-unsafe-eval', is {d.get('script-src')}")
    if any("unsafe-eval'" in v and 'wasm' not in v for vs in d.values() for v in vs): bad.append("CSP has 'unsafe-eval'")
    for k, vs in d.items():
        for v in vs:
            if '*' in v and v != 'https://*.api.radio-browser.info': bad.append(f'CSP {k}: wildcard {v}')
            if v.startswith('http') and v not in ('https://*.api.radio-browser.info',) and v not in ('http:', 'https:'): bad.append(f'CSP {k}: unexpected host {v}')
    if "'unsafe-inline'" in d.get('script-src', []) or "'unsafe-inline'" in d.get('default-src', []): bad.append("CSP allows inline scripts")
if re.search(r'<script(?![^>]*\bsrc=)[^>]*>', html): bad.append('an inline <script> in index.html')
if re.search(r'\son[a-z]+\s*=', re.sub(r'data-[\w-]+="[^"]*"', '', html)): bad.append('an inline event handler attribute in index.html')
if re.search(r'javascript:', html, re.I): bad.append('a javascript: URL')
for u in re.findall(r'<(?:script|link)[^>]*\b(?:src|href)="([^"]+)"', html):
    if re.match(r'[a-z]+:|//', u): bad.append(f'a non-local script/link: {u}')
own = [f for f in sorted(os.listdir(WEB)) if f.endswith('.js') and f not in ('i18n.js', 'fontsets.js', 'regions.js', 'pagemock.js')]   # the pretend device is test-only: boot.js loads it on ?mockserial, web_publish.py does not ship it
for f in own:
    s = open(os.path.join(WEB, f), encoding='utf-8').read()
    code = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    code = re.sub(r'(?m)^\s*//.*$', '', code)
    for pat, why in ((r'\beval\s*\(', 'eval'), (r'new\s+Function\b', 'new Function'), (r'document\.write', 'document.write'), (r'insertAdjacentHTML', 'insertAdjacentHTML'), (r'set(?:Timeout|Interval)\s*\(\s*[\'"`]', 'setTimeout(string)'), (r'\bimportScripts\s*\(\s*[^\'"\s)]', 'importScripts of a computed URL')):
        if re.search(pat, code) and f != 'check': bad.append(f'{f}: {why}')
    if '.innerHTML' in code and f != 'lang.js': bad.append(f'{f}: innerHTML (only lang.js, our own string table, may)')
    if re.search(r"serial\.(addEventListener|onconnect|ondisconnect)", code) or re.search(r"getPorts\s*\(", code): bad.append(f'{f}: automatic Web Serial port handling (getPorts / connect events)')
    for u in re.findall(r"""['"`](https?://[^'"`\s]+)""", code):
        if not re.match(r'https?://(\*\.|[\w-]+\.)?(api\.radio-browser\.info|localhost|127\.0\.0\.1)\b|https?://\.\.\.|http://…|https://github\.com/[\w-]+/JukeDial/releases$', u) and f not in ('pagemock.js',): bad.append(f'{f}: URL literal {u[:50]}')
conn = open(os.path.join(WEB, 'conn.js'), encoding='utf-8').read()
if '0x303a' not in conn: bad.append('conn.js: no Espressif vendor id filter')
vend = os.path.join(WEB, 'vendor')
for lib in sorted(os.listdir(vend)):
    for v in sorted(os.listdir(os.path.join(vend, lib))):
        d = os.path.join(vend, lib, v)
        for need in ('LICENSE', 'NOTICE', 'VENDOR.md', 'SHA256SUMS'):
            if not os.path.isfile(os.path.join(d, need)): bad.append(f'vendor/{lib}/{v}: no {need}')
print('check_web_security:', 'FAIL ' + '; '.join(bad) if bad else 'ok')
sys.exit(1 if bad else 0)
