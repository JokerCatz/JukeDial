# SPDX-License-Identifier: LGPL-2.1-or-later
# ---- the style ring and the borrowed knob (Task 7). "viz stat" line 1: ring=<0|1> knob=<vol|var>; every "viz ..." command (menu, pick, bars) prints the two stat lines too.
# vs: the stat lines as "kind var ramp ring knob", one per line; vols: the "SIM vol" answers
NOWCFG="viz_var_ring=2"
# items 1 and 2: the knob steps the current axis (here the variant, wraps), not the volume; a tap on the kind axis selects it, a second tap steps the kind (to that kind's own variant); the centre closes
nowrun vizring_knob_pick "screen on; vol 0; color 0; @3; viz bars; @1; simvol; viz menu; viz pick 1; knob 1; viz stat; simvol; knob -1; knob -1; viz stat; viz pick 0; viz pick 0; viz pick -1; @0.5; viz stat; @2; knob 1; simvol; viz bars; viz stat" && {
    # bars (page opens), menu (axis kind), pick 1 (axis variant), stat (var 1), stat (var 5 = wrapped: bars have six variants on the round screen), pick 0 (axis kind), pick 0 (the kind steps to ring, its own variant 2), pick -1 (closed), stat, then "viz bars" (bars again: 5 was stored on close)
    want='bars 0 theme 0 vol|bars 0 theme 1 kind|bars 0 theme 1 var|bars 1 theme 1 var|bars 5 theme 1 var|bars 5 theme 1 kind|ring 2 theme 1 kind|ring 2 theme 0 vol|ring 2 theme 0 vol|bars 5 theme 0 vol|bars 5 theme 0 vol|'
    [ "$(vs)" = "$want" ] || { EVID="$(vs)"; fail vizring_knob_pick_stat; }
    [ "$(vols)" = "0 0 2 " ] || { EVID="volumes: $(vols)"; fail vizring_knob_volume; }          # unchanged while the ring is open, +1 step after it closed
    :; }
# the timeout restarts with every turn: opened at 0, turned at 3, still open at 6, closed at 8; the variant is stored when it closes, also across a restart of the simulator
nowrun vizring_timeout "screen on; vol 0; track 3; @3; viz bars; viz menu; viz pick 1; @3; knob 1; @3; viz stat; @2; viz stat; knob 1; simvol; @4" && {
    [ "$(vs)" = "bars 0 theme 0 vol|bars 0 theme 1 kind|bars 0 theme 1 var|bars 1 theme 1 var|bars 1 theme 0 vol|" ] || { EVID="$(vs)"; fail vizring_timeout; }
    [ "$(vols)" = "2 " ] || { EVID="volumes: $(vols)"; fail vizring_timeout_volume; }
    # the second run on the same root: the variant that was stored when the ring closed (the first run quits with the stored setting on disk)
    out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
    [ "$(vs)" = "bars 1 theme 0 vol|" ] || { EVID="$(vs)"; fail vizring_persist_second_run; }; }
# the variant is written when the ring closes and also when the page goes away with the ring open
nowrun vizring_var_on_close "screen on; vol 0; color 0; @3; viz bars; viz menu; viz pick 1; knob 1; knob 1; viz pick -1; @0.3; viz var 0; viz menu; viz pick 1; knob 1; knob 1; knob 1; viz pick -1; back; viz bars; viz stat" && {
    vs_all=$(vs); [ "$vs_all" = "bars 0 theme 0 vol|bars 0 theme 1 kind|bars 0 theme 1 var|bars 2 theme 0 vol|bars 0 theme 0 vol|bars 0 theme 1 kind|bars 0 theme 1 var|bars 3 theme 0 vol|bars 3 theme 0 vol|bars 3 theme 0 vol|" ] || { EVID="$vs_all"; fail vizring_var_on_close; }; }
nowrun vizring_var_on_destroy "screen on; vol 0; track 3; @3; viz bars; viz menu; viz pick 1; knob 1; knob 1; back; uitop; viz bars; viz stat; viz menu; viz pick 1; knob 1; theme next; @0.3; viz stat" && {
    [ "$(vs)" = "bars 0 theme 0 vol|bars 0 theme 1 kind|bars 0 theme 1 var|bars 2 theme 0 vol|bars 2 theme 0 vol|bars 2 theme 1 kind|bars 2 theme 1 var|bars 3 theme 0 vol|" ] || { EVID="$(vs)"; fail vizring_var_on_destroy; }; }
