# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G3: 電台調頻尋寶 on the shared dial ui_dial (app/main/ui/scr_game_radio.c; rules in components/player_core/src/pc_game_radio.c); sourced by sim/test.sh ---
# `games count 0` = the first band: stations at 46, 71, 87 (classic; the seed is fixed in the simulator). `dial game on <k>` puts the dial on station k, `dial game pos <n>` on n,
# `dial game gold <k>` makes station k golden; `dial step <n>` n timer steps (1/30 s; 3 = one 100 ms sound step). Every `dial` command ends with both dumps.
rd_g() { grep '^APP game radio state=' <<<"$out" | sed -n "${1}p"; }
rd_d() { grep '^APP dial name=' <<<"$out" | sed -n "${1}p"; }
rd_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
RD="screen on; games open radio; @0.3; games mode 0; games style 0; games rule 0; games count 0"
nowrun rd_open "$RD; dial; uitop" && {
    d=$(rd_d 1); g=$(rd_g 1)
    [ "$(rd_v "$d" mode)" = dial ] && [ "$(rd_v "$d" target)" = 46 ] && [ "$(rd_v "$d" reach)" = 10 ] && [ "$(rd_v "$d" cuepct)" = 70 ] && [ "$(rd_v "$g" n)" = 3 ] \
        && [ "$(rd_v "$g" st)" = 46,71,87 ] && [ "$(tp)" = "tools_game_radio " ] || { EVID="$d | $g | $(tp)"; fail rd_open; }; }
# the cue: dark far from a station, lit near it (the level follows the distance), dark again after the lock; the needle shakes in the static only
nowrun rd_cue "$RD; dial tap 60 0; dial game pos 20; dial game pos 38; dial game pos 44; dial game on 0" && {
    c1=$(rd_v "$(rd_d 2)" cue); c2=$(rd_v "$(rd_d 3)" cue); c3=$(rd_v "$(rd_d 4)" cue); c4=$(rd_v "$(rd_d 5)" cue)
    [ "$c1" = 0 ] && [ "$c2" -gt 0 ] && [ "$c3" -gt "$c2" ] && [ "$c4" -gt "$c3" ] && [ "$c4" = 178 ] || { EVID="cue $c1 $c2 $c3 $c4"; fail rd_cue_level; }
    [ "$(rd_v "$(rd_d 2)" shake)" -gt 0 ] && [ "$(rd_v "$(rd_d 5)" shake)" = 0 ] || { EVID="shake $(rd_v "$(rd_d 2)" shake) $(rd_v "$(rd_d 5)" shake)"; fail rd_shake; }; }
# before the first tune there is no shake and no sound; after 10 s without input both stop, the next input brings them back
nowrun rd_idle "$RD; dial game pos 20; sfx; dial step 60; sfx; dial tap 60 0; sfx; dial step 60; sfx; dial step 330; dial; sfx; dial step 60; sfx; dial tap 60 0; dial" && {
    p=($(sed -n 's/^APP sfx .* posted=\([0-9]*\) .*/\1/p' <<<"$out"))
    [ "$(rd_v "$(rd_d 1)" shake)" = 0 ] && [ "${p[1]}" = "${p[0]}" ] || { EVID="shake $(rd_v "$(rd_d 1)" shake) posted ${p[*]}"; fail rd_quiet_before_move; }
    [ $((p[3] - p[2])) -ge 5 ] && [ "${p[5]}" = "${p[4]}" ] || { EVID="posted ${p[*]}"; fail rd_idle_stops; }
    [ "$(rd_v "$(rd_d 6)" shake)" = 0 ] && [ "$(rd_v "$(rd_d 8)" shake)" -gt 0 ] || { EVID="$(rd_d 6) | $(rd_d 8)"; fail rd_idle_shake; }; }
nowrun rd_glow_modes "$RD; games mode 1; dial game on 0; games mode 2; dial" && {
    [ "$(rd_v "$(rd_d 1)" cue)" = 255 ] && [ "$(rd_v "$(rd_d 2)" cue)" = 102 ] || { EVID="$(rd_d 1) | $(rd_d 2)"; fail rd_glow_modes; }; }
