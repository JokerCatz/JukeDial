# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G6: 旋鈕小玩意 (上發條 / 釣魚) on the shared dial ui_dial (app/main/ui/scr_game_knobtoy.c; rules in components/player_core/src/pc_game_knobtoy.c); sourced by sim/test.sh ---
# `dial game set <v>` puts the dial on v, `dial game sweet <n>` the wind-up's sweet spot, `dial game fish <n>` the fish's place, `dial game line|catch <n>`, `dial game gold`;
# `dial step <n>` n timer steps (1/30 s; 3 = one 100 ms game step). Every `dial` command ends with both dumps (the dial's, then the page's).
kt_g() { grep '^APP game knobtoy state=' <<<"$out" | sed -n "${1}p"; }
kt_d() { grep '^APP dial name=' <<<"$out" | sed -n "${1}p"; }
kt_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
KT="screen on; games open knobtoy; @0.3; games mode 0; games style 0; games rule 0; games count 0"
nowrun kt_open "$KT; dial; uitop" && {
    d=$(kt_d 1); g=$(kt_g 1)
    [ "$(kt_v "$d" mode)" = dial ] && [ "$(kt_v "$d" hi)" = 40 ] && [ "$(kt_v "$g" rule)" = 0 ] && [ "$(kt_v "$g" state)" = 0 ] && [ "$(kt_v "$g" launches)" = 0 ] && [ "$(tp)" = "tools_game_knobtoy " ] \
        || { EVID="$d | $g | $(tp)"; fail kt_open; }; }
# the rule picks the toy: the dial is made again for the other scale (fishing 0..99) and back
nowrun kt_rule_swaps_dial "$KT; games rule 1; dial; games rule 0; dial; games rule 1; dial" && {
    [ "$(kt_v "$(kt_d 1)" hi)" = 99 ] && [ "$(kt_v "$(kt_g 1)" rule)" = 1 ] && [ "$(kt_v "$(kt_d 2)" hi)" = 40 ] && [ "$(kt_v "$(kt_g 2)" rule)" = 0 ] && [ "$(kt_v "$(kt_d 3)" hi)" = 99 ] \
        || { EVID="$(kt_d 1) | $(kt_d 2) | $(kt_d 3)"; fail kt_rule_swaps_dial; }; }
# the knob is the volume until a tap lends it; one detent = one unit; taps: right +1, left -1
nowrun kt_knob "$KT; vol 0; @0.3; simknob 1; @0.2; dial; dial tap 60 0; dial turn 3; dial turn -10; simvol" && {
    [ "$(kt_v "$(kt_g 1)" v)" = 0 ] && [ "$(kt_v "$(kt_d 2)" v)" = 1 ] && [ "$(kt_v "$(kt_d 3)" v)" = 4 ] && [ "$(kt_v "$(kt_d 4)" v)" = 0 ] || { EVID="$(kt_d 1) | $(kt_d 2) | $(kt_d 3) | $(kt_d 4)"; fail kt_knob; }; }
# the glow: dark far from the sweet spot, lit near it, the strength of the mode on it
nowrun kt_cue "$KT; dial game sweet 20; dial game set 5; dial game set 17; dial game set 20; games mode 1; dial; games mode 2; dial" && {
    c1=$(kt_v "$(kt_d 2)" cue); c2=$(kt_v "$(kt_d 3)" cue); c3=$(kt_v "$(kt_d 4)" cue)
    [ "$c1" = 0 ] && [ "$c2" -gt 0 ] && [ "$c3" -gt "$c2" ] && [ "$c3" = 178 ] && [ "$(kt_v "$(kt_d 5)" cue)" = 255 ] && [ "$(kt_v "$(kt_d 6)" cue)" = 102 ] || { EVID="cue $c1 $c2 $c3"; fail kt_cue; }; }
