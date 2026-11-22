#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""check_palette: the spectrum ramps on every LIGHT card theme (make lint-app). Compiles components/player_core/src/pc_viz.c with the host compiler (nothing runs
on a board), asks pc_ramp_color for fire / ice / winamp / rainbow / duo at 33 levels on the theme's own colours (tools/gen/gen_card_themes.py THEMES) and judges:
  loud end   (level 1)  WCAG contrast against the panel  >= 3.0   (the ink end must read on the paper; the dark ramps end in pale yellow / white: ~1.3)
  span       quiet end vs loud end contrast               >= 3.0   (a ramp that does not travel is a flat bar)
  never brighter than the panel (relative luminance of every step <= the panel's)   (a ramp step is never paler than the panel; chipchill colour.rs test; the waterfall itself starts from the page background)
  --legacy   the same judging on the dark ramps (pc_ramp_color_ex(.., false)): it must fail, proof that the rows see the defect.
  check_palette.py [--legacy]   exit 1 on any failure; one summary line"""
import os, subprocess, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "gen"))   # tools/gen: sibling tools live in other folders
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import gen_card_themes as G

MIN_LOUD, MIN_SPAN = 3.0, 3.0
RAMPS = ["fire", "ice", "winamp", "rainbow", "duo"]
C_SRC = r'''
#include <stdio.h>
#include "pc/pc_viz.h"
int main(int argc, char **argv) {
    int legacy = argc > 1;
    static const pc_theme_t T[] = { THEMES };
    for (unsigned t = 0; t < sizeof T / sizeof T[0]; t++)
        for (int r = PC_RAMP_FIRE; r <= PC_RAMP_DUO; r++)
            for (int i = 0; i <= 32; i++) {
                float f = i / 32.0f;
                unsigned c = legacy ? pc_ramp_color_ex((pc_ramp_t)r, &T[t], f, 0) : pc_ramp_color((pc_ramp_t)r, &T[t], f);
                printf("%s %s %d %06x\n", T[t].name, pc_ramp_name((pc_ramp_t)r), i, c);
            }
    return 0;
}
'''

def run(legacy):
    light = [n for n, v in G.THEMES.items() if not v[3]]
    rows = []
    for n in light:
        c, _, _ = G.parse_theme(n)
        rows.append('{"%s","%s",0x%06x,0x%06x,0x%06x,0x%06x,0x%06x,0x%06x,0x%06x,0x%06x,0x%06x}' % (n, n, *[c[k] for k in G.CORE]))
    with tempfile.TemporaryDirectory(prefix="palette_") as d:
        src = os.path.join(d, "p.c"); exe = os.path.join(d, "p")
        open(src, "w").write(C_SRC.replace("THEMES", ",".join(rows), 1))
        cc = ["cc", "-std=gnu11", "-O1", "-I" + os.path.join(ROOT, "components/player_core/include"), "-I" + os.path.join(ROOT, "components/player_core/src"),
              src, os.path.join(ROOT, "components/player_core/src/pc_viz.c"), "-lm", "-o", exe]
        subprocess.run(cc, check=True)
        out = subprocess.run([exe] + (["x"] if legacy else []), check=True, capture_output=True, text=True).stdout
    ramp = {}
    for ln in out.splitlines():
        n, r, i, h = ln.split(); ramp.setdefault((n, r), {})[int(i)] = int(h, 16)
    bad = []
    for (n, r), st in ramp.items():
        c, _, _ = G.parse_theme(n); pn = c["panel"]
        lo, hi = st[0], st[32]
        if G.cr(hi, pn) < MIN_LOUD: bad.append("%s/%s loud end %06x on panel %06x: contrast %.2f < %.1f" % (n, r, hi, pn, G.cr(hi, pn), MIN_LOUD))
        if G.cr(lo, hi) < MIN_SPAN: bad.append("%s/%s quiet %06x vs loud %06x: contrast %.2f < %.1f" % (n, r, lo, hi, G.cr(lo, hi), MIN_SPAN))
        top = max(G.lum(v) for v in st.values())
        if top > G.lum(pn) + 1e-6: bad.append("%s/%s a step is brighter than the panel (%.3f > %.3f)" % (n, r, top, G.lum(pn)))
    return len(ramp), bad

def main():
    legacy = "--legacy" in sys.argv
    n, bad = run(legacy)
    for b in bad: print("check_palette:", b)
    print("check_palette: %d (theme, ramp) pairs%s, %d failure(s)" % (n, " (dark ramps, --legacy)" if legacy else "", len(bad)))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