# item 4: the back cell leaves the page and the ring with it; the knob is volume
nowrun vizring_back_cell "screen on; vol 0; track 3; @3; viz bars; viz menu; viz pick -3; uitop; uistack; knob 1; simvol; viz stat" && {
    [ "$(grep '^APP uitop' <<<"$out")" = "APP uitop now" ] || fail vizring_back_cell_top
    [ "$(vols)" = "2 " ] && [ "$(vs)" = "bars 0 theme 0 vol|bars 0 theme 1 kind|bars 0 theme 0 vol|bars 0 theme 0 vol|" ] || { EVID="$(vs) vol $(vols)"; fail vizring_back_cell_knob; }; }
# no standing Back button (user decision 2026-11-12): a tap where it used to be (180,318) opens a ring (with a source playing: the playing ring, else the style ring) and stays on the page; every producible state leaves through the ring's back cell
nowrun viz_noback_oldspot "screen on; vol 0; track 3; @3; viz bars; @1; simtap 180 318; @0.3; nowmenu -5; uitop; simflick up; @0.3; uitop; uistack" &&   # GS: the second step is the up flick (the ring has no Back cell) 
    {
    [ "$(grep '^APP now menu' <<<"$out" | head -1)" = "APP now menu=1 page=0" ] || { EVID="$(grep '^APP now menu' <<<"$out")"; fail viz_noback_oldspot_ring; }
    [ "$(grep '^APP uitop' <<<"$out" | tr '\n' '|')" = "APP uitop viz|APP uitop now|" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail viz_noback_oldspot_top; }; }
# per state: tap -> ring=1 and still on the page; viz pick -3 -> the page under it (now) is on top
NBP="uitop; simtap 180 318; @0.3; nowmenu -5; uitop; simflick up; @0.3; uitop"   # a source is playing: the tap opens the playing ring, its Back cell leaves
nowrun viz_noback_normal "screen on; vol 0; track 3; @3; viz bars; @1; $NBP" && noback_check_playing normal
nowrun viz_noback_lowmem "screen on; vol 0; track 3; @3; lowmem 2; viz bars; @1; $NBP" && noback_check_playing lowmem
# (nothing playing: the page is opened after the stop; a stop with the page on top makes it leave since Task 13)
nowrun viz_noback_stopped "screen on; vol 0; track 3; @3; stop; @1; viz bars; @1; $NB" && noback_check stopped
nowrun viz_noback_nosource "screen on; vol 0; color 0; @3; stop; @1; viz bars; @1; $NB" && noback_check nosource
nowrun viz_noback_noise "screen on; vol 0; color 0; @3; viz bars; @1; $NBP" && noback_check_playing noise
nowrun viz_noback_afterclose "screen on; vol 0; track 3; @3; viz bars; viz menu; viz pick 1; knob 1; @4.3; viz stat; $NBP" && noback_check_playing afterclose
nowrun viz_noback_ringkind "screen on; vol 0; track 3; @3; viz ring; @1; $NBP" && noback_check_playing ringkind
# item 5: a theme change rebuilds the page with the ring open (and again during the hint): ring closed, knob volume, same stack, no extra page
nowrun vizring_theme "screen on; vol 0; color 0; @3; viz bars; viz menu; viz pick 1; knob 1; uistack; theme next; @0.3; viz stat; uistack; knob 1; simvol; viz menu; viz pick -1; theme next; @0.3; viz stat; uistack; @2; viz stat" && {
    stack_is vizring_theme_before 1 "menu now viz"; stack_is vizring_theme_after 2 "menu now viz"; stack_is vizring_theme_after_hint 3 "menu now viz"
    [ "$(vs)" = "bars 0 theme 0 vol|bars 0 theme 1 kind|bars 0 theme 1 var|bars 1 theme 0 vol|bars 1 theme 1 kind|bars 1 theme 0 vol|bars 1 theme 0 vol|bars 1 theme 0 vol|" ] || { EVID="$(vs)"; fail vizring_theme_state; }
    [ "$(vols)" = "2 " ] || { EVID="volumes: $(vols)"; fail vizring_theme_volume; }; }
