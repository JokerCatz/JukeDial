# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G5: 氛圍頁 (app/main/ui/scr_ambient.c on ui_arena + game_ring + ui_exit; motion in components/player_core/src/pc_ambient.c); sourced by sim/test.sh ---
# Console: `tools ambient` opens it; `arena` dumps the field and then "APP ambient scene= speed= style= bright= prims= blobs= fish= food= bubbles= gold= flames= embers= firefly= phase=";
# `games rule|style|mode <n>` = scene / tone / speed; `arena game seed <n>|rare|poke <x> <y>|bright <0..4>`; `arena tap <x> <y>` a touch (px from the centre); `arena step <n>`.
am_l() { awk '/^APP uitop/ { want = 1; next } want && /^APP ambient scene=/ { n++; if (n == '"$1"') { print; exit } want = 0 }' <<<"$out"; }   # the dump of the n-th sample (each sample is `uitop; arena`: a console command dumps by itself too)
am_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
nowrun amb_open "screen on; vol 0; tools ambient; @0.5; uistack; arena hold 1; arena game seed 3; arena step 90; uitop; arena" && {
    stack_is amb_open 1 "menu tools tools_ambient"
    [ "$(am_v "$(am_l 1)" blobs)" -ge 2 ] || { EVID="$(am_l 1)"; fail amb_lava_start; }; }
# every scene runs: its own things show up (the scene is the 場景 value of the ring: games rule)
nowrun amb_scenes "screen on; vol 0; tools ambient; @0.3; arena hold 1; games rule 1; arena game seed 3; arena step 120; uitop; arena; games rule 2; arena game seed 3; arena step 120; uitop; arena; games rule 3; arena game seed 3; arena step 60; uitop; arena" && {
    [ "$(am_v "$(am_l 1)" fish)" = 5 ] && [ "$(am_v "$(am_l 1)" bubbles)" -ge 1 ] || { EVID="$(am_l 1)"; fail amb_fish; }
    [ "$(am_v "$(am_l 2)" flames)" -ge 10 ] || { EVID="$(am_l 2)"; fail amb_fire; }
    [ "$(am_v "$(am_l 3)" scene)" = 3 ] && [ "$(am_v "$(am_l 3)" prims)" -ge 8 ] || { EVID="$(am_l 3)"; fail amb_night; }; }
# the same seed and steps give the same picture data (no virtual-time phase)
nowrun amb_deterministic "screen on; vol 0; tools ambient; @0.3; arena hold 1; games rule 1; arena game seed 5; arena step 100; uitop; arena; @2; arena game seed 5; arena step 100; uitop; arena" && {
    [ "$(am_l 1)" = "$(am_l 2)" ] || { EVID="$(am_l 1) | $(am_l 2)"; fail amb_deterministic; }; }
# taps poke: a lava blob splits; the aquarium drops food; the fire flares (more flames at once)
nowrun amb_poke_lava "screen on; vol 0; tools ambient; @0.3; arena hold 1; arena game seed 3; arena step 30; uitop; arena; arena tap 0 20; uitop; arena" && {
    [ "$(am_v "$(am_l 2)" blobs)" -ge "$(am_v "$(am_l 1)" blobs)" ] || { EVID="$(am_l 1) | $(am_l 2)"; fail amb_poke_lava; }; }
nowrun amb_poke_fish "screen on; vol 0; tools ambient; @0.3; arena hold 1; games rule 1; arena game seed 3; arena step 30; arena tap 0 -60; uitop; arena; arena step 300; uitop; arena" && {
    [ "$(am_v "$(am_l 1)" food)" = 1 ] && [ "$(am_v "$(am_l 2)" food)" = 0 ] || { EVID="$(am_l 1) | $(am_l 2)"; fail amb_feed_eaten; }; }
nowrun amb_poke_fire "screen on; vol 0; tools ambient; @0.3; arena hold 1; games rule 2; arena game seed 3; arena step 60; uitop; arena; arena tap 0 0; uitop; arena" && {
    [ "$(am_v "$(am_l 2)" flames)" -gt "$(am_v "$(am_l 1)" flames)" ] || { EVID="$(am_l 1) | $(am_l 2)"; fail amb_poke_fire; }; }
# a rare event on demand
nowrun amb_rare "screen on; vol 0; tools ambient; @0.3; arena hold 1; games rule 1; arena game seed 3; arena game rare; arena step 30; uitop; arena; arena game poke -60 0; uitop; arena" && {
    [ "$(am_v "$(am_l 1)" gold)" = 1 ] || { EVID="$(am_l 1)"; fail amb_rare_seen; }; }
