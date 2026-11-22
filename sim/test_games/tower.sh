# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G3: 疊塔 on the shared play field ui_arena (app/main/ui/scr_game_tower.c, ui_arena.c; rules in components/player_core/src/pc_game_tower.c); sourced by sim/test.sh ---
# `arena tap 0 0` is a tap on the field (any point: one action, the first starts the slide, the next ones drop the block), `arena step <n>` n fixed steps (1/30 s) now, `arena hold 1`
# stops the field's own steps, `arena game x <units>` puts the sliding block at x (a drop's offset on demand; the tower's base is 60 wide at 0), `arena game w <units>` its width,
# `arena game gold 1|0` a golden block or not, `arena game put <n>` n perfect floors at once. Every `arena` command ends with both dumps ("APP arena ..." then
# "APP game tower state=<0 wait|1 play|2 over> n= height= level= combo= perfects= golds= gold= sx= sw= top_cx= top_w= cam= speed=<100 = normal> rule= debris= paused=").
# TWO = the dumps the test's own prefix already made (`arena hold`, `arena game ...` and `arena tap` each dump): TW makes 2, TWS 3; tw_g / tw_a skip them
TWO=0
tw_g() { grep '^APP game tower state=' <<<"$out" | sed -n "$(($1 + TWO))p"; }
tw_a() { grep '^APP arena name=' <<<"$out" | sed -n "$(($1 + TWO))p"; }
tw_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
TW="screen on; games open tower; @0.3; games mode 0; games style 0; games rule 0; arena hold 1; arena game gold 0"
TWS="$TW; arena tap 0 0"                                        # started: the slide runs by steps only
# open: waiting for the first tap, the timer stopped, the knob still the volume, the base block and the sliding block are the two objects
nowrun tw_open "screen on; games open tower; @0.3; games mode 0; games style 0; games rule 0; arena; uiboxes; uitop" && {
    a=$(tw_a 1); g=$(tw_g 1)
    [ "$(tw_v "$a" objs)" = 2/20 ] && [ "$(tw_v "$a" run)" = 0 ] && [ "$(tw_v "$a" knob)" = 0 ] && [ "$(tw_v "$g" state)" = 0 ] && [ "$(tw_v "$g" n)" = 1 ] \
        && [ "$(tw_v "$g" top_w)" = 60 ] && [ "$(tp)" = "tools_game_tower " ] || { EVID="$a | $g | $(tp)"; fail tw_open; }
    ! grep -q '^APP uibox label .*|Tap to start$' <<<"$out" || { EVID="$(grep '^APP uibox label' <<<"$out" | head -1)"; fail tw_open_status; }; }
# the first tap only starts the slide; the block then sweeps within +-45 (steps)
TWO=2
nowrun tw_start "$TW; arena tap 0 0; arena; arena step 30; arena step 60; arena step 90" && {
    [ "$(tw_v "$(tw_g 1)" state)" = 1 ] && [ "$(tw_v "$(tw_g 1)" n)" = 1 ] && [ "$(tw_v "$(tw_a 1)" run)" = 1 ] || { EVID="$(tw_g 1) | $(tw_a 1)"; fail tw_start; }
    sx=""; for i in 3 4 5; do sx="$sx $(tw_v "$(tw_g $i)" sx)"; done
    [ "$(tw_v "$(tw_g 3)" sx)" != "$(tw_v "$(tw_g 4)" sx)" ] && ! tr ' ' '\n' <<<"$sx" | awk 'NF && ($1+0>45||$1+0<-45){f=1} END{exit !f}' || { EVID="sx:$sx"; fail tw_slide_range; }; }
# a perfect drop: the width kept, a combo of 1; a cut keeps what overlaps (50 wide, centre 5) and a piece falls; the next block is as wide
TWO=3
nowrun tw_drops "$TWS; arena game x 0; arena tap 0 0; arena game gold 0; arena game x 10; arena tap 0 0; arena game gold 0; arena game x 5; arena tap 0 0; arena game gold 0; arena game x 5; arena tap 0 0" && {
    g=$(tw_g 2); [ "$(tw_v "$g" n)" = 2 ] && [ "$(tw_v "$g" combo)" = 1 ] && [ "$(tw_v "$g" perfects)" = 1 ] && [ "$(tw_v "$g" top_w)" = 60 ] && [ "$(tw_v "$g" debris)" = 0 ] || { EVID="$g"; fail tw_perfect; }
    g=$(tw_g 5); [ "$(tw_v "$g" n)" = 3 ] && [ "$(tw_v "$g" combo)" = 0 ] && [ "$(tw_v "$g" top_w)" = 50 ] && [ "$(tw_v "$g" top_cx)" = 5 ] && [ "$(tw_v "$g" debris)" = 1 ] && [ "$(tw_v "$g" sw)" = 50 ] \
        || { EVID="$g"; fail tw_cut; }
    g=$(tw_g 8); [ "$(tw_v "$g" combo)" = 1 ] && [ "$(tw_v "$g" top_w)" = 50 ] || { EVID="$g"; fail tw_perfect_again; }
    g=$(tw_g 11); [ "$(tw_v "$g" combo)" = 2 ] && [ "$(tw_v "$g" top_w)" = 52 ] || { EVID="$g"; fail tw_combo_grows; }; }
