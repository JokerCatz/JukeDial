#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Palette contact sheets (wave 3 PAL): pal_sheets.py OUTDIR [--before SIM] [--after SIM] [--fp]
For the themes default / aqua / dawn / mint: the spectrum bars in each ramp, the waterfall, the lava lamp, the breathing coach, the multi-ring clock face, a Neon-style
game and the spirograph, drawn by the BEFORE simulator (an older build) and the AFTER one -> OUTDIR/<theme>_before_after.png (two rows); and then the lava lamp and the moon page on every
light theme, before | after -> OUTDIR/lava_light_themes.png, moon_light_themes.png. Needs the two simulators and Pillow; reads nothing outside the repo and OUTDIR."""
import os, sys, subprocess, tempfile, shutil
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "gen"))   # tools/gen: sibling tools live in other folders
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_card_themes as G
from PIL import Image, ImageDraw
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
THEMES = ["default", "aqua", "dawn", "mint"]
TRACK = "track 3; @3"
PAGES = [("bars_fire", "cfgset viz_ramp fire; %s; viz bars; @1" % TRACK), ("bars_ice", "cfgset viz_ramp ice; %s; viz bars; @1" % TRACK),
         ("bars_winamp", "cfgset viz_ramp winamp; %s; viz bars; @1" % TRACK), ("bars_rainbow", "cfgset viz_ramp rainbow; %s; viz bars; @1" % TRACK),
         ("bars_duo", "cfgset viz_ramp duo; %s; viz bars; @1" % TRACK), ("waterfall", "cfgset viz_ramp fire; %s; viz fall; @3" % TRACK),
         ("lava", "tools ambient; arena hold 1; games mode 0; games style 0; games rule 0; arena game bright 3; arena game seed 3; arena step 150; arena game rare; arena step 3"),
         ("breath", "uipush breath; arena hold 1; games mode 0; games style 0; games rule 1; arena step 90"),
         ("rings", "clock; clockface rings; @0.3"),
         ("neon_game", "games open c4; games mode 0; games style 1; games rule 0; board tap 3; board tap 4; board tap 3; board tap 2; @1"),
         ("spiro", "games open spiro; games mode 0; games style 1; games rule 0; games colour 2; arena hold 1; arena game gold 0; arena game new 2 3 0; arena step 35")]
MOON = "simtime 2026-02-24 12:00:00; @1.5; clock; clockface moon; @1"      # a waxing half moon: both the lit and the dark side show

def fixture(theme, extra):
    root = tempfile.mkdtemp(prefix="pal_")
    r = os.path.join(root, "r"); shutil.copytree(os.path.join(REPO, "sim", "fixture"), r, symlinks=True)
    shutil.rmtree(os.path.join(r, "_player", "fonts"), ignore_errors=True); os.symlink(os.path.join(REPO, "fonts"), os.path.join(r, "_player", "fonts"))
    ap = os.path.join(r, "_player", "config", "app.txt")
    if theme != "default":
        os.makedirs(os.path.join(r, "_player", "themes"), exist_ok=True)
        open(os.path.join(r, "_player", "themes", theme + ".txt"), "w").write(G.build_one(theme))
        open(ap, "a").write("theme=%s\n" % theme)
    open(ap, "a").write(extra)
    return root, r

def shoot(sim, theme, pages, out, extra="", tag=""):
    root, r = fixture(theme, extra)
    seq = ["vol 0", "screen on"]
    for name, c in pages: seq += [c, "@0.8", "shot %s/%s%s_%s.png" % (out, tag, theme, name), "uihome"]
    subprocess.run([sim, "--headless", "--root", r, "--cmd", "; ".join(seq)], capture_output=True, text=True, timeout=300)
    shutil.rmtree(root, ignore_errors=True)

def grid(cells, labels, cell, per_row, path):
    """cells: image paths in reading order, wrapped every per_row (a phone-width sheet)"""
    n = len(cells); rows = (n + per_row - 1) // per_row
    W = Image.new("RGB", (cell * per_row, cell * rows), (128, 128, 128)); d = ImageDraw.Draw(W)
    for i, (pth, lab) in enumerate(zip(cells, labels)):
        x, y = (i % per_row) * cell, (i // per_row) * cell
        W.paste(Image.open(pth).convert("RGB").resize((cell, cell)), (x, y)); d.text((x + 3, y + 3), lab, fill=(255, 0, 255))
    W.save(path)

def game_pages(st):   # the eight boards of the light-board rule, in style st (0 classic, 1 neon, 2 candy)
    g = lambda n: ["games open %s" % n, "games mode 0", "games style %d" % st, "games rule 0"]
    return [("breakout", "; ".join(g("breakout") + ["arena hold 1", "arena tap 0 0", "arena step 23"])),
            ("safe", "; ".join(g("safe") + ["games count 0", "dial game pos 35"])),
            ("tower", "; ".join(g("tower") + ["arena hold 1", "arena game gold 0", "arena tap 0 0", "arena game put 4"])),
            ("c4", "; ".join(g("c4") + ["board tap 3", "board tap 4", "board tap 3", "board tap 2"])),
            ("simon", "; ".join(g("simon") + ["games count 0", "sectors hub"])),
            ("ttt", "; ".join(g("ttt") + ["board tap 0", "board tap 4", "board tap 8"])),
            ("dominoes", "; ".join(g("dominoes") + ["games dom deal 1", "games dom gold -1", "games dom hold"])),
            ("puzzle15", "; ".join(g("puzzle15") + ["games count 0"]))]

LIGHT = [n for n, v in G.THEMES.items() if not v[3]]

def main():
    a = sys.argv[1:]; out = a[0]
    arg = lambda k, d: a[a.index(k) + 1] if k in a else d
    fp = "--fp" in a; build = "build-fp" if fp else "build"
    after = arg("--after", os.path.join(REPO, "sim", build, "sim")); before = arg("--before", after)
    os.makedirs(out, exist_ok=True); tmp = tempfile.mkdtemp(prefix="palpng_")
    for th in ["default"] + LIGHT:                       # every page of the wave, before | after, two rows of six
        shoot(before, th, PAGES, tmp, tag="b_"); shoot(after, th, PAGES, tmp, tag="a_")
    for th in ["default"] + LIGHT:
        cells = [os.path.join(tmp, "%s_%s_%s.png" % (t, th, n)) for n, _ in PAGES for t in ("b", "a")]
        grid(cells, ["%s %s %s" % (th, n, t) for n, _ in PAGES for t in ("before", "after")], 300, 6, os.path.join(out, "%s_before_after.png" % th))
    for name in ("lava", "moon"):                         # one page on every light theme: before | after pairs, three themes per row
        cells, labs = [], []
        for th in LIGHT:
            for t, sim in (("before", before), ("after", after)):
                shoot(sim, th, [(name, dict(PAGES)["lava"] if name == "lava" else MOON)], tmp, tag="%s_%s_" % (name, t))
                cells.append(os.path.join(tmp, "%s_%s_%s_%s.png" % (name, t, th, name))); labs.append("%s %s" % (th, t))
        grid(cells, labs, 360, 6, os.path.join(out, "%s_light_themes.png" % name))
    for th in ("aqua", "dawn"):                           # the boards: classic (style 0), and Neon / Candy; each sheet is two rows of four
        for st, nm in ((0, "classic"), (1, "neon"), (2, "candy")):
            pg = game_pages(st)
            for t, sim in (("before", before), ("after", after)):
                shoot(sim, th, pg, tmp, tag="g%d%s_" % (st, t))
                grid([os.path.join(tmp, "g%d%s_%s_%s.png" % (st, t, th, n)) for n, _ in pg], ["%s %s %s %s" % (th, nm, n, t) for n, _ in pg], 300, 4,
                     os.path.join(out, "games_%s_%s_%s.png" % (th, nm, t)))
    shutil.rmtree(tmp, ignore_errors=True)
if __name__ == "__main__":
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    main()
