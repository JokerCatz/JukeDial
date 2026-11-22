# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G2: 保險箱開鎖 on the shared dial ui_dial (app/main/ui/scr_game_safe.c, ui_dial.c; rules in components/player_core/src/pc_game_safe.c); sourced by sim/test.sh ---
# `dial turn <n>` = the knob by n detents, `dial tap <x> <y>` a touch from the dial's centre (0 0 = the hub = confirm, +-60 0 = the right / left half), `dial step <n>` n timer
# steps (1/30 s), `dial game pos <n>` puts the dial on n, `games count 0` = the first combination 35, 67, 84 (classic; the seed is fixed in the simulator).
# Every `dial` command ends with both dumps ("APP dial ..." then "APP game safe ...").
sf_g() { grep '^APP game safe state=' <<<"$out" | sed -n "${1}p"; }
sf_d() { grep '^APP dial name=' <<<"$out" | sed -n "${1}p"; }
sf_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
SF="screen on; games open safe; @0.3; games mode 0; games style 0; games rule 0; games count 0"
nowrun sf_open "$SF; dial; uitop" && {
    d=$(sf_d 1); g=$(sf_g 1)
    [ "$(sf_v "$d" mode)" = dial ] && [ "$(sf_v "$d" target)" = 35 ] && [ "$(sf_v "$d" reach)" = 12 ] && [ "$(sf_v "$d" knob)" = 0 ] && [ "$(sf_v "$g" n)" = 3 ] \
        && [ "$(sf_v "$g" target)" = 35,67,84,-1 ] && [ "$(tp)" = "tools_game_safe " ] || { EVID="$d | $g | $(tp)"; fail sf_open; }; }
# the knob is the volume until a tap on the dial lends it; then one detent = one number, both ways, wrapping
nowrun sf_knob "$SF; vol 0; @0.3; simknob 1; @0.2; dial; dial tap 60 0; dial turn 3; dial turn -10; simvol" && {
    [ "$(sf_v "$(sf_g 1)" pos)" = 0 ] && [ "$(sf_v "$(sf_d 2)" v)" = 1 ] && [ "$(sf_v "$(sf_d 3)" v)" = 4 ] && [ "$(sf_v "$(sf_d 4)" v)" = 94 ] && [ "$(sf_v "$(sf_g 4)" pos)" = 94 ] \
        || { EVID="$(sf_g 1) | $(sf_d 2) | $(sf_d 3) | $(sf_d 4)"; fail sf_knob; }; }
