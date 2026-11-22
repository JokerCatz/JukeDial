# SPDX-License-Identifier: LGPL-2.1-or-later
# --- G4b: the shared exit (app/main/ui/ui_exit.[ch]); sourced by sim/test.sh (name order: after beat..dominoes, before guess..ttt) ---
# Full-screen field pages have no Back button. Mode A: a knob turn that falls to the volume shows a 返回 pill for 4 s (bubbles, every game before a tap lends the knob);
# CF2: no `...` bubble anywhere: C (every game, the toys that take taps) = the popup waits for a free knob (a turn shows it); the pause overlay opens by `exit pause` or, in mode B, a tap on the field. Mode B is unused by a page: `exit mode b` tests it.
# either 返回 goes home. Console: `exit` dumps "APP exit attached= mode= a= pause= frozen= hold=", `exit tap back|resume|more`, `exit show|hide|pause|resume`.
ex_d() { grep '^APP exit attached=' <<<"$out" | sed -n "${1}p"; }
ex_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
# A on the bubble toy: the pill is hidden, a turn shows it AND changes the volume, a turn renews the 4 s, then it hides; a tap on it leaves for the 小工具 ring
nowrun ex_a_bubbles "screen on; vol 0; tools bubbles; @1; exit; simknob 1; @0.2; exit; simvol; @3; simknob 1; @0.2; @3; exit; @2; exit" && {
    [ "$(ex_v "$(ex_d 1)" mode)" = C ] && [ "$(ex_v "$(ex_d 1)" a)" = 0 ] && [ "$(ex_v "$(ex_d 2)" a)" = 1 ] && [ "$(ex_v "$(ex_d 3)" a)" = 1 ] && [ "$(ex_v "$(ex_d 4)" a)" = 0 ] || { EVID="$(ex_d 1) | $(ex_d 2) | $(ex_d 3) | $(ex_d 4)"; fail ex_a_pill_timing; }
    [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="vol $(vols)"; fail ex_a_volume_still_changes; }; }
nowrun ex_a_tap_back "screen on; vol 0; tools bubbles; @1; simknob 1; @0.2; exit tap back; @0.3; uitop" && { [ "$(tp)" = "tools " ] || { EVID="$(tp)"; fail ex_a_tap_back; }; }
# the FakePod's keys take the knob's path (ui_knob_inject): the same pill
nowrun ex_a_keys "screen on; vol 0; tools bubbles; @1; knob 1; @0.2; exit" && { [ "$(ex_v "$(ex_d 1)" a)" = 1 ] || { EVID="$(ex_d 1)"; fail ex_a_keys; }; }
# no Back button any more: a tap beside the old Back place on the bubble page pops a bubble, it does not leave
nowrun ex_bubbles_no_back "screen on; tools bubbles; @1; simtap 180 296; @0.3; uitop; bubbles" && {
    [ "$(tp)" = "tools_bubbles " ] && [ "$(sed -n 's/^APP bubbles n=[0-9]* left=\([0-9]*\) .*/\1/p' <<<"$out" | tail -1)" -lt "$(sed -n 's/^APP bubbles n=\([0-9]*\) .*/\1/p' <<<"$out" | tail -1)" ] || { EVID="$(tp) $(grep '^APP bubbles n=' <<<"$out")"; fail ex_bubbles_no_back; }; }
# the sheet fills the whole design circle: more bubbles than the 41 of the sheet that stopped above Back, the lowest one below the old Back's top (y 294)
nowrun ex_bubbles_full "screen on; tools bubbles; @1; bubbles" && {
    n=$(sed -n 's/^APP bubbles n=\([0-9]*\) .*/\1/p' <<<"$out" | tail -1)
    [ "${n:-0}" -ge 46 ] || { EVID="n=$n"; fail ex_bubbles_count; }; }
nowrun ex_bubbles_lowest "screen on; tools bubbles; @1; bubbles; bubbles at 47" && {
    y=$(sed -n 's/^APP bubbles at 47 x=[0-9-]*,y=\([0-9-]*\).*/\1/p' <<<"$out")
    [ "${y:-0}" -ge 296 ] && [ "${y:-999}" -le 360 ] || { EVID="y=$y"; fail ex_bubbles_lowest; }; }
# 環形打磚塊 is mode C: serving and steering taps are the game's own (no pause), the ... bubble pauses (the steps stop, the scrim takes touches), 繼續 runs again
nowrun ex_c_breakout "screen on; games open breakout; @0.3; exit; simtap 180 193; @0.5; exit; exit pause; @0.1; arena; @1; arena; simtap 70 193; @0.1; arena; exit tap resume; @0.5; arena; exit" && {
    [ "$(ex_v "$(ex_d 1)" mode)" = C ] && [ "$(ex_v "$(ex_d 2)" pause)" = 0 ] && [ "$(ex_v "$(ex_d 3)" pause)" = 1 ] || { EVID="$(ex_d 1) | $(ex_d 2) | $(ex_d 3)"; fail ex_c_breakout_bubble; }
    t1=$(ex_v "$(grep '^APP arena name=' <<<"$out" | sed -n 1p)" ticks); t2=$(ex_v "$(grep '^APP arena name=' <<<"$out" | sed -n 2p)" ticks); t3=$(ex_v "$(grep '^APP arena name=' <<<"$out" | sed -n 3p)" ticks); t4=$(ex_v "$(grep '^APP arena name=' <<<"$out" | sed -n 4p)" ticks)
    [ "$t1" = "$t2" ] && [ "$t3" = "$t2" ] && [ "$t4" -gt "$t3" ] && [ "$(ex_v "$(ex_d 4)" pause)" = 0 ] || { EVID="ticks $t1 $t2 $t3 $t4 | $(ex_d 4)"; fail ex_c_breakout_really_paused; }; }