# a release on the exact sweet spot: 120 cm, the spring is slack, the spot shows on the rim until the next move; a release with nothing wound: 0 cm
nowrun kt_release "$KT; dial game sweet 20; dial game set 20; dial tap 0 0; dial tap 60 0; dial tap 0 0" && {
    [ "$(kt_v "$(kt_g 3)" last)" = 120 ] && [ "$(kt_v "$(kt_g 3)" total)" = 120 ] && [ "$(kt_v "$(kt_g 3)" launches)" = 1 ] && [ "$(kt_v "$(kt_g 3)" v)" = 0 ] && [ "$(kt_v "$(kt_d 3)" zones)" = 1 ] \
        || { EVID="$(kt_g 3) | $(kt_d 3)"; fail kt_release; }
    [ "$(kt_v "$(kt_d 4)" zones)" = 0 ] || { EVID="$(kt_d 4)"; fail kt_reveal_hides; }
    [ "$(kt_v "$(kt_g 5)" last)" -le 100 ] && [ "$(kt_v "$(kt_g 5)" launches)" = 2 ] || { EVID="$(kt_g 5)"; fail kt_release_nothing; }; }
# wound to the end: the spring snaps (v 0, no release counted)
nowrun kt_snap "$KT; dial game set 39; dial tap 60 0" && {
    [ "$(kt_v "$(kt_g 2)" v)" = 0 ] && [ "$(kt_v "$(kt_g 2)" launches)" = 0 ] && [ "$(kt_v "$(kt_d 2)" v)" = 0 ] || { EVID="$(kt_g 2) | $(kt_d 2)"; fail kt_snap; }; }
# a wound spring slackens by one every 1.5 s (the clock starts at the first move)
nowrun kt_leak "$KT; dial step 90; dial tap 60 0; dial game set 10; dial step 42; dial step 6" && {
    [ "$(kt_v "$(kt_g 1)" v)" = 0 ] && [ "$(kt_v "$(kt_g 4)" v)" = 10 ] && [ "$(kt_v "$(kt_d 5)" v)" = 9 ] || { EVID="$(kt_g 1) | $(kt_g 4) | $(kt_d 5)"; fail kt_leak; }; }
# three releases: the end, the total, the card and the end line; a tap on the card = again
nowrun kt_win "$KT; dial game sweet 20; dial game set 20; dial tap 0 0; dial game sweet 20; dial game set 20; dial tap 0 0; dial game sweet 20; dial game set 20; dial tap 0 0; @1; games result; games result tap; dial" && {
    [ "$(kt_v "$(kt_g 9)" state)" = 1 ] && [ "$(kt_v "$(kt_g 9)" total)" = 360 ] && [ "$(kt_v "$(kt_g 9)" launches)" = 3 ] || { EVID="$(kt_g 9)"; fail kt_win_state; }
    grep -q '^APP game knobtoy end outcome=1 streak=0 ' <<<"$out" || { EVID="$(grep ' end ' <<<"$out")"; fail kt_end_line; }
    grep -q '^APP game_result up=1 shown=1 title=All released moves=360 ' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out" | tr '\n' '|')"; fail kt_card; }
    [ "$(kt_v "$(kt_g '$')" state)" = 0 ] && [ "$(kt_v "$(kt_g '$')" total)" = 0 ] || { EVID="$(kt_g '$')"; fail kt_again; }; }
# the golden spring doubles the distance
nowrun kt_gold "$KT; dial game gold; dial game sweet 20; dial game set 20; dial tap 0 0; dial" && {
    [ "$(kt_v "$(kt_g '$')" last)" = 240 ] && [ "$(kt_v "$(kt_g '$')" gold)" = 1 ] || { EVID="$(kt_g '$')"; fail kt_gold; }; }
# fishing: the zone is on the rim; inside it the catch fills and the line relaxes, outside the line tightens and the catch slips
FK="$KT; games rule 1; games count 0"
nowrun kt_fish_open "$FK; dial" && {
    [ "$(kt_v "$(kt_g 1)" rule)" = 1 ] && [ "$(kt_v "$(kt_d 1)" zones)" = 1 ] && [ "$(kt_v "$(kt_g 1)" v)" = 50 ] && [ "$(kt_v "$(kt_g 1)" catch)" = 0 ] || { EVID="$(kt_g 1) | $(kt_d 1)"; fail kt_fish_open; }; }