nowrun sf_borrow_timeout "$SF; vol 0; dial tap 60 0; @4.5; dial; simknob 1; @0.2; dial; simvol" && {
    [ "$(sf_v "$(sf_d 2)" knob)" = 0 ] && [ "$(sf_v "$(sf_d 3)" v)" = 1 ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(sf_d 2) | $(sf_d 3) vol $(vols)"; fail sf_borrow_timeout; }; }
# taps: right half +1, left half -1, the hub confirms (a wrong number: an attempt, the combination stays at the first)
nowrun sf_taps "$SF; dial tap 60 0; dial tap 60 0; dial tap -60 0; dial tap 0 0" && {
    [ "$(sf_v "$(sf_g 3)" pos)" = 1 ] && [ "$(sf_v "$(sf_g 4)" attempts)" = 1 ] && [ "$(sf_v "$(sf_g 4)" stage)" = 0 ] && [ "$(sf_v "$(sf_d 4)" fx)" = 1 ] || { EVID="$(sf_g 3) | $(sf_g 4) | $(sf_d 4)"; fail sf_taps; }; }
# the graded haptic and the tick: landing on the number plays the tick sound; the haptic file counts the strong click
nowrun sf_detent "$SF; sfx; dial game pos 34; dial turn 1; sfx" && {
    p1=$(sed -n 's/^APP sfx .* posted=\([0-9]*\) .*/\1/p' <<<"$out" | sed -n 1p); p2=$(sed -n 's/^APP sfx .* posted=\([0-9]*\) .*/\1/p' <<<"$out" | sed -n 2p)
    [ "$(sf_v "$(sf_g 2)" pos)" = 35 ] && [ $((p2 - p1)) = 1 ] || { EVID="posted $p1 -> $p2 | $(sf_g 2)"; fail sf_detent_tick; }; }
nowrun sf_pass_over "$SF; sfx; dial game pos 33; dial turn 5; sfx" && {
    p1=$(sed -n 's/^APP sfx .* posted=\([0-9]*\) .*/\1/p' <<<"$out" | sed -n 1p); p2=$(sed -n 's/^APP sfx .* posted=\([0-9]*\) .*/\1/p' <<<"$out" | sed -n 2p)
    [ "$(sf_v "$(sf_g 2)" pos)" = 38 ] && [ $((p2 - p1)) = 1 ] || { EVID="posted $p1 -> $p2"; fail sf_pass_over_tick; }; }
# in order: lock, lock, wrong (resets to the first), then all three: open, the door ring, the card after its delay (6 attempts here)
nowrun sf_solve "$SF; dial game pos 35; dial tap 0 0; dial game pos 67; dial tap 0 0; dial game pos 5; dial tap 0 0; dial game pos 35; dial tap 0 0; dial game pos 67; dial tap 0 0; dial game pos 84; dial tap 0 0; games result; @1; games result" && {
    [ "$(sf_v "$(sf_g 2)" stage)" = 1 ] && [ "$(sf_v "$(sf_d 2)" zones)" = 1 ] && [ "$(sf_v "$(sf_d 2)" target)" = 67 ] && [ "$(sf_v "$(sf_d 2)" fx)" = 1 ] || { EVID="$(sf_g 2) | $(sf_d 2)"; fail sf_lock; }
    [ "$(sf_v "$(sf_g 6)" stage)" = 0 ] && [ "$(sf_v "$(sf_g 6)" attempts)" = 3 ] && [ "$(sf_v "$(sf_d 6)" zones)" = 0 ] || { EVID="$(sf_g 6) | $(sf_d 6)"; fail sf_wrong_resets; }
    [ "$(sf_v "$(sf_g 12)" state)" = 1 ] && [ "$(sf_v "$(sf_g 12)" attempts)" = 6 ] && [ "$(sf_v "$(sf_d 12)" zones)" = 3 ] && [ "$(sf_v "$(sf_d 12)" target)" = -1 ] || { EVID="$(sf_g 12) | $(sf_d 12)"; fail sf_open_state; }
    grep -qx 'APP game safe end outcome=1 streak=0 best=6 new_best=1 sound=win' <<<"$out" || { EVID="$(grep ' end ' <<<"$out")"; fail sf_end_line; }
    grep -q '^APP game_result up=1 shown=0 title=Cracked! moves=6 ' <<<"$out" && grep -q '^APP game_result up=1 shown=1 title=Cracked! moves=6 ' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out" | tr '\n' '|')"; fail sf_card; }; }
# a tap on the card = again: a new combination, the dial back at 0
nowrun sf_again "$SF; dial game pos 35; dial tap 0 0; dial game pos 67; dial tap 0 0; dial game pos 84; dial tap 0 0; @1; games result tap; dial" && {
    [ "$(sf_v "$(sf_g '$')" state)" = 0 ] && [ "$(sf_v "$(sf_g '$')" pos)" = 0 ] && [ "$(sf_v "$(sf_g '$')" attempts)" = 0 ] || { EVID="$(sf_g '$')"; fail sf_again; }; }
# rules: expert has four numbers and a smaller reach; timed runs out at 60 s (the clock starts at the first move; 1700 + 100 steps of 1/30 s = 60 s) and the card says so
nowrun sf_rules "$SF; games rule 1; dial; games rule 2; dial game pos 3; dial tap 60 0; dial step 1700; dial step 100; @1; games result" && {
    [ "$(sf_v "$(sf_g 1)" n)" = 4 ] && [ "$(sf_v "$(sf_g 1)" reach)" = 5 ] || { EVID="$(sf_g 1)"; fail sf_rule_expert; }
    [ "$(sf_v "$(sf_g 4)" n)" = 3 ] && [ "$(sf_v "$(sf_g 4)" left)" -gt 0 ] && [ "$(sf_v "$(sf_g 4)" state)" = 0 ] && [ "$(sf_v "$(sf_g 5)" state)" = 2 ] || { EVID="$(sf_g 4) | $(sf_g 5)"; fail sf_rule_timed; }
    grep -q '^APP game_result up=1 shown=1 title=Time is up' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out")"; fail sf_timed_card; }; }
nowrun sf_hint "$SF; games mode 1; dial; games mode 2; dial" && {
    [ "$(sf_v "$(sf_g 1)" reach)" = 18 ] && [ "$(sf_v "$(sf_g 2)" reach)" = 6 ] || { EVID="$(sf_g 1) | $(sf_g 2)"; fail sf_hint_reach; }; }
# the screen off pauses the running clock; the next tap only resumes; the game's ring pauses too
nowrun sf_screen_off "$SF; dial tap 60 0; @0.2; screen off; @0.5; dial; screen on; @0.3; dial; dial tap 60 0; @0.1; dial" && {
    [ "$(sf_v "$(sf_d 2)" paused)" = 1 ] && [ "$(sf_v "$(sf_d 2)" run)" = 0 ] && grep -qx 'APP game safe paused=1' <<<"$out" || { EVID="$(sf_d 2)"; fail sf_screen_off_pause; }
    [ "$(sf_v "$(sf_d 3)" paused)" = 1 ] && [ "$(sf_v "$(sf_d 4)" paused)" = 0 ] && [ "$(sf_v "$(sf_d 4)" v)" = 1 ] && grep -qx 'APP game safe paused=0' <<<"$out" || { EVID="$(sf_d 3) | $(sf_d 4)"; fail sf_tap_resumes; }; }
