# SPDX-License-Identifier: LGPL-2.1-or-later
# --- F1a2: the content-first page frame (app/main/ui/ui_frame.[ch]) on the round glass: only the pilot pages (clock 喝水 and 習慣, the ambient scenes) have one; sourced by sim/test.sh (name order: after exit) ---
# Resting: content only (no title, no 選單 pill, no 返回 pill). The overlay (the page's options, 返回) shows by the ... bubble (rim, lower right: 284,284), by a knob turn that falls to the volume
# (the volume changes exactly as before), and hides by itself after 4 s; 返回 in it leaves, a hold anywhere goes home (CF2: no bubble). Console: `frame` dumps "APP frame armed= mode= shown= chrome=",
# `frame show|hide|hold`, `frame tap back|bubble|bg|opt <i> [<cell>]`.
fm_d() { grep '^APP frame armed=' <<<"$out" | sed -n "${1}p"; }
nowrun frame_pilot_knob_shows "screen on; vol 0; show drink; @0.3; frame; simtap 180 318; @0.2; frame; uiboxes; simknob 1; @0.2; frame; uiboxes; frame tap back; @0.3; uitop" && {
    [ "$(ex_v "$(fm_d 1)" armed)" = 1 ] && [ "$(ex_v "$(fm_d 1)" mode)" = C ] && [ "$(ex_v "$(fm_d 1)" shown)" = 0 ] && [ "$(ex_v "$(fm_d 2)" shown)" = 0 ] && [ "$(ex_v "$(fm_d 3)" shown)" = 1 ] || { EVID="$(fm_d 1) | $(fm_d 2) | $(fm_d 3)"; fail frame_pilot_knob_states; }
    n1=$(awk '/^APP uiboxes end/{k++} k==0 && /^APP uibox button/' <<<"$out" | grep -c '|\(返回\|Back\|離開\|Leave\)$'); n2=$(awk '/^APP uiboxes end/{k++} k==1 && /^APP uibox button/' <<<"$out" | grep -c '|\(返回\|Back\|離開\|Leave\)$')
    [ "$n1" = 0 ] && [ "$n2" = 1 ] || { EVID="back buttons resting $n1 shown $n2"; fail frame_pilot_back_hidden_then_shown; }
    [ "$(tp)" = "menu " ] || { EVID="$(tp)"; fail frame_pilot_back_leaves; }; }
# CF2: there is no ... bubble: nothing at the old bubble place (the rim at the lower right) shows the overlay, and the frame dump has no bubble field
nowrun frame_no_bubble "screen on; vol 0; show drink; @0.3; simtap 284 284; @0.2; frame" && {
    [ "$(ex_v "$(fm_d 1)" shown)" = 0 ] && ! grep -q '^APP frame .*bubble' <<<"$out" || { EVID="$(fm_d 1)"; fail frame_no_bubble; }; }
# the status stays in the content (no title label): the drink page's 休息中 / 下次 line is the sectors' status
nowrun frame_pilot_status_in_content "screen on; show drink; drink set 1 2 9 21 8 3; drink tick; @0.3; uiboxes; sectors" && {
    grep -q '^APP sectors status ' <<<"$out" && ! grep -q '^APP uibox label .*|\(休息中\|下次\|Resting\|Next\)' <<<"$out" || { EVID="$(grep '^APP sectors status' <<<"$out")"; fail frame_pilot_status_in_content; }; }