nowrun kt_fish_zone "$FK; dial tap 60 0; dial game fish 52; dial game set 52; dial step 3; dial game fish 80; dial game set 10; dial step 3" && {
    [ "$(kt_v "$(kt_g 4)" catch)" = 2 ] && [ "$(kt_v "$(kt_g 4)" inzone)" = 1 ] && [ "$(kt_v "$(kt_g 4)" line)" = 0 ] || { EVID="$(kt_g 4)"; fail kt_fish_inside; }
    [ "$(kt_v "$(kt_g '$')" inzone)" = 0 ] && [ "$(kt_v "$(kt_g '$')" line)" = 5 ] || { EVID="$(kt_g '$')"; fail kt_fish_outside; }; }
nowrun kt_fish_caught "$FK; dial tap 60 0; dial game fish 52; dial game set 52; dial game catch 975; dial step 3; dial; @1; games result" && {
    [ "$(kt_v "$(kt_g '$')" state)" = 1 ] && grep -q '^APP game knobtoy end outcome=1 ' <<<"$out" && grep -q '^APP game_result up=1 shown=1 title=Caught it! ' <<<"$out" || { EVID="$(kt_g '$') | $(grep ' end \|^APP game_result' <<<"$out" | tr '\n' '|')"; fail kt_fish_caught; }; }
nowrun kt_fish_snapped "$FK; dial tap 60 0; dial game fish 80; dial game set 10; dial game line 99; dial step 3; dial; @1; games result" && {
    [ "$(kt_v "$(kt_g '$')" state)" = 2 ] && grep -q '^APP game knobtoy end outcome=0 ' <<<"$out" && grep -q '^APP game_result up=1 shown=1 title=Line snapped ' <<<"$out" || { EVID="$(kt_g '$') | $(grep ' end \|^APP game_result' <<<"$out" | tr '\n' '|')"; fail kt_fish_snapped; }; }
# the line tight: the needle shakes; the fish's dash is an event (the clock starts at the first move, the seed decides)
nowrun kt_fish_shake "$FK; dial tap 60 0; dial game fish 80; dial game set 10; dial game line 80; dial game line 10" && {
    [ "$(kt_v "$(kt_d 4)" shake)" -gt 0 ] && [ "$(kt_v "$(kt_d 5)" shake)" = 0 ] || { EVID="$(kt_d 4) | $(kt_d 5)"; fail kt_fish_shake; }; }
# the fish does not move before the first move; a long idle changes nothing
nowrun kt_fish_waits "$FK; dial; dial step 120" && {
    [ "$(kt_v "$(kt_g 1)" fish)" = "$(kt_v "$(kt_g 2)" fish)" ] && [ "$(kt_v "$(kt_g 2)" ms)" = 0 ] || { EVID="$(kt_g 1) | $(kt_g 2)"; fail kt_fish_waits; }; }
nowrun kt_glow_fish "$FK; dial game fish 50; dial game set 50; games mode 2; dial" && {
    [ "$(kt_v "$(kt_d 2)" cue)" = 178 ] && [ "$(kt_v "$(kt_d 3)" cue)" = 102 ] || { EVID="$(kt_d 2) | $(kt_d 3)"; fail kt_glow_fish; }; }
# the screen off pauses; the next tap only resumes; the ring pauses too
nowrun kt_screen_off "$KT; dial tap 60 0; @0.2; screen off; @0.5; dial; screen on; @0.3; dial; dial tap 60 0; @0.1; dial" && {
    [ "$(kt_v "$(kt_d 2)" paused)" = 1 ] && [ "$(kt_v "$(kt_d 2)" run)" = 0 ] && grep -qx 'APP game knobtoy paused=1' <<<"$out" || { EVID="$(kt_d 2)"; fail kt_screen_off_pause; }
    [ "$(kt_v "$(kt_d 3)" paused)" = 1 ] && [ "$(kt_v "$(kt_d 4)" paused)" = 0 ] && [ "$(kt_v "$(kt_d 4)" v)" = 1 ] || { EVID="$(kt_d 3) | $(kt_d 4)"; fail kt_tap_resumes; }; }
