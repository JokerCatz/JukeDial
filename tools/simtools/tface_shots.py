#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Owner sheets of the designed HUD faces (TFACE): both simulators (sim/build/sim round, sim/build-fp/sim square), every feature, every colour
scheme on the clock, the warning states, the aqua (light) theme. Usage: tools/simtools/tface_shots.py OUTDIR   (make sim sim-fp first; Pillow).
Writes OUTDIR/{rd,sq}_<scheme>_<page>.png and the sheets OUTDIR/sheet_{round,square}$SHEET_SUFFIX.png."""
import os, shutil, subprocess, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "gen"))   # tools/gen: sibling tools live in other folders
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_card_themes as G

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIMS = {'rd': os.path.join(ROOT, 'sim/build/sim'), 'sq': os.path.join(ROOT, 'sim/build-fp/sim')}
BASE = "screen on; vol 0; @0.5; clock; @0.3"
PAGES = [
    ('clock', BASE + "; @1.5"),
    ('stopwatch', BASE + "; show stopwatch; timers sw start; @1.3; timers sw lap; @1.1; timers sw lap; @0.9; timers sw lap; @0.77"),
    ('countdown', BASE + "; show countdown; timers cd set 300; timers cd start; @2.2"),
    ('pomodoro', BASE + "; show pomo; pomo; @2.2"),
    ('countdown_last10', BASE + "; show countdown; timers cd set 300; timers cd start; @1; timers cd jump 7; @0.6"),
    ('clock_unsynced', "simtime 2000-01-01 00:00:00; @1.2; " + BASE + "; @1.5"),
]

def fixture(theme, col):
    d = tempfile.mkdtemp(prefix='tface_')
    r = os.path.join(d, 'root')
    shutil.copytree(os.path.join(ROOT, 'sim/fixture'), r)
    os.makedirs(os.path.join(r, '_player'), exist_ok=True)
    os.symlink(os.path.join(ROOT, '.local', 'faces.zip'), os.path.join(r, '_player', 'faces.zip'))   # make faces-zip
    ap = os.path.join(r, '_player', 'config', 'app.txt')
    os.makedirs(os.path.dirname(ap), exist_ok=True)
    with open(ap, 'a') as f:
        f.write('\nclockface=hud\nswface=hud\ncdface=hud\npomoface=hud\nhudcol=%s\n' % col)
        if theme != 'default':
            f.write('theme=%s\n' % theme)
    if theme != 'default':
        os.makedirs(os.path.join(r, '_player', 'themes'), exist_ok=True)
        open(os.path.join(r, '_player', 'themes', theme + '.txt'), 'w').write(G.build_one(theme))
    return d, r

def shoot(board, theme, col, pages, out):
    d, r = fixture(theme, col)
    files = []
    for name, c in pages:
        fn = os.path.join(out, '%s_%s_%s_%s.png' % (board, theme, col, name))
        p = subprocess.run([SIMS[board], '--headless', '--root', r, '--cmd', c + '; shot ' + fn + '; timers stats; quit'], capture_output=True, text=True, timeout=120)
        for ln in p.stdout.splitlines():
            if ln.startswith('APP tface stats'): print(board, theme, col, name, ln)
        files.append((name, fn))
    shutil.rmtree(d)
    return files

def sheet(cells, size, out, cols=4):
    pad, lab = 8, 18
    rows = (len(cells) + cols - 1) // cols
    im = Image.new('RGB', (cols * (size + pad) + pad, rows * (size + pad + lab) + pad), (40, 40, 40))
    dr = ImageDraw.Draw(im)
    for i, (label, fn) in enumerate(cells):
        x = pad + (i % cols) * (size + pad); y = pad + (i // cols) * (size + pad + lab)
        dr.text((x, y), label, fill=(230, 230, 230))
        if os.path.exists(fn): im.paste(Image.open(fn).convert('RGB').resize((size, size)), (x, y + lab))
    im.save(out)

def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for b, nm in (('rd', 'round'), ('sq', 'square')):
        cells = []
        for name, fn in shoot(b, 'default', 'green', PAGES, out): cells.append(('green ' + name, fn))
        for col in ('red', 'cyan', 'tri'):
            for name, fn in shoot(b, 'default', col, PAGES[:1] + PAGES[2:3], out): cells.append((col + ' ' + name, fn))
        for name, fn in shoot(b, 'aqua', 'green', PAGES[:2], out): cells.append(('aqua theme ' + name, fn))
        sheet(cells, 360, os.path.join(out, 'sheet_%s%s.png' % (nm, os.environ.get('SHEET_SUFFIX', ''))))

if __name__ == '__main__':
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    main()