nowrun sf_ring_pauses "$SF; dial tap 60 0; @0.2; frame show; simtaprow 1; @0.3; dial; popring -1; @0.2; dial tap 60 0; @0.2; dial" && {
    [ "$(sf_v "$(sf_d 2)" paused)" = 1 ] && [ "$(sf_v "$(sf_d 3)" paused)" = 0 ] && [ "$(sf_v "$(sf_d 3)" run)" = 1 ] || { EVID="$(sf_d 2) | $(sf_d 3)"; fail sf_ring_pauses; }; }
# the golden safe: twice the reach
nowrun sf_gold "$SF; games gold 1; dial game pos 35; dial tap 0 0; dial game pos 67; dial tap 0 0; dial game pos 84; dial tap 0 0; @1; games result tap; dial" && { [ "$(sf_v "$(sf_g '$')" reach)" = 24 ] && [ "$(sf_v "$(sf_g '$')" gold)" = 1 ] || { EVID="$(sf_g '$')"; fail sf_gold; }; }
# the meter mode of the piece (read only, smoothed, a peak that holds then falls): force it on the open dial
nowrun sf_meter "$SF; dial meter 1; dial feed 80; dial step 3; dial step 60; dial tap 60 0; dial feed 10; dial step 20; dial step 120" && {
    s1=$(sf_v "$(sf_d 3)" shown); s2=$(sf_v "$(sf_d 4)" shown)
    [ "$(sf_v "$(sf_d 1)" mode)" = meter ] && [ "$s1" -gt 0 ] && [ "$s1" -lt 80 ] && [ "$s2" = 80 ] && [ "$(sf_v "$(sf_g 5)" pos)" = 0 ] || { EVID="$(sf_d 3) | $(sf_d 4)"; fail sf_meter_smooth; }
    [ "$(sf_v "$(sf_d 7)" peak)" = 80 ] && [ "$(sf_v "$(sf_d 7)" shown)" -lt 80 ] && [ "$(sf_v "$(sf_d 8)" peak)" = 10 ] && [ "$(sf_v "$(sf_d 8)" run)" = 0 ] || { EVID="$(sf_d 7) | $(sf_d 8)"; fail sf_meter_peak; }; }
# the falling peak redraws its own marker (the needle has come to rest at 10 while the peak is still coming down: peak_inv counts the marker's invalidations, fix round 1)
nowrun sf_meter_peak_marker "$SF; dial meter 1; dial feed 80; dial step 60; dial feed 10; dial step 50; dial step 5; dial step 5" && {
    p5=$(sf_v "$(sf_d 5)" peak); p7=$(sf_v "$(sf_d 7)" peak); i5=$(sf_v "$(sf_d 5)" peak_inv); i7=$(sf_v "$(sf_d 7)" peak_inv)
    [ "$(sf_v "$(sf_d 5)" shown)" = 10 ] && [ "$(sf_v "$(sf_d 7)" shown)" = 10 ] && [ "$p7" -lt "$p5" ] && [ "$i7" -gt "$i5" ] || { EVID="$(sf_d 5) | $(sf_d 7)"; fail sf_meter_peak_marker; }; }
# after the end a knob turn does nothing (the card stays, the game is not restarted); a tap on the dial = again
nowrun sf_knob_after_end "$SF; dial game pos 35; dial tap 0 0; dial game pos 67; dial tap 0 0; dial game pos 84; dial tap 0 0; @1; dial turn 3; games result; dial tap 60 0; dial" && {
    grep -q '^APP game_result up=1 shown=1 title=Cracked!' <<<"$out" && [ "$(sf_v "$(sf_g 7)" state)" = 1 ] && [ "$(sf_v "$(sf_g '$')" state)" = 0 ] && [ "$(sf_v "$(sf_g '$')" attempts)" = 0 ] \
        || { EVID="$(sf_g 7) | $(sf_g '$')"; fail sf_knob_after_end; }; }
# a turn redraws only the needle's boxes, never the whole face twice over
nowrun sf_redraw_small "$SF; dial turn 1" && {
    px=$(sf_v "$(sf_d 1)" inv_px); f=$(sf_v "$(sf_d 1)" face_px)
    [ "$px" -gt 0 ] && [ "$px" -lt $((f * 3 / 4)) ] || { EVID="$(sf_d 1)"; fail sf_redraw_small; }; }
nowrun sf_back "$SF; dial tap 60 0; exit pause; @0.2; exit tap back; @0.3; uitop; dial" && {
    [ "$(tp)" = "tools_games " ] && grep -qx 'APP dial none' <<<"$out" || { EVID="$(tp)"; fail sf_back; }; }