nowrun kt_ring_pauses "$KT; dial tap 60 0; @0.2; frame show; simtaprow 1; @0.3; dial; popring -1; @0.2; dial tap 60 0; @0.2; dial" && {
    [ "$(kt_v "$(kt_d 2)" paused)" = 1 ] && [ "$(kt_v "$(kt_d 3)" paused)" = 0 ] && [ "$(kt_v "$(kt_d 3)" run)" = 1 ] || { EVID="$(kt_d 2) | $(kt_d 3)"; fail kt_ring_pauses; }; }
# the shared exit: mode C (knob and taps both play)
nowrun kt_exit "$KT; exit" && grep -q '^APP exit attached=1 mode=C ' <<<"$out" || { EVID="$(grep '^APP exit' <<<"$out")"; fail kt_exit; }
# --- G6b 旋轉迷宮: rules 2..5 of the page (重力 / 斥力 / 吸力 / 傾斜); `arena game ...` the field's console (marble <x> <y>, gap <k> <deg>, theta <deg>, grav <dx> <dy>, start, hole <i> <x> <y>),
# `maze grav <dx> <dy>` the tilt vector (percent) and the stand-in for the IMU: the 傾斜 rule exists only after it; `arena step <n>` runs n fixed steps (pictures and tests: no time waits).
# Every `arena ...` command ends with both dumps (the field's, then the page's maze line): mz_g <n> / mz_a <n> are the n-th (`$`: the last). A knob turn right after a tap is swallowed: @0.2 between ---
mz_g() { grep '^APP game knobtoy maze state=' <<<"$out" | sed -n "${1}p"; }
mz_a() { grep '^APP arena name=knobtoy' <<<"$out" | sed -n "${1}p"; }
MZ="screen on; games open knobtoy; @0.3; games mode 0; games style 0; games count 0; games rule 2; arena hold 1"      # dump 1
# the field replaces the dial on a maze rule and the dial is back for the wind-up
nowrun mz_open "$MZ; arena; games rule 0; dial; games rule 2; arena" && {
    [ "$(kt_v "$(mz_g 2)" rule)" = 0 ] && [ "$(kt_v "$(mz_g 2)" started)" = 0 ] && [ "$(kt_v "$(mz_a 2)" objs)" = 6/44 ] && [ "$(kt_v "$(kt_d 1)" hi)" = 40 ] && [ "$(kt_v "$(mz_g 3)" rule)" = 0 ] \
        || { EVID="$(mz_a 2) | $(mz_g 2) | $(kt_d 1)"; fail mz_open; }; }
# 傾斜 is hidden until a tilt source exists: the rule number wraps to the wind-up; after `maze grav` it is the 4th maze rule
nowrun mz_tilt_hidden "$MZ; games rule 5; maze grav 0 0; games rule 5; arena" && {
    grep -q '^APP game knobtoy new rule=0 ' <<<"$out" && grep -q '^APP game knobtoy new rule=5 ' <<<"$out" && [ "$(kt_v "$(mz_g '$')" rule)" = 3 ] || { EVID="$(grep '^APP game knobtoy new' <<<"$out" | tr '\n' '|')"; fail mz_tilt_hidden; }; }
# gravity: the marble rests on the first wall down; a tap on the field's right / left half = one detent forward / back (10 degrees), the knob (borrowed by a tap) too
nowrun mz_grav_rest "$MZ; arena game gap 0 270; arena tap 0 0; arena step 120; arena tap 60 0; arena tap -60 0; @0.2; simknob 1; @0.2; arena" && {
    g=$(mz_g 4); [ "$(kt_v "$g" level)" = 0 ] && [ "$(kt_v "$g" a)" -ge 88 ] && [ "$(kt_v "$g" a)" -le 92 ] && [ "$(kt_v "$g" r)" -ge 22 ] && [ "$(kt_v "$g" r)" -le 24 ] && [ "$(kt_v "$g" state)" = 0 ] \
        || { EVID="$g"; fail mz_grav_rest; }
    [ "$(kt_v "$(mz_g 5)" theta)" = 100 ] && [ "$(kt_v "$(mz_g 5)" turns)" = 1 ] && [ "$(kt_v "$(mz_g 6)" theta)" = 90 ] && [ "$(kt_v "$(mz_g 6)" turns)" = 2 ] || { EVID="$(mz_g 5) | $(mz_g 6)"; fail mz_grav_turn_taps; }
    [ "$(kt_v "$(mz_g '$')" theta)" = 100 ] && [ "$(kt_v "$(mz_g '$')" turns)" = 3 ] || { EVID="$(mz_g '$')"; fail mz_grav_knob; }; }