# item 6: every way out of the page with the ring open gives the knob back to the volume, and the changed variant is kept:
# 1 back, 2 a page pushed over it and popped at once (no timer tick in between), 3 the same with time passing, 4 screen off and on, 5 a page over it for 6 s
nowrun vizring_leave_ways "screen on; vol 0; track 3; @3; viz bars; viz menu; viz pick 1; knob 1; back; viz stat; knob 1; simvol; viz bars; viz menu; viz pick 1; knob 1; uipush settings; back; viz stat; knob 1; simvol; viz menu; viz pick 1; knob 1; uipush settings; @0.3; back; @0.3; viz stat; knob 1; simvol; viz menu; viz pick 1; knob 1; screen off; @0.3; screen on; @1; viz stat; knob 1; simvol; viz menu; uipush settings; @6; back; viz stat; knob 1; simvol; uitop" && {
    # stat lines: 1 bars(open), 2 menu, 3 after back, 4 viz bars, 5 menu, 6 after pop, 7 menu, 8 after pop 2, 9 menu, 10 after screen on, 11 menu, 12 after the timeout while covered
    got=$(vs); want='bars 0 theme 0 vol|bars 0 theme 1 kind|bars 0 theme 1 var|bars 1 theme 0 vol|bars 1 theme 0 vol|bars 1 theme 1 kind|bars 1 theme 1 var|bars 2 theme 0 vol|bars 2 theme 1 kind|bars 2 theme 1 var|bars 3 theme 0 vol|bars 3 theme 1 kind|bars 3 theme 1 var|bars 4 theme 0 vol|bars 4 theme 1 kind|bars 4 theme 0 vol|'
    [ "$got" = "$want" ] || { EVID="$got"; fail vizring_leave_ways_stat; }
    [ "$(vols)" = "2 4 6 8 10 " ] || { EVID="volumes: $(vols) (each knob after a close is +1 step: 2 4 6 8 10)"; fail vizring_leave_ways_volume; }
    [ "$(grep '^APP uitop' <<<"$out" | tail -1)" = "APP uitop viz" ] || fail vizring_leave_ways_top; }
# the stereo tap follows the kind picked through the ring (the same apply() as the console): on for phase, off for bars, off while a page covers it
nowrun vizring_stereo "screen on; vol 0; track 3; @3; viz bars; viz menu; @0.2; knob 4; @0.2; viz stat; knob 1; @0.2; viz stat; knob -1; @0.2; viz stat; uipush settings; @0.3; viz stat; back; @0.3; viz stat" && {
    sv=$(grep '^APP viz open' <<<"$out" | sed 's/.*stereo=\([0-9]\).*/\1/' | tr '\n' ' ')
    [ "$sv" = "0 0 1 0 1 0 1 " ] || { EVID="stereo per stat line: $sv"; fail vizring_stereo; }; }
# the hint state is observable: "viz stat" line 3 = "APP viz ring hint=<0|1> title=<0|1> line=<first text line, or - when hidden>" (the fixture root has no CJK font: English text; since R55 the first line is the axis' value alone, a clipped line would end in "...")
nowrun vizring_hint_state "screen on; vol 0; color 0; @4; viz bars; viz menu; viz pick 1; knob 1; viz stat; viz pick 2; viz stat; viz pick -1; @0.5; viz stat; @2; viz stat" && {
    # bars (page open, ring never opened), menu, pick 1 (variant axis), after a knob turn (variant changes), pick 2 (colour axis) and its stat, pick -1 (closes), 0.5 s later, 2.5 s later
    want='0 1 -|0 0 Bars|0 0 Solid|0 0 Segments|0 0 Theme|0 0 Theme|1 0 Bars/Segments/Theme|1 0 Bars/Segments/Theme|0 1 -|'
    [ "$(rl)" = "$want" ] || { EVID="$(rl)"; fail vizring_hint_state; }
    [ "$(vs | sed 's/|/\n/g' | sed -n 8p)" = "bars 1 theme 0 vol" ] || { EVID="$(vs)"; fail vizring_hint_knob_is_volume; }; }
# a tap during the hint opens the ring again; the old hint deadline must not close or hide anything of the new ring
nowrun vizring_hint_retap "screen on; vol 0; color 0; @4; viz bars; stop; viz menu; viz pick -1; @0.5; simtap 180 180; @0.2; viz stat; @1.5; viz stat; @3; viz stat; @1.5; viz stat" && {
    want='0 1 -|0 0 Bars|1 0 Bars/Solid/Theme|0 0 Bars|0 0 Bars|1 0 Bars/Solid/Theme|0 1 -|'
    [ "$(rl)" = "$want" ] || { EVID="$(rl)"; fail vizring_hint_retap_lines; }
    st=$(vs | sed 's/|/\n/g' | sed -n '4,7p' | tr '\n' '|')   # after the tap, +1.7 s, +4.7 s (the tap's 4 s ran out at 4.2), +6.2 s
    [ "$st" = "bars 0 theme 1 kind|bars 0 theme 1 kind|bars 0 theme 0 vol|bars 0 theme 0 vol|" ] || { EVID="$st"; fail vizring_hint_retap_state; }; }