# taps only: a turn of the knob never drops a block, before or after a tap lent it to the field (while the game runs the field keeps the borrow, so it is not the volume then; the keys of a build without a knob act as taps)
TWO=0
nowrun tw_knob "screen on; games open tower; @0.3; vol 0; arena hold 1; arena game gold 0; @0.3; simknob 1; @0.3; simvol; arena; arena tap 0 0; arena game x 0; simknob 1; simknob -2; @0.1; arena; simvol" && {
    [ "$(tw_v "$(tw_g 3)" state)" = 0 ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(tw_g 3) vol $(vols)"; fail tw_knob_volume_first; }
    [ "$(tw_v "$(tw_a 4)" knob)" = 1 ] && [ "$(tw_v "$(tw_g 6)" state)" = 1 ] && [ "$(tw_v "$(tw_g 6)" n)" = 1 ] || { EVID="$(tw_a 4) | $(tw_g 6)"; fail tw_knob_no_drop; }; }
nowrun tw_borrow_timeout "screen on; games open tower; @0.3; vol 0; arena hold 1; arena tap 0 0; @4.5; arena; simknob 1; @0.2; simvol" && {
    [ "$(tw_v "$(tw_a 1)" knob)" = 0 ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(tw_a 1) vol $(vols)"; fail tw_borrow_timeout; }; }
# the camera: 15 floors at once -> the tower's top sits at the same height (cam = 16 * 10 - 90 = 70 world units), the debris and effects keep within the object slots
TWO=3
nowrun tw_camera "$TWS; arena game put 15; arena; arena step 20; arena" && {
    [ "$(tw_v "$(tw_g 2)" height)" = 15 ] && [ "$(tw_v "$(tw_g 2)" cam)" = 70 ] && [ "$(tw_v "$(tw_a 2)" objs | cut -d/ -f1)" -le 18 ] || { EVID="$(tw_g 2) | $(tw_a 2)"; fail tw_camera; }; }
# a step redraws little: the moving block's boxes, far below the field's area
nowrun tw_step_redraws_little "$TWS; arena game put 8; arena step 3; arena" && {
    a=$(tw_a 2); [ "$(tw_v "$a" inv_px)" -gt 0 ] && [ "$(tw_v "$a" inv_px)" -lt $(( $(tw_v "$a" field_px) / 3 )) ] || { EVID="$a"; fail tw_step_redraws_little; }; }
# a miss ends it: the block falls, the end line and the card after its delay (the height as points, the level), a tap = again
nowrun tw_over "$TWS; arena game put 3; arena game x 70; arena tap 0 0; arena; games result; @1; games result; uiboxes; games result tap; arena" && {
    g=$(tw_g 4); [ "$(tw_v "$g" state)" = 2 ] && [ "$(tw_v "$g" height)" = 3 ] && [ "$(tw_v "$g" debris)" = 1 ] || { EVID="$g"; fail tw_over_state; }
    grep -qx 'APP game tower end outcome=0 streak=0 best=3 new_best=1 points=3 level=1 sound=win' <<<"$out" || { EVID="$(grep ' end ' <<<"$out")"; fail tw_end_line; }
    grep -qx 'APP game_result up=1 shown=0 title=Toppled points=3 level=1 best=3 new_best=1 gold=0' <<<"$out" && grep -qx 'APP game_result up=1 shown=1 title=Toppled points=3 level=1 best=3 new_best=1 gold=0' <<<"$out" \
        || { EVID="$(grep '^APP game_result' <<<"$out" | tr '\n' '|')"; fail tw_card; }
    grep -q '^APP uibox label .*|Toppled$' <<<"$out" && [ "$(tw_v "$(tw_g 5)" state)" = 0 ] && [ "$(tw_v "$(tw_g 5)" n)" = 1 ] || { EVID="$(tw_g 5)"; fail tw_card_again; }; }
# the debris falls out of the field and the field rests (nothing runs after the end once it is gone)
nowrun tw_debris_gone "$TWS; arena game x 70; arena tap 0 0; arena; arena step 120; arena" && {
    [ "$(tw_v "$(tw_g 2)" debris)" = 1 ] && [ "$(tw_v "$(tw_g 5)" debris)" = 0 ] && [ "$(tw_v "$(tw_a 5)" run)" = 0 ] || { EVID="$(tw_g 2) | $(tw_g 5) | $(tw_a 5)"; fail tw_debris_gone; }; }
# the golden block: the full width comes back, no cut
nowrun tw_gold "$TWS; arena game x 10; arena tap 0 0; arena game gold 1; arena game x 13; arena tap 0 0; arena" && {
    g=$(tw_g 5); [ "$(tw_v "$g" golds)" = 1 ] && [ "$(tw_v "$g" top_w)" = 60 ] && [ "$(tw_v "$g" n)" = 3 ] || { EVID="$g"; fail tw_golden_block; }; }
# rules: 窄塔 starts blocks at the halving cap (30 at the 10th floor), 搖擺 swings (sx moves by steps, the dump says rule 2)
TWO=2
nowrun tw_rules "$TW; games rule 1; arena tap 0 0; arena game put 10; arena; games rule 2; arena tap 0 0; arena step 10; arena" && {
    g=$(tw_g 2); [ "$(tw_v "$g" rule)" = 1 ] && [ "$(tw_v "$g" height)" = 10 ] && [ "$(tw_v "$g" sw)" = 30 ] || { EVID="$g"; fail tw_rule_narrow; }
    [ "$(tw_v "$(tw_g 6)" rule)" = 2 ] && [ "$(tw_v "$(tw_g 4)" sx)" != "$(tw_v "$(tw_g 6)" sx)" ] || { EVID="$(tw_g 4) | $(tw_g 6)"; fail tw_rule_swing; }; }
nowrun tw_speed "$TW; games mode 1; arena; games mode 2; arena; games mode 0; arena" && {
    [ "$(tw_v "$(tw_g 1)" speed)" = 130 ] && [ "$(tw_v "$(tw_g 2)" speed)" = 75 ] && [ "$(tw_v "$(tw_g 3)" speed)" = 100 ] || { EVID="$(tw_g 1) | $(tw_g 2) | $(tw_g 3)"; fail tw_speed; }; }
# the screen off pauses a running slide; the next tap only resumes
TWO=1
nowrun tw_screen_off "screen on; games open tower; @0.3; games rule 0; arena game gold 0; simtap 180 193; @0.3; screen off; @0.5; arena; @1; arena; screen on; @0.5; arena; simtap 180 193; @0.1; arena" && {
    t1=$(tw_v "$(tw_a 1)" ticks); t2=$(tw_v "$(tw_a 2)" ticks)
    [ "$(tw_v "$(tw_a 1)" paused)" = 1 ] && [ "$(tw_v "$(tw_a 1)" run)" = 0 ] && [ "$t1" = "$t2" ] && grep -qx 'APP game tower paused=1' <<<"$out" || { EVID="$(tw_a 1) | $(tw_a 2)"; fail tw_screen_off_pause; }
    [ "$(tw_v "$(tw_a 4)" paused)" = 0 ] && [ "$(tw_v "$(tw_g 4)" n)" = 1 ] && grep -qx 'APP game tower paused=0' <<<"$out" || { EVID="$(tw_a 4) | $(tw_g 4)"; fail tw_tap_resumes; }; }
TWO=0
nowrun tw_ring_pauses "screen on; games open tower; @0.3; simtap 180 193; @0.2; frame show; simtaprow 1; @0.3; arena; popring -1; @0.2; simtap 180 193; @0.2; arena" && {
    [ "$(tw_v "$(tw_a 1)" paused)" = 1 ] && [ "$(tw_v "$(tw_a 2)" paused)" = 0 ] && [ "$(tw_v "$(tw_a 2)" run)" = 1 ] || { EVID="$(tw_a 1) | $(tw_a 2)"; fail tw_ring_pauses; }; }
nowrun tw_back_mid_play "screen on; games open tower; @0.3; simtap 180 193; @0.5; exit pause; @0.2; exit tap back; @0.3; uitop; arena" && {
    [ "$(tp)" = "tools_games " ] && grep -qx 'APP arena none' <<<"$out" || { EVID="$(tp) $(grep '^APP arena' <<<"$out")"; fail tw_back_mid_play; }; }
nowrun tw_own_help "screen on; games open tower; @0.3; frame show; simtaprow 1; @0.3; popring 3; @0.3; help show; simflick up; @0.3; uitop" && {
    grep -qx 'APP explain id=game title=Stack tower paras=1 qr=0 web=0' <<<"$out" && [ "$(tp)" = "tools_game_tower " ] || { EVID="$(grep '^APP explain' <<<"$out") $(tp)"; fail tw_own_help; }; }
nowrun tw_gold_game "screen on; games open tower; @0.3; games gold 1; frame show; simtaprow 1; @0.3; popring 2; @0.3; arena; games gold auto" && {
    [ "$(tw_v "$(tw_g 1)" n)" = 1 ] && grep -q '^APP game tower new rule=0 style=0 mode=0 gold=1$' <<<"$out" || { EVID="$(tw_g 1)"; fail tw_gold_game; }; }
nowrun tw_music_plays "screen on; vol 0; track 3; @2; audio info; games open tower; @0.3; simtap 180 193; @2; audio info; simflick up; @0.3; audio info" && {
    a=$(grep '^APP audio info ' <<<"$out" | sed 's/ underruns_src.*//' | sort -u | wc -l)
    [ "$(grep -c '^APP audio info ' <<<"$out")" = 3 ] && [ "$a" = 1 ] && ! grep -q '^APP mp3 stop\|^APP play stop' <<<"$out" || { EVID="$(grep '^APP audio info' <<<"$out" | cut -c1-80 | tr '\n' '|')"; fail tw_music_plays; }; }
# --- end 疊塔 (tower stacking) ---