# the speed cell changes the speed at once, keeps the scene
nowrun amb_speed "screen on; vol 0; tools ambient; @0.3; arena hold 1; uitop; arena; games mode 1; uitop; arena; games mode 2; uitop; arena" && {
    [ "$(am_v "$(am_l 1)" speed)" = 100 ] && [ "$(am_v "$(am_l 2)" speed)" = 160 ] && [ "$(am_v "$(am_l 3)" speed)" = 60 ] || { EVID="$(am_l 1) | $(am_l 2) | $(am_l 3)"; fail amb_speed; }; }
# a tap borrows the knob: the turn is the brightness (65 -> 80), four idle seconds later it is the volume again and the 返回 pill shows
nowrun amb_knob "screen on; vol 0; tools ambient; @1; uitop; arena; simtap 180 200; simknob 1; @0.2; uitop; arena; @5; simknob 1; @0.2; uitop; arena; exit; simvol" && {
    [ "$(am_v "$(am_l 1)" bright)" = 65 ] && [ "$(am_v "$(am_l 2)" bright)" = 80 ] && [ "$(am_v "$(am_l 3)" bright)" = 80 ] || { EVID="$(am_l 1) | $(am_l 2) | $(am_l 3)"; fail amb_knob_brightness; }
    grep -q '^APP exit attached=1 mode=C a=1' <<<"$out" || fail amb_exit_pill
    [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="vol $(vols)"; fail amb_volume_back; }; }
# the knob never stays pinned: no tap, a turn is the volume at once
nowrun amb_no_pin "screen on; vol 0; tools ambient; @8; simknob 1; @0.2; uitop; arena; simvol" && {
    [ "$(am_v "$(am_l 1)" bright)" = 65 ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(am_l 1) vol $(vols)"; fail amb_no_pin; }; }
# covers never leave the scene frozen: the screen off stops the timer (no steps), the screen on restarts it by itself (no tap); the same for the ring and for 說明
at() { sed -n "s/.* ticks=\([0-9]*\) .*/\1/p" <<<"$1"; }
ad() { grep '^APP arena name=ambient' <<<"$out" | sed -n "${1}p"; }
nowrun amb_screen_off "screen on; vol 0; tools ambient; @1; arena; screen off; @1; arena; @1; arena; screen on; @1; arena" && {
    t1=$(at "$(ad 2)"); t2=$(at "$(ad 3)"); t3=$(at "$(ad 4)")
    [ -n "$t1" ] && [ "$t1" = "$t2" ] && [ "$t3" -gt "$t2" ] || { EVID="ticks off $t1 $t2 on $t3"; fail amb_screen_off_restart; }; }
nowrun amb_ring_cover "screen on; vol 0; tools ambient; @1; games ring; @1; arena; @1; arena; popring -1; @1; arena" && {
    t1=$(at "$(ad 1)"); t2=$(at "$(ad 2)"); t3=$(at "$(ad 3)")
    [ -n "$t1" ] && [ "$t1" = "$t2" ] && [ "$t3" -gt "$t2" ] || { EVID="ticks ring $t1 $t2 closed $t3"; fail amb_ring_cover_restart; }; }
nowrun amb_help_cover "screen on; vol 0; tools ambient; @1; games ring; popring 4; @1; arena; back; @1; arena" && {
    t1=$(grep '^APP arena name=ambient' <<<"$out" | tail -2 | head -1); t2=$(grep '^APP arena name=ambient' <<<"$out" | tail -1)
    [ "$(at "$t2")" -gt "$(at "$t1")" ] 2>/dev/null || { EVID="$t1 | $t2"; fail amb_help_cover_restart; }; }
# a knob turn on a covered-then-back scene is the field's again after a tap (not frozen): the brightness steps
# the ring: 場景 / 色調 / 亮度 / 重來 and 說明 last (five cells); the brightness cell steps; 說明 opens the explain page; 重來 starts the scene again
nowrun amb_ring "screen on; vol 0; tools ambient; @1; games ring; @0.3; popring 2; uitop; arena; popring 4; @0.3; uitop; back; @0.3; games ring; popring 3; @0.3; uitop" && {
    [ "$(grep '^APP popring open=1' <<<"$out" | sed -n 1p | sed 's/.* cells=\([0-9]*\) .*/\1/')" = 5 ] || fail amb_ring_cells
    [ "$(am_v "$(am_l 1)" bright)" = 80 ] || { EVID="$(am_l 1)"; fail amb_ring_bright; }
    grep -qx 'APP uitop explain' <<<"$out" && [ "$(grep '^APP uitop' <<<"$out" | tail -1)" = "APP uitop tools_ambient" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail amb_ring_help_restart; }; }
# no Back button: the pill's 返回 leaves for the 小工具 ring; the tools ring has the 氛圍 cell
nowrun amb_back "screen on; vol 0; tools ambient; @1; simknob 1; @0.2; exit tap back; @0.3; uitop; uitop; arena" && {
    [ "$(grep '^APP uitop' <<<"$out" | tail -1)" = "APP uitop tools" ] && grep -qx 'APP arena none' <<<"$out" || { EVID="$(tp)"; fail amb_back; }; }