# a variant that ends where it started is not written: no config save is scheduled (the save is 3 s after the last change: "APP cfg saved"); a real change is, as the control
nowrun vizring_no_write_same "screen on; vol 0; color 0; @4; viz bars; @5; viz menu; knob 1; knob -1; viz pick -1; @6" && { [ "$(saves)" = 0 ] || { EVID="saves after the menu: $(saves)"; fail vizring_no_write_same; }; }
nowrun vizring_write_changed "screen on; vol 0; color 0; @4; viz bars; @5; viz menu; knob 1; viz pick -1; @6" && { [ "$(saves)" = 1 ] || { EVID="saves after the menu: $(saves)"; fail vizring_write_changed; }; }
# turning the variant keeps the stereo tap on: the fake prints "SIM stereo <0|1>" at every switch of the tap; one switch-on up to the last stat, no off in between (the page's destroy at exit switches it off)
nowrun vizring_stereo_stays "screen on; vol 0; color 0; @4; viz phase; @0.5; viz menu; viz pick 1; knob 1; @0.1; knob 1; knob -1; @0.2; viz pick 1; @0.2; viz pick 1; @0.2; viz pick -1; @0.3; viz stat" && {
    last=$(grep -n '^APP viz ring' <<<"$out" | tail -1 | cut -d: -f1)
    sw=$(head -n "$last" <<<"$out" | grep '^SIM stereo' | sed 's/SIM stereo //' | tr '\n' ' ')
    [ "$sw" = "1 " ] || { EVID="tap switches: $sw (wanted a single switch-on)"; fail vizring_stereo_stays; }; }
NOWCFG=""
# the four axes (spec amendment 2026-11-12): kind, variant, colour, map. "viz pick 0..3" taps an axis cell, the knob steps the current axis (kind on open), "knob=" says which
nowrun viz_axis_kind "screen on; vol 0; color 0; @3; viz bars; viz menu; knob 1; viz stat; knob -1; viz stat; knob -1; viz stat; knob 1; viz stat" && {
    [ "$(ax)" = "bars:0:theme:0:vol:height bars:0:theme:1:kind:height ring:0:theme:1:kind:height bars:0:theme:1:kind:height lyrics:0:theme:1:kind:height bars:0:theme:1:kind:height " ] || { EVID="$(ax)"; fail viz_axis_kind; }; }
# several detents at once end where the same number of single steps do (kind: bars +4 = phase, -7 more = waterfall, six kinds around when nothing has voices: lyrics is the sixth), the stat is sane
nowrun viz_axis_knob_many "screen on; vol 0; color 0; @3; viz bars; viz menu; knob 4; viz stat; knob -7; viz stat; @0.3; viz stat" && {
    [ "$(ax | cut -d' ' -f3-5 | tr ' ' '\n' | cut -d: -f1 | tr '\n' ' ')" = "phase fall fall " ] || { EVID="$(ax)"; fail viz_axis_knob_many; }; }
# the console's viz var follows the ring's working kind: turn the kind to ring, set its variant, close; both stay
nowrun viz_axis_console_var "screen on; vol 0; color 0; @3; viz bars; viz menu; knob 1; viz var 2; viz stat; viz pick -1; @4" && {
    [ "$(ax | cut -d' ' -f4)" = "ring:2:theme:1:kind:height" ] || { EVID="$(ax)"; fail viz_axis_console_var; }
    out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
    [ "$(ax)" = "ring:2:theme:0:vol:height " ] || { EVID="$(ax)"; fail viz_axis_console_var_persist; }; }
nowrun viz_axis_var "screen on; vol 0; color 0; @3; viz bars; viz menu; viz pick 1; viz pick 1; knob -1; viz stat" && {
    [ "$(ax)" = "bars:0:theme:0:vol:height bars:0:theme:1:kind:height bars:0:theme:1:var:height bars:1:theme:1:var:height bars:0:theme:1:var:height " ] || { EVID="$(ax)"; fail viz_axis_var; }; }
