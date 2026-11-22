# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G7: 呼吸教練 in the 白噪音 app (app/main/ui/scr_breath.c on ui_arena + game_ring + ui_exit; timing in components/player_core/src/pc_breath.c); sourced by sim/test.sh ---
# Console: `uipush breath` opens it (the last row of the noise list does the same); `arena` dumps the field and then "APP breath pattern= phase= phase_left= left= level= cycles= done= follow= gain= noise=";
# `games mode|rule|style <n>` = session length / pattern / look; `arena game follow <0..2>|end`; `arena step <n>` fixed steps; `arena tap 0 0` a touch. gain = the noise follow's permille (1000 = none).
br_l() { awk '/^APP uitop/ { want = 1; next } want && /^APP breath pattern=/ { n++; if (n == '"$1"') { print; exit } want = 0 }' <<<"$out"; }   # the dump of the n-th sample (each sample is `uitop; arena`)
br_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
# the noise list's second row is 呼吸教練 (right under 噪音顏色); a tap opens the page
nowrun br_row "screen on; vol 0; noiseui; @0.5; uiboxes" && {
    second=$(block_rows "$out" 1 | sed -n 2p)
    grep -q '呼吸教練\|Breathing coach' <<<"$second" || { EVID="second row: $second"; fail br_row_second; }
    read -r x1 y1 x2 y2 <<<"$(cut -d'|' -f1 <<<"$second")"
    nowrun br_row_tap "screen on; vol 0; noiseui; @0.5; simtap $(((x1 + x2) / 2)) $(((y1 + y2) / 2)); @0.5; uistack" && stack_is br_row_tap 1 "menu noise noise_breath"; }
# the box pattern: the phase changes at 120 / 240 / 360 / 480 steps, one cycle; the level goes up, stays, goes down, stays
nowrun br_box "screen on; vol 0; uipush breath; @0.3; arena hold 1; games rule 1; uitop; arena; arena step 119; uitop; arena; arena step 1; uitop; arena; arena step 120; uitop; arena; arena step 119; uitop; arena; arena step 1; uitop; arena; arena step 120; uitop; arena" && {
    [ "$(br_v "$(br_l 1)" phase)" = 0 ] && [ "$(br_v "$(br_l 1)" level)" = 0 ] || { EVID="$(br_l 1)"; fail br_box_start; }
    [ "$(br_v "$(br_l 2)" phase)" = 0 ] && [ "$(br_v "$(br_l 2)" level)" -gt 900 ] || { EVID="$(br_l 2)"; fail br_box_inhale; }
    [ "$(br_v "$(br_l 3)" phase)" = 1 ] && [ "$(br_v "$(br_l 3)" level)" = 1000 ] || { EVID="$(br_l 3)"; fail br_box_hold; }
    [ "$(br_v "$(br_l 4)" phase)" = 2 ] || { EVID="$(br_l 4)"; fail br_box_exhale; }
    [ "$(br_v "$(br_l 5)" phase)" = 2 ] && [ "$(br_v "$(br_l 5)" level)" -lt 100 ] || { EVID="$(br_l 5)"; fail br_box_exhale_end; }
    [ "$(br_v "$(br_l 6)" phase)" = 3 ] && [ "$(br_v "$(br_l 6)" level)" = 0 ] || { EVID="$(br_l 6)"; fail br_box_rest; }
    [ "$(br_v "$(br_l 7)" phase)" = 0 ] && [ "$(br_v "$(br_l 7)" cycles)" = 1 ] || { EVID="$(br_l 7)"; fail br_box_cycle; }; }
