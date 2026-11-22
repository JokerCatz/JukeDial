#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Host render time of the heavy full-field pages (a RELATIVE measure: the host is not the board). Builds the -O2, no-sanitizer simulator in
# .local/sim/perfbuild and prints, per page, the CPU ms of 120 virtual seconds (30 Hz steps + redraws) beyond the page's opening; best of 5.
# Usage: tools/simtools/sim_perf.sh [label]   (run it before and after a change; compare the lines)
set -e
cd "$(dirname "$0")/../.."
B=.local/sim/perfbuild
python3 tools/simtools/sim_lvconf.py app/sdkconfig $B/lv_conf_sim.h
cmake -S sim -B $B -DSIM_LVCONF=$PWD/$B/lv_conf_sim.h -DSIM_PERF=1 >/dev/null
make -C $B -j"$(nproc)" >/dev/null 2>&1
R=$(mktemp -d); trap 'rm -rf "$R" "$R.t"' EXIT
cpu() {  # cpu <cmds> -> user+sys ms
    local t; /usr/bin/time -o "$R.t" -f "%U %S" $B/sim --headless --root "$R" --cmd "$1" >/dev/null 2>&1 || true; t=$(tail -1 "$R.t")
    awk -v t="$t" 'BEGIN{split(t,a," "); printf "%d", (a[1]+a[2])*1000}'
}
REP=$(python3 -c "print('; '.join(['arena step 1; @0.034'] * 900))")   # 900 held field steps, each followed by a render pass
page() {  # page <name> <setup cmds> <run cmds>
    local best=999999999 i a b d
    for i in 1 2 3 4 5; do
        rm -rf "$R"/*; cp -r sim/fixture/. "$R"/
        a=$(cpu "$2; @0"); rm -rf "$R"/*; cp -r sim/fixture/. "$R"/
        b=$(cpu "$2; $3"); d=$((b - a)); [ $d -lt $best ] && best=$d
    done
    printf "%-18s %6d ms\n" "$1" "$best"
}
echo "sim_perf ${1:-}: CPU ms of 900 steps+renders (games) or 120 virtual s (viz), best of 5"
H="arena hold 1"
page life_1px_glow   "screen on; games open life; games mode 3; games style 0; games rule 0; games cell 1; games glow 2; $H; arena game seed 25 1" "$REP"
page life_3px        "screen on; games open life; games mode 3; games style 0; games rule 0; games cell 3; games glow 1; $H; arena game seed 25 1" "$REP"
page spiro           "screen on; games open spiro; games mode 0; games style 0; games rule 0; games colour 1; $H; arena game new 2 3 0" "$REP"
page beat            "screen on; games open beat; games mode 0; games style 0; games rule 1; $H; arena tap 0 0" "$REP"
for r in 0 1 2 3; do page "ambient_$r" "screen on; tools ambient; games mode 0; games style 0; games rule $r; $H; arena game bright 3; arena game seed 3" "$REP"; done
for k in bars ring fall scope; do page "viz_$k" "screen on; vol 0; track 3; @3; simvoices nes; viz $k" "@120"; done