nowrun viz_axis_col "screen on; vol 0; color 0; @3; viz bars; viz menu; viz pick 2; knob 1; viz stat; knob -1; knob -1; viz stat; knob -1; viz stat; knob 1; knob 1; viz stat" && {
    [ "$(ax | cut -d' ' -f3-)" = "bars:0:theme:1:col:height bars:0:fire:1:col:height bars:0:mono:1:col:height bars:0:duo:1:col:height bars:0:theme:1:col:height " ] || { EVID="$(ax)"; fail viz_axis_col; }; }
nowrun viz_axis_map "screen on; vol 0; color 0; @3; viz bars; viz menu; viz pick 3; knob 1; knob 1; knob 1; viz stat; knob 1; viz stat; viz pick 3" && {
    [ "$(ax | cut -d' ' -f3-)" = "bars:0:theme:1:map:height bars:0:theme:1:map:level bars:0:theme:1:map:height bars:0:theme:1:map:tip " ] || { EVID="$(ax)"; fail viz_axis_map; }; }
nowrun viz_axis_map_na "screen on; vol 0; color 0; @3; viz bars; viz scope; viz menu; viz pick 3; knob 1; viz stat; viz pick 3" && {
    [ "$(ax | cut -d' ' -f4-)" = "scope:0:theme:1:map:height scope:0:theme:1:map:height scope:0:theme:1:map:height " ] || { EVID="$(ax)"; fail viz_axis_map_na; }
    grep '^APP viz ring' <<<"$out" | sed -n 4p | grep -q 'line=— cells=' || { EVID="$(grep '^APP viz ring' <<<"$out" | sed -n 4p)"; fail viz_axis_map_na_line; }; }   # the English text (no CJK font here): the value alone, an em dash (R55)
# closing writes what the axes changed (the centre; the second run on the same root reads it), then the knob is volume
nowrun viz_axis_close_saved "screen on; vol 0; color 0; @3; viz bars; viz menu; knob 1; viz pick 1; knob 1; viz pick 2; knob 2; viz pick 3; knob 2; viz pick -1; @2; simvol; knob 1; simvol; viz stat; @4" && {
    [ "$(ax | awk '{print $NF}')" = "ring:1:ice:0:vol:band" ] || { EVID="$(ax)"; fail viz_axis_close_state; }
    [ "$(vols)" = "0 2 " ] || { EVID="volumes: $(vols)"; fail viz_axis_close_volume; }
    out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
    [ "$(ax)" = "ring:1:ice:0:vol:band " ] || { EVID="$(ax)"; fail viz_axis_close_persist; }; }
nowrun viz_axis_leave_ways "screen on; vol 0; color 0; @3; viz bars; viz menu; knob 1; viz stat; back; @0.3; viz stat; viz bars; viz menu; viz pick 2; knob 1; theme next; @0.3; viz stat; viz bars; viz menu; viz pick 3; knob 1; screen off; @0.3; screen on; @1; viz stat; viz menu; viz pick 3; knob 2; @6; viz stat" && {
    [ "$(ax | cut -d' ' -f3-)" = "ring:0:theme:1:kind:height ring:0:theme:0:vol:height bars:0:theme:0:vol:height bars:0:theme:1:kind:height bars:0:theme:1:col:height bars:0:fire:0:vol:height bars:0:fire:0:vol:height bars:0:fire:1:kind:height bars:0:fire:1:map:height bars:0:fire:0:vol:tip bars:0:fire:1:kind:tip bars:0:fire:1:map:tip bars:0:fire:0:vol:level " ] || { EVID="$(ax)"; fail viz_axis_leave_ways; }; }
# 30 times open, turn every axis, close: no leak the test harness ("APP ui leak") or sanitizer would report
loop=""; for i in $(seq 30); do loop="$loop viz menu; knob 1; viz pick 1; knob 1; viz pick 2; knob 1; viz pick 3; knob 1; viz pick -1; @1.6;"; done
nowrun viz_axis_loop "screen on; vol 0; color 0; @3; viz bars;$loop viz stat" && { grep -q '^APP viz open=1 ' <<<"$out" || fail viz_axis_loop_page; }
# ---- Task 8b: the top title shows 10 s after entering / a track change / the end of the hint; the ring has no song line and, for MP3, the Details cell
# rf: "hint title cells detail" per "APP viz ring" line (every "viz ..." command and "viz stat" print one)
nowrun viz_title_timeout "screen on; vol 0; track 3; @3; viz bars; viz stat; @9; viz stat; @1.5; viz stat; track 5; @0.5; viz stat; @10.5; viz stat" && {
    # lines: page open, +0, +9 s (still shown), +10.5 s (hidden), new track +0.5 s (shown), +11 s (hidden)
    [ "$(rf)" = "0100 0100 0100 0000 0100 0000 " ] || { EVID="$(rf)"; fail viz_title_timeout; }; }