# a knob turn that falls to the volume changes the volume AND shows the overlay; it hides after 4 s
nowrun frame_pilot_knob_volume "screen on; @0.5; vol 0; show habits; @0.3; simknob 1; @0.2; frame; simvol; @5; frame" && {
    [ "$(ex_v "$(fm_d 1)" shown)" = 1 ] && [ "$(ex_v "$(fm_d 2)" shown)" = 0 ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(fm_d 1) | $(fm_d 2) vol $(vols)"; fail frame_pilot_knob_volume; }; }
# an option through the overlay: 選單 opens the page's ring (the overlay gives way to it), whose last cell is 說明
nowrun frame_pilot_option_ring_help "screen on; show habits; @0.3; frame show; frame tap opt 0 1; @0.3; popring -5; popring 3; @0.3; uitop" && {
    grep -q '^APP popring open=1 ' <<<"$out" && grep -qx 'APP uitop explain' <<<"$out" || { EVID="$(grep 'APP popring\|APP uitop' <<<"$out" | tr '\n' '|')"; fail frame_pilot_option_ring_help; }; }
# a game page with the shared exit (ambient): the exit's pill is the overlay's Back, the HEAD row's cells are its options; a borrowed axis (the speed cell) holds the overlay past the 4 s
nowrun frame_pilot_ambient "screen on; @0.5; vol 0; tools ambient; @0.5; frame; simknob 1; @0.2; frame; frame tap opt 0 0; @4.5; frame; frame tap back; @0.3; uitop; simvol" && {
    [ "$(ex_v "$(fm_d 1)" mode)" = X ] && [ "$(ex_v "$(fm_d 1)" chrome)" = 1 ] && [ "$(ex_v "$(fm_d 1)" shown)" = 0 ] && [ "$(ex_v "$(fm_d 2)" shown)" = 1 ] && [ "$(ex_v "$(fm_d 4)" shown)" = 1 ] || { EVID="$(fm_d 1) | $(fm_d 2) | $(fm_d 4)"; fail frame_pilot_ambient_overlay; }
    [ "$(tp)" = "tools " ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(tp) vol $(vols)"; fail frame_pilot_ambient_back; }; }
# the escape hatch (CF2): a hold of home_hold_ms (default 3000) ANYWHERE on a content page goes home; 2000 ms is not enough; the setting moves it (4000: 3100 is not enough)
nowrun frame_pilot_long_press_home "screen on; show habits; @0.3; simpress 180 180 3100; @0.3; uistack" && { [ "$(us)" = "menu|" ] || { EVID="$(us)"; fail frame_pilot_long_press_home; }; }
# every other round page makes no frame: the settings list keeps its Back pill and no bubble
nowrun frame_round_others_unchanged "screen on; setui; @0.3; frame; uiboxes" && {
    [ "$(ex_v "$(fm_d 1)" armed)" = 0 ] && ! grep -q '^APP uibox button .*|\(返回\|Back\)$' <<<"$out" || { EVID="$(fm_d 1)"; fail frame_round_others_unchanged; }; }   # GS: no Back on a list page (the up flick goes back)

nowrun frame_hold_not_2000 "screen on; show habits; @0.3; simpress 180 180 2000; @0.3; uistack" && { [ "$(us)" != "menu|" ] || { EVID="$(us)"; fail frame_hold_not_2000; }; }
NOWCFG=home_hold_ms=4000 nowrun frame_hold_cfg_4000 "screen on; show habits; @0.3; simpress 180 180 3100; @0.3; uistack; simpress 180 180 4100; @0.3; uistack" && {
    a=$(grep '^APP uistack' <<<"$out" | sed -n 1p); b=$(grep '^APP uistack' <<<"$out" | sed -n 2p)
    [ "$a" != "APP uistack menu" ] && [ "$b" = "APP uistack menu" ] || { EVID="$a | $b"; fail frame_hold_cfg_4000; }; }
NOWCFG=home_hold_ms=2000 nowrun frame_hold_cfg_2000 "screen on; show habits; @0.3; simpress 180 180 2100; @0.3; uistack" && { [ "$(us)" = "menu|" ] || { EVID="$(us)"; fail frame_hold_cfg_2000; }; }
# a short press and a drag are no hold; a hold on a list page's Back goes home too, a hold on the list page's body does not
nowrun frame_hold_drag_no "screen on; show habits; @0.3; simdrag 100 180 260 180 3400; @0.3; uistack" && { [ "$(us)" != "menu|" ] || { EVID="$(us)"; fail frame_hold_drag_no; }; }
nowrun frame_hold_list_body_home "screen on; setui; @0.5; simpress 180 150 3300; @0.3; uistack" && { [ "$(us)" = "menu|" ] || { EVID="$(us)"; fail frame_hold_list_body_home; }; }   # GS: a hold anywhere goes home on every page
# trigger: mode A (a page that takes no taps; the exit's mode override) -> a tap on the content shows the popup and nothing else happens on the field
nowrun cf2_tap_popup_mode_a "screen on; vol 0; tools ambient; @0.5; exit mode a; frame; simtap 180 200; @0.2; frame; exit; simvol" && {
    [ "$(ex_v "$(fm_d 1)" shown)" = 0 ] && [ "$(ex_v "$(fm_d 2)" shown)" = 1 ] && [ "$(vols | cut -d' ' -f1)" = 0 ] || { EVID="$(fm_d 1) | $(fm_d 2) vol $(vols)"; fail cf2_tap_popup_mode_a; }; }
# trigger: knob free (a toy before any tap) -> a turn changes the volume AND shows the popup
nowrun cf2_knob_free_popup "screen on; vol 0; tools bubbles; @0.5; frame; simknob 1; @0.2; frame; simvol" && {
    [ "$(ex_v "$(fm_d 1)" shown)" = 0 ] && [ "$(ex_v "$(fm_d 2)" shown)" = 1 ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(fm_d 1) | $(fm_d 2) vol $(vols)"; fail cf2_knob_free_popup; }; }
# trigger: mode C (taps AND knob used): a tap borrows the knob, so a turn is the page's (no popup, no volume); after the 4 s borrow ran out a turn falls to the volume and shows the popup
nowrun cf2_mode_c_after_timeout "screen on; vol 0; tools ambient; @0.5; simtap 180 200; @0.2; simknob 1; @0.2; frame; simvol; @4.5; simknob 1; @0.2; frame; simvol" && {
    [ "$(ex_v "$(fm_d 1)" shown)" = 0 ] && [ "$(ex_v "$(fm_d 2)" shown)" = 1 ] || { EVID="$(fm_d 1) | $(fm_d 2)"; fail cf2_mode_c_after_timeout; }
    [ "$(grep '^SIM vol' <<<"$out" | sed -n 1p)" = "SIM vol 0" ] && [ "$(grep '^SIM vol' <<<"$out" | sed -n 2p)" != "SIM vol 0" ] || { EVID="$(grep '^SIM vol' <<<"$out" | tr '\n' '|')"; fail cf2_mode_c_volume; }; }
# trigger: mode B (knob borrowed, taps unused; `exit mode b`): a tap on the field PAUSES and shows the popup (繼續 / 返回 and the options); the options move by the knob (a tap on one borrows it, then the knob steps it), the volume is not touched
nowrun cf2_mode_b_pause_popup "screen on; vol 0; games open tower; @0.5; exit mode b; simtap 180 200; @0.3; simtap 180 200; @0.3; exit; frame; frame tap opt 0 0; @0.2; games sel; simknob 1; @0.2; games sel; simvol" && {
    [ "$(ex_v "$(ex_d 2)" pause)" = 1 ] && [ "$(ex_v "$(fm_d 1)" shown)" = 1 ] || { EVID="$(ex_d 2) | $(fm_d 1)"; fail cf2_mode_b_pause; }
    [ "$(sed -n 's/.* mode=\([0-9]*\) .*/\1/p' <<<"$(grep '^APP game_ring' <<<"$out" | sed -n 1p)")" != "$(sed -n 's/.* mode=\([0-9]*\) .*/\1/p' <<<"$(grep '^APP game_ring' <<<"$out" | sed -n 2p)")" ] && [ "$(vols | cut -d' ' -f1)" = 0 ] || { EVID="$(grep '^APP game_ring' <<<"$out" | tr '\n' '|') vol $(vols)"; fail cf2_mode_b_items_by_knob; }; }
# list / menu / settings pages on the square panel keep their title and Back; round list pages are as before (no frame: frame_round_others_unchanged)
# CF2 fix round: (3) every press starts clean: a timer left by a press LVGL swallowed (a drag that latched its slop) never fires for the next press; (4) a finger that leaves its spot and comes back is no hold;
# (5) the screen going off during a hold stops it (no home in the dark)
nowrun frame_hold_stale_timer "screen on; show habits; @0.3; simdrag 100 180 130 180 100; simpress 100 180 2950; @0.3; uistack" && { [ "$(us)" != "menu|" ] || { EVID="$(us)"; fail frame_hold_stale_timer; }; }
nowrun frame_hold_out_and_back "screen on; show habits; @0.3; simdrag 100 180 100 180 3400; @0.3; uistack; simdrag 100 180 180 180 1200; simdrag 180 180 100 180 1200; @0.3; uistack" && { [ "$(us)" != "menu|" ] || { EVID="$(us)"; fail frame_hold_out_and_back; }; }
nowrun frame_hold_screen_off "screen on; show habits; @0.3; simpress 180 180 3600 800; @0.3; screen on; uistack" && { [ "$(us)" != "menu|" ] || { EVID="$(us)"; fail frame_hold_screen_off; }; }
# (1) ONE shared FAST UP FLICK (the finger moves within 150 ms of the press, 40 % of the panel height within 300 ms, lifts) shows a content page's popup (dice / slot / lists /
# quiz take every knob turn on the round glass; this works on every content page and on the FakePod); a slow drag of the same length, rapid taps, a hold are none; the knob stays with the page
fl_n() { grep -c '^APP flick up' <<<"$out"; }
for pg in "tools dice" "tools slot" "tools lunch" "tools people" "tools ideas" "vocab start" "games open tower" "tools ambient" "tools counter"; do
    nm=$(tr ' ' '_' <<<"$pg")
    nowrun "cf2_flick_up_$nm" "screen on; vol 0; $pg; @0.6; frame; simdrag 180 320 180 90 150; @0.2; frame; simvol" && {
        [ "$(ex_v "$(fm_d 1)" shown)" = 0 ] && [ "$(ex_v "$(fm_d 2)" shown)" = 1 ] && [ "$(fl_n)" = 1 ] && [ "$(vols | cut -d' ' -f1)" = 0 ] || { EVID="$(fm_d 1) | $(fm_d 2) flicks $(fl_n) vol $(vols)"; fail "cf2_flick_up_$nm"; }; }
done
nowrun cf2_flick_slow_no "screen on; vol 0; tools dice; @0.6; simdrag 180 320 180 90 1200; @0.2; frame; simdrag 180 320 180 90 500; @0.2; frame; simdrag 180 90 180 320 1200; @0.2; frame" && {
    [ "$(fl_n)" = 0 ] && [ "$(ex_v "$(fm_d 1)" shown)" = 0 ] && [ "$(ex_v "$(fm_d 2)" shown)" = 0 ] && [ "$(ex_v "$(fm_d 3)" shown)" = 0 ] || { EVID="flicks $(fl_n) $(fm_d 1) $(fm_d 2) $(fm_d 3)"; fail cf2_flick_slow_no; }; }
nowrun cf2_flick_short_no "screen on; vol 0; tools dice; @0.6; simdrag 180 320 180 260 100; @0.2; frame" && { [ "$(fl_n)" = 0 ] && [ "$(ex_v "$(fm_d 1)" shown)" = 0 ] || { EVID="flicks $(fl_n) $(fm_d 1)"; fail cf2_flick_short_no; }; }
nowrun cf2_flick_rapid_taps_beat "screen on; vol 0; games open beat; @0.3; games rule 1; arena hold 1; simtap 180 200; simtap 180 200; simtap 200 220; simtap 160 190; simtap 180 200; @0.2; frame" && {
    [ "$(fl_n)" = 0 ] && [ "$(ex_v "$(fm_d 1)" shown)" = 0 ] || { EVID="flicks $(fl_n) $(fm_d 1)"; fail cf2_flick_rapid_taps_beat; }; }
nowrun cf2_flick_not_on_list "screen on; setui; @0.5; simdrag 180 320 180 90 150; @0.2; uistack" && { [ "$(fl_n)" = 1 ] && [ "$(us)" = "menu|" ] || { EVID="flicks $(fl_n) $(us)"; fail cf2_flick_not_on_list; }; }   # GS: the up flick on a list page is Back (no popup)
