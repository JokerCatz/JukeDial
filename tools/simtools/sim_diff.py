#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compares the simulator's screens with a baseline directory (the board's, .local/ui_kit/base, or the simulator's own, .local/sim/base).
usage: sim_diff.py [--base DIR] [--out DIR] [--known FILE] [--times FILE] [--root DIR] [--inst] [--ext] [--safe]
Runs tools/simtools/ui_baseline.py's page list in the simulator (against the board's baseline: a fresh copy of .local/sim_root when it exists - the board's own
settings, theme and library - else sim/fixture with a printed notice; against any other baseline, e.g. the simulator's own: always sim/fixture, the data
make sim-shots / make sim-base use), then prints one line per page: "<name> diff_px=<n> bbox=<x,y,w,h>". Difference images go to --out
(.local/sim/diff/<name>.png: the baseline dimmed, differing pixels red). Pages listed in the known-differences file (sim/known_diffs.txt; name + reason per line) are
reported but do not fail; any other page with diff_px>0 makes the exit code 1, and so does a page present on one side only or a baseline with no PNG to compare.
The times file (sim/diff_times.txt; name + UTC "YYYY-MM-DD HH:MM:SS[.mmm]" per line) sets the virtual clock to that instant right before the page's screenshot
(a clock page shot on the board at a known moment). --inst: the instrument app (sim --mode inst, a second SimCon) is shot as well and its inst_* pictures are compared;
without it the inst_* baselines are skipped. --ext: the same for the external mode (sim --mode ext, ext_*; the retired usb_* pictures of the old USB page are always skipped); it runs also when the baseline holds no usb_*
picture yet (the board baseline: its pictures are new, EXTRA with a known_diffs line). --safe: the same for the safe-mode screen (sim --mode safe, the one
picture `safe`; the board has no baseline picture of it, so on the board half it is EXTRA with a known_diffs line). Page sets: Makefile's SIM_PAGESETS holds the
three flags for sim-shots, sim-base and both halves of sim-diff (test tcfix_pagesets_match fails when one of them does not use it).
A known_diffs name with a `*` (game_*) stands for every picture of that shape. A picture the simulator makes that the baseline does not have (new since the board shot) is an error unless known_diffs.txt has a line for it; a baseline
picture the simulator no longer makes is an error unless its known_diffs reason starts with "retired". A baseline of one app only (no player picture, or
with --inst no instrument picture) skips the other app's pass and says so.
A listed page that is now pixel-identical is printed as "listed but identical: <name>" and counted in the summary line, without failing the run (a page
that differs only by data or chance can match); its line may be stale (final review M-3).
Both files are used only with the board's baseline; for the simulator's own set (BASE=.local/sim/base) every page must be identical."""
import argparse, filecmp, fnmatch, os, shutil, struct, sys, zlib
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import png_diff


def read_table(path):
    out = {}
    if path and os.path.exists(path):
        for l in open(path, encoding="utf-8"):
            l = l.strip()
            if l and not l.startswith("#"):
                name, _, rest = l.partition(" ")
                if name in out:   # parallel branches each adding a line for one page: the later line silently replaced the earlier (W1, 2026-11-15)
                    sys.exit(f"sim_diff: {os.path.relpath(path, REPO)} lists {name} twice: fold the reasons into one line")
                out[name] = rest.strip()
    return out


def write_png(path, w, h, rows):
    raw = b"".join(b"\0" + r for r in rows)
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def _check_rgb8(path):
    """The old reader (png_diff.read_png) asserted 8-bit RGB: so does this (Pillow's convert would take RGBA / palette / 16-bit and drop what it cannot show)."""
    with open(path, "rb") as f: h = f.read(26)
    if h[:8] != b"\x89PNG\r\n\x1a\n" or h[12:16] != b"IHDR": raise AssertionError(f"{path}: not a PNG")
    if h[24] != 8 or h[25] != 2: raise AssertionError(f"{path}: only 8-bit RGB")


def _compare_pil(a, b, outpng):
    """compare() with Pillow (C loops; the pure-Python loop below took 0.15 s a picture); False when Pillow is missing. Same result and same difference image:
    a pixel differs when ANY of its R, G, B bytes differs (I6: a luma conversion of the difference called a 1-level blue change identical)."""
    try: from PIL import Image, ImageChops
    except ImportError: return False
    _check_rgb8(a); _check_rgb8(b)
    if filecmp.cmp(a, b, shallow=False):   # the same bytes: no pixel work (the fast path of the old loop kept)
        if os.path.exists(outpng): os.remove(outpng)
        return 0, (0, 0, 0, 0)
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if ia.size != ib.size: return None
    r, g, bl = (band.point(lambda v: 255 if v else 0) for band in ImageChops.difference(ia, ib).split())
    m = ImageChops.lighter(ImageChops.lighter(r, g), bl)
    bb = m.getbbox()
    if bb is None:
        if os.path.exists(outpng): os.remove(outpng)
        return 0, (0, 0, 0, 0)
    n = m.histogram()[255]
    dim = ia.point(lambda v: v // 3)
    dim.paste(Image.new("RGB", ia.size, (255, 0, 0)), mask=m)
    dim.save(outpng, compress_level=6)
    return n, (bb[0], bb[1], bb[2] - bb[0], bb[3] - bb[1])


def compare(a, b, outpng):
    """(diff_px, bbox) of two PNGs, or None when the sizes differ; writes the difference image."""
    r = _compare_pil(a, b, outpng)
    if r is not False: return r
    if filecmp.cmp(a, b, shallow=False):   # same encoder, same bytes: no pixel loop (29 of sim-diff's 55 s were this loop on 194 identical pictures)
        if os.path.exists(outpng): os.remove(outpng)
        return 0, (0, 0, 0, 0)
    (w, h, ra), (w2, h2, rb) = png_diff.read_png(a), png_diff.read_png(b)
    if (w, h) != (w2, h2): return None
    n, x0, y0, x1, y1, rows = 0, w, h, -1, -1, []
    for y in range(h):
        row = bytearray(w * 3)
        for x in range(w):
            pa, pb = ra[y][3 * x:3 * x + 3], rb[y][3 * x:3 * x + 3]
            if pa != pb:
                n += 1; x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
                row[3 * x:3 * x + 3] = b"\xff\x00\x00"
            else:
                row[3 * x:3 * x + 3] = bytes(v // 3 for v in pa)
        rows.append(bytes(row))
    if n: write_png(outpng, w, h, rows)
    elif os.path.exists(outpng): os.remove(outpng)
    return n, ((x0, y0, x1 - x0 + 1, y1 - y0 + 1) if n else (0, 0, 0, 0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default=os.path.join(REPO, ".local", "ui_kit", "base"))
    ap.add_argument("--out", default=os.path.join(REPO, ".local", "sim", "diff"))
    ap.add_argument("--known", help="default: sim/known_diffs.txt for the board's baseline, none for any other (the simulator's own set must match exactly)")
    ap.add_argument("--times", help="default: sim/diff_times.txt for the board's baseline, none for any other")
    ap.add_argument("--root", help="data root to copy; default: .local/sim_root for the board's baseline (if it exists), sim/fixture for any other")
    ap.add_argument("--shots", default=os.path.join(REPO, ".local", "sim", "diff_shots"))
    ap.add_argument("--inst", action="store_true", help="also run the instrument app (sim --mode inst) and compare its inst_* pictures")
    ap.add_argument("--ext", action="store_true", help="also run the external mode (sim --mode ext) and compare its ext_* pictures")
    ap.add_argument("--safe", action="store_true", help="also run the safe-mode screen (sim --mode safe) and compare its picture `safe`")
    ap.add_argument("--args", default="", help="extra simulator arguments, e.g. '--time 2026-11-11T07:02:00'")
    o = ap.parse_args()
    board = os.path.realpath(o.base) == os.path.realpath(os.path.join(REPO, ".local", "ui_kit", "base"))
    if o.known is None and board: o.known = os.path.join(REPO, "sim", "known_diffs.txt")
    if o.times is None and board: o.times = os.path.join(REPO, "sim", "diff_times.txt")
    if not os.path.isdir(o.base): sys.exit(f"sim_diff: baseline directory {o.base} does not exist")
    names = sorted(f[:-4] for f in os.listdir(o.base) if f.endswith(".png") and (o.inst or not f.startswith("inst_")) and (o.ext or not f.startswith("ext_")) and not f.startswith("usb_") and (o.safe or f != "safe.png"))
    if not names: sys.exit(f"sim_diff: {o.base} holds no PNG to compare with")
    # a baseline of one app only (the board's `ui_baseline.py <port> OUT --inst` shoots only the instrument): the other app's pass is skipped, not reported as EXTRA
    player = any(not n.startswith(("inst_", "ext_", "usb_")) and n != "safe" for n in names)
    inst = o.inst and any(n.startswith("inst_") for n in names)
    if not player: print("sim_diff: the baseline holds no player picture: the player pass is skipped", flush=True)
    if o.inst and not inst: print("sim_diff: the baseline holds no instrument picture: the instrument pass is skipped", flush=True)
    if o.root is None and board: o.root = os.path.join(REPO, ".local", "sim_root")
    if o.root is None:
        print("sim_diff: data = a copy of sim/fixture (the simulator's own baseline is made from it)", flush=True)
    elif os.path.isdir(o.root): print(f"sim_diff: data = a copy of {os.path.relpath(o.root, REPO)} (the board's own settings and library)", flush=True)
    else: print(f"sim_diff: NOTICE {os.path.relpath(o.root, REPO)} does not exist (make sim-root): using the synthetic fixture; pages with the board's theme or data will differ", flush=True)
    root = o.root if o.root and os.path.isdir(o.root) else None
    shutil.rmtree(o.shots, ignore_errors=True); os.makedirs(o.shots)
    os.makedirs(o.out, exist_ok=True)
    import ui_baseline
    rc = ui_baseline.sim_run_all(o.shots, root, o.args.split(), None, read_table(o.times), inst, player, o.ext, o.safe)
    if rc: sys.exit(rc)
    known = read_table(o.known)
    for pat in [k for k in known if "*" in k]:   # a line `game_* ...` stands for every picture of that shape (the games of games.def: one line, not one per game)
        for n in {f[:-4] for f in os.listdir(o.shots) if f.endswith(".png")} | set(names):
            if fnmatch.fnmatchcase(n, pat): known.setdefault(n, known[pat])
        del known[pat]
    bad = 0
    same = []           # listed in known_diffs but pixel-identical now: printed, not failed (a data/random page can match by chance)
    for name in sorted(f[:-4] for f in os.listdir(o.shots) if f.endswith(".png") and f[:-4] not in names):
        if name in known:       # a new picture the board baseline does not have yet: a known_diffs line says why (re-shot at the batch close)
            print(f"{name} EXTRA (known: {known[name]})")
        else:
            print(f"{name} EXTRA: the simulator made it, the baseline has no counterpart"); bad += 1
    for name in names:
        s = os.path.join(o.shots, name + ".png")
        if not os.path.exists(s):
            if known.get(name, "").startswith("retired"):   # a picture the simulator no longer makes (the page was replaced): its "retired ..." line says so until the board baseline drops it
                print(f"{name} MISSING (known: {known[name]})"); continue
            print(f"{name} MISSING in the simulator's set"); bad += 1; continue
        r = compare(os.path.join(o.base, name + ".png"), s, os.path.join(o.out, name + ".png"))
        if r is None:
            print(f"{name} SIZE differs"); bad += 1; continue
        n, bb = r
        tag = "" if n == 0 else f"  (known: {known[name]})" if name in known else "  <-- DIFF"
        print(f"{name} diff_px={n} bbox={bb[0]},{bb[1]},{bb[2]},{bb[3]}{tag}")
        if n and name not in known: bad += 1
        if not n and name in known: same.append(name)
    extra_ok = {f[:-4] for f in os.listdir(o.shots) if f.endswith(".png")}
    stale = sorted(set(known) - set(names) - extra_ok)
    if stale: print("warning: known_diffs.txt lists pages that are not compared: " + " ".join(stale), file=sys.stderr)
    for name in same: print(f"listed but identical: {name}")
    print(f"{len(names)} pages, {bad} not ok ({sum(1 for n in names if n in known)} listed in known_diffs, {len(same)} listed but identical)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
