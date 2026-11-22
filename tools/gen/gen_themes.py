#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Generate components/player_core/src/pc_theme_table.inc (spec 5.7): every DARK palette from
rgxx (built-ins + docs/mockups/round7/palettes/*.ini) and hiby_r3_pro_2 (EVA + built-in default),
values copied from the source files.

The second accent `on` (spec fx apps 6.6) is computed, never hand-edited: the first of a2, a3, 0x3ddc84, 0xffb000, 0xff52a0 whose hue is
>= 60 degrees from a1, HSV saturation >= 0.25 and WCAG contrast with panel >= 3:1; no candidate qualifies -> exit 1 naming the theme.

usage: gen_themes.py <rgxx dir> <hiby-shell dir> <out.inc>
       gen_themes.py --check <table.inc>      (every row has an `on` that follows the rule; prints "<n> themes ok")"""
import colorsys, glob, os, re, sys

def hx(s):
    return int(s.strip().lstrip('#'), 16)

def mix(a, b, t):   # t of b over a
    ch = lambda c, s: (c >> s) & 255
    return sum(int(ch(a, s) * (1 - t) + ch(b, s) * t + 0.5) << s for s in (16, 8, 0))

FALLBACKS = (0x3ddc84, 0xffb000, 0xff52a0)

def hsv(c):
    h, s, v = colorsys.rgb_to_hsv((c >> 16) / 255, (c >> 8 & 255) / 255, (c & 255) / 255)
    return h * 360, s, v

def luminance(c):
    f = lambda v: v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * f((c >> 16) / 255) + 0.7152 * f((c >> 8 & 255) / 255) + 0.0722 * f((c & 255) / 255)

def contrast(a, b):
    x, y = sorted((luminance(a), luminance(b)), reverse=True)
    return (x + 0.05) / (y + 0.05)

def on_ok(c, a1, panel):
    dh = abs(hsv(c)[0] - hsv(a1)[0])
    return min(dh, 360 - dh) >= 60 and hsv(c)[1] >= 0.25 and contrast(c, panel) >= 3.0

def pick_on(name, panel, a1, a2, a3):
    for c in (a2, a3) + FALLBACKS:
        if on_ok(c, a1, panel):
            return c
    sys.exit(f'gen_themes: theme {name}: no candidate for `on` (hue >= 60 from a1, saturation >= 0.25, contrast >= 3 with panel)')

def check(path):
    rows = re.findall(r'\{ "(\w[\w-]*)", "[^"]*", ((?:0x[0-9a-f]{6}(?:, )?){8,9}) \}', open(path, encoding='utf-8').read())
    bad = []
    for name, vals in rows:
        v = [int(x, 16) for x in vals.split(', ')]
        if len(v) != 9:
            bad.append(f'{name}: no on column')
            continue
        panel, a1, a2, a3, on = v[1], v[5], v[6], v[7], v[8]
        if not on_ok(on, a1, panel) or on != pick_on(name, panel, a1, a2, a3):
            bad.append(f'{name}: on 0x{on:06x} is not the rule\'s pick')
    if bad or not rows:
        sys.exit('gen_themes --check: ' + ('; '.join(bad) or 'no rows'))
    print(f'{len(rows)} themes ok')

def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--check':
        return check(sys.argv[2])
    rg, hb, out = sys.argv[1:4]
    themes = []
    # rgxx built-ins: {"name", "label", bg, grid, panel, panel2, fg, dim, faint, a1, a2, a3, ok, purp, warn, moon, black, chip, ink, light, ...}
    src = open(os.path.join(rg, 'src/common/palette.c'), encoding='utf-8').read()
    for m in re.finditer(r'\{"(\w+)", "([^"]+)",((?:\s*C\(\d+, \d+, \d+\),){17})\s*(\d)', src):
        cs = [int(r) << 16 | int(g) << 8 | int(b) for r, g, b in re.findall(r'C\((\d+), (\d+), (\d+)\)', m.group(3))]
        if m.group(4) == '1':
            continue
        bg, grid, panel, panel2, fg, dim, faint, a1, a2, a3 = cs[:10]
        themes.append((m.group(1), m.group(2), bg, panel, panel2, fg, dim, a1, a2, a3, 'rgxx palette.c'))
    # rgxx round-7 candidates
    for p in sorted(glob.glob(os.path.join(rg, 'docs/mockups/round7/palettes/*.ini'))):
        kv = dict(l.split('=', 1) for l in open(p, encoding='utf-8').read().splitlines() if '=' in l and not l.startswith(('#', ';')))
        kv = {k.strip(): v.strip() for k, v in kv.items()}
        if kv.get('light') == '1':
            continue
        bg, fg = hx(kv['bg']), hx(kv['fg'])
        dim = hx(kv['dim']) if 'dim' in kv else mix(fg, bg, 0.45)
        name = os.path.basename(p)[:-4]
        themes.append((name, kv.get('label', name), bg, hx(kv['panel']), hx(kv['panel2']), fg, dim,
                       hx(kv['accent1']), hx(kv['accent2']), hx(kv['highlight']), 'rgxx round7 ' + name + '.ini'))
    # hiby EVA (surface -> panel, border_color -> panel2; no second accent: a2 = accent, a3 = text)
    kv = {}
    for l in open(os.path.join(hb, 'themes/eva.ini'), encoding='utf-8').read().splitlines():
        if '=' in l and not l.startswith(';'):
            k, v = l.split('=', 1)
            kv[k.strip()] = v.strip()
    themes.append(('eva', 'EVA', hx(kv['bg']), hx(kv['surface']), hx(kv['border_color']), hx(kv['text']), hx(kv['text_dim']),
                   hx(kv['accent']), hx(kv['accent']), hx(kv['text']), 'hiby themes/eva.ini'))
    tc = open(os.path.join(hb, 'src/core/theme.c'), encoding='utf-8').read()
    d = dict(re.findall(r't->(\w+) = lv_color_hex\(0x([0-9A-Fa-f]{6})\)', tc))
    sv = lambda k: int(d[k], 16)
    themes.append(('hiby', 'HiBy 預設', sv('bg'), sv('surface'), mix(sv('surface'), sv('text'), 0.15), sv('text'), sv('text_dim'),
                   sv('accent'), sv('accent'), sv('text'), 'hiby src/core/theme.c defaults()'))
    themes = [t[:10] + (pick_on(t[0], t[3], t[7], t[8], t[9]),) + t[10:] for t in themes]
    with open(out, 'w', encoding='utf-8') as f:
        f.write('/* generated by tools/gen/gen_themes.py (spec 5.7) - do not edit */\n')
        f.write('/* name, label, bg, panel, panel2, fg, dim, a1, a2, a3, on */\n')
        for t in themes:
            f.write('    { "%s", "%s", 0x%06x, 0x%06x, 0x%06x, 0x%06x, 0x%06x, 0x%06x, 0x%06x, 0x%06x, 0x%06x },   /* %s */\n' % t)
    print(f'{len(themes)} dark themes -> {out}')

main()