# the knob is the volume until a tap lends it; taps: right +1, left -1; the hub shows the frequency
nowrun rd_knob "$RD; vol 0; @0.3; simknob 1; @0.2; dial; dial tap 60 0; dial turn 3; dial turn -10; simvol" && {
    [ "$(rd_v "$(rd_g 1)" pos)" = 0 ] && [ "$(rd_v "$(rd_d 2)" v)" = 1 ] && [ "$(rd_v "$(rd_d 3)" v)" = 4 ] && [ "$(rd_v "$(rd_d 4)" v)" = 0 ] || { EVID="$(rd_g 1) | $(rd_d 2) | $(rd_d 3) | $(rd_d 4)"; fail rd_knob; }; }
# a lock tap with no station: a miss; on a station: a lock, the zone, the next target; the same station again is a miss
nowrun rd_lock "$RD; dial game pos 5; dial tap 0 0; dial game on 0; dial tap 0 0; dial tap 0 0" && {
    [ "$(rd_v "$(rd_g 2)" misses)" = 1 ] && [ "$(rd_v "$(rd_g 2)" attempts)" = 1 ] || { EVID="$(rd_g 2)"; fail rd_miss; }
    [ "$(rd_v "$(rd_g 4)" found)" = 1 ] && [ "$(rd_v "$(rd_d 4)" zones)" = 1 ] && [ "$(rd_v "$(rd_d 4)" target)" = 71 ] && [ "$(rd_v "$(rd_g 4)" locked)" = 100 ] || { EVID="$(rd_g 4) | $(rd_d 4)"; fail rd_lock; }
    [ "$(rd_v "$(rd_g 5)" misses)" = 2 ] || { EVID="$(rd_g 5)"; fail rd_lock_twice; }; }
# all three: the win, the card, the end line; a tap on the card = again
nowrun rd_win "$RD; dial game on 0; dial tap 0 0; dial game on 1; dial tap 0 0; dial game on 2; dial tap 0 0; games result; @1; games result; games result tap; dial" && {
    [ "$(rd_v "$(rd_g 6)" state)" = 1 ] && [ "$(rd_v "$(rd_g 6)" found)" = 3 ] && [ "$(rd_v "$(rd_d 6)" zones)" = 3 ] && [ "$(rd_v "$(rd_d 6)" target)" = -1 ] || { EVID="$(rd_g 6) | $(rd_d 6)"; fail rd_win_state; }
    grep -q '^APP game radio end outcome=1 streak=0 ' <<<"$out" || { EVID="$(grep ' end ' <<<"$out")"; fail rd_end_line; }
    grep -q '^APP game_result up=1 shown=1 title=All locked! moves=3 ' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out" | tr '\n' '|')"; fail rd_card; }
    [ "$(rd_v "$(rd_g '$')" state)" = 0 ] && [ "$(rd_v "$(rd_g '$')" found)" = 0 ] && [ "$(rd_v "$(rd_g '$')" pos)" = 0 ] || { EVID="$(rd_g '$')"; fail rd_again; }; }
# the sound only through sfx_play, one a 100 ms step: static far away (a post a step at most), notes near; nothing once the screen is off
nowrun rd_sound "$RD; dial tap 60 0; sfx; dial step 60; sfx; dial game on 0; dial step 60; sfx" && {
    p=($(sed -n 's/^APP sfx .* posted=\([0-9]*\) .*/\1/p' <<<"$out"))
    [ $((p[1] - p[0])) -ge 5 ] && [ $((p[1] - p[0])) -le 10 ] && [ $((p[2] - p[1])) -ge 5 ] && [ $((p[2] - p[1])) -le 10 ] || { EVID="posted ${p[*]}"; fail rd_sound; }; }
nowrun rd_sound_off "$RD; dial tap 60 0; screen off; @0.5; sfx; @1; sfx" && {
    p=($(sed -n 's/^APP sfx .* posted=\([0-9]*\) .*/\1/p' <<<"$out"))
    [ "${p[1]}" = "${p[0]}" ] || { EVID="posted ${p[*]}"; fail rd_sound_off; }; }
