# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G1: 環形打磚塊 on the shared play field ui_arena (app/main/ui/scr_game_breakout.c, ui_arena.c; rules and physics in components/player_core/src/pc_game_breakout.c); sourced by sim/test.sh ---
# The field is the circle between the HEAD row and Back, centred at screen (180,193); `arena tap <x> <y>` touches it at a point from that centre, `arena step <n>` runs n fixed
# steps (1/30 s) now, `arena hold 1` stops the field's own steps (the tests' steps are then the only ones). The dumps: "APP arena ... knob=<0|1> ..." (the knob lent to the
# field) and "APP game breakout state=<0 serve|1 play|2 clear|3 over> level= lives= score= ... paddle=<deg> half=<deg> ...". The paddle starts at 90 (6 o'clock).
# Every `arena` command but `at` ends with both dumps, so the n-th dump counts every arena command before it.
bo_g() { grep '^APP game breakout state=' <<<"$out" | sed -n "${1}p"; }
bo_a() { grep '^APP arena name=' <<<"$out" | sed -n "${1}p"; }
bo_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }        # <line> <key>: the value
BO="screen on; games open breakout; @0.3"
# open: the ball waits on the paddle, the timer is stopped (nothing moves), the knob is still the volume, the status asks for the serve
nowrun bo_open "$BO; arena; uiboxes; uitop" && {
    a=$(bo_a 1); g=$(bo_g 1)
    [ "$(bo_v "$a" objs)" = 26/44 ] && [ "$(bo_v "$a" run)" = 0 ] && [ "$(bo_v "$a" knob)" = 0 ] && [ "$(bo_v "$g" state)" = 0 ] && [ "$(bo_v "$g" bricks)" = 24 ] \
        && [ "$(bo_v "$g" lives)" = 3 ] && [ "$(bo_v "$g" paddle)" = 90 ] && [ "$(tp)" = "tools_game_breakout " ] || { EVID="$a | $g | $(tp)"; fail bo_open; }
    ! grep -q '^APP uibox label .*|Tap to serve$' <<<"$out" || { EVID="$(grep '^APP uibox label' <<<"$out" | head -1)"; fail bo_open_status; }; }
# the slots' boxes are screen px from the first set on (the field is laid out at create): the paddle's box sits inside the field's square (87..272, 100..285)
nowrun bo_boxes_on_screen "$BO; arena at 36; arena at 38" && {
    b=$(sed -n 's/^APP arena at 36 .* box=\([0-9-]*\),\([0-9-]*\),.*/\1 \2/p' <<<"$out")
    [ -n "$b" ] && [ "${b% *}" -ge 87 ] && [ "${b#* }" -ge 100 ] || { EVID="$(grep '^APP arena at' <<<"$out" | tr '\n' '|')"; fail bo_boxes_on_screen; }; }