# a source without details (white noise): the centre closes the ring with the hint, then the title for 10 s. fields: hint title cells detail
nowrun viz_title_ring_hint "screen on; vol 0; color 0; @3; viz bars; viz menu; viz pick -1; @0.5; viz stat; @2; viz stat; @10.5; viz stat" && {
    # lines: open, ring (title hidden, no song line, 7 cells, no details), centre -> hint, +0.5 s hint, +2.5 s title again, +13 s hidden again
    [ "$(rf)" = "0100 0070 1000 1000 0100 0000 " ] || { EVID="$(rf)"; fail viz_title_ring_hint; }; }
# MP3: the centre tap closes the ring (no hint), writes the variant and opens the details page; coming back: ring closed, knob volume, title shown again for 10 s
nowrun viz_ring_details_mp3 "screen on; vol 0; track 3; @3; viz bars; viz menu; viz pick 1; knob 1; viz stat; viz pick 7; viz stat; viz pick -1; uitop; @11; uitop; back; @0.3; uitop; viz stat" && {
    [ "$(grep '^APP uitop' <<<"$out" | tr '\n' '|')" = "APP uitop meta|APP uitop meta|APP uitop viz|" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail viz_ring_details_top; }
    # lines: bars, menu, stat (variant turned), pick 7 (nothing) and its stat, pick -1 (no hint), last stat
    [ "$(rf)" = "0100 0071 0071 0071 0071 0071 0100 0100 " ] || { EVID="$(rf)"; fail viz_ring_details_stat; }
    [ "$(vs | sed 's/|/\n/g' | tail -2 | head -1)" = "bars 1 theme 0 vol" ] || { EVID="$(vs)"; fail viz_ring_details_back; }; }
nowrun viz_ring_no_details_noise "screen on; vol 0; color 0; @3; viz bars; viz menu; viz stat; viz pick 7; uitop; viz pick -1; @0.5; viz stat" && {
    [ "$(rf)" = "0100 0070 0070 0070 1000 1000 " ] && [ "$(grep '^APP uitop' <<<"$out")" = "APP uitop viz" ] || { EVID="$(rf)"; fail viz_ring_no_details_noise; }; }