# 4-7-8 (the first pattern: 120 + 210 + 240 steps, no rest), 5-5 (150 + 150, no hold); the length cell: 3 / 5 / 10 minutes
nowrun br_patterns "screen on; vol 0; uipush breath; @0.3; arena hold 1; arena step 330; uitop; arena; arena step 240; uitop; arena; games rule 2; arena step 150; uitop; arena; arena step 150; uitop; arena; games mode 1; uitop; arena; games mode 2; uitop; arena" && {
    [ "$(br_v "$(br_l 1)" pattern)" = 478 ] && [ "$(br_v "$(br_l 1)" phase)" = 2 ] || { EVID="$(br_l 1)"; fail br_478_exhale; }
    [ "$(br_v "$(br_l 2)" phase)" = 0 ] && [ "$(br_v "$(br_l 2)" cycles)" = 1 ] || { EVID="$(br_l 2)"; fail br_478_cycle; }
    [ "$(br_v "$(br_l 3)" pattern)" = 55 ] && [ "$(br_v "$(br_l 3)" phase)" = 2 ] || { EVID="$(br_l 3)"; fail br_55_exhale; }
    [ "$(br_v "$(br_l 4)" phase)" = 0 ] && [ "$(br_v "$(br_l 4)" cycles)" = 1 ] || { EVID="$(br_l 4)"; fail br_55_cycle; }
    [ "$(br_v "$(br_l 5)" left)" = 300 ] && [ "$(br_v "$(br_l 6)" left)" = 600 ] || { EVID="$(br_l 5) | $(br_l 6)"; fail br_lengths; }; }
# the same steps give the same picture data (no virtual-time phase)
nowrun br_deterministic "screen on; vol 0; uipush breath; @0.3; arena hold 1; games rule 1; arena step 200; uitop; arena; @3; games rule 1; arena step 200; uitop; arena" && {
    [ "$(br_l 1)" = "$(br_l 2)" ] || { EVID="$(br_l 1) | $(br_l 2)"; fail br_deterministic; }; }
# the session ends at its time: done, the timer stops, a tap starts another
nowrun br_end "screen on; vol 0; uipush breath; @0.3; arena hold 1; arena game end; arena step 29; uitop; arena; arena step 1; uitop; arena; arena step 30; arena; arena tap 0 0; uitop; arena" && {
    [ "$(br_v "$(br_l 1)" done)" = 0 ] && [ "$(br_v "$(br_l 1)" left)" = 1 ] || { EVID="$(br_l 1)"; fail br_end_before; }
    [ "$(br_v "$(br_l 2)" done)" = 1 ] && [ "$(br_v "$(br_l 2)" left)" = 0 ] || { EVID="$(br_l 2)"; fail br_end_done; }
    grep -qx 'APP breath done cycles=0' <<<"$out" || { EVID="$(grep '^APP breath' <<<"$out" | tr '\n' '|')"; fail br_end_event; }
    [ "$(br_v "$(br_l 3)" done)" = 0 ] && [ "$(br_v "$(br_l 3)" left)" = 180 ] || { EVID="$(br_l 3)"; fail br_end_again; }; }
# the noise follows the breath: a playing scene's gain is 1000 at full level, 500 (deep) / 750 (soft) at none, never above 1000; off or no scene: 1000
nowrun br_follow "screen on; vol 0; simnoise 30 3; noise 0; uipush breath; @0.3; arena hold 1; games rule 1; arena game follow 2; arena step 1; uitop; arena; arena step 140; uitop; arena; arena step 160; uitop; arena; arena game follow 1; arena step 60; uitop; arena; arena game follow 0; arena step 10; uitop; arena" && {
    g1=$(br_v "$(br_l 1)" gain); g2=$(br_v "$(br_l 2)" gain); g3=$(br_v "$(br_l 3)" gain); g4=$(br_v "$(br_l 4)" gain); g5=$(br_v "$(br_l 5)" gain)
    [ "$g1" -ge 500 ] && [ "$g1" -lt 520 ] && [ "$g2" = 1000 ] && [ "$g3" -ge 700 ] && [ "$g3" -le 800 ] && [ "$g4" -ge 750 ] && [ "$g4" -le 760 ] && [ "$g5" = 1000 ] || { EVID="gains $g1 $g2 $g3 $g4 $g5"; fail br_follow_gain; }
    [ "$(br_v "$(br_l 1)" noise)" = 0 ] || { EVID="$(br_l 1)"; fail br_follow_noise_on; }; }
