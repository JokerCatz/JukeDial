#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The flasher page (tools/web/flasher.js) hard-codes the partition slots it checks image sizes against; they must be app/partitions.csv's.
Also: the vendored esptool-js and harfbuzzjs files match their SHA256SUMS and the version in flasher.js is the vendored folder.  Standard library only."""
import hashlib, os, re, sys
R = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
js = open(os.path.join(R, 'tools/web/flasher.js'), encoding='utf-8').read()
bad = 0
csv = {}
for ln in open(os.path.join(R, 'app/partitions.csv'), encoding='utf-8'):
    f = [x.strip() for x in ln.split('#')[0].split(',')]
    if len(f) >= 5 and f[0]:
        sz = f[4]; n = int(sz[:-1]) << 20 if sz.endswith('M') else int(sz, 0)
        csv[f[0]] = (int(f[3], 0), n)
want = {'app': csv['factory'], 'fonts': csv['fonts']}
for name, (off, size) in want.items():
    m = re.search(r"\b%s: \{ off: (0x[0-9a-f]+), size: ([^}]+?) \}" % re.escape(name), js)
    if not m: print(f'check_web_flasher: no slot {name} in flasher.js'); bad = 1; continue
    got = (int(m.group(1), 16), eval(m.group(2), {}, {}))
    if got != (off, size): print(f'check_web_flasher: slot {name}: flasher.js has {got}, partitions.csv has {(off, size)}'); bad = 1

# every region the page writes: flasher.js SLOTS (bootloader, partition table, app, fonts) = release.py SLOT = the build's flasher_args.json (when a build exists)
slots = {m.group(1).strip("'"): int(m.group(2), 16) for m in re.finditer(r"('?[\w-]+'?): \{ off: (0x[0-9a-f]+)", js)}
rel = open(os.path.join(R, 'tools/release.py'), encoding='utf-8').read()
slot2 = {m.group(1): int(m.group(2), 16) for m in re.finditer(r"'([\w-]+)': (0x[0-9a-f]+)", rel[rel.index('SLOT = {'):rel.index('SLOT = {') + 200])}
for k in ('bootloader', 'partition-table', 'app', 'fonts'):
    if slots.get(k) is None or slots[k] != slot2.get(k): print(f'check_web_flasher: part {k}: flasher.js {slots.get(k)} vs release.py {slot2.get(k)}'); bad = 1
for b in ('build', 'build-fakepod'):
    fa = os.path.join(R, 'app', b, 'flasher_args.json')
    if os.path.isfile(fa):
        import json
        ff = {int(k, 16) for k in json.load(open(fa))['flash_files']}
        if ff != {slots['bootloader'], slots['partition-table'], slots['app']}: print(f'check_web_flasher: {b} flashes at {sorted(map(hex, ff))}, flasher.js writes bootloader/table/app at {[hex(slots[k]) for k in ("bootloader", "partition-table", "app")]}'); bad = 1
# the board marker the page searches for is the one the firmware carries, and the ids are the BSPs' BSP_BOARD_ID
g = open(os.path.join(R, 'app/main/guard.c'), encoding='utf-8').read()
if '"JUKEDIAL_BOARD=" BSP_BOARD_ID ";"' not in g or 'retain' not in g: print('check_web_flasher: app/main/guard.c lost the JUKEDIAL_BOARD marker'); bad = 1
if 'JUKEDIAL_BOARD=([a-z0-9]+);' not in js: print('check_web_flasher: flasher.js does not search for the JUKEDIAL_BOARD marker'); bad = 1
for comp, bid in (('jc3636f118_bsp', 'jc3636f118'), ('fakepod_bsp', 'fakepod')):
    if f'#define BSP_BOARD_ID' not in open(os.path.join(R, 'components', comp, 'include/bsp/bsp_geom.h')).read() or f'"{bid}"' not in open(os.path.join(R, 'components', comp, 'include/bsp/bsp_geom.h')).read(): print(f'check_web_flasher: {comp} BSP_BOARD_ID is not "{bid}"'); bad = 1
if "const BOARDS = ['jc3636f118', 'fakepod']" not in js: print('check_web_flasher: flasher.js BOARDS changed'); bad = 1
sd = os.path.join(R, 'app/sdkconfig.defaults')
ver = re.search(r"ESPTOOL_VERSION = '([^']+)'", js).group(1)
hb = re.search(r"HB_VERSION = '([^']+)'", open(os.path.join(R, 'tools/web/fontsubset.js'), encoding='utf-8').read()).group(1)
for lib, v in (('esptool-js', ver), ('harfbuzzjs', hb)):
    d = os.path.join(R, 'tools/web/vendor', lib, v)
    if not os.path.isdir(d): print(f'check_web_flasher: vendor folder {lib}/{v} is missing'); bad = 1; continue
    for ln in open(os.path.join(d, 'SHA256SUMS')):
        h, fn = ln.split()
        if hashlib.sha256(open(os.path.join(d, fn), 'rb').read()).hexdigest() != h: print(f'check_web_flasher: {lib}/{fn} differs from SHA256SUMS'); bad = 1
print('check_web_flasher:', 'FAIL' if bad else 'ok'); sys.exit(bad)
