#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Contact sheets of every card theme in the simulator: themes_sheet.py OUTDIR [--fp]
Six pages per theme (home ring, now playing with the spectrum, settings list, a game, quick settings, help) -> OUTDIR/<name>.png (round) or <name>_fp.png (--fp, the
FakePod simulator, make sim-fp) and OUTDIR/themes_all[_fp].png (one row per theme, the default look first). Needs `make sim` (and `make sim-fp` for --fp), Pillow."""
import os, sys, subprocess, tempfile, shutil
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "gen"))   # tools/gen: sibling tools live in other folders
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_card_themes as G
from PIL import Image, ImageDraw
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGES = [("home", "uipush menu"), ("playing", "mp3ui; mp3 1; @1.5"), ("settings", "setui"), ("game", "games open c4; board tap 4; @1"), ("quick", "qs close; qs; @0.05"), ("help", "uipush help")]

def run(name, fp, out, lang=None):
    """lang: None = the fixture's own (zh-TW); 'zh-TW' / 'zh-CN' / 'en' set cfg lang before boot"""
    sim = os.path.join(REPO, "sim", "build-fp" if fp else "build", "sim")
    root = tempfile.mkdtemp(prefix="sheet_")
    r = os.path.join(root, "r"); shutil.copytree(os.path.join(REPO, "sim", "fixture"), r, symlinks=True)
    os.makedirs(os.path.join(r, "_player", "fonts"), exist_ok=True)
    shutil.rmtree(os.path.join(r, "_player", "fonts")); os.symlink(os.path.join(REPO, "fonts"), os.path.join(r, "_player", "fonts"))
    if lang:
        ap = os.path.join(r, "_player", "config", "app.txt"); t = open(ap, encoding="utf-8").read()
        open(ap, "w", encoding="utf-8").write(t.replace("lang=0", "lang=%d" % {"zh-TW": 0, "zh-CN": 1, "en": 2}[lang]))
    if name != "default":
        os.makedirs(os.path.join(r, "_player", "themes"), exist_ok=True)
        open(os.path.join(r, "_player", "themes", name + ".txt"), "w").write(G.build_one(name))
        open(os.path.join(r, "_player", "config", "app.txt"), "a").write("theme=%s\n" % name)
    cmd = ["vol 0", "screen on"]
    for pg, c in PAGES: cmd += [c, "@0.8", "shot %s/%s_%s.png" % (out, name, pg), "back" if pg != "home" else "@0"]
    # each page is opened from a fresh state: go home between pages
    seq = []
    for pg, c in PAGES: seq += [c, "@0.8" if pg != "quick" else "@0", "shot %s/%s_%s.png" % (out, name, pg), "uihome"]
    p = subprocess.run([sim, "--headless", "--root", r, "--cmd", "; ".join(["vol 0", "screen on"] + seq)], capture_output=True, text=True, timeout=180)
    shutil.rmtree(root, ignore_errors=True)
    return p

def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    out = sys.argv[1]; fp = "--fp" in sys.argv; sfx = "_fp" if fp else ""
    os.makedirs(out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="sheetpng_")
    names = ["default"] + list(G.THEMES)
    rows = []
    for n in names:
        p = run(n, fp, tmp)
        ims = [Image.open(os.path.join(tmp, "%s_%s.png" % (n, pg))).convert("RGB") for pg, _ in PAGES]
        w, h = ims[0].size
        sheet = Image.new("RGB", (w * 3, h * 2 + 0), (0, 0, 0))
        for i, im in enumerate(ims): sheet.paste(im, ((i % 3) * w, (i // 3) * h))
        d = ImageDraw.Draw(sheet); d.text((4, 4), n, fill=(255, 255, 255))
        sheet.save(os.path.join(out, "%s%s.png" % (n, sfx)))
        strip = Image.new("RGB", (w * 6, h)); [strip.paste(im, (i * w, 0)) for i, im in enumerate(ims)]
        ImageDraw.Draw(strip).text((4, 4), n, fill=(255, 255, 255)); rows.append(strip)
    allp = Image.new("RGB", (rows[0].width, rows[0].height * len(rows)))
    for i, r in enumerate(rows): allp.paste(r, (0, i * r.height))
    allp.save(os.path.join(out, "themes_all%s.png" % sfx))
    shutil.rmtree(tmp, ignore_errors=True)
if __name__ == "__main__":   # guide_shots.py imports run(): importing must not start the sheet job (it wrote a "--compose" folder)
    main()
