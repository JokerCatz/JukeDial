# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G3: 節拍點擊 on the shared field ui_arena (app/main/ui/scr_game_beat.c) and the shared beat feed ui_beat (pc_beat over the spectrum); sourced by sim/test.sh ---
# Dumps: "APP game beat state=<0 ready|1 play|2 over> rule= window= bpm= combo= points= perfect= good= miss= stray= spawned= resolved= golds= locked= nobeat= ..." and
# "APP beat on= src=<spec|sim> bpm= conf= locked= onsets= ticks= steps= us= max=". The metronome's first ring arrives at 1533 ms after the start (46 fixed steps).
bt_g() { grep '^APP game beat state=' <<<"$out" | sed -n "${1}p"; }
bt_b() { grep '^APP beat on=' <<<"$out" | sed -n "${1}p"; }
bt_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
BT="screen on; games open beat; @0.3"
nowrun bt_open "$BT; beat; arena; uitop" && {
    [ "$(bt_v "$(bt_b 1)" on)" = 1 ] && [ "$(bt_v "$(bt_g 1)" state)" = 0 ] && [ "$(bt_v "$(bt_g 1)" rule)" = 0 ] && [ "$(tp)" = "tools_game_beat " ] || { EVID="$(bt_b 1) | $(bt_g 1) | $(tp)"; fail bt_open; }; }
nowrun bt_detector_off_after_back "$BT; beat; back; @0.3; beat" && {
    [ "$(bt_v "$(bt_b 1)" on)" = 1 ] && [ "$(bt_v "$(bt_b 2)" on)" = 0 ] || { EVID="$(bt_b 1) | $(bt_b 2)"; fail bt_detector_off_after_back; }; }
# the metronome: a tap starts, the first ring is perfect at 46 steps (+200 when it is the golden one, else +100), a stray tap resets the combo
nowrun bt_metro_hit "$BT; games rule 1; arena hold 1; arena tap 0 0; arena step 46; arena tap 0 0; arena" && {
    g=$(bt_g 4); [ "$(bt_v "$g" perfect)" = 1 ] && [ "$(bt_v "$g" combo)" = 1 ] && [ "$(bt_v "$g" resolved)" = 1 ] && [ "$(bt_v "$g" points)" -ge 100 ] || { EVID="$g"; fail bt_metro_hit; }; }
nowrun bt_stray "$BT; games rule 1; arena hold 1; arena tap 0 0; arena step 10; arena tap 0 0; arena" && {
    g=$(bt_g 4); [ "$(bt_v "$g" stray)" = 1 ] && [ "$(bt_v "$g" combo)" = 0 ] && [ "$(bt_v "$g" resolved)" = 0 ] || { EVID="$g"; fail bt_stray; }; }
# the knob: first the volume; after a tap the metronome's tempo, 5 BPM a detent, between 60 and 120
nowrun bt_knob_bpm "$BT; games rule 1; vol 0; @0.3; simknob 1; @0.2; frame hide; arena; simtap 180 193; @0.2; simknob 2; @0.2; arena; simknob -20; @0.2; arena" && {
    [ "$(bt_v "$(bt_g 1)" bpm)" = 90 ] && [ "$(bt_v "$(bt_g 2)" bpm)" = 100 ] && [ "$(bt_v "$(bt_g 3)" bpm)" = 60 ] || { EVID="$(bt_g 1) | $(bt_g 2) | $(bt_g 3)"; fail bt_knob_bpm; }; }
# a whole round nobody taps: every ring a miss, the end, the card, a tap = again
nowrun bt_round_end "$BT; games rule 1; arena hold 1; arena tap 0 0; arena step 2400; arena; games result; @1; games result; games result tap; arena" && {
    g=$(bt_g 3); [ "$(bt_v "$g" state)" = 2 ] && [ "$(bt_v "$g" miss)" = 32 ] && [ "$(bt_v "$g" points)" = 0 ] || { EVID="$g"; fail bt_round_end; }
    grep -q '^APP game beat end .* points=0 ' <<<"$out" && grep -q '^APP game_result up=1 shown=1 title=Round over points=0' <<<"$out" && [ "$(bt_v "$(bt_g 5)" state)" = 0 ] || { EVID="$(grep 'game_result\| end ' <<<"$out" | tr '\n' '|')"; fail bt_card_again; }; }
# classic with no music beat: after the listening time it says so and a tap switches to the metronome (the rule, and a new game)
nowrun bt_nobeat_fallback "$BT; @6; arena; uiboxes; simtap 180 193; @0.3; arena" && {
    ! grep -q '^APP uibox label .*|No beat: tap for metronome$' <<<"$out" && [ "$(bt_v "$(bt_g 2)" rule)" = 1 ] && [ "$(bt_v "$(bt_g 2)" state)" = 0 ] || { EVID="$(bt_g 1) | $(bt_g 2)"; fail bt_nobeat_fallback; }; }