nowrun br_follow_idle "screen on; vol 0; uipush breath; @0.3; arena hold 1; games rule 1; arena game follow 2; arena step 30; uitop; arena" && {
    [ "$(br_v "$(br_l 1)" gain)" = 1000 ] && [ "$(br_v "$(br_l 1)" noise)" = -1 ] || { EVID="$(br_l 1)"; fail br_follow_idle; }; }
# a tap borrows the knob: the turn is the noise follow (off -> soft); four idle seconds later it is the volume again and the 返回 pill shows
nowrun br_knob "screen on; vol 0; uipush breath; @1; uitop; arena; simtap 180 200; simknob 1; @0.2; uitop; arena; @5; simknob 1; @0.2; uitop; arena; exit; simvol" && {
    [ "$(br_v "$(br_l 1)" follow)" = 0 ] && [ "$(br_v "$(br_l 2)" follow)" = 1 ] && [ "$(br_v "$(br_l 3)" follow)" = 1 ] || { EVID="$(br_l 1) | $(br_l 2) | $(br_l 3)"; fail br_knob_follow; }
    grep -q '^APP exit attached=1 mode=C a=1' <<<"$out" || fail br_exit_pill
    [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="vol $(vols)"; fail br_volume_back; }; }
# covers: the screen off stops the timer (no steps) and the screen on restarts it by itself; the same for the ring and for 說明
bat() { sed -n "s/.* ticks=\([0-9]*\) .*/\1/p" <<<"$1"; }
bad() { grep '^APP arena name=breath' <<<"$out" | sed -n "${1}p"; }
nowrun br_screen_off "screen on; vol 0; uipush breath; @1; arena; screen off; @1; arena; @1; arena; screen on; @1; arena" && {
    t1=$(bat "$(bad 2)"); t2=$(bat "$(bad 3)"); t3=$(bat "$(bad 4)")
    [ -n "$t1" ] && [ "$t1" = "$t2" ] && [ "$t3" -gt "$t2" ] || { EVID="ticks off $t1 $t2 on $t3"; fail br_screen_off_restart; }; }
nowrun br_ring_cover "screen on; vol 0; uipush breath; @1; games ring; @1; arena; @1; arena; popring -1; @1; arena" && {
    t1=$(bat "$(bad 1)"); t2=$(bat "$(bad 2)"); t3=$(bat "$(bad 3)")
    [ -n "$t1" ] && [ "$t1" = "$t2" ] && [ "$t3" -gt "$t2" ] || { EVID="ticks ring $t1 $t2 closed $t3"; fail br_ring_cover_restart; }; }
# the ring: 節奏 / 外觀 / 噪音 / 重來 and 說明 last (five cells); the noise cell steps; 重來 starts over; 說明 opens the explain page and the page goes on after it
nowrun br_ring "screen on; vol 0; uipush breath; @1; games ring; @0.3; popring 2; uitop; arena; popring 4; @0.3; uitop; back; @1; arena; games ring; popring 3; @0.3; uitop; arena" && {
    [ "$(grep '^APP popring open=1' <<<"$out" | sed -n 1p | sed 's/.* cells=\([0-9]*\) .*/\1/')" = 5 ] || fail br_ring_cells
    [ "$(br_v "$(br_l 1)" follow)" = 1 ] || { EVID="$(br_l 1)"; fail br_ring_follow; }
    grep -qx 'APP uitop explain' <<<"$out" && [ "$(grep '^APP uitop' <<<"$out" | tail -1)" = "APP uitop noise_breath" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail br_ring_help_back; }
    a=$(grep '^APP arena name=breath' <<<"$out"); t1=$(bat "$(tail -3 <<<"$a" | head -1)"); t2=$(bat "$(tail -2 <<<"$a" | head -1)"); [ -n "$t1" ] && [ "$t2" -gt "$t1" ] || { EVID="ticks $t1 $t2"; fail br_help_cover_restart; }
    [ "$(br_v "$(br_l 2)" left)" -ge 178 ] && [ "$(br_v "$(br_l 2)" phase)" = 0 ] && [ "$(br_v "$(br_l 2)" cycles)" = 0 ] || { EVID="$(br_l 2)"; fail br_ring_again; }; }
