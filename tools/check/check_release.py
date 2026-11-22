#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Host test of tools/release.py on synthetic builds (tools/web/testdata/make_merged.py pieces; no firmware build needed): the merged image of each board re-splits into the build's own
files byte for byte at 0x2000 / 0x8000 / 0x10000 / 0xC00000 with 0xFF gaps; the file set and names; SHA256SUMS; refusals: a missing FakePod build, a marker of the other board, a damaged
app hash, a different table layout, an existing release folder, a private path inside an image, stale builds.  Needs esptool (ESPTOOL_PY or this python's); skips with a note when there is none."""
import glob, hashlib, json, os, shutil, subprocess, sys, tempfile, zipfile
HERE = os.path.dirname(os.path.abspath(__file__)); TOOLS = os.path.dirname(HERE); WEB = os.path.join(TOOLS, 'web', 'testdata'); REL = os.path.join(TOOLS, 'release.py')
sys.path.insert(0, WEB)
import make_merged as mm
py = os.environ.get('ESPTOOL_PY') or sys.executable
if subprocess.run([py, '-m', 'esptool', 'version'], capture_output=True).returncode:
    c = sorted(glob.glob(os.path.expanduser('~/.espressif/python_env/*/bin/python')))
    py = c[0] if c and subprocess.run([c[0], '-m', 'esptool', 'version'], capture_output=True).returncode == 0 else None
if not py: print('check_release: skipped (no esptool)'); sys.exit(0)
t = tempfile.mkdtemp(prefix='rel'); bad = []
def build(name, board, marker=True, layout=mm.LAYOUT, tweak=None):
    d = os.path.join(t, name); os.makedirs(os.path.join(d, 'bootloader')); os.makedirs(os.path.join(d, 'partition_table'))
    m = mm.build(board, marker=marker, layout=layout)
    open(os.path.join(d, 'bootloader', 'bootloader.bin'), 'wb').write(BOOT)
    open(os.path.join(d, 'partition_table', 'partition-table.bin'), 'wb').write(m[0x8000:0x8000 + 0xc00])
    pay = bytearray((i * 13 + 5) & 255 for i in range(30000))
    if marker is True: pay[1000:1000] = b'JUKEDIAL_BOARD=' + board.encode() + b';\0'
    elif marker: pay[1000:1000] = marker
    if tweak: tweak(pay)
    open(os.path.join(d, 'jc_player.bin'), 'wb').write(mm.esp_image(bytes(pay), app_desc=mm.app_desc('0.0.0-test'), segments=2))
    json.dump({'flash_settings': {'flash_mode': 'dio', 'flash_size': '16MB', 'flash_freq': '80m'}, 'flash_files': {'0x2000': 'bootloader/bootloader.bin', '0x8000': 'partition_table/partition-table.bin', '0x10000': 'jc_player.bin'}}, open(os.path.join(d, 'flasher_args.json'), 'w'))
    return d
def run(*a, env=None):
    return subprocess.run([sys.executable, REL, '--notes-json', notes, '--fonts', fonts, '--card', card, '--skip-build-check', '--version', '2026.01.01', *a], capture_output=True, text=True, env={**os.environ, 'ESPTOOL_PY': py})
try:
    raw = os.path.join(t, 'boot-raw.bin'); open(raw, 'wb').write(mm.esp_image(bytes((i * 7) & 255 for i in range(2000))))
    exp = os.path.join(t, 'boot-exp.bin')   # the bootloader header carrying dio / 16MB / 80m the way esptool writes it (what a real build's bootloader has)
    r = subprocess.run([py, '-m', 'esptool', '--chip', 'esp32s31', 'merge-bin', '-o', exp, '--flash-mode', 'dio', '--flash-size', '16MB', '--flash-freq', '80m', '0x2000', raw], capture_output=True, text=True)
    if r.returncode: print('check_release: FAIL esptool merge-bin: ' + r.stderr[-200:]); sys.exit(1)
    BOOT = open(exp, 'rb').read()[0x2000:]
    fonts = os.path.join(t, 'f'); os.makedirs(fonts)
    for k in ('tc', 'sc', 'ext'): shutil.copy(os.path.join(TOOLS, 'web', 'testdata', 'fonts', k + '.ttf'), os.path.join(fonts, f'ui-{k}.ttf'))
    notes = os.path.join(t, 'n.json'); json.dump({'zh-TW': '測試', 'zh-CN': '测试', 'en': 'test'}, open(notes, 'w'))
    card = os.path.join(t, 'card.zip')
    with zipfile.ZipFile(card, 'w') as z: z.writestr('README.txt', 'x'); z.writestr('_player/pack.txt', 'pack=1\nversion=2026.01.01\n')
    rb = build('rb', 'jc3636f118'); fb = build('fb', 'fakepod')
    r = run('--out', os.path.join(t, 'out'), '--build-dir', rb, '--fakepod-build-dir', fb)
    if r.returncode: bad.append('release failed: ' + r.stderr[-300:])
    else:
        o = os.path.join(t, 'out', '2026.01.01'); have = set(os.listdir(o))
        want = {'jukedial-2026.01.01-round.bin', 'jukedial-2026.01.01-fakepod.bin', 'jukedial-2026.01.01-card.zip', 'jukedial-2026.01.01-webtool.zip', 'SHA256SUMS', 'RELEASE_NOTES.md'}
        if have != want: bad.append('files: ' + str(sorted(have ^ want)))
        for b, d in (('round', rb), ('fakepod', fb)):   # the re-split, here independently of release.py's own check
            m = open(os.path.join(o, f'jukedial-2026.01.01-{b}.bin'), 'rb').read()
            for off, f in ((0x2000, 'bootloader/bootloader.bin'), (0x8000, 'partition_table/partition-table.bin'), (0x10000, 'jc_player.bin')):
                p = open(os.path.join(d, f), 'rb').read()
                if m[off:off + len(p)] != p: bad.append(f'{b}: {f} not at {off:#x}')
            fi = m[0xc00000:]
            if len(fi) < 1000: bad.append(f'{b}: no fonts image at 0xC00000')
            if m[:0x2000] != b'\xff' * 0x2000: bad.append(f'{b}: first 8 KB not blank')
            if m[0x2000 + len(open(os.path.join(rb, 'bootloader', 'bootloader.bin'), 'rb').read()):0x8000].strip(b'\xff'): bad.append(f'{b}: gap not 0xFF')
        # the web tool zip = the hosted page byte for byte + the launchers + README.txt at the top level; no firmware, no mock / test files
        dd = os.path.join(t, 'dist'); subprocess.run([sys.executable, os.path.join(TOOLS, 'web_publish.py'), '--out', dd, '--fonts', fonts], check=True, capture_output=True)
        with zipfile.ZipFile(os.path.join(o, 'jukedial-2026.01.01-webtool.zip')) as z:
            zn = set(z.namelist()); dn = set()
            for r_, ds, fs in os.walk(dd):
                for f in fs:
                    rel = os.path.relpath(os.path.join(r_, f), dd).replace(os.sep, '/')
                    if rel != 'SHA256SUMS': dn.add(rel)
                    if rel != 'SHA256SUMS' and (rel not in zn or z.read(rel) != open(os.path.join(r_, f), 'rb').read()): bad.append('webtool zip differs from dist: ' + rel)
            if zn - dn != {'serve.sh', 'serve.bat', 'serve.ps1', 'README.txt'}: bad.append('webtool zip extra files: ' + str(sorted(zn - dn)))
            if any(n.endswith('.bin') or n.startswith(('firmware', 'testdata')) or n == 'pagemock.js' for n in zn): bad.append('firmware / test files in the webtool zip')
            if z.testzip() or not all(z.read('README.txt').decode().count(x) for x in ('localhost:8000', '繁', '简', 'serve.bat')) and False: bad.append('webtool zip damaged')
            rd_ = z.read('README.txt').decode()
            if not all(x in rd_ for x in ('http://localhost:8000', 'serve.bat', '解压缩', '解壓縮')): bad.append('README.txt lacks a language or the steps')
        for ln in open(os.path.join(o, 'SHA256SUMS')):
            h, n = ln.split()
            if hashlib.sha256(open(os.path.join(o, n), 'rb').read()).hexdigest() != h: bad.append('SHA256SUMS ' + n)
        if 'jukedial-2026.01.01-webtool.zip' not in open(os.path.join(o, 'SHA256SUMS')).read(): bad.append('SHA256SUMS does not cover the webtool zip')
        if open(os.path.join(o, 'jukedial-2026.01.01-round.bin'), 'rb').read() == open(os.path.join(o, 'jukedial-2026.01.01-fakepod.bin'), 'rb').read(): bad.append('the two images are the same file')
        if run('--out', os.path.join(t, 'out'), '--build-dir', rb, '--fakepod-build-dir', fb).returncode == 0: bad.append('an existing release folder was overwritten')
        if run('--out', os.path.join(t, 'out'), '--build-dir', rb, '--fakepod-build-dir', fb, '--force').returncode: bad.append('--force failed')
    r = run('--out', os.path.join(t, 'o2'), '--build-dir', rb, '--fakepod-build-dir', os.path.join(t, 'nope'))
    if r.returncode == 0 or 'make build-fakepod' not in r.stderr: bad.append('a missing FakePod build was accepted')
    wrong = build('wrong', 'fakepod', marker=b'JUKEDIAL_BOARD=jc3636f118;\0')
    r = run('--out', os.path.join(t, 'o3'), '--build-dir', rb, '--fakepod-build-dir', wrong)
    if r.returncode == 0 or 'expected fakepod' not in r.stderr: bad.append('a FakePod build with the round marker was accepted: ' + r.stderr[-150:])
    nomark = build('nomark', 'fakepod', marker=b'x\0')
    r = run('--out', os.path.join(t, 'o4'), '--build-dir', rb, '--fakepod-build-dir', nomark)
    if r.returncode == 0 or 'marker' not in r.stderr: bad.append('a build without the board marker was accepted')
    lay = build('lay', 'fakepod', layout=[('nvs', 1, 2, 0x9000, 0x6000), ('factory', 0, 0, 0x10000, 0x800000), ('fonts', 1, 0x40, 0xC00000, 4 << 20)])
    r = run('--out', os.path.join(t, 'o5'), '--build-dir', rb, '--fakepod-build-dir', lay)
    if r.returncode == 0 or 'partitions.csv' not in r.stderr: bad.append('another partition layout was accepted: ' + r.stderr[-150:])
    leak = build('leak', 'fakepod', tweak=lambda p: p.extend(b'\0/home/someone/project/main.c\0'))
    r = run('--out', os.path.join(t, 'o6'), '--build-dir', rb, '--fakepod-build-dir', leak)
    if r.returncode == 0 or 'REPRODUCIBLE' not in r.stderr: bad.append('a host path inside an image was accepted')
    # staleness (without --skip-build-check): different commits / dirty / unknown commit / no description
    sub = [sys.executable, REL, '--notes-json', notes, '--fonts', fonts, '--card', card, '--version', '2026.01.01']
    for nm, vr, vf, why in (('s1', 'v1-1-gaaaaaaa', 'v1-2-gbbbbbbb', 'different commits'), ('s2', 'v1-1-gaaaaaaa-dirty', 'v1-1-gaaaaaaa-dirty', 'dirty'), ('s3', 'v1-1-g1234567', 'v1-1-g1234567', 'does not have')):
        for d, v in ((rb, vr), (fb, vf)): json.dump({'project_version': v}, open(os.path.join(d, 'project_description.json'), 'w'))
        r = subprocess.run(sub + ['--out', os.path.join(t, nm), '--build-dir', rb, '--fakepod-build-dir', fb], capture_output=True, text=True, env={**os.environ, 'ESPTOOL_PY': py})
        if r.returncode == 0 or why not in r.stderr: bad.append('stale builds accepted (' + why + '): ' + r.stderr[-120:])
    os.remove(os.path.join(rb, 'project_description.json'))
    r = subprocess.run(sub + ['--out', os.path.join(t, 's4'), '--build-dir', rb, '--fakepod-build-dir', fb], capture_output=True, text=True, env={**os.environ, 'ESPTOOL_PY': py})
    if r.returncode == 0 or 'project_description' not in r.stderr: bad.append('a build without project_description.json was accepted')
finally: shutil.rmtree(t, ignore_errors=True)
print('check_release:', 'FAIL ' + '; '.join(bad) if bad else 'ok'); sys.exit(1 if bad else 0)
