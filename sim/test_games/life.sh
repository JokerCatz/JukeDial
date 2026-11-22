# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G6: 生命遊戲 on the shared field's canvas, fine square grid (app/main/ui/scr_game_life.c; the grid and its rules in components/player_core/src/pc_game_life.c); sourced by sim/test.sh ---
# `arena game gen <n>` n generations now, `arena game seed <pct> [rng]` a clear grid with that % random, `arena game place <i> <x> <y> <orient>` a library pattern centred at a cell,
# `arena game clear`, `arena game splash <x> <y> <r> <pct>`, `arena game fresh` (重新播種 as the axes say), `arena game beat` one onset's cluster; `arena step <n>` n fixed steps of the field
# (a generation every "every" steps), `arena hold 1` stops its own timer, `arena tap <x> <y>` a touch from the centre (px). The axes as the ring sets them: `games cell|glow|pat|edge <n>` (absolute).
# `arena game state` prints one "APP life state cell= w= gen= pop= peak= rule= edge= trail= pat= every= gps= cap= speed= quiet= rest= over= painted= canvas= mem=" line (lf_g n = the n-th);
# every `arena` command also ends with the arena's dump and the same fields as "APP game life ..." (lf_a n = the n-th arena line, lf_gd n = the n-th game line).
LFO=0
lf_g() { grep '^APP life state cell=' <<<"$out" | sed -n "${1}p"; }
lf_a() { grep '^APP arena name=' <<<"$out" | sed -n "$(($1 + LFO))p"; }
lf_gd() { grep '^APP game life cell=' <<<"$out" | sed -n "$(($1 + LFO))p"; }
lf_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
ST="arena game state"
LF="screen on; games open life; @0.3; games mode 0; games style 0; games rule 0; games gold 0; arena hold 1; games edge 0; games glow 0; games pat 1; games cell 2"
LFO=0
nowrun lf_open "screen on; games open life; @0.3; arena; uitop" && {
    a=$(lf_a 1); g=$(lf_gd 1)
    [ "$(lf_v "$g" canvas)" = 1 ] && [ "$(lf_v "$g" over)" = 0 ] && [ "$(lf_v "$g" pop)" -gt 0 ] && [ "$(lf_v "$a" run)" = 1 ] && [ "$(tp)" = "tools_game_life " ] || { EVID="$a | $g | $(tp)"; fail lf_open; }; }
# the field's steps give a generation every "every" steps: 10 a second at 3 px, normal speed; the cell size caps it (1 px: 6), fast and slow move it
nowrun lf_rate "$LF; arena game fresh; arena step 30; $ST; games cell 0; arena game fresh; $ST; games cell 2; games mode 1; arena game fresh; arena step 5; $ST; games mode 2; arena game fresh; $ST" && {
    g=$(lf_g 1); [ "$(lf_v "$g" gen)" = 10 ] && [ "$(lf_v "$g" gps)" = 10 ] && [ "$(lf_v "$g" cap)" = 0 ] || { EVID="$g"; fail lf_rate_normal; }
    g=$(lf_g 2); [ "$(lf_v "$g" cell)" = 1 ] && [ "$(lf_v "$g" w)" = 360 ] && [ "$(lf_v "$g" gps)" = 6 ] && [ "$(lf_v "$g" cap)" = 1 ] || { EVID="$g"; fail lf_rate_cap_1px; }
    g=$(lf_g 3); [ "$(lf_v "$g" gen)" = 2 ] && [ "$(lf_v "$g" gps)" = 15 ] && [ "$(lf_v "$g" cap)" = 1 ] || { EVID="$g"; fail lf_rate_fast_capped_at_3px; }
    g=$(lf_g 4); [ "$(lf_v "$g" gps)" = 3 ] && [ "$(lf_v "$g" cap)" = 0 ] || { EVID="$g"; fail lf_rate_slow; }; }
# every cell size: the grid is the box in cells (60 / 90 / 120 / 180 / 360), and a generation of a lone glider redraws a few cells' boxes (far less than the field)
nowrun lf_sizes "$LF; games cell 4; $ST; games cell 3; $ST; games cell 2; $ST; games cell 1; $ST; games cell 0; $ST; games cell 2; arena game clear; arena game place 0 60 60 0; arena step 3; arena" && {
    [ "$(lf_v "$(lf_g 1)" w)" = 60 ] && [ "$(lf_v "$(lf_g 2)" w)" = 90 ] && [ "$(lf_v "$(lf_g 3)" w)" = 120 ] && [ "$(lf_v "$(lf_g 4)" w)" = 180 ] && [ "$(lf_v "$(lf_g 5)" w)" = 360 ] || { EVID="$(lf_g 1) | $(lf_g 5)"; fail lf_sizes; }
    a=$(grep '^APP arena name=' <<<"$out" | tail -1); [ "$(lf_v "$a" inv_px)" -gt 0 ] && [ "$(lf_v "$a" inv_px)" -lt $(( $(lf_v "$a" field_px) / 20 )) ] || { EVID="$a"; fail lf_glider_redraws_little; }; }
