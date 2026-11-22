#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Host test of tools/web_publish.py (the page-only dist; the firmware is tools/release.py, check_release.py): dist layout (page, vendored libraries, fonts / Noto downloads + the OFL text), NO firmware
folder and no manifest, no test files shipped, deterministic (twice = the same SHA256SUMS), private-looking inputs refused.  Needs the generated test fonts (tools/web/testdata/make_testfont.py)."""
import hashlib, os, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); TOOLS = os.path.dirname(HERE); WEB = os.path.join(TOOLS, 'web'); FX = os.path.join(WEB, 'testdata')
PUB = os.path.join(TOOLS, 'web_publish.py')
t = tempfile.mkdtemp(prefix='wp'); bad = []
def run(*a): return subprocess.run([sys.executable, PUB, *a], capture_output=True, text=True)
try:
    fonts = os.path.join(t, 'f'); os.makedirs(fonts)
    for k in ('tc', 'sc', 'ext'): shutil.copy(os.path.join(FX, 'fonts', k + '.ttf'), os.path.join(fonts, f'ui-{k}.ttf'))
    outs = []
    for i in (1, 2):
        o = os.path.join(t, f'd{i}'); r = run('--out', o, '--fonts', fonts)
        if r.returncode: bad.append('publish failed: ' + r.stderr); break
        outs.append(o)
    if len(outs) == 2:
        if open(os.path.join(outs[0], 'SHA256SUMS')).read() != open(os.path.join(outs[1], 'SHA256SUMS')).read(): bad.append('not deterministic')
        o = outs[0]
        for f in ('index.html', 'flashtab.js', 'flasher.js', 'board_round.svg', 'board_fakepod.svg', 'vendor/esptool-js/0.7.1/bundle.js', 'vendor/harfbuzzjs/1.6.4/harfbuzz-subset.wasm', 'fonts/ui-tc.ttf', 'fontsets.js', 'page.js', 'boot.js'):
            if not os.path.isfile(os.path.join(o, f)): bad.append('missing ' + f)
        if os.path.exists(os.path.join(o, 'firmware')): bad.append('the page hosts firmware: dist/firmware exists')
        for dp, dn, fn in os.walk(o):
            for f in fn:
                if f.endswith('.bin') or f == 'manifest.json': bad.append('firmware-like file in dist: ' + f)
        if open(os.path.join(o, 'fonts', 'OFL.txt'), 'rb').read() != open(os.path.join(TOOLS, 'OFL-1.1.txt'), 'rb').read(): bad.append('fonts/OFL.txt is not the OFL text')
        for f in ('testdata', 'check.sh', 'conn_test.mjs', 'flasher_test.mjs', 'pagemock.js'):
            if os.path.exists(os.path.join(o, f)): bad.append('test file shipped: ' + f)
        for ln in open(os.path.join(o, 'SHA256SUMS')):
            h, n = ln.split(None, 1)
            if hashlib.sha256(open(os.path.join(o, n.strip()), 'rb').read()).hexdigest() != h: bad.append('SHA256SUMS ' + n)
    fs = os.path.join(t, 'fs'); os.makedirs(fs); shutil.copy(os.path.join(FX, 'fonts', 'tc.ttf'), os.path.join(fs, 'NotoSansTC-Regular.ttf')); open(os.path.join(fs, 'OFL.txt'), 'w').write('OFL'); open(os.path.join(fs, 'notes.txt'), 'w').write('x')
    r = run('--out', os.path.join(t, 'd6'), '--fontsrc', fs); nd = os.path.join(t, 'd6', 'noto')
    if r.returncode or sorted(os.listdir(nd)) != ['NotoSansTC-Regular.ttf', 'OFL.txt']: bad.append('--fontsrc ttf: ' + r.stderr + str(os.listdir(nd) if os.path.isdir(nd) else ''))
    open(os.path.join(fs, 'X-Regular.otf'), 'wb').write(b'OTTO')
    if run('--out', os.path.join(t, 'd7'), '--fontsrc', fs).returncode == 0: bad.append('a bad .otf was accepted')
    priv = os.path.join(t, '.local', 'x'); os.makedirs(priv)
    for k in ('tc', 'sc', 'ext'): shutil.copy(os.path.join(FX, 'fonts', k + '.ttf'), os.path.join(priv, f'ui-{k}.ttf'))
    if run('--out', os.path.join(t, 'd3'), '--fonts', priv).returncode == 0: bad.append('a .local input was accepted')
    noto = os.path.join(t, 'noto'); os.makedirs(noto); open(os.path.join(noto, 'wifi.txt'), 'w').write('x')
    if run('--out', os.path.join(t, 'd4'), '--noto', noto).returncode == 0: bad.append('a wifi file was accepted')
finally: shutil.rmtree(t, ignore_errors=True)
print('check_web_publish:', 'FAIL ' + '; '.join(bad) if bad else 'ok'); sys.exit(1 if bad else 0)