# rules: timed has one station at a time and a 60 s clock (it starts at the first tune), more stations after each lock; weak a narrower reach
nowrun rd_timed "$RD; games rule 1; games count 0; dial; dial game on 0; dial tap 60 0; dial game on 0; dial tap 0 0; dial; dial step 1700; dial step 100; @1; games result" && {
    [ "$(rd_v "$(rd_g 1)" n)" = 1 ] && [ "$(rd_v "$(rd_g 1)" state)" = 0 ] && [ "$(rd_v "$(rd_g 1)" left)" = 60000 ] || { EVID="$(rd_g 1)"; fail rd_timed_open; }
    [ "$(rd_v "$(rd_g 5)" found)" = 1 ] && [ "$(rd_v "$(rd_g 5)" state)" = 0 ] && [ "$(rd_v "$(rd_g 5)" left)" -gt 0 ] || { EVID="$(rd_g 5)"; fail rd_timed_found; }
    [ "$(rd_v "$(rd_g 8)" state)" = 2 ] || { EVID="$(rd_g 8)"; fail rd_timed_up; }
    grep -q '^APP game_result up=1 shown=1 title=Time is up' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out")"; fail rd_timed_card; }; }
nowrun rd_weak "$RD; games rule 2; dial" && { [ "$(rd_v "$(rd_d 1)" reach)" = 6 ] && [ "$(rd_v "$(rd_g 1)" n)" = 3 ] || { EVID="$(rd_d 1) | $(rd_g 1)"; fail rd_weak; }; }
# the golden station: a narrower reach, a bonus of 10 s, the dial goes gold
nowrun rd_gold "$RD; dial game gold 0; dial game on 0; dial tap 60 0; dial game on 0; dial tap 0 0" && {
    [ "$(rd_v "$(rd_d 1)" reach)" = 6 ] && [ "$(rd_v "$(rd_g '$')" gold)" = 1 ] && [ "$(rd_v "$(rd_g '$')" bonus)" = 10000 ] || { EVID="$(rd_d 1) | $(rd_g '$')"; fail rd_gold; }; }
# the screen off pauses; the next tap only resumes; the ring pauses too
nowrun rd_screen_off "$RD; dial tap 60 0; @0.2; screen off; @0.5; dial; screen on; @0.3; dial; dial tap 60 0; @0.1; dial" && {
    [ "$(rd_v "$(rd_d 2)" paused)" = 1 ] && [ "$(rd_v "$(rd_d 2)" run)" = 0 ] && grep -qx 'APP game radio paused=1' <<<"$out" || { EVID="$(rd_d 2)"; fail rd_screen_off_pause; }
    [ "$(rd_v "$(rd_d 3)" paused)" = 1 ] && [ "$(rd_v "$(rd_d 4)" paused)" = 0 ] && [ "$(rd_v "$(rd_d 4)" v)" = 1 ] || { EVID="$(rd_d 3) | $(rd_d 4)"; fail rd_tap_resumes; }; }
nowrun rd_ring_pauses "$RD; dial tap 60 0; @0.2; frame show; simtaprow 1; @0.3; dial; popring -1; @0.2; dial tap 60 0; @0.2; dial" && {
    [ "$(rd_v "$(rd_d 2)" paused)" = 1 ] && [ "$(rd_v "$(rd_d 3)" paused)" = 0 ] && [ "$(rd_v "$(rd_d 3)" run)" = 1 ] || { EVID="$(rd_d 2) | $(rd_d 3)"; fail rd_ring_pauses; }; }
# the safe's glow (G3 extra): follows the hint's strength; dark at the start, lit at the target, expert dimmer
SFR="screen on; games open safe; @0.3; games mode 0; games style 0; games rule 0; games count 0"
nowrun rd_safe_cue "$SFR; dial; dial game pos 35; games mode 1; dial; games mode 2; dial; games mode 0; games rule 1; games count 0; dial game pos 35" && {
    [ "$(rd_v "$(rd_d 1)" cue)" = 0 ] && [ "$(rd_v "$(rd_d 2)" cue)" = 178 ] && [ "$(rd_v "$(rd_d 3)" cue)" = 255 ] && [ "$(rd_v "$(rd_d 4)" cue)" = 102 ] || { EVID="$(rd_d 1) | $(rd_d 2) | $(rd_d 3) | $(rd_d 4)"; fail rd_safe_cue; }
    [ "$(rd_v "$(rd_d 5)" cue)" -gt 0 ] && [ "$(rd_v "$(rd_d 5)" cuepct)" = 52 ] || { EVID="$(rd_d 5)"; fail rd_safe_expert_cue; }; }