# before a tap on the field the knob is the volume (the paddle does not move); a tap serves and lends the knob to the paddle: one detent = 6 degrees, both ways
nowrun bo_knob "$BO; vol 0; @0.3; simknob 1; @0.3; simvol; frame hide; arena; simtap 180 193; @0.1; arena; simknob 2; @0.1; arena; simknob -3; @0.1; arena; simvol" && {
    [ "$(bo_v "$(bo_g 1)" paddle)" = 90 ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(bo_g 1) vol $(vols)"; fail bo_knob_volume_first; }
    v1=$(vols | cut -d' ' -f1)
    [ "$(bo_v "$(bo_g 2)" state)" = 1 ] && [ "$(bo_v "$(bo_a 2)" knob)" = 1 ] && [ "$(bo_v "$(bo_g 3)" paddle)" = 102 ] && [ "$(bo_v "$(bo_g 4)" paddle)" = 84 ] \
        && [ "$(vols | cut -d' ' -f2)" = "$v1" ] || { EVID="$(bo_g 2) | $(bo_g 3) | $(bo_g 4) | vol $(vols)"; fail bo_knob_paddle; }; }
# a tap beside the paddle (its right, then its left as seen from the centre; at 6 o'clock that is the screen's right and left) moves it 18 degrees that way; the touch the same
nowrun bo_tap_sides "$BO; arena hold 1; arena tap 0 0; arena tap 60 60; arena; arena tap -60 60; arena; simtap 240 253; @0.1; arena" && {
    [ "$(bo_v "$(bo_g 3)" paddle)" = 72 ] && [ "$(bo_v "$(bo_g 5)" paddle)" = 90 ] && [ "$(bo_v "$(bo_g 7)" paddle)" = 72 ] || { EVID="$(bo_g 3) | $(bo_g 5) | $(bo_g 7)"; fail bo_tap_sides; }; }
# the borrowed knob's 4 s: with the game held nothing keeps it, so it is the volume again; while the game runs (steps) it is kept alive
nowrun bo_borrow_timeout "$BO; vol 0; arena hold 1; arena tap 0 0; @4.5; arena; simknob 1; @0.2; arena; simvol" && {
    [ "$(bo_v "$(bo_a 3)" knob)" = 0 ] && [ "$(bo_v "$(bo_g 4)" paddle)" = 90 ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(bo_a 3) | $(bo_g 4) | vol $(vols)"; fail bo_borrow_timeout; }; }
nowrun bo_borrow_kept "$BO; arena hold 1; arena tap 0 0; @3; arena step 30; @3; arena; @1.5; arena" && {
    [ "$(bo_v "$(bo_a 4)" knob)" = 1 ] && [ "$(bo_v "$(bo_a 5)" knob)" = 0 ] || { EVID="$(bo_a 4) | $(bo_a 5)"; fail bo_borrow_kept; }; }
# fixed steps are deterministic: 23 steps after the serve the first brick is broken (step 19), its burst is in flight; a step redraws only what moved
nowrun bo_steps "$BO; arena hold 1; arena tap 0 0; arena step 23; arena step 1; sfx" && {
    a=$(bo_a 3); g=$(bo_g 3)
    [ "$(bo_v "$a" ticks)" = 23 ] && [ "$(bo_v "$a" fx)" = 1 ] && [ "$(bo_v "$g" score)" = 10 ] && [ "$(bo_v "$g" bricks)" = 23 ] || { EVID="$a | $g"; fail bo_steps; }
    a=$(bo_a 4); n=$(bo_v "$a" inv_n); px=$(bo_v "$a" inv_px); f=$(bo_v "$a" field_px)
    [ "$n" -ge 1 ] && [ "$n" -le 4 ] && [ "$px" -lt $((f / 8)) ] || { EVID="$a"; fail bo_step_redraws_little; }
    [ "$(sed -n 's/^APP sfx .* posted=\([0-9]*\) .*/\1/p' <<<"$out")" = 2 ] || { EVID="$(grep '^APP sfx' <<<"$out")"; fail bo_step_sounds; }; }   # the serve and the break: one sound each
# the screen off mid-rally: the game pauses at once (no steps while off), stays paused when the screen comes back, the next tap only resumes (the paddle stays)
nowrun bo_screen_off "$BO; simtap 180 193; @0.2; screen off; @0.5; arena; @1; arena; screen on; @0.5; arena; simtap 180 193; @0.1; arena" && {
    t1=$(bo_v "$(bo_a 1)" ticks); t2=$(bo_v "$(bo_a 2)" ticks); t3=$(bo_v "$(bo_a 3)" ticks)
    [ "$(bo_v "$(bo_a 1)" paused)" = 1 ] && [ "$(bo_v "$(bo_a 1)" run)" = 0 ] && [ "$t1" = "$t2" ] && [ "$t2" = "$t3" ] && [ "$(bo_v "$(bo_a 3)" paused)" = 1 ] \
        && grep -qx 'APP game breakout paused=1' <<<"$out" || { EVID="$(bo_a 1) | $(bo_a 3)"; fail bo_screen_off_pause; }
    [ "$(bo_v "$(bo_a 4)" paused)" = 0 ] && [ "$(bo_v "$(bo_g 4)" paddle)" = 90 ] && [ "$(bo_v "$(bo_g 4)" state)" = 1 ] && grep -qx 'APP game breakout paused=0' <<<"$out" \
        || { EVID="$(bo_a 4) | $(bo_g 4)"; fail bo_screen_on_tap_resumes; }; }
# the game's ring over the field pauses it too
nowrun bo_ring_pauses "$BO; simtap 180 193; @0.2; frame show; simtaprow 1; @0.3; arena; popring -1; @0.2; simtap 180 193; @0.2; arena" && {
    [ "$(bo_v "$(bo_a 1)" paused)" = 1 ] && [ "$(bo_v "$(bo_a 2)" paused)" = 0 ] && [ "$(bo_v "$(bo_a 2)" run)" = 1 ] || { EVID="$(bo_a 1) | $(bo_a 2)"; fail bo_ring_pauses; }; }
# three balls run out (a still paddle; the first game after boot is the same every time): the end, the result card after its delay (points, level, best), a tap = again
nowrun bo_game_over "$BO; arena hold 1; arena tap 0 0; arena step 600; arena tap 0 0; arena step 600; arena tap 0 0; arena step 600; arena; games result; @1; games result; uiboxes; games result tap; arena" && {
    grep -qx 'APP game breakout end outcome=0 streak=0 best=260 new_best=1 points=260 level=1 sound=win' <<<"$out" && [ "$(bo_v "$(bo_g 8)" state)" = 3 ] \
        || { EVID="$(grep ' end ' <<<"$out") $(bo_g 8)"; fail bo_game_over; }
    grep -qx 'APP game_result up=1 shown=0 title=Game over points=260 level=1 best=260 new_best=1 gold=0' <<<"$out" \
        && grep -qx 'APP game_result up=1 shown=1 title=Game over points=260 level=1 best=260 new_best=1 gold=0' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out" | tr '\n' '|')"; fail bo_card; }
    grep -q '^APP uibox label .*|Game over$' <<<"$out" && [ "$(bo_v "$(bo_g 9)" state)" = 0 ] && [ "$(bo_v "$(bo_g 9)" score)" = 0 ] && [ "$(bo_v "$(bo_g 9)" lives)" = 3 ] \
        || { EVID="$(bo_g 9)"; fail bo_card_again; }; }
# the rules: 雙拍 a second, shorter paddle opposite (slot 37); 黏球 the ball sticks on the paddle, a tap lets it go
nowrun bo_rule_twin "$BO; games rule 1; arena; arena at 37; games rule 0; arena at 37" && {
    [ "$(bo_v "$(bo_g 1)" half)" = 15 ] && grep -q '^APP arena at 37 kind=arc ' <<<"$out" && grep -q '^APP arena at 37 kind=- ' <<<"$out" || { EVID="$(bo_g 1) $(grep '^APP arena at' <<<"$out" | tr '\n' '|')"; fail bo_rule_twin; }; }
nowrun bo_rule_catch "$BO; games rule 2; arena hold 1; arena tap 0 0; arena step 60; arena; arena tap 0 0; arena" && {
    [ "$(bo_v "$(bo_g 4)" stuck)" = 1 ] && [ "$(bo_v "$(bo_g 6)" stuck)" = 0 ] && [ "$(bo_v "$(bo_g 6)" rule)" = 2 ] || { EVID="$(bo_g 4) | $(bo_g 6)"; fail bo_rule_catch; }; }
# the styles: 霓虹 pulses with the music only while the ball flies (fix round 1 I1): waiting for the serve the timer stays stopped; served with music, the feed runs
nowrun bo_style_pulse "screen on; vol 0; track 3; @2; games open breakout; @0.3; games style 1; @0.5; arena; simtap 180 193; @1; arena; games style 0; @0.3; arena" && {
    [ "$(bo_v "$(bo_a 1)" run)" = 0 ] && [ "$(bo_v "$(bo_a 2)" run)" = 1 ] && [ "$(bo_v "$(bo_a 2)" energy)" -gt 0 ] && [ "$(bo_v "$(bo_a 3)" energy)" = 0 ] \
        || { EVID="$(bo_a 1) | $(bo_a 2) | $(bo_a 3)"; fail bo_style_pulse; }; }
# fix round 1 I1: what LVGL redraws stays small on a turning level (the boss ring, level 5) and on 霓虹 level 2 with music: few areas a frame (a turning ring
# merges into one band), far from the whole screen (129600 px); a held step sends at most 6 areas
bo_px() { grep '^APP uistat ' <<<"$out" | sed -n "${1}p" | sed -n 's/.* areas=\([0-9]*\)\.[0-9]* px_avg=\([0-9]*\).*/\1 \2/p'; }
nowrun bo_redraw_rotating "$BO; arena game level 5; arena tap 0 0; @0.2; uistat; @2; uistat; arena hold 1; arena step 1; arena step 1; arena step 1" && {
    read -r ar px <<<"$(bo_px 2)"
    [ -n "$px" ] && [ "$ar" -le 6 ] && [ "$px" -lt 65000 ] || { EVID="$(grep '^APP uistat' <<<"$out" | tr '\n' '|')"; fail bo_redraw_rotating; }
    for k in 4 5 6; do [ "$(bo_v "$(bo_a $k)" inv_n)" -le 6 ] || { EVID="$(bo_a $k)"; fail bo_redraw_rotating_step; break; }; done
    [ "$(bo_v "$(bo_g 1)" level)" = 5 ] || { EVID="$(bo_g 1)"; fail bo_redraw_rotating_level; }; }
nowrun bo_redraw_neon "screen on; vol 0; track 3; @2; games open breakout; @0.3; games style 1; arena game level 2; arena tap 0 0; @0.2; uistat; @2; uistat; arena" && {
    read -r ar px <<<"$(bo_px 2)"
    [ -n "$px" ] && [ "$ar" -le 6 ] && [ "$px" -lt 60000 ] && [ "$(bo_v "$(bo_a 3)" energy)" -gt 0 ] || { EVID="$(grep '^APP uistat' <<<"$out" | tr '\n' '|') $(bo_a 3)"; fail bo_redraw_neon; }; }   # dumps: level, tap, arena
# fix round 1 I3: after a pause longer than the knob's 4 s, the resuming tap lends the knob to the paddle again
nowrun bo_resume_borrows "$BO; vol 0; simtap 180 193; @0.2; screen off; @5; screen on; @0.3; arena; simtap 180 193; @0.1; arena; simknob 2; @0.1; arena; simvol" && {
    [ "$(bo_v "$(bo_a 1)" paused)" = 1 ] && [ "$(bo_v "$(bo_a 1)" knob)" = 0 ] && [ "$(bo_v "$(bo_a 2)" paused)" = 0 ] && [ "$(bo_v "$(bo_a 2)" knob)" = 1 ] \
        && [ "$(bo_v "$(bo_g 3)" paddle)" = 102 ] && [ "$(vols | cut -d' ' -f1)" = 0 ] || { EVID="$(bo_a 1) | $(bo_a 2) | $(bo_g 3) | vol $(vols)"; fail bo_resume_borrows; }; }
# fix round 1 m1: 重來 from the ring that paused a rally: no pause left, the first tap serves
nowrun bo_new_game_unpauses "$BO; simtap 180 193; @0.2; frame show; simtaprow 1; @0.3; popring 2; @0.3; arena; simtap 180 193; @0.1; arena" && {
    [ "$(bo_v "$(bo_a 1)" paused)" = 0 ] && [ "$(bo_v "$(bo_g 1)" state)" = 0 ] && [ "$(bo_v "$(bo_g 2)" state)" = 1 ] || { EVID="$(bo_a 1) | $(bo_g 1) | $(bo_g 2)"; fail bo_new_game_unpauses; }; }
# fix round 1 m7: the page's power-up and boom paths on demand (`arena game put`): every brick a 2-balls capsule: the one dropped at step 19 is caught, two balls;
# every brick a boom: the first hit takes the whole level in one chain (level clear), its sounds posted
nowrun bo_power_page "$BO; arena hold 1; arena game put all 4 1; arena tap 0 0; arena step 70" && {
    g=$(bo_g 4); [ "$(bo_v "$g" balls)" = 2 ] && [ "$(bo_v "$g" bricks)" = 22 ] || { EVID="$g"; fail bo_power_page; }; }
nowrun bo_boom_page "$BO; arena hold 1; arena game put all 3; arena tap 0 0; arena step 23; sfx" && {
    g=$(bo_g 4); a=$(bo_a 4)
    [ "$(bo_v "$g" state)" = 2 ] && [ "$(bo_v "$g" bricks)" = 0 ] && [ "$(bo_v "$g" combo_best)" = 24 ] && [ "$(bo_v "$a" fx)" -ge 8 ] && [ "$(bo_v "$a" inv_n)" -le 6 ] \
        && [ "$(sed -n 's/^APP sfx .* posted=\([0-9]*\) .*/\1/p' <<<"$out")" = 2 ] || { EVID="$g | $a"; fail bo_boom_page; }; }
# ruling (fix round 1): 黏球 catches a ball three times, the dump counts them
nowrun bo_catch_count "$BO; games rule 2; arena hold 1; arena tap 0 0; arena step 60; games rule 0" && {
    g=$(bo_g 3); [ "$(bo_v "$g" stuck)" = 1 ] && [ "$(bo_v "$g" catches)" = 1 ] || { EVID="$g"; fail bo_catch_count; }; }
# Back mid-rally: the field, its timer and its knob go with the page
nowrun bo_back_mid_play "$BO; simtap 180 193; @0.5; simtap 180 193; @0.2; exit pause; @0.2; exit tap back; @0.3; uitop; arena" && {
    [ "$(tp)" = "tools_games " ] && grep -qx 'APP arena none' <<<"$out" || { EVID="$(tp) $(grep '^APP arena' <<<"$out")"; fail bo_back_mid_play; }; }
# the game's own 說明 (the ring's last cell)
nowrun bo_own_help "$BO; frame show; simtaprow 1; @0.3; popring 3; @0.3; help show; simflick up; @0.3; uitop" && {
    grep -qx 'APP explain id=game title=Ring breakout paras=1 qr=0 web=0' <<<"$out" && [ "$(tp)" = "tools_game_breakout " ] || { EVID="$(grep '^APP explain' <<<"$out") $(tp)"; fail bo_own_help; }; }
# the golden game: rolled by 重來 (forced here), the dump says so
nowrun bo_gold "$BO; games gold 1; frame show; simtaprow 1; @0.3; popring 2; @0.3; arena; games gold auto" && {
    [ "$(bo_v "$(bo_g 1)" gold)" = 1 ] && grep -q '^APP game breakout new rule=0 style=0 mode=0 gold=1$' <<<"$out" || { EVID="$(bo_g 1)"; fail bo_gold; }; }
# the music keeps playing under the game (as the board games)
nowrun bo_music_plays "screen on; vol 0; track 3; @2; audio info; games open breakout; @0.3; simtap 180 193; @2; audio info; simflick up; @0.3; audio info" && {
    a=$(grep '^APP audio info ' <<<"$out" | sed 's/ underruns_src.*//' | sort -u | wc -l)
    [ "$(grep -c '^APP audio info ' <<<"$out")" = 3 ] && [ "$a" = 1 ] && ! grep -q '^APP mp3 stop\|^APP play stop' <<<"$out" || { EVID="$(grep '^APP audio info' <<<"$out" | cut -c1-80 | tr '\n' '|')"; fail bo_music_plays; }; }
# the piece is registered: the shared-area doc's table and its lint line
# --- end 環形打磚塊 (ring breakout) ---