# the detector on the synthetic click track: locked at about 120 BPM within 8 s, the classic rings follow it, and the sim feed stops with `beat sim 0`
nowrun bt_classic_follows "$BT; beat sim 120; @9; beat; arena tap 0 0; @3; arena; beat sim 0" && {
    b=$(bt_b 2); bpm=$(bt_v "$b" bpm); bpm=${bpm%.*}
    [ "$(bt_v "$b" locked)" = 1 ] && [ "$bpm" -ge 118 ] && [ "$bpm" -le 121 ] && [ "$(bt_v "$b" src)" = sim ] || { EVID="$b"; fail bt_detector_locks; }
    g=$(bt_g 2); [ "$(bt_v "$g" state)" = 1 ] && [ "$(bt_v "$g" spawned)" -ge 2 ] && [ "$(bt_v "$g" locked)" = 1 ] || { EVID="$g"; fail bt_classic_follows; }; }
nowrun bt_offbeat "$BT; beat sim 120; @9; games rule 2; @0.2; arena tap 0 0; @2.5; arena" && {
    g=$(bt_g 2); [ "$(bt_v "$g" rule)" = 2 ] && [ "$(bt_v "$g" spawned)" -ge 2 ] || { EVID="$g"; fail bt_offbeat; }; }
# the cost of the feed: the average microseconds a detector step takes in the simulator (host speed; the report scales it)
nowrun bt_cost "$BT; beat sim 120; @5; beat" && {
    b=$(bt_b 2); [ "$(bt_v "$b" steps)" -ge 50 ] && [ "$(bt_v "$b" us)" -lt 2000 ] || { EVID="$b"; fail bt_cost; }; }
# pause: the screen off pauses a running round, the next tap resumes
nowrun bt_screen_off "$BT; games rule 1; arena tap 0 0; @1; screen off; @0.5; arena; screen on; @0.3; arena tap 0 0; @0.2; arena" && {
    grep -qx 'APP game beat paused=1' <<<"$out" && grep -qx 'APP game beat paused=0' <<<"$out" || { EVID="$(grep 'paused' <<<"$out" | tr '\n' '|')"; fail bt_screen_off; }; }
# the help of the page
nowrun bt_own_help "$BT; frame show; simtaprow 1; @0.3; popring 3; @0.3; help show; simflick up; @0.3; uitop" && {
    grep -qx 'APP explain id=game title=Beat tap paras=1 qr=0 web=0' <<<"$out" && [ "$(tp)" = "tools_game_beat " ] || { EVID="$(grep '^APP explain' <<<"$out") $(tp)"; fail bt_own_help; }; }
# the feed is shared: a second user does not reset or stop it; it stops with the last one (the page's end, then the test user's)
nowrun bt_two_users "$BT; beat; beat user 1; beat; beat user 0; @0.5; beat; back; @0.3; beat" && {
    [ "$(bt_v "$(bt_b 1)" users)" = 1 ] && [ "$(bt_v "$(bt_b 2)" users)" = 2 ] && [ "$(bt_v "$(bt_b 4)" users)" = 1 ] && [ "$(bt_v "$(bt_b 5)" users)" = 1 ] && [ "$(bt_v "$(bt_b 5)" on)" = 1 ] \
        && [ "$(bt_v "$(bt_b 5)" steps)" -gt "$(bt_v "$(bt_b 3)" steps)" ] && [ "$(bt_v "$(bt_b 6)" on)" = 0 ] || { EVID="$(bt_b 3) | $(bt_b 5) | $(bt_b 6)"; fail bt_two_users; }; }
# no FFT when it is not wanted: the metronome and the screen off do not step the detector (steps stay), classic does
nowrun bt_feed_held "$BT; games rule 1; @1; beat; @2; beat; games rule 0; @2; beat; screen off; @1.5; beat; @2; beat" && {
    s1=$(bt_v "$(bt_b 1)" steps); s2=$(bt_v "$(bt_b 2)" steps); s3=$(bt_v "$(bt_b 3)" steps); s4=$(bt_v "$(bt_b 4)" steps); s5=$(bt_v "$(bt_b 5)" steps)
    [ "$s1" = "$s2" ] && [ "$s3" -gt "$s2" ] && [ "$s4" -ge "$s3" ] && [ "$s5" = "$s4" ] || { EVID="$s1 $s2 $s3 $s4 $s5"; fail bt_feed_held; }; }
# the default latency follows the sink: wired 30 ms, Bluetooth 180 ms (a console value sticks until `latency auto`)
nowrun bt_latency_sink "$BT; games rule 0; @2; beat; simsink 1; @2; beat; beat latency 77; @1; beat; beat latency auto; beat" && {
    [ "$(bt_v "$(bt_b 1)" lat)" = 30 ] && [ "$(bt_v "$(bt_b 2)" lat)" = 180 ] && [ "$(bt_v "$(bt_b 3)" lat)" = 77 ] && [ "$(bt_v "$(bt_b 5)" lat)" = 180 ] || { EVID="$(bt_b 1) | $(bt_b 2) | $(bt_b 3) | $(bt_b 5)"; fail bt_latency_sink; }; }