# a glider on the wrap edge keeps its 5 cells and goes round; on the wall edge it ends at the circle
nowrun lf_edge "$LF; games edge 1; arena game clear; arena game place 0 60 60 0; arena game gen 40; $ST; games edge 0; arena game clear; arena game place 0 60 60 0; arena game gen 400; $ST" && {
    [ "$(lf_v "$(lf_g 1)" pop)" = 5 ] && [ "$(lf_v "$(lf_g 1)" edge)" = 1 ] || { EVID="$(lf_g 1)"; fail lf_torus_glider; }
    [ "$(lf_v "$(lf_g 2)" pop)" -le 4 ] && [ "$(lf_v "$(lf_g 2)" edge)" = 0 ] || { EVID="$(lf_g 2)"; fail lf_wall_glider; }; }
# a tap = 搗亂: a random blob; with a pattern chosen it drops that pattern, centred on the tap
nowrun lf_tap "$LF; arena game clear; arena tap 0 0; $ST; games pat 3; arena game clear; arena tap 0 0; $ST; games pat 8; arena game clear; arena tap -60 40; $ST" && {
    [ "$(lf_v "$(lf_g 1)" pop)" -gt 20 ] || { EVID="$(lf_g 1)"; fail lf_tap_blob; }
    [ "$(lf_v "$(lf_g 2)" pop)" = 5 ] && [ "$(lf_v "$(lf_g 3)" pop)" = 7 ] || { EVID="$(lf_g 2) | $(lf_g 3)"; fail lf_tap_pattern; }; }
# the knob a tap lent steps the pattern (taps drop it); a knob turn before a tap is the volume
nowrun lf_knob_pattern "$LF; arena tap 0 0; simknob 1; @0.3; simknob 1; @0.3; $ST; simknob -2; @0.3; $ST" && {
    p1=$(lf_v "$(lf_g 1)" pat); p2=$(lf_v "$(lf_g 2)" pat); [ "$p1" = 2 ] && [ "$p2" = 0 ] || { EVID="$p1 $p2"; fail lf_knob_pattern; }; }
nowrun lf_knob_volume "screen on; games open life; @0.3; vol 0; arena hold 1; @0.3; simknob 1; @0.3; simvol" && {
    [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="vol $(vols)"; fail lf_knob_volume; }; }
# the axes: 殘影 (trail steps 0 / 1 / 3 / 5), 邊緣, 圖案 and 格子 as the ring sets them; the cell size starts a new game by the pattern, the others keep the field
nowrun lf_axes "$LF; arena game seed 25 3; arena game gen 5; games glow 3; $ST; games edge 1; $ST; games pat 9; $ST; games cell 3; $ST" && {
    g=$(lf_g 1); [ "$(lf_v "$g" trail)" = 5 ] && [ "$(lf_v "$g" gen)" = 5 ] || { EVID="$g"; fail lf_glow; }
    g=$(lf_g 2); [ "$(lf_v "$g" edge)" = 1 ] && [ "$(lf_v "$g" gen)" = 5 ] || { EVID="$g"; fail lf_edge_axis; }
    g=$(lf_g 3); [ "$(lf_v "$g" pat)" = 9 ] && [ "$(lf_v "$g" gen)" = 5 ] || { EVID="$g"; fail lf_pat_axis; }
    g=$(lf_g 4); [ "$(lf_v "$g" cell)" = 4 ] && [ "$(lf_v "$g" w)" = 90 ] && [ "$(lf_v "$g" gen)" = 0 ] && [ "$(lf_v "$g" pop)" = 7 ] || { EVID="$g"; fail lf_cell_axis_reseeds_by_pattern; }; }
# the rules: 節拍 drops a cluster per onset of the music (a simulated tempo: the field never dies), 高生命 / 日與夜 step with their own birth rules
nowrun lf_beat "$LF; games rule 1; arena game clear; arena game beat; $ST; arena hold 0; beat sim 120; @3; $ST" && {
    [ "$(lf_v "$(lf_g 1)" rule)" = 1 ] && [ "$(lf_v "$(lf_g 1)" pop)" -gt 0 ] || { EVID="$(lf_g 1)"; fail lf_beat_cluster; }
    [ "$(lf_v "$(lf_g 2)" gen)" -gt 5 ] && [ "$(lf_v "$(lf_g 2)" over)" = 0 ] || { EVID="$(lf_g 2)"; fail lf_beat_runs; }; }
