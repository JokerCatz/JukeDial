#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""`make release`: the files of ONE release, local only (no upload, no network): release/<version>/
    jukedial-<version>-round.bin      one merged flash image per board (bootloader 0x2000 + partition table 0x8000 + app 0x10000 + fonts 0xC00000, 0xFF between), written from offset 0
    jukedial-<version>-fakepod.bin
    jukedial-<version>-card.zip       the TF card content pack
    jukedial-<version>-webtool.zip    the web tool for localhost use: the hosted page (dist/) byte for byte + serve.sh / serve.bat / serve.ps1 + README.txt
    SHA256SUMS, RELEASE_NOTES.md      (the notes: --notes-json three languages + the version's section of CHANGELOG.md when that file exists)
The owner uploads that folder as Release assets (GitHub, Gitee); the flashing page (tools/web_publish.py) hosts no firmware and flashes the file the user picks.

  python3 tools/release.py --notes-json F [--out release] [--version V] [--build-dir app/build] [--fakepod-build-dir app/build-fakepod] [--fonts DIR] [--dist DIR] [--card FILE] [--force] [--skip-build-check]
Version: --version, else the first line of version.txt, else YYYY.MM.DD-<git short> of HEAD.  Fails (exit 1) when either build is missing, older than the sources (anything changed under app/ or
components/ since the commit the build was configured from), dirty, or when the two builds are from different commits.  The merge is esptool merge-bin (ESPTOOL_PY = a python with esptool, else
this python): `--flash-mode keep --flash-size keep --flash-freq keep`, i.e. every piece goes in byte for byte (verified below: re-split at the offsets, gaps 0xFF, the bootloader header of the
build already carries dio / 16MB / 80m, so an explicit merge gives the same file).  Every merged image is then re-checked: pieces equal the build's files, ESP image hashes, partition table
layout (app 0x10000, fonts 0xC00000, app/partitions.csv), the board marker JUKEDIAL_BOARD=<id>; inside the app is the board the image is named for."""
import argparse, hashlib, json, os, re, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
from web_publish import PRIVATE_NAME, BIN_PRIVATE, MAC, USB_SERIAL, PRIVATE_TEXT, guard_in, sha
SLOT = {'bootloader': 0x2000, 'partition-table': 0x8000, 'app': 0x10000, 'fonts': 0xc00000}   # app/partitions.csv; tools/check/check_web_flasher.py keeps tools/web/flasher.js equal
BOARDS = {'jc3636f118': 'round', 'fakepod': 'fakepod'}
FLASH_MB = 16

def die(m): print('release:', m, file=sys.stderr); sys.exit(1)

def check_builds(git, builds):
    """both builds were configured from the same, clean commit and nothing that ends up in the firmware changed since: a stale build of one board must not ship"""
    vers = {}
    for name, d in builds:
        pd = os.path.join(d, 'project_description.json')
        if not os.path.isfile(pd): die(f'the {name} build has no project_description.json ({d}): rebuild it')
        vers[name] = json.load(open(pd)).get('project_version', '')
    if len(set(vers.values())) != 1: die('the round and FakePod builds are from different commits (' + ', '.join(f'{k}: {v}' for k, v in vers.items()) + '): rebuild the stale one (make build / make build-fakepod)')
    v = next(iter(vers.values()))
    if v.endswith('-dirty'): die(f'the builds were made from a dirty tree ({v}): commit, then rebuild')
    m = re.search(r'-g?([0-9a-f]{7,40})$', v)   # git describe (-g<hash>) or the date version (version.txt-<hash>, e.g. 2026.11.22-349f350)
    c = git('rev-parse', '--verify', '--quiet', (m.group(1) if m else v) + '^{commit}')
    if not c: die(f'the builds name a commit this repository does not have ({v}): rebuild')
    changed = git('diff', '--name-only', c, 'HEAD', '--', 'app', 'components').splitlines() + git('status', '--porcelain', '--', 'app', 'components').splitlines()
    if changed: die(f'the builds ({v}) are older than the sources: {len(changed)} changed path(s) under app/ components/ since then (first: {changed[0]}): rebuild both (make build build-fakepod)')

def check_card(data, version):
    """the card pack must be a zip, carry _player/pack.txt with this release's version, hold no private-looking name or text; returns the pack revision"""
    import io, zipfile
    try: z = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile: die('the card pack is not a zip')
    if z.testzip(): die('the card pack has a corrupt entry')
    for n in z.namelist():
        if PRIVATE_NAME.search(n) or '.local' in n.split('/'): die(f'the card pack holds a private-looking name: {n}')
        if n.endswith(('.txt', '.toml', '.json', '.example')):
            d = z.read(n)
            for rx in PRIVATE_TEXT:
                if rx.search(d): die(f'the card pack file {n} matches {rx.pattern.decode()}')
    if '_player/pack.txt' not in z.namelist(): die('the card pack has no _player/pack.txt (tools/gen/make_card_zip.py writes it)')
    L = z.read('_player/pack.txt').decode().splitlines()
    m = re.fullmatch(r'pack=(\d+)', L[0]) if L else None
    if not m or len(L) < 2 or L[1] != 'version=' + version: die(f'_player/pack.txt does not carry the release version {version} (build the pack with --version {version})')
    return int(m.group(1))


def esp_image_end(d, off, what):
    """end offset of the ESP image at `off` (header, segments, checksum block, appended SHA-256 which is verified); dies with the reason"""
    if d[off] != 0xe9: die(f'{what}: no ESP image at {off:#x}')
    if int.from_bytes(d[off + 12:off + 14], 'little') != 32: die(f'{what}: not an ESP32-S31 image (chip id)')
    p = off + 24
    for _ in range(d[off + 1]): p += 8 + int.from_bytes(d[p + 4:p + 8], 'little')
    end = ((p - off) | 15) + off + 1
    if d[off + 23] == 1:
        if hashlib.sha256(d[off:end]).digest() != d[end:end + 32]: die(f'{what}: the appended SHA-256 is wrong')
        end += 32
    return end

def partition_entries(t):
    out = []
    for p in range(0, 0xc00, 32):
        if t[p:p + 2] == b'\xaa\x50': out.append((t[p + 12:p + 28].split(b'\0')[0].decode(), t[p + 2], t[p + 3], int.from_bytes(t[p + 4:p + 8], 'little'), int.from_bytes(t[p + 8:p + 12], 'little')))
        else: break
    return out

def csv_layout():
    r = {}
    for ln in open(os.path.join(REPO, 'app', 'partitions.csv'), encoding='utf-8'):
        f = [x.strip() for x in ln.split('#')[0].split(',')]
        if len(f) >= 5 and f[0] and f[0] != 'Name':
            sz = f[4]; r[f[0]] = (int(f[3], 0), int(sz[:-1]) << 20 if sz.endswith('M') else int(sz, 0))
    return r

def merge(py, out, pieces, mode='keep'):
    args = []
    for off, path in pieces: args += [hex(off), path]
    r = subprocess.run(py + ['--chip', 'esp32s31', 'merge-bin', '-o', out, '--flash-mode', mode, '--flash-size', 'keep' if mode == 'keep' else f'{FLASH_MB}MB', '--flash-freq', 'keep' if mode == 'keep' else '80m', *args], capture_output=True, text=True)
    if r.returncode: die('esptool merge-bin failed: ' + (r.stderr or r.stdout)[-300:])

def verify_merged(path, board, pieces, csv):
    """re-split the merged file at the offsets: every piece equals the build's file byte for byte, every gap is 0xFF, the layout and the board are the expected ones"""
    m = open(path, 'rb').read()
    if m[:SLOT['bootloader']] != b'\xff' * SLOT['bootloader']: die(f'{path}: the first 8 KB are not blank')
    pos = 0
    for off, name, data in sorted(pieces):
        if m[off:off + len(data)] != data: die(f'{path}: {name} is not at {off:#x} byte for byte')
        if off > pos and m[pos:off] != b'\xff' * (off - pos): die(f'{path}: the gap before {name} ({pos:#x}..{off:#x}) is not 0xFF')
        pos = off + len(data)
    if len(m) != pos: die(f'{path}: {len(m) - pos:+d} bytes after the last piece')
    boot_end = esp_image_end(m, SLOT['bootloader'], 'bootloader'); app_end = esp_image_end(m, SLOT['app'], 'app')
    ents = partition_entries(m[SLOT['partition-table']:SLOT['partition-table'] + 0xc00])
    got = {n: (o, s) for n, ty, st, o, s in ents}
    if got.get('factory') != csv['factory'] or got.get('fonts') != csv['fonts'] or got.get('factory') != (SLOT['app'], got['factory'][1]) or got['fonts'][0] != SLOT['fonts']: die(f'{path}: partition table {got} is not app/partitions.csv {csv}')
    if int.from_bytes(m[SLOT['app'] + 0x20:SLOT['app'] + 0x24], 'little') != 0xabcd5432: die(f'{path}: no esp_app_desc_t at the app + 0x20')
    ids = set(re.findall(rb'JUKEDIAL_BOARD=([a-z0-9]+);', m[SLOT['app']:app_end]))
    if ids != {board.encode()}: die(f'{path}: the app says board {sorted(ids) or "nothing"}, expected {board}: rebuild (the build predates the marker in app/main/guard.c, or it is the other board)')
    if app_end - SLOT['app'] > got['factory'][1]: die(f'{path}: the app is bigger than its partition')
    return m, boot_end, app_end

README = """JukeDial web tool (settings, radio, effects, firmware flashing) -- runs on your own computer
1. Unzip this file into an empty folder.
2. Linux / macOS: run  sh serve.sh      Windows: double-click  serve.bat  (nothing to install)
3. Open http://localhost:8000 in Chrome or Edge (they have Web Serial; Firefox / Safari do not). Press Ctrl+C in the window to stop.
The page never uploads your files; it only talks to your player over USB.

JukeDial 網頁工具（設定、電台、音效、刷韌體），在你自己的電腦上執行
1. 把這個壓縮檔解壓縮到一個空資料夾。
2. Linux / macOS：執行  sh serve.sh      Windows：點兩下  serve.bat（不用安裝任何東西）
3. 用 Chrome 或 Edge 開啟 http://localhost:8000（要有 Web Serial；Firefox、Safari 不行）。要停止時在視窗按 Ctrl+C。
頁面不會上傳你的檔案，只會經 USB 和你的播放器通訊。

JukeDial 网页工具（设置、电台、音效、刷固件），在你自己的电脑上运行
1. 把这个压缩包解压缩到一个空文件夹。
2. Linux / macOS：运行  sh serve.sh      Windows：双击  serve.bat（不用安装任何东西）
3. 用 Chrome 或 Edge 打开 http://localhost:8000（需要 Web Serial；Firefox、Safari 不行）。要停止时在窗口按 Ctrl+C。
页面不会上传你的文件，只会经 USB 和你的播放器通信。
"""
LAUNCHERS = ['serve.sh', 'serve.bat', 'serve.ps1']

def make_webtool(path, dist, version):
    """the hosted page (dist/, byte for byte; no firmware, no test files) + the launchers + README.txt at the top level; deterministic (sorted, fixed time)"""
    import zipfile
    files = {}
    for r, dirs, fs in os.walk(dist):
        dirs.sort()
        for f in fs:
            rel = os.path.relpath(os.path.join(r, f), dist).replace(os.sep, '/')
            if rel == 'SHA256SUMS': continue   # dist's own list: the release's SHA256SUMS covers the zip
            files[rel] = open(os.path.join(r, f), 'rb').read()
    for n in LAUNCHERS:
        if n in files: die(f'dist already has {n}')
        files[n] = open(os.path.join(HERE, 'web', n), 'rb').read()
    files['README.txt'] = README.encode()
    for n in files:
        if n.endswith('.bin') or n.startswith('firmware/') or n == 'pagemock.js' or n.startswith('testdata/'): die(f'the web tool zip must not hold {n}')
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for n in sorted(files):
            zi = zipfile.ZipInfo(n, (2020, 1, 1, 0, 0, 0)); zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = (0o755 if n == 'serve.sh' else 0o644) << 16
            z.writestr(zi, files[n])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, 'release')); ap.add_argument('--build-dir', default=os.path.join(REPO, 'app', 'build')); ap.add_argument('--fakepod-build-dir', default=os.path.join(REPO, 'app', 'build-fakepod'))
    ap.add_argument('--dist', help='an existing dist/ (make web-publish) to zip as the web tool; default: built now with --fonts'); ap.add_argument('--fonts'); ap.add_argument('--notes-json', required=True); ap.add_argument('--version'); ap.add_argument('--card'); ap.add_argument('--force', action='store_true'); ap.add_argument('--skip-build-check', action='store_true')
    a = ap.parse_args()
    git = lambda *x: subprocess.run(['git', '-C', REPO, *x], capture_output=True, text=True).stdout.strip()
    vt = os.path.join(REPO, 'version.txt')
    version = a.version or (open(vt).readline().strip() if os.path.isfile(vt) else '') or (git('log', '-1', '--format=%cd', '--date=format:%Y.%m.%d') + '-' + git('rev-parse', '--short', 'HEAD'))
    if not re.fullmatch(r'[\w.-]+', version): die('bad version')
    notes = json.load(open(a.notes_json))
    if sorted(notes) != ['en', 'zh-CN', 'zh-TW'] or not all(isinstance(v, str) and v.strip() for v in notes.values()): die('--notes-json needs the three non-empty keys zh-TW, zh-CN, en')
    if not a.fonts: die('--fonts DIR is needed (ui-tc.ttf ui-sc.ttf ui-ext.ttf: the fonts partition is part of every merged image)')
    csv = csv_layout()
    if csv['factory'][0] != SLOT['app'] or csv['fonts'] != (SLOT['fonts'], 4 << 20): die(f'app/partitions.csv {csv} does not match the offsets this tool and the page use {SLOT}')
    builds = {'jc3636f118': a.build_dir, 'fakepod': a.fakepod_build_dir}
    for b, d in builds.items():
        if not os.path.isfile(os.path.join(d, 'flasher_args.json')): die(f'the {BOARDS[b]} build is missing ({d}): run make build{"-fakepod" if b == "fakepod" else ""}; a release always carries both boards')
    if not a.skip_build_check: check_builds(git, [(BOARDS[b], d) for b, d in builds.items()])
    out = os.path.join(a.out, version)
    if os.path.exists(out):
        if not a.force: die(f'{out} exists: refusing to overwrite a release folder (--force replaces it)')
        shutil.rmtree(out)
    py = ([os.environ['ESPTOOL_PY']] if os.environ.get('ESPTOOL_PY') else [sys.executable]) + ['-m', 'esptool']
    if subprocess.run(py + ['version'], capture_output=True).returncode: die('esptool is not available: set ESPTOOL_PY=<python with esptool> (the IDF python env has it)')
    for n in ('ui-tc.ttf', 'ui-sc.ttf', 'ui-ext.ttf'): guard_in(os.path.join(a.fonts, n))
    tmp = tempfile.mkdtemp(prefix='release')
    try:
        fp = os.path.join(tmp, 'fonts.bin')
        r = subprocess.run([sys.executable, os.path.join(HERE, 'gen', 'make_fontpak.py'), fp, str(csv['fonts'][1])] + [os.path.join(a.fonts, n) for n in ('ui-tc.ttf', 'ui-sc.ttf', 'ui-ext.ttf')], capture_output=True, text=True)
        if r.returncode: die('make_fontpak failed: ' + r.stdout + r.stderr)
        fonts_img = open(fp, 'rb').read()
        if a.card: card = open(a.card if os.path.abspath(a.card) == os.path.join(REPO, '.local', 'card-template.zip') else guard_in(a.card), 'rb').read()
        else:
            cz = os.path.join(tmp, 'card.zip')
            r = subprocess.run([sys.executable, os.path.join(HERE, 'gen', 'make_card_zip.py'), '--out', cz, '--version', version], capture_output=True, text=True)
            if r.returncode: die('make_card_zip failed: ' + r.stdout + r.stderr)
            card = open(cz, 'rb').read()
        check_card(card, version)
        os.makedirs(out)
        names = []
        for b, d in builds.items():
            fa = json.load(open(os.path.join(d, 'flasher_args.json'))); ff = {int(k, 16): v for k, v in fa['flash_files'].items()}
            if sorted(ff) != [SLOT['bootloader'], SLOT['partition-table'], SLOT['app']]: die(f'{d}/flasher_args.json flashes at {[hex(k) for k in sorted(ff)]}, expected {[hex(SLOT[k]) for k in ("bootloader", "partition-table", "app")]}')
            fs = fa['flash_settings']
            if (fs['flash_mode'], fs['flash_size'], fs['flash_freq']) != ('dio', f'{FLASH_MB}MB', '80m'): die(f'{d}: flash settings {fs} are not dio / {FLASH_MB}MB / 80m')
            pieces = [(SLOT['bootloader'], 'bootloader', open(guard_in(os.path.join(d, ff[SLOT['bootloader']])), 'rb').read()), (SLOT['partition-table'], 'partition-table', open(guard_in(os.path.join(d, ff[SLOT['partition-table']])), 'rb').read()),
                      (SLOT['app'], 'app', open(guard_in(os.path.join(d, ff[SLOT['app']])), 'rb').read()), (SLOT['fonts'], 'fonts', fonts_img)]
            if len(pieces[1][2]) != 0xc00: die(f'{d}: the partition table file is {len(pieces[1][2])} bytes, not 0xc00')
            files = []
            for off, name, data in pieces:
                pth = os.path.join(tmp, f'{b}-{name}'); open(pth, 'wb').write(data); files.append((off, pth))
            dst = os.path.join(out, f'jukedial-{version}-{BOARDS[b]}.bin')
            merge(py, dst, files)
            alt = os.path.join(tmp, f'{b}-explicit.bin'); merge(py, alt, files, mode='dio')
            if open(alt, 'rb').read() != open(dst, 'rb').read(): die(f'{d}: merging with --flash-mode dio --flash-size {FLASH_MB}MB --flash-freq 80m changes the file: the build\'s bootloader header does not carry those values; the page writes the file as it is')
            m, boot_end, app_end = verify_merged(dst, b, pieces, csv)
            names.append(os.path.basename(dst))
            print(f'release: {os.path.basename(dst)}: {len(m)} bytes, bootloader {boot_end - SLOT["bootloader"]} B, app {app_end - SLOT["app"]} B, fonts {len(fonts_img)} B, verified (re-split, gaps 0xFF, hashes, layout, board marker)')
        dist = a.dist
        if not dist:
            dist = os.path.join(tmp, 'dist')
            r = subprocess.run([sys.executable, os.path.join(HERE, 'web_publish.py'), '--out', dist, '--fonts', a.fonts], capture_output=True, text=True)
            if r.returncode: die('web_publish failed: ' + r.stdout + r.stderr)
        wn = f'jukedial-{version}-webtool.zip'; make_webtool(os.path.join(out, wn), dist, version); names.append(wn)
        cn = f'jukedial-{version}-card.zip'; open(os.path.join(out, cn), 'wb').write(card); names.append(cn)
        for n in names:   # nothing private inside what goes public
            data = open(os.path.join(out, n), 'rb').read()
            if n.endswith('.bin'):
                x = BIN_PRIVATE.search(data) or MAC.search(data) or USB_SERIAL.search(data)
                if x: die(f'{n} holds a host path, MAC address or USB serial ({x.group(0)[:40]!r}): for a build path use CONFIG_APP_REPRODUCIBLE_BUILD=y')
        cl = ''
        if os.path.isfile(os.path.join(REPO, 'CHANGELOG.md')):
            txt = open(os.path.join(REPO, 'CHANGELOG.md'), encoding='utf-8').read()
            mt = re.search(r'^##[^\n]*' + re.escape(version) + r'[^\n]*\n(.*?)(?=^## |\Z)', txt, re.S | re.M)
            cl = mt.group(0).strip() if mt else ''
        body = [f'# JukeDial {version}', '', '## Notes', ''] + [f'- {k}: {notes[k]}' for k in ('zh-TW', 'zh-CN', 'en')] + ['', '## Changelog', '', cl or '(no section for this version in CHANGELOG.md: write one)', '']
        body += ['## Files', '', f'- `jukedial-{version}-round.bin`: JC3636F118 (round, knob): ONE merged image (bootloader 0x2000, partition table 0x8000, app 0x10000, fonts 0xC00000), flash from offset 0, or pick it in the web flasher (which keeps settings and pairings)',
                 f'- `jukedial-{version}-fakepod.bin`: FakePod (460x460, two keys), same layout; after a manual esptool write use `--after watchdog-reset` (a plain reset stays in download mode)',
                 f'- `jukedial-{version}-card.zip`: the TF card content pack (unzip to the card root)', f'- `jukedial-{version}-webtool.zip`: the web flashing/settings tool to run on localhost (unzip; `sh serve.sh` or `serve.bat`; open http://localhost:8000 in Chrome or Edge)', '- `SHA256SUMS`', '', 'Manual flash with esptool (erases settings and pairings in the nvs area; the web flasher does not): `esptool --chip esp32s31 write-flash 0x0 <file>`', '']
        open(os.path.join(out, 'RELEASE_NOTES.md'), 'w', encoding='utf-8').write('\n'.join(body))
        open(os.path.join(out, 'SHA256SUMS'), 'w').write(''.join(f'{sha(open(os.path.join(out, n), "rb").read())}  {n}\n' for n in sorted(names)))
    finally: shutil.rmtree(tmp, ignore_errors=True)
    print(f'release: {out} ({len(names) + 2} files)')

if __name__ == '__main__':
    main()
