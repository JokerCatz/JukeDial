#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Icon registry check (ICON-A; make lint-app). app/main/ui/icons.def is the registry (tools/gen/make_icons.py).
 1. every line parses; ids and code points are unique; code points are U+E000..U+E7FF (never LVGL's U+F000+); FILLED ids are < U+E400 <= LINE ids
 2. app/main/ui/icons_gen.h is what icons.def says
 3. the context map (the themes-icons design notes "Icons"): an icon array given to ui_ring_set_icons (a menu ring cell) holds FILLED icons only; a LINE icon is never a ring cell
 4. every FILLED icon is used somewhere in app/main (no unused glyph is packed into flash); LINE icons are reserved for the inline rows of ICON-B and are not counted
 5. fonts/ui-icon.ttf, when present, has a glyph for every code point (skipped without it); ui-tc / ui-sc / ui-ext own no code point of U+E000..E7FF (FIX4)
 6. scr_games.c's GAME_ICONS (a ring array filled at run time) holds FILLED icons only (FIX4)"""
import os, re, struct, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "gen"))
import make_icons
bad = []
icons = make_icons.parse()
ids, cps = {}, {}
for i, fam, src, cp, why in icons:
    if i in ids: bad.append(f"duplicate id {i}")
    if cp in cps: bad.append(f"code point U+{cp:04X}: {cps[cp]} and {i}")
    ids[i], cps[cp] = fam, i
    if not 0xE000 <= cp <= 0xE7FF: bad.append(f"{i}: U+{cp:04X} outside U+E000..U+E7FF")
    if (fam == "FILLED") != (cp < 0xE400): bad.append(f"{i}: {fam} must be in {'U+E000..E3FF' if fam == 'FILLED' else 'U+E400..E7FF'}")
try:
    make_icons.main(["--check-header"])
except SystemExit as e:
    bad.append(str(e))
src = {}
for d, _, fs in os.walk(os.path.join(ROOT, "app/main")):
    for f in fs:
        if f.endswith((".c", ".h", ".inc")) and f != "icons_gen.h":
            src[os.path.join(d, f)] = open(os.path.join(d, f), encoding="utf-8", errors="replace").read()
alltext = "\n".join(src.values())
for i, fam, *_ in icons:
    if fam == "FILLED" and f"UI_ICON_{i}" not in alltext: bad.append(f"{i}: FILLED but never used (an unused glyph is not packed into flash: remove it from icons.def)")
for path, t in src.items():
    for m in re.finditer(r"ui_ring_set_icons\([^;]*?(\b\w+)\s*,\s*[^,;]+\)\s*;", t):
        arr = m.group(1)
        d = re.search(r"\b" + arr + r"\s*\[[^\]]*\]\s*=\s*\{(.*?)\};", t, re.S)
        for u in re.findall(r"UI_ICON_(\w+)", d.group(1) if d else ""):
            if ids.get(u) != "FILLED": bad.append(f"{os.path.relpath(path, ROOT)}: ring cell icon UI_ICON_{u} is not FILLED (menu cells use the FILLED family)")
    for m in re.finditer(r"icons\[[^\]]+\]\s*=\s*UI_ICON_(\w+)", t):
        if ids.get(m.group(1)) != "FILLED": bad.append(f"{os.path.relpath(path, ROOT)}: ring cell icon UI_ICON_{m.group(1)} is not FILLED")
# FIX4 M3: scr_games.c fills its ring array from a table at run time: every `{ "<id>", UI_ICON_X }` entry of GAME_ICONS is a ring cell too
gp = os.path.join(ROOT, "app/main/ui/scr_games.c")
if os.path.exists(gp):
    gt = open(gp, encoding="utf-8").read()
    tab = re.search(r"GAME_ICONS\[\]\s*=\s*\{(.*?)\};", gt, re.S)
    if not tab: bad.append("scr_games.c: GAME_ICONS table not found (check_icons scans it for LINE icons)")
    for u in re.findall(r'\{\s*"[^"]+"\s*,\s*UI_ICON_(\w+)\s*\}', tab.group(1) if tab else ""):
        if ids.get(u) != "FILLED": bad.append(f"scr_games.c: GAME_ICONS entry UI_ICON_{u} is not FILLED (menu cells use the FILLED family)")
ttf = os.path.join(ROOT, "fonts/ui-icon.ttf")
if os.path.exists(ttf):
    sys.path.insert(0, os.path.join(ROOT, "tools", "check"))
    from check_font_glyphs import cmap_chars
    have = cmap_chars(ttf)
    for i, fam, src_, cp, why in icons:
        if cp not in have: bad.append(f"{i}: U+{cp:04X} is not in fonts/ui-icon.ttf (make icons-build)")
    # FIX4 M3: the fonts before ui-icon.ttf in the fallback chain must own no code point of the icon range (a glyph there would hide the icon)
    for other in ("ui-tc.ttf", "ui-sc.ttf", "ui-ext.ttf"):
        op = os.path.join(ROOT, "fonts", other)
        if os.path.exists(op):
            for cp in sorted(c for c in cmap_chars(op) if 0xE000 <= c <= 0xE7FF):
                bad.append(f"fonts/{other} has a glyph at U+{cp:04X}, inside the icon range U+E000..U+E7FF: it would hide the icon")
if bad:
    print("\n".join("check_icons: " + b for b in bad)); sys.exit(1)
print(f"check_icons: ok ({len(icons)} icons)")