# a gap under the marble: it falls through to the next channel
nowrun mz_grav_gap "$MZ; arena game gap 0 90; arena tap 0 0; arena step 60; arena" && {
    g=$(mz_g '$'); [ "$(kt_v "$g" level)" -ge 1 ] && [ "$(kt_v "$g" r)" -gt 30 ] || { EVID="$g"; fail mz_grav_gap; }; }
# `arena game theta` sets gravity's direction (the knob's); the tilt vector (`maze grav`) does NOT reach 重力: the force and the marble stay
nowrun mz_vector "$MZ; arena game start; arena game theta 0; arena step 60; arena; maze grav 0 -100; maze grav 0 0; arena step 60; arena" && {
    a1=$(kt_v "$(mz_g 5)" a); a2=$(kt_v "$(mz_g '$')" a)
    { [ "$a1" -le 2 ] || [ "$a1" -ge 358 ]; } && [ "$(kt_v "$(mz_g '$')" theta)" = 0 ] && [ "$(kt_v "$(mz_g '$')" gx)" = 100 ] && { [ "$a2" -le 2 ] || [ "$a2" -ge 358 ]; } || { EVID="$(mz_g 5) | $(mz_g '$')"; fail mz_vector; }; }
# the tilt does not start the clock of 斥力 / 吸力
nowrun mz_tilt_no_start "games open knobtoy; @0.3; games mode 0; games style 0; games count 0; games rule 3; arena hold 1; maze grav 100 0; arena; games rule 4; maze grav 100 0; arena" && {
    [ "$(kt_v "$(mz_g 2)" started)" = 0 ] && [ "$(kt_v "$(mz_g 3)" started)" = 0 ] || { EVID="$(mz_g 2) | $(mz_g 3)"; fail mz_tilt_no_start; }; }
# out of the outer exit: the end line, the card (turns + time), a tap on the card = a new game
nowrun mz_exit_card "$MZ; arena tap 0 0; arena game marble 0 94; arena step 2; @1; games result; games result tap; arena" && {
    [ "$(kt_v "$(mz_g 4)" state)" = 1 ] && grep -q '^APP game knobtoy end outcome=1 ' <<<"$out" && grep -q '^APP game_result up=1 shown=1 title=Out! ' <<<"$out" && [ "$(kt_v "$(mz_g '$')" state)" = 0 ] && [ "$(kt_v "$(mz_g '$')" started)" = 0 ] \
        || { EVID="$(mz_g 4) | $(grep ' end \|^APP game_result' <<<"$out" | tr '\n' '|')"; fail mz_exit_card; }; }
# 斥力: pushed out to the first wall, the knob walks it round; a gap in its path lets it through
nowrun mz_repel "games open knobtoy; @0.3; games mode 0; games style 0; games count 0; games rule 3; arena hold 1; arena tap 0 0; arena step 60; arena; arena game gap 0 80; arena game marble 0 20; arena tap -60 0; arena step 30; arena" && {
    g=$(mz_g 4); [ "$(kt_v "$g" rule)" = 1 ] && [ "$(kt_v "$g" level)" = 0 ] && [ "$(kt_v "$g" r)" -ge 22 ] && [ "$(kt_v "$g" r)" -le 24 ] || { EVID="$g"; fail mz_repel_rest; }
    [ "$(kt_v "$(mz_g '$')" level)" -ge 1 ] && [ "$(kt_v "$(mz_g '$')" r)" -gt 30 ] || { EVID="$(mz_g '$')"; fail mz_repel_pop; }; }