# a tap with the ball in flight is a steering tap, not a pause
nowrun ex_c_breakout_flight_tap_steers "screen on; games open breakout; @0.3; arena hold 1; arena tap 0 0; arena tap 60 60; exit; arena" && {
    [ "$(ex_v "$(ex_d 1)" pause)" = 0 ] && [ "$(ex_v "$(grep '^APP game breakout state=' <<<"$out" | sed -n 3p)" paddle)" = 72 ] || { EVID="$(ex_d 1)"; fail ex_c_breakout_steers; }; }
# under the overlay the knob is the volume only (no game move, no ring): the paddle stays, the volume changes; and the dial between turns too
nowrun ex_knob_under_overlay "screen on; vol 0; games open breakout; @1; simtap 180 193; @0.3; arena; exit pause; @0.3; simknob 3; @0.3; arena; simvol" && {
    [ "$(ex_v "$(grep '^APP game breakout state=' <<<"$out" | sed -n 1p)" paddle)" = "$(ex_v "$(grep '^APP game breakout state=' <<<"$out" | sed -n 2p)" paddle)" ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(grep '^APP game breakout state=' <<<"$out" | tr '\n' '|') vol $(vols)"; fail ex_knob_under_overlay_breakout; }; }
nowrun ex_knob_under_overlay_dial "screen on; vol 0; games open safe; @1; games count 0; dial tap 60 0; dial; exit pause; @0.2; simknob 2; @0.3; dial; simvol" && {
    [ "$(ex_v "$(grep '^APP game safe state=' <<<"$out" | sed -n 2p)" pos)" = "$(ex_v "$(grep '^APP game safe state=' <<<"$out" | sed -n 3p)" pos)" ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(grep '^APP game safe state=' <<<"$out" | tr '\n' '|') vol $(vols)"; fail ex_knob_under_overlay_dial; }; }
# a pause of 5 s or more outlasts the 4 s borrow: after 繼續 the knob is the field's again (paddle moves, volume does not)
nowrun ex_resume_reborrows_arena "screen on; vol 0; games open breakout; @1; simtap 180 193; @0.3; arena; exit pause; @5; exit tap resume; @0.3; simknob 2; @0.3; arena; simvol" && {
    p1=$(ex_v "$(grep '^APP game breakout state=' <<<"$out" | sed -n 1p)" paddle); p2=$(ex_v "$(grep '^APP game breakout state=' <<<"$out" | sed -n 2p)" paddle)
    [ "$p1" != "$p2" ] && [ "$(vols | cut -d' ' -f1)" = 0 ] || { EVID="paddle $p1 -> $p2 vol $(vols)"; fail ex_resume_reborrows_arena; }; }
nowrun ex_resume_reborrows_dial "screen on; vol 0; games open safe; @1; games count 0; dial tap 60 0; dial; exit pause; @5; exit tap resume; @0.3; simknob 2; @0.3; dial; simvol" && {
    p1=$(ex_v "$(grep '^APP game safe state=' <<<"$out" | sed -n 2p)" pos); p2=$(ex_v "$(grep '^APP game safe state=' <<<"$out" | sed -n 3p)" pos)
    [ "$p1" != "$p2" ] && [ "$(vols | cut -d' ' -f1)" = 0 ] || { EVID="pos $p1 -> $p2 vol $(vols)"; fail ex_resume_reborrows_dial; }; }
# A on a game before a tap lends the knob: the pill shows; while the ball runs the knob is the paddle's, no pill
nowrun ex_a_breakout "screen on; vol 0; games open breakout; @1; simknob 1; @0.2; exit; exit hide; simtap 180 193; @0.1; simknob 1; @0.2; exit" && {
    [ "$(ex_v "$(ex_d 1)" a)" = 1 ] && [ "$(ex_v "$(ex_d 2)" a)" = 0 ] || { EVID="$(ex_d 1) | $(ex_d 2)"; fail ex_a_breakout; }; }
# the pause overlay's 返回 leaves for the games hub, and the page's arena is gone
nowrun ex_c_breakout_back "screen on; games open breakout; @0.3; simtap 180 193; @0.3; exit pause; @0.1; exit tap back; @0.3; uitop; arena" && {
    [ "$(tp)" = "tools_games " ] && grep -qx 'APP arena none' <<<"$out" || { EVID="$(tp)"; fail ex_c_breakout_back; }; }
# C on 保險箱開鎖: the bubble is beside the field; a tap on the dial is the game's (one number), the bubble pauses (the dial ignores touches under the scrim), 繼續 goes on
nowrun ex_c_safe "screen on; games open safe; @0.3; games count 0; exit; dial tap 60 0; exit pause; @0.2; dial; simtap 250 150; @0.1; dial; exit tap resume; @0.2; simtap 250 150; @0.1; dial; exit" && {
    [ "$(ex_v "$(ex_d 1)" mode)" = C ] && [ "$(ex_v "$(ex_d 2)" pause)" = 1 ] || { EVID="$(ex_d 1) | $(ex_d 2)"; fail ex_c_safe_bubble; }
    p1=$(ex_v "$(grep '^APP game safe state=' <<<"$out" | sed -n 2p)" pos); p2=$(ex_v "$(grep '^APP game safe state=' <<<"$out" | sed -n 3p)" pos); p3=$(ex_v "$(grep '^APP game safe state=' <<<"$out" | sed -n 4p)" pos)
    [ "$p1" = 1 ] && [ "$p2" = 1 ] && [ "$p3" = 2 ] && [ "$(ex_v "$(ex_d 3)" pause)" = 0 ] || { EVID="pos $p1 $p2 $p3 | $(ex_d 3)"; fail ex_c_safe_scrim; }; }
# C on 四色記憶: the sequence's timer is paused under the overlay (the show ends only after 繼續)
nowrun ex_c_simon_timer_paused "screen on; games open simon; games mode 0; games count 0; @0.3; sectors hub; exit pause; @5; exit tap resume; @3" && {
    first=$(grep -n '^APP game simon input len=' <<<"$out" | head -1 | cut -d: -f1); res=$(grep -n '^APP exit resumed' <<<"$out" | head -1 | cut -d: -f1)
    [ "$(ex_v "$(ex_d 1)" frozen)" -ge 1 ] && [ -n "$first" ] && [ -n "$res" ] && [ "$first" -gt "$res" ] || { EVID="$(ex_d 1) input line $first resumed line $res"; fail ex_c_simon_frozen; }; }
# the escape hatch: a long press on the pill, on the pause overlay's 返回 and on the bubble goes home
nowrun ex_home_pill "screen on; vol 0; tools bubbles; @1; simknob 1; @0.2; simpress 180 318 3100; @0.3; uistack" && { [ "$(us)" = "menu|" ] || { EVID="$(us)"; fail ex_home_pill; }; }
nowrun ex_home_bubble "screen on; games open safe; @0.3; simpress 180 318 3100; @0.3; uistack" && { [ "$(us)" = "menu|" ] || { EVID="$(us)"; fail ex_home_bubble; }; }
nowrun ex_home_overlay "screen on; games open safe; @0.3; exit pause; @0.2; exit at back; simpress 180 212 3100; @0.3; uistack" && { [ "$(us)" = "menu|" ] || { EVID="$(us) $(grep '^APP exit at' <<<"$out")"; fail ex_home_overlay; }; }
# every game page has its exit (one mode each: A is on all of them), no Back button of its own: the pause overlay opens from the bubble (C) or a flight tap (B), back leaves
for g in $(grep '^GAME(' app/main/ui/games.def | sed 's/GAME(\([a-z0-9]*\),.*/\1/'); do
    m=C   # CF2: every game page is mode C (taps are used; the popup waits for a free knob), the pause overlay opens by the console (mode B: tap pauses)
    nowrun "ex_page_$g" "screen on; games open $g; @0.3; exit; exit pause; exit; exit tap resume; exit; exit pause; exit tap back; @0.3; uitop" && {
        [ "$(ex_v "$(ex_d 1)" mode)" = $m ] && [ "$(ex_v "$(ex_d 2)" pause)" = 1 ] && [ "$(ex_v "$(ex_d 4)" pause)" = 0 ] && [ "$(tp)" = "tools_games " ] || { EVID="$(ex_d 1) | $(ex_d 2) | $(ex_d 4) | $(tp)"; fail "ex_page_$g"; }; }
done
# 說明 from a game still works and Back from it returns to the game (the page keeps its exit)
nowrun ex_help_back "screen on; games open safe; @0.3; games ring; popring 3; @0.3; uitop; back; @0.3; uitop; exit" && {
    [ "$(ex_v "$(ex_d 1)" attached)" = 1 ] && [ "$(tp | cut -d' ' -f2)" = "tools_game_safe" ] || { EVID="$(tp) | $(ex_d 1)"; fail ex_help_back; }; }
# guess: a clip that plays is stopped by the pause (the shared stop call), the question waits; Back puts the song back
nowrun ex_c_guess "screen on; vol 0; track 3; @3; games open guess; @0.3; sectors hub; @0.5; mp3cur; exit pause; @0.3; mp3cur; exit tap back; @0.6; mp3cur" && {
    grep -q '^APP game guess pause clip stopped' <<<"$out" || { EVID="$(grep 'APP game guess' <<<"$out" | head -3 | tr '\n' '|')"; fail ex_c_guess_clip_stopped; }; }