nowrun lf_beat_onsets_fill "$LF; games rule 1; arena game clear; arena hold 0; beat sim 150; @4; beat; $ST" && {
    b=$(grep '^APP beat on=' <<<"$out" | tail -1)
    [ "$(lf_v "$b" onsets)" -gt 2 ] && [ "$(lf_v "$(lf_g 1)" peak)" -gt 10 ] || { EVID="$b | $(lf_g 1)"; fail lf_beat_onsets_fill; }; }
# the detector is on while the page lives, held in the classic rule, and off after Back (the users share one detector: the page is the only one here)
nowrun lf_beat_feed "$LF; games rule 1; arena hold 0; beat sim 120; @1.2; beat; games rule 0; @1.2; beat; back; @0.5; beat" && {
    b=$(grep '^APP beat on=' <<<"$out")   # 1 = `beat sim`, 2 / 3 = the rule 節拍 then 經典 (held: no more steps), 4 = after Back
    b2=$(sed -n 2p <<<"$b"); b3=$(sed -n 3p <<<"$b"); b4=$(sed -n 4p <<<"$b")
    [ "$(lf_v "$b2" on)" = 1 ] && [ "$(lf_v "$b2" steps)" -gt 5 ] && [ "$(lf_v "$b3" steps)" = "$(lf_v "$b2" steps)" ] && [ "$(lf_v "$b4" on)" = 0 ] || { EVID="$(tr '\n' '|' <<<"$b")"; fail lf_beat_feed; }; }
nowrun lf_beat_held_in_classic "screen on; games open life; @0.3; games rule 0; @1.5; beat; @1.5; beat" && {
    [ "$(lf_v "$(grep '^APP beat on=' <<<"$out" | sed -n 1p)" steps)" = "$(lf_v "$(grep '^APP beat on=' <<<"$out" | sed -n 2p)" steps)" ] || { EVID="$(grep '^APP beat on=' <<<"$out" | tr '\n' '|')"; fail lf_beat_held_in_classic; }; }
nowrun lf_rules "$LF; arena game seed 30 5; games rule 2; arena game gen 20; $ST; games rule 3; arena game gen 20; $ST" && {
    [ "$(lf_v "$(lf_g 1)" rule)" = 2 ] && [ "$(lf_v "$(lf_g 2)" rule)" = 3 ] && [ "$(lf_v "$(lf_g 2)" gen)" = 40 ] || { EVID="$(lf_g 1) | $(lf_g 2)"; fail lf_rules; }; }
# a field that stops changing rests (the field's timer stops; a tap wakes it): a block, 40 generations without a change
nowrun lf_rest "$LF; arena game clear; arena game block 60 60; arena step 100; $ST; arena step 200; $ST; arena; arena tap 0 0; arena" && {
    g=$(lf_g 1); [ "$(lf_v "$g" rest)" = 0 ] || { EVID="$g"; fail lf_rest_not_yet; }
    g=$(lf_g 2); a=$(grep '^APP arena name=' <<<"$out" | tail -3 | head -1); [ "$(lf_v "$g" rest)" = 1 ] && [ "$(lf_v "$a" run)" = 0 ] || { EVID="$g | $a"; fail lf_rest; }
    [ "$(lf_v "$(grep '^APP arena name=' <<<"$out" | tail -1)" run)" = 1 ] || { EVID="$(grep '^APP arena name=' <<<"$out" | tail -1)"; fail lf_rest_tap_wakes; }; }
# the field dies out (classic): the result card (generations, peak population), a tap on the field = a new game
nowrun lf_extinct "$LF; arena game seed 1 5; arena step 60; $ST; @1; games result; arena tap 0 0; $ST" && {
    g=$(lf_g 1); [ "$(lf_v "$g" over)" = 1 ] && [ "$(lf_v "$g" pop)" = 0 ] && grep -q '^APP game_result up=1 .*title=Extinct' <<<"$out" || { EVID="$g | $(grep 'game_result\|life end' <<<"$out" | head -3 | tr '\n' '|')"; fail lf_extinct_card; }
    g=$(lf_g 2); [ "$(lf_v "$g" over)" = 0 ] && [ "$(lf_v "$g" pop)" -gt 0 ] || { EVID="$g"; fail lf_card_tap_again; }; }
