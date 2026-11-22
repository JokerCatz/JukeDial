#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The ten colour themes shipped on the card (_player/themes/<name>.txt), written from ONE table below and checked for contrast.
  gen_card_themes.py DIR          write <name>.txt for every theme into DIR (make card-zip calls build() instead)
  gen_card_themes.py --check      WCAG contrast of every theme (exit 1 on a failure; make lint-app); prints one summary line
  gen_card_themes.py --check --legacy   the same check against the pre-fix drawing rules: it must fail (proof that the rows see the old defects)
  gen_card_themes.py --table      the contrast numbers per theme (markdown, for THEMES-report.md)
A theme here is the nine core colours + warn / good / purple / gold; every other token (dice, kuji, cards, dials, the games' and ambient palettes ...)
is derived from them by the rules in derive(), then nudged until it reads (ensure()). Palettes: an internal note (sources and licences).
Effective colours follow ui_theme.h: SEL = mix(a1, panel, 110), SYS = mix(a2, panel, 80), ON = mix(on, panel, 110), CARD = mix(fg, panel, 30)."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_theme_template as T

# name: (zh, cn, en, dark?, source, core bg panel panel2 fg dim a1 a2 a3 on, warn, good, purple, gold)
H = lambda s: int(s.lstrip("#"), 16)
THEMES = {
 "carbon":  ("碳纖維", "碳纤维", "Carbon", True,  "chipchill skins.py CARBON",
             "0C0C0E 18181C 222228 EBEBF0 A0A0AA FF7800 00C8E6 FFC800 50C878", "FF4646 50C878 A064FF FFC800"),
 "caution": ("警示", "警示", "Caution", True, "chipchill skins.py CAUTION",
             "161022 241C36 342848 F0ECF8 AAA0BE FA8C00 8CFF50 FFDC00 60C830", "FF3C3C 8CFF50 9C64E8 FFDC00"),
 "night":   ("夜", "夜", "Night", True, "sleepmixer Theme.kt (Night)",
             "10161E 1A222C 26303B E0E3E8 98A3AF D4B068 B0C9E8 C3B8E8 7FA8E0", "FF6B6B 8FD6A0 C3B8E8 E8C983"),
 "sfc":     ("超級任天堂·日", "超级任天堂·日", "Super Famicom", True, "chipchill skins.py BAKE sfc",
             "242430 3E3E4C 505062 F6F6FA D0D0DA D9A800 3C9CE8 F0506A 2FC878", "E8344F 2FC878 9A6CD8 FAC800"),
 "snes":    ("超級任天堂·美", "超级任天堂·美", "Super NES", True, "chipchill skins.py BAKE snes",
             "242430 3E3E4C 505062 F6F6FA D0D0DA 8A7FD6 F0585C F7C948 52C878", "F0585C 52C878 B59CFF F7C948"),
 "mint":    ("薄荷", "薄荷", "Mint", False, "rgxx palettes/mint.ini",
             "EEFAF3 FBFFFD D9EFE4 1E3A30 3C594B 0F7A6A B4453C 3A7AB0 B4453C", "C0392B 2E8B57 7A5BB0 9A6A00"),
 "hanami":  ("花見", "花见", "Hanami", False, "rgxx palettes/hanami.ini",
             "FDF0F3 FFFAFB F4DDE4 3C2030 6E4A5C C0356A 2F7D62 C4761A 2F7D62", "C62828 2F7D62 7A4FB0 9A5A00"),
 "aqua":    ("水藍", "水蓝", "Aqua", False, "chipchill skins.py AQUA",
             "F4FAFA FFFFFF ECF7F6 1E282C 46565A 1E968E D6447F B07A00 D6447F", "D63C3C 2E9A4E 7A50B8 A87400"),
 "dawn":    ("黎明", "黎明", "Dawn", False, "sleepmixer Theme.kt (Dawn)",
             "FAF8F4 FFFFFF E7E4DC 1B1C1E 5A5C62 4A5D8A 8A6A1E 66558E 9A6A12", "BA1A1A 2E7D4F 66558E 8A6A00"),
 "cozy":    ("暖窩", "暖窝", "Cozy", False, "chipchill skins.py COZY",
             "FAF2E2 FFFCF6 F2E6CE 463228 634D3C 78543C C85A5A A87000 5A8A4A", "C0463C 5A8A4A 8F5A8F A87000"),
}
CORE = ["bg", "panel", "panel2", "fg", "dim", "a1", "a2", "a3", "on"]

# ---- colour arithmetic ----
def ch(c): return (c >> 16 & 255, c >> 8 & 255, c & 255)
def rgb(r, g, b): return (max(0, min(255, int(round(r)))) << 16) | (max(0, min(255, int(round(g)))) << 8) | max(0, min(255, int(round(b))))
def mix(a, b, t):   # t of b over a
    return rgb(*[x * (1 - t) + y * t for x, y in zip(ch(a), ch(b))])
def lvmix(c1, c2, m):   # lv_color_mix: c1 weighted m/255
    return rgb(*[(x * m + y * (255 - m)) // 255 for x, y in zip(ch(c1), ch(c2))])
def lum(c):
    f = lambda v: v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = [f(v / 255) for v in ch(c)]
    return 0.2126 * r + 0.7152 * g + 0.0722 * b
def cr(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)
def ensure(c, bgc, ratio, toward):
    """c nudged toward `toward` (white on a dark ground, black on a light one) until it reads on bgc at `ratio`"""
    for i in range(21):
        if cr(c, bgc) >= ratio: return c
        c = mix(c, toward, 0.08)
    return c

# ---- the firmware's text rules (ui_theme.c ui_text_on / ui_ink_on / ui_theme_is_light), mirrored so the lint judges what the device draws ----
def yiq(c): r, g, b = ch(c); return (r * 299 + g * 587 + b * 114) // 1000
def text_on(surface, fg, bg):
    cf, cb = cr(fg, surface), cr(bg, surface)
    best = max(cf, cb)
    if best < 4.5:
        k = 0x000000 if cr(0, surface) >= cr(0xFFFFFF, surface) else 0xFFFFFF
        if cr(k, surface) > best: return k
    return fg if cf >= cb else bg
def ink_on(want, surface, fg, bg):
    return want if cr(want, surface) >= 3.0 else text_on(surface, fg, bg)
GAME_DEF = {"game_neon": (0x0B0B16, 0x34346A, 0x19E6FF, 0xFF4FD8, 0xFFF27A, 0x7DF9FF, 0xFF2BD6, 0x19E6FF, 0xFFF01F, 0x39FF14),    # ui_tokens.inc game_neon / candy / gold (cell line A B hi fx, four sectors)
            "game_candy": (0x3B2F4A, 0x5A4870, 0xFF8FAB, 0x8FD8FF, 0xFFE66D, 0xFFD1E0, 0xFF9AA2, 0xA0C4FF, 0xFFE08A, 0x9BE7B0),
            "game_gold": (0x2A2A2A, 0x444444, 0xFFC43D, 0xD9DEE8, 0xFFF1A8, 0xFFD76A, 0xFFC43D, 0xD9DEE8, 0xD08A4A, 0xF2A7C0)}
def hue_on(want, surface, fg):   # ui_hue_on (a light theme): darkened toward fg (lv_color_mix(UI_FG, want, 48) per step, 12 steps) until it reads 3:1 on the surface
    for _ in range(12):
        if cr(want, surface) >= 3.0: break
        want = lvmix(fg, want, 48)
    return want
SECTOR_DEF = {"game_neon": (0xFF2BD6, 0x19E6FF, 0xFFF01F, 0x39FF14), "game_candy": (0xFF9AA2, 0xA0C4FF, 0xFFE08A, 0x9BE7B0)}   # ui_tokens.inc, the entries 6..9 a light theme leaves alone

def parse_theme(name):
    zh, cn, en, dark, src, core, extra = THEMES[name]
    c = dict(zip(CORE, [H(v) for v in core.split()]))
    w, g, p, gold = [H(v) for v in extra.split()]
    return c, dict(warn=w, good=g, p=p, gold=gold), dark

def derive(name):
    """{token key (palette entry as key or key.i): rgb}"""
    c, x, dark = parse_theme(name)
    bg, pn, p2, fg, dim, a1, a2, a3, on = [c[k] for k in CORE]
    warn, good, pur, gold = x["warn"], x["good"], x["p"], x["gold"]
    toward = 0xFFFFFF if dark else 0x000000
    need = lambda col, ratio=3.0, ground=None: ensure(col, bg if ground is None else ground, ratio, toward)
    t = {}
    def put(key, vals):
        for i, v in enumerate(vals): t[key if len(vals) == 1 else "%s.%d" % (key, i)] = v
    put("warn", [warn]); put("gold", [gold])
    face = fg if dark else pn
    put("dice", [face, bg if dark else fg, ensure(a2, face, 3.0, 0x000000 if lum(face) > 0.4 else 0xFFFFFF), bg if dark else fg])
    put("slot_lines", [need(v) for v in (warn, gold, a1, on, pur)])
    put("kuji", [need(v) for v in (gold, a2, on, a1, pur, dim)])
    put("kuji_stick", [mix(0xE8D8B0, bg, 0.0) if dark else 0x9A6B30])
    put("jiao", [need(warn), mix(warn, 0x000000, 0.35)])
    put("rarity", [dim, a1, pur, gold])
    put("tarot_rev", [need(pur)])
    put("quiz", [need(good), need(warn)])
    put("moon", [mix(gold, 0xFFFFFF, 0.55), p2] if dark else [gold, mix(fg, bg, 0.7)])
    put("inst_voices", [need(v) for v in (pur, good, gold, warn, mix(a1, good, 0.5), mix(a2, pur, 0.5))])
    cface = mix(fg, gold, 0.12) if dark else pn
    put("cards", [cface, mix(fg, bg, 0.5) if dark else mix(fg, bg, 0.45), bg if dark else fg, mix(a1, bg, 0.55), mix(gold, 0xFFFFFF, 0.2),
                  mix(gold, 0x000000, 0.85), mix(gold, 0xFFFFFF, 0.55)])
    dface = bg if dark else p2
    put("dial", [dface] + [mix(dface, fg, f) for f in (0.30, 0.50, 0.75, 1.0)] + [p2 if dark else mix(p2, fg, 0.12), a2, a2])
    put("game_classic", [need(v) for v in (warn, a1, gold, good)])
    put("game_field", [need(a2), need(gold), fg] if dark else [ensure(a2, mix(bg, fg, 0.10), 3.0, 0x000000), ensure(gold, mix(bg, fg, 0.10), 3.0, 0x000000), fg])
    if not dark:   # the classic game board on a light page (a LIGHT board with DARK content): cells a tone of fg over the page, lines a darker one,
                   # pieces darkened toward black until they read on the cells (3:1)
        cell = mix(bg, fg, 0.10); line = mix(bg, fg, 0.28)
        lt = lambda v: ensure(v, cell, 3.0, 0x000000)
        pa_ = lt(a1)
        d2 = lambda v: sum((x - y) ** 2 for x, y in zip(ch(pa_), ch(v)))
        pb_ = max((lt(v) for v in (a2, on, a3)), key=d2)   # side B: the accent farthest from side A
        put("game_board", [cell, line, pa_, pb_, lt(a3), lt(mix(a1, fg, 0.3))])
    else:          # a dark theme keeps its panel as the board; side B uses the raw `on` (the effective UI_ON of the default board is blended toward the panel and too dim to read as a piece)
        put("game_board", [pn, p2, a1, ensure(on, pn, 3.0, toward), a3, a1])
    if dark:   # a light theme leaves the games' own boards at their built-in (dark, self-contained) colours; its ambient scenes have a light set below
        empty = mix(bg, 0x000000, 0.35); grid = mix(bg, a1, 0.30)
        put("game_neon", [empty, grid, a1, a2, a3, mix(a1, 0xFFFFFF, 0.5), a2, a1, a3, good])
        pastel = lambda v: mix(v, 0xFFFFFF, 0.30)
        put("game_candy", [mix(bg, pur, 0.30), mix(bg, pur, 0.50), pastel(a2), pastel(a1), pastel(a3), mix(a2, 0xFFFFFF, 0.7), pastel(a2), pastel(a1), pastel(a3), pastel(good)])
        base = mix(bg, 0x000000, 0.35)
        def amb(scene, style):
            q = (a1, a2, a3) if style == 1 else (a2, a1, a3) if style == 2 else tuple(mix(v, fg, 0.30) for v in (a1, a2, a3))
            x1, x2, x3 = q
            if scene == "lava":  v = [base, mix(x1, bg, 0.6), x2, mix(x2, x3, 0.5), x3, fg, fg]
            elif scene == "fire": v = [base, mix(fg, bg, 0.8), x2, mix(x2, x3, 0.5), x3, fg, mix(x3, bg, 0.65)]
            elif scene == "night": v = [base, mix(pur, bg, 0.55), pur, x2, x3, fg, fg]
            else:                 v = [base, mix(x3, bg, 0.35), x2, x1, mix(x1, fg, 0.6), gold, mix(base, 0x000000, 0.5)]
            put("amb_%s_%d" % (scene, style), v)
        for sc in ("lava", "fish", "fire", "night"):
            for st in (1, 2, 3): amb(sc, st)
    else:      # a light theme: the ambient scenes get their own light set (ui/ambient_tone.c: authored at full strength, the default brightness shows them as written).
               # A soft tinted disc on the light page, the blob / flame / fish tones inked darker until they read on it (3:1); highlights are the panel colour
        base = mix(bg, fg, 0.10)
        ink = lambda v: ensure(v, base, 3.0, 0x000000)
        def amb(scene, style):
            q = (a1, a2, a3) if style == 1 else (a2, a1, a3) if style == 2 else tuple(mix(v, fg, 0.30) for v in (a1, a2, a3))
            x1, x2, x3 = [ink(v) for v in q]
            if scene == "lava":   v = [base, mix(x1, base, 0.55), x2, mix(x2, x3, 0.5), x3, pn, pn]
            elif scene == "fire": v = [base, mix(fg, base, 0.7), x2, mix(x2, x3, 0.5), x3, pn, mix(x3, base, 0.5)]
            elif scene == "night": v = [base, mix(ink(pur), base, 0.55), ink(pur), x2, x3, pn, pn]
            else:                 v = [base, mix(x3, base, 0.35), x2, x1, mix(x1, fg, 0.3), ink(gold), mix(base, fg, 0.6)]
            put("amb_%s_%d" % (scene, style), v)
        for sc in ("lava", "fish", "fire", "night"):
            for st in (1, 2, 3): amb(sc, st)
    return t

def check_theme(name, legacy=False):
    """[(label, ratio, minimum)], core and derived tokens. legacy=True judges the PRE-fix drawing rules (UI_FG on every cell and result card, UI_BG on a lit sector,
    LVGL's white button text in light mode, the result card's accents as they were) so the new rows can be shown to fail on them (--legacy)"""
    c, x, dark = parse_theme(name)
    bg, pn, p2, fg, dim, a1, a2, a3, on = [c[k] for k in CORE]
    t = derive(name)
    sel, sysc, onc, card = lvmix(a1, pn, 110), lvmix(a2, pn, 80), lvmix(on, pn, 110), lvmix(fg, pn, 30)
    R = []
    for lab, a, b in (("fg/bg", fg, bg), ("fg/panel", fg, pn), ("fg/panel2", fg, p2), ("fg/SEL", fg, sel), ("fg/CARD", fg, card), ("fg/SYS", fg, sysc), ("fg/ON", fg, onc),
                      ("dim/panel2", dim, p2)):
        R.append((lab, cr(a, b), 4.5))
    for lab, a, b in (("dim/bg (QS status line)", dim, bg), ("dim/panel (ring centre text)", dim, pn)):
        R.append((lab, cr(a, b), 6.0))
    for lab, a, b in (("a1/bg", a1, bg), ("a2/bg", a2, bg), ("a3/bg", a3, bg), ("a1/panel", a1, pn), ("a2/panel", a2, pn), ("a3/panel", a3, pn), ("on/panel", on, pn),
                      ("warn/bg", t["warn"], bg), ("gold/bg", t["gold"], bg)):
        R.append((lab, cr(a, b), 3.0))
    for k in ("kuji", "slot_lines", "inst_voices"):
        n = 6 if k != "slot_lines" else 5
        for i in range(n): R.append(("%s.%d/bg" % (k, i), cr(t["%s.%d" % (k, i)], bg), 3.0))
    for k in ("quiz.0", "quiz.1", "tarot_rev", "game_classic.0", "game_classic.1", "game_classic.2", "game_classic.3", "game_field.0", "game_field.1", "jiao.0"):
        R.append((k + "/bg", cr(t[k], bg), 3.0))
    R.append(("dice pip/face", cr(t["dice.1"], t["dice.0"]), 4.5)); R.append(("dice lit/face", cr(t["dice.2"], t["dice.0"]), 3.0))
    R.append(("cards pips/face", cr(t["cards.2"], t["cards.0"]), 4.5)); R.append(("cards gold pips/face", cr(t["cards.5"], t["cards.4"]), 4.5))
    R.append(("dial needle/face", cr(t["dial.6"], t["dial.0"]), 3.0)); R.append(("dial tick4/face", cr(t["dial.4"], t["dial.0"]), 3.0))
    # the classic game board (game_style_palette): cells, lines, pieces; the theme's own panel/panel2/a1/on/a3 unless the theme sets game_board
    gb = [t.get("game_board.%d" % i) for i in range(6)]
    cell, line, pa, pb, phi, pfx = [g if g is not None else d for g, d in zip(gb, (pn, p2, a1, onc, a3, a1))]
    if not dark:   # a light board: distinguishable from the page, the lines a darker tone of it
        R.append(("board cell/bg (distinct)", cr(cell, bg), 1.1)); R.append(("board line/cell (distinct)", cr(line, cell), 1.2))
    for lab, v in (("A", pa), ("B", pb), ("hi", phi), ("fx", pfx)): R.append(("board %s/cell" % lab, cr(v, cell), 3.0))
    if not dark:   # the light ambient scenes (ui/ambient_tone.c): the scene disc differs from the page, the body tones (entries 2..4) read on the disc
        for sc in ("lava", "fish", "fire", "night"):
            for st in (1, 2, 3):
                k = "amb_%s_%d" % (sc, st)
                R.append(("%s disc vs page" % k, cr(t[k + ".0"], bg), 1.1))
                for i in (2, 3, 4): R.append(("%s.%d/disc" % (k, i), cr(t["%s.%d" % (k, i)], t[k + ".0"]), 3.0))
    # firmware rule: light mode = background luminance (YIQ) >= 128; the derivation rules above must be the same kind
    assert (yiq(bg) >= 128) == (not dark), "%s: marked %s but the firmware (ui_theme_is_light, YIQ %d) reads it as %s" % (name, "dark" if dark else "light", yiq(bg), "light" if yiq(bg) >= 128 else "dark")
    tx = (lambda surf: fg) if legacy else (lambda surf: text_on(surf, fg, bg))
    ik = (lambda want, surf: want) if legacy else (lambda want, surf: ink_on(want, surf, fg, bg))
    # text and pieces on a game_board cell: the breakout paddle / ball, the safe's needle, the tower slider (ui/game_style.c) and the hub text (ui/ui_sectors.c)
    R.append(("paddle/ball/needle/hub text on cell", cr(tx(cell), cell), 3.0))
    # the result card (ui/game_result.c): mix(cell, bg, 160) (a light theme: mix(cell, fg, 160)); moves line, title, best line, hint (dim or the accent)
    rc = lvmix(cell, bg, 160) if legacy or dark else lvmix(cell, pn, 120)   # a light theme: a card a step lighter than its light board
    R.append(("result moves line/card", cr(tx(rc), rc), 4.5))
    R.append(("result title (hi)/card", cr(ik(phi, rc), rc), 3.0))
    R.append(("result best line (hi)/card", cr(ik(phi, rc), rc), 3.0)); R.append(("result hint (dim)/card", cr(ik(dim, rc), rc), 4.5)); R.append(("result badge line (a)/card", cr(ik(pa, rc), rc), 3.0))
    # a lit sector with a game style (ui/ui_sectors.c colours()): mix(white, sector, 40) and the text on it
    for sty, key in (("classic", "game_classic"), ("neon", "game_neon"), ("candy", "game_candy")):
        secs = [t.get("%s.%d" % (key, i + (0 if key == "game_classic" else 6))) for i in range(4)]
        if key != "game_classic": secs = [v if v is not None else d for v, d in zip(secs, SECTOR_DEF[key])]
        if not dark and not legacy: secs = [hue_on(v, bg, fg) for v in secs]   # ui/game_style.c col(): a light theme darkens the sectors until they read on the page
        for i, k in enumerate(secs):
            fill = lvmix(0xFFFFFF if dark or legacy else fg, k, 40)
            R.append(("lit sector %s.%d text/fill" % (sty, i), cr(bg if legacy else text_on(fill, fg, bg), fill), 3.0))
    if not dark and not legacy:   # Neon / Candy / Gold on a light theme (ui/game_style.c field_of): the field is the page's tones, the marks are darkened until they read on its cells
        fcell, fline = lvmix(fg, bg, 26), lvmix(fg, bg, 72)
        R.append(("fw field cell/bg (distinct)", cr(fcell, bg), 1.1)); R.append(("fw field line/cell (distinct)", cr(fline, fcell), 1.2))
        for key, d in GAME_DEF.items():
            for lab, v in zip(("A", "B", "hi", "fx"), d[2:6]): R.append(("%s %s/field cell" % (key, lab), cr(hue_on(v, fcell, fg), fcell), 3.0))
        fgm = t["game_field.0"], t["game_field.1"], 0xFFFFFF   # the field games' brick that bursts, the golden brick, the ball
        for lab, v in zip(("boom", "gold brick", "ball"), fgm): R.append(("%s/field cell" % lab, cr(hue_on(v, fcell, fg), fcell), 3.0))
    # button labels: LVGL's primary-colour buttons carry white text; a light theme sets UI_FG instead (ui_button / ui_list_row / back_button), the labels sit on SEL / SYS / panel2
    for lab, fill in (("SEL", sel), ("SYS", sysc), ("panel2", p2)):
        R.append(("button label/%s" % lab, cr(0xFFFFFF if legacy and not dark else fg, fill), 3.0))
    R.append(("board A/B colour distance", sum((x - y) ** 2 for x, y in zip(ch(pa), ch(pb))) ** 0.5, 60.0))
    # the `on` rule of gen_themes.py: hue 60 degrees or more from a1
    import colorsys
    hue = lambda col: colorsys.rgb_to_hsv(*[v / 255 for v in ch(col)])[0] * 360
    d = abs(hue(on) - hue(a1)); d = min(d, 360 - d)
    R.append(("on hue vs a1 (deg)", d, 60.0))
    return R

def build_one(name):
    zh, cn, en, dark, src, _, _ = THEMES[name]
    c, x, _ = parse_theme(name)
    t = derive(name)
    known = {}
    core, toks = T.parse()
    for key, n, vs, _, _ in toks:
        for i in range(n): known[key if n == 1 else "%s.%d" % (key, i)] = True
    for k in t: assert k in known, "%s: unknown token %s" % (name, k)
    nm = lambda v: v.replace(" ", "_")
    out = ["# %s（%s）/ %s / %s — %s\n" % (zh, "dark" if dark else "light", cn, en, "暗色主題" if dark else "淺色主題"),
           "# 配色來源 / palette source: %s。由 tools/gen/gen_card_themes.py 產生 / generated by tools/gen/gen_card_themes.py.\n" % src,
           "# 用法 / use: 複製到 _player/themes/，app.txt 寫 theme=%s / put theme=%s in app.txt.\n" % (name, name), "base = neon\n", "name_tc = %s\n" % nm(zh), "name_sc = %s\n" % nm(cn), "name_en = %s\n" % nm(en)]
    for k in CORE: out.append("%s = #%06X\n" % (k, c[k]))
    for k, v in t.items(): out.append("%s = #%06X\n" % (k, v))
    return "".join(out)

def build():
    """{themes/<name>.txt: text}"""
    return {"themes/%s.txt" % n: build_one(n) for n in THEMES}

def fails(legacy=False):
    bad = []
    for n in THEMES:
        for lab, r, mn in check_theme(n, legacy):
            if r + 1e-9 < mn: bad.append("%s %s %.2f < %.1f" % (n, lab, r, mn))
    return bad

def table():
    out = ["| theme | kind | fg/bg | fg/SEL | dim/panel2 | a1/bg | a2/bg | a3/bg | on/panel | worst body | worst glyph |", "|---|---|---|---|---|---|---|---|---|---|---|"]
    for n in THEMES:
        R = {l: r for l, r, m in check_theme(n)}
        body = min(r for l, r, m in check_theme(n) if m == 4.5); glyph = min(r for l, r, m in check_theme(n) if m == 3.0)
        out.append("| %s | %s | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f |" % (n, "dark" if THEMES[n][3] else "light", R["fg/bg"], R["fg/SEL"], R["dim/panel2"], R["a1/bg"], R["a2/bg"], R["a3/bg"], R["on/panel"], body, glyph))
    return "\n".join(out)

if __name__ == "__main__":
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    if "--table" in sys.argv: print(table())
    elif "--check" in sys.argv:
        bad = fails("--legacy" in sys.argv)
        if bad: print("\n".join(bad)); sys.exit(1)
        print("%d card themes ok (contrast: body text >= 4.5, glyphs >= 3)" % len(THEMES))
    elif len(sys.argv) > 1:
        os.makedirs(sys.argv[1], exist_ok=True)
        for p, s in build().items(): open(os.path.join(sys.argv[1], os.path.basename(p)), "w", encoding="utf-8").write(s)
    else: print(table())