# nobody owns playback: the centre tap closes the ring with the normal hint and opens no details page. (The "No track" hint, MP3 owning playback with no current
# track, is a window of a few ms in the firmware; the simulator cannot hold it, so that branch has no test.)
nowrun viz_ring_noowner_hint "screen on; vol 0; color 0; @3; viz bars; viz menu; viz pick -1; uitop; viz stat; @2; viz stat" && {
    ht=$(grep '^APP viz hinttext=' <<<"$out" | tr '\n' '|')
    [ "$(grep '^APP uitop' <<<"$out")" = "APP uitop viz" ] && [ "$ht" = "APP viz hinttext=-|APP viz hinttext=Knob: kind|APP viz hinttext=Knob = volume|APP viz hinttext=Knob = volume|APP viz hinttext=-|" ] || { EVID="$ht $(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail viz_ring_noowner_hint; }; }
# the screen wakes with the page on top: the title row again for 10 s (the page is untouched: nothing but the screen changes)
nowrun viz_title_wake "screen on; vol 0; track 3; @3; viz bars; @11; viz stat; screen off; @1; screen on; @0.5; viz stat; @9; viz stat; @1.5; viz stat" && {
    [ "$(rf)" = "0100 0000 0100 0100 0000 " ] || { EVID="$(rf)"; fail viz_title_wake; }; }
# a track change while the ring is open does not restart its 4 s (opened at 0, track at 1.5, still open at 3.5, closed at 4.5); the ring's text does not change
nowrun viz_ring_track_change "screen on; vol 0; track 3; @3; viz bars; viz menu; @1.5; viz stat; track 5; @0.5; viz stat; @1.5; viz stat; @1; viz stat" && {
    st=$(grep '^APP viz open' <<<"$out" | sed -E 's/.* ring=([01]) .*/\1/' | tr '\n' ' ')
    ln=$(grep '^APP viz ring' <<<"$out" | sed -E 's/^APP viz ring hint=[01] title=[01] line=(.*) cells=[0-9]+ detail=[01]$/\1/' | tr '\n' '|')
    lk=${ln%|}; lk=${lk%|*}
    [ "$st" = "0 1 1 1 1 0 " ] && [ "$lk" = "-|Bars|Bars|Bars|Bars" ] || { EVID="ring: $st lines: $ln"; fail viz_ring_track_change; }; }
nowrun viz_title_lowmem_stays "screen on; vol 0; track 3; @3; lowmem 2; viz bars; @20; viz stat" && { [ "$(rf | awk '{print $NF}')" = "0100" ] || { EVID="$(rf)"; fail viz_title_lowmem_stays; }; }

# ---- bar width and sensitivity (Task 8): "viz barw|floor [next]" print "APP viz barw=<n>" / "APP viz floor=<n>"; three taps come back; the setting is saved (3 s after the change) and read in a second run
nowrun viz_barw_cycle "screen on; vol 0; track 3; @4; viz barw; viz barw next; viz barw next; viz barw next; viz barw next; viz barw" && { [ "$(vv barw)" = "7 9 5 7 9 9 " ] || { EVID="$(vv barw)"; fail viz_barw_cycle; }; }
nowrun viz_floor_cycle "screen on; vol 0; track 3; @4; viz floor; viz floor next; viz floor next; viz floor next; viz floor next; viz floor" && { [ "$(vv floor)" = "-48 -36 -60 -48 -36 -36 " ] || { EVID="$(vv floor)"; fail viz_floor_cycle; }; }
nowrun viz_barw_floor_second_run "screen on; vol 0; track 3; @4; viz barw next; viz floor next; viz floor next; @5" && {
    out=$($SIM --headless --root "$T/fix" --cmd "viz barw; viz floor" 2>&1)
    { [ "$(vv barw)" = "9 " ] && [ "$(vv floor)" = "-60 " ]; } || { EVID="barw $(vv barw) floor $(vv floor)"; fail viz_barw_floor_second_run; }; }
# a stored value that is not one of the three reads as the default and cycles from there
NOWCFG=$'viz_w=6\nviz_floor=3'
nowrun viz_barw_floor_invalid "viz barw; viz barw next; viz floor; viz floor next" && { [ "$(vv barw)" = "7 9 " ] && [ "$(vv floor)" = "-48 -36 " ] || { EVID="barw $(vv barw) floor $(vv floor)"; fail viz_barw_floor_invalid; }; }
NOWCFG=""
# the page reads the values when it is created, also after a theme rebuild: the picture of a run that changed them first equals the picture of a run that had them from the start (virtual time: no command costs any), and the three differ
vshot "" "" "" vz_def; vshot "viz_w=9" "" "" vz_w9; vshot "viz_w=5" "" "" vz_w5; vshot "viz_floor=-60" "" "" vz_f60; vshot "viz_floor=-36" "" "" vz_f36
vshot "" "viz barw next" "" vz_w9n; vshot "" "viz floor next; viz floor next" "" vz_f60n
{ [ -s "$T/vz_def.png" ] && ! cmp -s "$T/vz_def.png" "$T/vz_w9.png" && ! cmp -s "$T/vz_def.png" "$T/vz_w5.png" && ! cmp -s "$T/vz_w5.png" "$T/vz_w9.png"; } || fail viz_barw_pictures_differ
{ ! cmp -s "$T/vz_def.png" "$T/vz_f60.png" && ! cmp -s "$T/vz_def.png" "$T/vz_f36.png" && ! cmp -s "$T/vz_f60.png" "$T/vz_f36.png"; } || fail viz_floor_pictures_differ
cmp -s "$T/vz_w9.png" "$T/vz_w9n.png" && cmp -s "$T/vz_f60.png" "$T/vz_f60n.png" || fail viz_new_values_on_next_open
vshot "" "viz bars; @1; viz barw next; viz floor next" "theme next; @1; " vz_th_n; vshot "viz_w=9
viz_floor=-36" "viz bars; @1" "theme next; @1; " vz_th_c
cmp -s "$T/vz_th_n.png" "$T/vz_th_c.png" || fail viz_new_values_after_theme_rebuild