# 吸力: starts in the outer strip, held on the outer wall; a gap under it and it goes in
nowrun mz_attract "games open knobtoy; @0.3; games mode 0; games style 0; games count 0; games rule 4; arena hold 1; arena tap 0 0; arena step 60; arena; arena game gap 3 270; arena game marble 0 -91; arena step 60; arena" && {
    g=$(mz_g 4); [ "$(kt_v "$g" rule)" = 2 ] && [ "$(kt_v "$g" level)" = 4 ] && [ "$(kt_v "$g" r)" -ge 90 ] || { EVID="$g"; fail mz_attract_rest; }
    [ "$(kt_v "$(mz_g '$')" level)" -le 3 ] && [ "$(kt_v "$(mz_g '$')" r)" -lt 84 ] || { EVID="$(mz_g '$')"; fail mz_attract_in; }; }
# the centre ends 吸力
nowrun mz_attract_goal "games open knobtoy; @0.3; games mode 0; games style 0; games count 0; games rule 4; arena hold 1; arena tap 0 0; arena game marble 0 -9; arena step 2; arena" && {
    [ "$(kt_v "$(mz_g '$')" state)" = 1 ] || { EVID="$(mz_g '$')"; fail mz_attract_goal; }; }
# the gaps drift in the harder modes (mode 1 slow: a degree every 20 steps for ring 0, mode 2 fast: every 8): none in mode 0
nowrun mz_drift "$MZ; arena game gap 0 90; arena tap 0 0; arena step 80; arena; games mode 1; arena game gap 0 90; arena tap 0 0; arena step 80; arena; games mode 2; arena game gap 0 90; arena tap 0 0; arena step 80; arena" && {
    d() { local v; v=$(kt_v "$(mz_g $1)" gap); v=${v%%,*}; v=$((v - 90)); echo ${v#-}; }
    [ "$(kt_v "$(mz_g 5)" drift)" = 0 ] && [ "$(kt_v "$(mz_g 9)" drift)" = 1 ] && [ "$(kt_v "$(mz_g 13)" drift)" = 2 ] || { EVID="$(mz_g 5) | $(mz_g 9) | $(mz_g 13)"; fail mz_drift_mode; }
    [ "$(d 5)" = 0 ] && [ "$(d 9)" = 4 ] && [ "$(d 13)" = 10 ] || { EVID="gap0 moved: $(d 5) $(d 9) $(d 13)"; fail mz_drift_gaps; }; }
# 傾斜: a tap starts (a tilt keeps the vector, a zero one no clock), the tilt rolls the marble; the top wall keeps it in
nowrun mz_tilt "maze grav 0 0; $MZ; games rule 5; arena; maze grav 0 -100; arena step 60; arena" && {
    g=$(mz_g 2); [ "$(kt_v "$g" rule)" = 3 ] && [ "$(kt_v "$g" holes)" -ge 1 ] && [ "$(kt_v "$g" walls)" -ge 4 ] && [ "$(kt_v "$g" started)" = 0 ] || { EVID="$g"; fail mz_tilt_open; }
    [ "$(kt_v "$(mz_g '$')" y)" -le -57 ] && [ "$(kt_v "$(mz_g '$')" started)" = 1 ] || { EVID="$(mz_g '$')"; fail mz_tilt_roll; }; }
# a hole: the marble goes back to the start and the fall is counted
nowrun mz_tilt_hole "maze grav 0 0; $MZ; games rule 5; arena game hole 0 -44 -52; arena tap 0 0; maze grav 100 0; arena step 40; arena" && {
    g=$(mz_g '$'); [ "$(kt_v "$g" falls)" -ge 1 ] || { EVID="$g"; fail mz_tilt_hole; }; }
# the knob does nothing in 傾斜; the goal ends it with the card
nowrun mz_tilt_goal "maze grav 0 0; $MZ; games rule 5; arena tap 0 0; simknob 3; @0.2; arena; arena game marble 52 52; arena step 2; arena; @1; games result" && {
    [ "$(kt_v "$(mz_g 3)" turns)" = 0 ] && [ "$(kt_v "$(mz_g 5)" state)" = 1 ] && grep -q '^APP game_result up=1 shown=1 title=Done! ' <<<"$out" || { EVID="$(mz_g 3) | $(mz_g 5) | $(grep '^APP game_result' <<<"$out" | tr '\n' '|')"; fail mz_tilt_goal; }; }
# the screen off pauses; the exit is mode C
nowrun mz_pause_exit "screen on; games open knobtoy; @0.3; games mode 0; games style 0; games count 0; games rule 2; arena tap 0 0; @0.2; screen off; @0.5; arena; screen on; @0.3; arena; exit" && {
    [ "$(kt_v "$(mz_a 2)" paused)" = 1 ] && [ "$(kt_v "$(mz_a 2)" run)" = 0 ] && grep -qx 'APP game knobtoy paused=1' <<<"$out" && grep -q '^APP exit attached=1 mode=C ' <<<"$out" || { EVID="$(mz_a 2)"; fail mz_pause_exit; }; }
# F1b: the IMU stand-in (`simimu x y`, percent of a full tilt: imu_svc's vector) offers 傾斜 by itself and feeds the force every fixed step: no `maze grav` needed, a zero vector lets go
nowrun mz_tilt_imu "simimu 0 0; $MZ; games rule 5; arena; arena tap 0 0; simimu 0 -100; arena step 60; arena; simimu 0 0; arena step 3; arena" && {
    g=$(mz_g 2); [ "$(kt_v "$g" rule)" = 3 ] && [ "$(kt_v "$g" started)" = 0 ] || { EVID="$g"; fail mz_tilt_imu_open; }
    g=$(mz_g 5); [ "$(kt_v "$g" y)" -le -57 ] && [ "$(kt_v "$g" gy)" = -100 ] && [ "$(kt_v "$g" started)" = 1 ] || { EVID="$g"; fail mz_tilt_imu_roll; }
    g=$(mz_g '$'); [ "$(kt_v "$g" gy)" = 0 ] || { EVID="$g"; fail mz_tilt_imu_release; }; }
# imucal: the pose held at entry is level (gx 0 there), a tilt from it is relative; 重新歸零 (the ring's action cell) takes the pose held now
nowrun mz_tilt_zero "simimu 30 0; $MZ; games rule 5; arena tap 0 0; arena step 3; arena; simimu 60 0; arena step 3; arena; games ring; popring 3; @0.3; arena step 3; arena; imu" && {
    [ "$(kt_v "$(mz_g 4)" gx)" = 0 ] && [ "$(kt_v "$(mz_g 6)" gx)" = 30 ] && [ "$(kt_v "$(mz_g 8)" gx)" = 0 ] && grep -q '^APP imu scale_pm=1000 learned=0 zero=0.60,0.00 ' <<<"$out" || { EVID="$(mz_g 4) | $(mz_g 6) | $(mz_g 8) | $(grep '^APP imu' <<<"$out" | tr '\n' '|')"; fail mz_tilt_zero; }; }
# a moving device cannot take a neutral: the "hold still" message, absolute tilt meanwhile
nowrun mz_tilt_zero_moving "simimu 30 0; simimu moving 1; $MZ; games rule 5; arena step 3; uitop; arena; imu; simimu moving 0" && {
    grep -q '^APP imu scale_pm=1000 learned=0 zero=failed ' <<<"$out" && [ "$(kt_v "$(mz_g '$')" gx)" = 30 ] || { EVID="$(grep '^APP imu\|^APP game knobtoy maze' <<<"$out" | tr '\n' '|')"; fail mz_tilt_zero_moving; }; }
# a machine without the source (every board but the FakePod): 傾斜 stays hidden, simimu off hides it for the next page
nowrun mz_tilt_imu_off "simimu 0 0; simimu off; $MZ; games rule 5" && {
    grep -q '^APP game knobtoy new rule=0 ' <<<"$out" && ! grep -q '^APP game knobtoy new rule=5 ' <<<"$out" || { EVID="$(grep '^APP game knobtoy new' <<<"$out" | tr '\n' '|')"; fail mz_tilt_imu_off; }; }
# --- end knobtoy ---
