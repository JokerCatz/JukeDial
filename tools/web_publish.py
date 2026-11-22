#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Build dist/: the settings + flashing page and its vendored libraries (+ prebuilt fonts and Noto sources when given).  No upload, no network.  The page hosts NO firmware: the owner's
release (`make release`, tools/release.py) is uploaded to the project's Releases page and the user picks the downloaded file in the page, so this dist is version-independent and is
published once (and again only when the page itself changes).

  python3 tools/web_publish.py [--out dist]
                               [--fonts DIR]      DIR holds ui-tc.ttf ui-sc.ttf ui-ext.ttf: offered as downloads on the fonts tab
                               [--fontsrc DIR]    the local Noto sources (default .local/fontsrc when it exists; the same ones make fonts-build uses): *.otf are converted to TrueType
                                                  offline with fontTools (tools/gen/otf2ttf.py, interpreter FONT_PY or python3), *.ttf copied; the TTFs and the OFL texts go to dist/noto/ only
                               [--noto DIR]       an override: *.ttf / OFL files bundled as they are (no conversion)
Deterministic: files copied byte for byte in sorted order, no timestamps.  Refuses (exit 1) any input that looks private: a path with .local, a name containing wifi / secret / password / .env /
id_rsa, and a finished dist whose text files hold a home path, a private key marker, or a 192.168. address.  Writes dist/SHA256SUMS (every file)."""
import argparse, hashlib, json, os, re, shutil, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..'))
WEB = os.path.join(HERE, 'web')
PAGE = ['index.html', 'page.js', 'boot.js', 'conn.js', 'i18n.js', 'lang.js', 'flasher.js', 'flashtab.js', 'fontsets.js', 'fontsubset.js', 'fontbuild.js', 'fontput.js', 'fontworker.js', 'fonttab.js', 'glyphpack.js', 'lists.js', 'regions.js', 'board_round.svg', 'board_fakepod.svg']
PAGE_DIRS = ['lists', 'vendor']
PRIVATE_NAME = re.compile(r'wifi|secret|passw|\.env|id_rsa|credential', re.I)
BIN_PRIVATE = re.compile(rb'/home/[A-Za-z0-9_.-]+/|/Users/[A-Za-z0-9_.-]+/|/media/[A-Za-z0-9_.-]+/|[A-Za-z]:\\Users\\|[A-Za-z]:/Users/')
# a MAC address or a USB serial (by-id names carry one: usb-Espressif_..._<MAC>-if00, usb-1a86_..._<serial>-if00): never in a published file; an all-zero / all-ff address is a placeholder
MAC = re.compile(rb'(?<![0-9A-Fa-f:])(?!00(?::00){5}|ff(?::ff){5}|FF(?::FF){5})[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}(?![0-9A-Fa-f:])')
USB_SERIAL = re.compile(rb'usb-[A-Za-z0-9]+_[A-Za-z0-9_.-]+_[A-Za-z0-9:]{8,}-if\d\d|/dev/serial/by-id|/dev/tty(?:ACM|USB)\d')
PRIVATE_TEXT = [re.compile(rb'/home/[a-z]'), re.compile(rb'/media/[a-z]'), re.compile(rb'/Users/[A-Za-z]'), re.compile(rb'[A-Za-z]:\\{1,2}Users'), MAC, USB_SERIAL, re.compile(rb'-----BEGIN [A-Z ]*PRIVATE KEY'), re.compile(rb'192\.168\.\d+\.\d+')]

def die(m): print('web_publish:', m, file=sys.stderr); sys.exit(1)
def sha(b): return hashlib.sha256(b).hexdigest()
def guard_in(path):
    if os.path.abspath(path).startswith(os.path.join(REPO, '.local', 'fontsrc') + os.sep) or os.path.abspath(path) == os.path.join(REPO, '.local', 'fontsrc'): path = path if not PRIVATE_NAME.search(os.path.basename(path)) else die(f'refusing a private-looking input: {path}'); return path   # the Noto sources are the one place under .local that is meant to ship (converted)
    p = os.path.relpath(os.path.abspath(path), REPO) if os.path.abspath(path).startswith(REPO) else os.path.abspath(path)
    if '.local' in p.split(os.sep) or any(PRIVATE_NAME.search(x) for x in p.split(os.sep)[-2:]): die(f'refusing a private-looking input: {path}')
    return path
def read(path): return open(guard_in(path), 'rb').read()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, 'dist'))
    ap.add_argument('--fonts'); ap.add_argument('--noto'); ap.add_argument('--fontsrc')
    a = ap.parse_args()
    out = a.out
    if os.path.exists(out): shutil.rmtree(out)
    os.makedirs(out)
    for f in PAGE: shutil.copyfile(os.path.join(WEB, f), os.path.join(out, f))
    for d in PAGE_DIRS:
        for root, dirs, fs in os.walk(os.path.join(WEB, d)):
            dirs.sort()
            for f in sorted(fs):
                if f.endswith('.md') and f != 'VENDOR.md': continue
                dst = os.path.join(out, os.path.relpath(os.path.join(root, f), WEB)); os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copyfile(os.path.join(root, f), dst)
    if a.fonts:
        os.makedirs(os.path.join(out, 'fonts'))
        for n in ('ui-tc.ttf', 'ui-sc.ttf', 'ui-ext.ttf'): shutil.copyfile(guard_in(os.path.join(a.fonts, n)), os.path.join(out, 'fonts', n))
    if a.noto:
        os.makedirs(os.path.join(out, 'noto'), exist_ok=True)
        for n in sorted(os.listdir(a.noto)):
            guard_in(os.path.join(a.noto, n))   # a private-looking name anywhere in the folder stops the publish, whether or not it would be copied
            if not allowed_font_file(n): continue
            shutil.copyfile(os.path.join(a.noto, n), os.path.join(out, 'noto', n))
    src = a.fontsrc or (os.path.join(REPO, '.local', 'fontsrc') if os.path.isdir(os.path.join(REPO, '.local', 'fontsrc')) and not a.noto else None)
    if src:
        os.makedirs(os.path.join(out, 'noto'), exist_ok=True)
        py = os.environ.get('FONT_PY', sys.executable)
        for n in sorted(os.listdir(src)):
            p = guard_in(os.path.join(src, n)); low = n.lower()
            if low.endswith('.otf'):
                r = subprocess.run([py, '-I', os.path.join(HERE, 'gen', 'otf2ttf.py'), p, os.path.join(out, 'noto', n[:-4] + '.ttf')], capture_output=True, text=True)
                if r.returncode: die(f'converting {n} needs fontTools (FONT_PY={py}): ' + r.stderr.strip().splitlines()[-1] if r.stderr.strip() else f'converting {n} failed')
            elif allowed_font_file(n): shutil.copyfile(p, os.path.join(out, 'noto', n))
    fonts_published = os.path.isdir(os.path.join(out, 'fonts')) or os.path.isdir(os.path.join(out, 'noto'))
    if fonts_published:   # the OFL requires its text to travel with the fonts: a source's own OFL*.txt, else the repository's copy
        ofl = [os.path.join(out, d, f) for d in ('noto', 'fonts') if os.path.isdir(os.path.join(out, d)) for f in sorted(os.listdir(os.path.join(out, d))) if re.fullmatch(r'OFL.*\.txt', f)]
        text = open(ofl[0], 'rb').read() if ofl else open(os.path.join(HERE, 'OFL-1.1.txt'), 'rb').read() if os.path.isfile(os.path.join(HERE, 'OFL-1.1.txt')) else None
        if not text: die('fonts are published but there is no OFL text (OFL*.txt next to the sources or tools/OFL-1.1.txt)')
        for d in ('fonts', 'noto'):
            if os.path.isdir(os.path.join(out, d)) and not os.path.isfile(os.path.join(out, d, 'OFL.txt')): open(os.path.join(out, d, 'OFL.txt'), 'wb').write(text)
    scan(out)
    sums(out, os.path.join(out, 'SHA256SUMS'))
    print(f'web_publish: {out}, {sum(len(f) for _, _, f in os.walk(out))} files')


def allowed_font_file(n): return bool(re.fullmatch(r'.*\.ttf|OFL[\w.-]*\.txt|LICENSE[\w.-]*', n, re.I))

def listing(root):
    for r, dirs, fs in os.walk(root):
        dirs.sort()
        for f in sorted(fs):
            if f != 'SHA256SUMS': yield os.path.join(r, f)
def sums(root, dst):
    open(dst, 'w').write(''.join(f'{sha(open(p, "rb").read())}  {os.path.relpath(p, root)}\n' for p in listing(root)))
def scan(out):
    bad = []
    for p in listing(out):
        rel = os.path.relpath(p, out)
        if PRIVATE_NAME.search(os.path.basename(p)) or '.local' in rel.split(os.sep): bad.append(f'{rel}: private-looking name')
        if rel.endswith(('.bin', '.ttf', '.otf', '.wasm', '.js')):   # binaries too: an IDF image holds the build host's absolute paths unless CONFIG_APP_REPRODUCIBLE_BUILD=y
            d = open(p, 'rb').read()
            m = BIN_PRIVATE.search(d)
            if not m and not rel.startswith('vendor/') and not rel.endswith('.wasm'): m = MAC.search(d) or USB_SERIAL.search(d)
            if m: bad.append(f'{rel}: holds a host path, MAC address or USB serial ({m.group(0)[:40].decode(errors="replace")}...): for a build path use CONFIG_APP_REPRODUCIBLE_BUILD=y)')
        if rel.endswith(('.js', '.html', '.json', '.md', '.txt')) and not rel.startswith('vendor/'):
            d = open(p, 'rb').read()
            for rx in PRIVATE_TEXT:
                if rx.search(d): bad.append(f'{rel}: matches {rx.pattern.decode()}')
    if bad: die('private data scan failed:\n  ' + '\n  '.join(bad))

if __name__ == '__main__':
    main()