# 重新播種 seeds by the pattern (R-五格: 7 ... 5 cells), 清空 empties the grid
nowrun lf_clear_reseed "$LF; games pat 8; arena game fresh; $ST; arena game clear; $ST" && {
    [ "$(lf_v "$(lf_g 1)" pop)" = 7 ] && [ "$(lf_v "$(lf_g 1)" gen)" = 0 ] && [ "$(lf_v "$(lf_g 2)" pop)" = 0 ] || { EVID="$(lf_g 1) | $(lf_g 2)"; fail lf_reseed_clear; }; }
# the screen off stops the field (a toy: its timer stops; the screen waking restarts it)
nowrun lf_screen_off "screen on; games open life; @0.3; screen off; @0.5; arena; @1; arena; screen on; @0.5; arena" && {
    [ "$(lf_v "$(lf_a 1)" run)" = 0 ] && [ "$(lf_v "$(lf_a 1)" ticks)" = "$(lf_v "$(lf_a 2)" ticks)" ] || { EVID="$(lf_a 1) | $(lf_a 2)"; fail lf_screen_off; }
    [ "$(lf_v "$(lf_a 3)" run)" = 1 ] && [ "$(lf_v "$(lf_a 3)" ticks)" -gt "$(lf_v "$(lf_a 2)" ticks)" ] || { EVID="$(lf_a 3)"; fail lf_screen_on_restarts; }; }
nowrun lf_back "screen on; games open life; @0.5; back; @0.3; uitop; arena" && {
    [ "$(tp)" = "tools_games " ] && grep -qx 'APP arena none' <<<"$out" || { EVID="$(tp)"; fail lf_back; }; }
nowrun lf_own_help "screen on; games open life; @0.3; frame show; simtaprow 1; @0.3; popring 8; @0.3; help show; uitop" && {
    grep -q '^APP explain id=game title=Game of Life' <<<"$out" || { EVID="$(grep '^APP explain' <<<"$out")"; fail lf_own_help; }; }
# music keeps playing while the field runs
nowrun lf_music_plays "screen on; vol 0; track 3; @2; audio info; games open life; @0.3; games rule 1; @2; audio info" && {
    a=$(grep '^APP audio info ' <<<"$out" | sed 's/ underruns_src.*//' | sort -u | wc -l)
    [ "$(grep -c '^APP audio info ' <<<"$out")" = 2 ] && [ "$a" = 1 ] && ! grep -q '^APP mp3 stop\|^APP play stop' <<<"$out" || { EVID="$(grep '^APP audio info' <<<"$out" | cut -c1-80 | tr '\n' '|')"; fail lf_music_plays; }; }
# honest rate: the status shows the generations counted over a second (10 a second at 3 px, normal), and the count of generations matches it
nowrun lf_gps_measured "$LF; games glow 0; arena game fresh; arena hold 0; @3.2; $ST" && {
    g=$(lf_g 1); gps=$(lf_v "$g" gps); gen=$(lf_v "$g" gen); [ "$gps" -ge 9 ] && [ "$gps" -le 11 ] && [ "$gen" -ge 28 ] && [ "$gen" -le 34 ] || { EVID="$g"; fail lf_gps_measured; }; }
# the afterglow of a dying field fades out before the card (no trail cell frozen under it)
nowrun lf_extinct_trails_fade "$LF; games glow 3; arena game seed 1 5; arena step 90; $ST; @1; $ST" && {
    g=$(lf_g 2); [ "$(lf_v "$g" over)" = 1 ] && [ "$(lf_v "$g" trailcells)" = 0 ] || { EVID="$g"; fail lf_extinct_trails_fade; }; }
# out of memory (the next open finds none): no crash on a tap, a style, a size, a reseed, a clear, the console; the status says so; Back and a normal open work again
nowrun lf_oom "screen on; games open life; @0.3; games oom 1; back; @0.3; games open life; @0.3; arena tap 0 0; games style 1; games style 0; games cell 0; games cell 2; games glow 1; games pat 4; games edge 1; games rule 1; games rule 0; games ring; simtap 180 180; arena game fresh; arena game gen 3; arena game clear; arena step 20; @0.5; uitop; back; @0.3; games open life; @0.3; arena game state" && {
    grep -q 'cmd no memory' <<<"$out" && [ "$(lf_v "$(lf_g 1)" canvas)" = 1 ] && [ "$(lf_v "$(lf_g 1)" w)" = 120 ] && [ "$(lf_v "$(lf_g 1)" pop)" -gt 0 ] || { EVID="$(lf_g 1)"; fail lf_oom; }; }
# --- end 生命遊戲 (Game of Life) ---
