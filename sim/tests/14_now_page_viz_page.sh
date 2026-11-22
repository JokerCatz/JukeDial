# SPDX-License-Identifier: LGPL-2.1-or-later
# ---- one now-playing page on the stack (now_open): a pick from a list above a now page returns to it instead of stacking a second one.
# Taps on fixed geometry: the now page's picture (180,180) opens its menu; the playing ring's cells: MP3 slot 2 = upper left (81,81) = the list; colour noise slot 0 = bottom left (81,279) = the list, centre = pause; a ring
# segment is selected by a tap and confirmed by the centre; the track list's 4th row is at y=212. The sim's radio and scene fakes do not take
# the player, so the list above the now page that the radio pick needs is pushed by the console; chiptune cannot play (no /chiptune, decoders fail).
nowrun now_single_noise "screen on; vol 0; track 1; @3; noiseui; @0.5; simtap 180 186; @0.5; uistack; back; @1; uistack" &&   # the scene list's second row (the first scene; Task 13: one tap plays)
    { stack_is now_single_noise 1 "menu now"; stack_is now_single_noise_back 2 "menu"; }
nowrun now_single_list_pick_mp3 "screen on; vol 0; track 1; @3; $NOWMENU; simtap 81 81; @0.5; uitop; simtap 180 238; @0.5; uistack; back; @1; uistack" &&
    { grep -qx 'APP uitop tracks' <<<"$out" || { EVID="the menu's 4th slot did not open the track list"; fail now_single_list_pick_mp3_open; }
      stack_is now_single_list_pick_mp3 1 "menu now"; stack_is now_single_list_pick_mp3_back 2 "menu"; }
COLORPICK="$NOWMENU; simtap 81 279; @0.5; uitop; simtap 279 81; @0.3; simtap 180 180; @0.5; uistack"
nowrun now_single_list_pick_colors "screen on; vol 0; color 2; @3; $COLORPICK; back; @1; uistack" &&
    { grep -qx 'APP uitop colors' <<<"$out" || { EVID="the menu's top slot did not open the colour list"; fail now_single_list_pick_colors_open; }
      stack_is now_single_list_pick_colors 1 "menu now"; stack_is now_single_list_pick_colors_back 2 "menu"; }
nowrun now_single_list_pick_radio "screen on; vol 0; track 1; @3; uipush radio; @0.5; simtap 180 82; @0.5; uistack; back; @1; uistack" &&   # the station list's first row (Task 13: one tap plays)
    { stack_is now_single_list_pick_radio 1 "menu now"; stack_is now_single_list_pick_radio_back 2 "menu"; }
nowrun now_single_console "screen on; vol 0; track 1; @1; noiseui; @0.5; track 2; @1; uistack; back; @1; uistack" &&
    { stack_is now_single_console 1 "menu now"; stack_is now_single_console_back 2 "menu"; }
nowrun now_stack_no_growth "screen on; vol 0; color 2; @3; $COLORPICK; $COLORPICK; $COLORPICK; $COLORPICK; $COLORPICK; back; @1" && {
    # every round must prove its pick: the list was on top before it (uitop colors) and is gone after it (uistack menu now)
    [ "$(grep -cx 'APP uitop colors' <<<"$out")" = 5 ] || { EVID="the list opened $(grep -cx 'APP uitop colors' <<<"$out") of 5 times"; fail now_stack_no_growth_opened; }
    [ "$(grep -cx 'APP uistack menu now' <<<"$out")" = 5 ] && [ "$(grep -c '^APP uistack' <<<"$out")" = 5 ] ||
        { EVID="uistack after each pick: $(grep '^APP uistack' <<<"$out" | sed 's/^APP uistack //' | tr '\n' '|')"; fail now_stack_no_growth; }
    ! grep -q '^APP ui stack full' <<<"$out" || { EVID="APP ui stack full"; fail now_stack_no_growth_full; }; }
# the re-created page takes the audio's pause state: paused colour noise, 清單, the SAME colour (nothing new starts) -> still paused, the
# menu's pause (the centre) now resumes; a DIFFERENT colour starts a new source -> page and audio both playing. Read back with "nowmenu -3".
nowrun now_pause_same_pick "screen on; vol 0; color 2; @3; $NOWMENU; $PAUSE; nowmenu -3; simtap 81 279; @0.5; uitop; simtap 81 81; @0.3; simtap 180 180; @0.5; uistack; nowmenu -3; @1; shot $T/pa.png; @1; shot $T/pb.png; $NOWMENU; $PAUSE; nowmenu -3" && {
    pz=$(grep '^APP now paused' <<<"$out" | tr '\n' '|')
    [ "$pz" = "APP now paused=1 audio=paused|APP now paused=1 audio=paused|APP now paused=0 audio=playing|" ] || { EVID="pause states: $pz"; fail now_pause_same_pick; }
    stack_is now_pause_same_pick_stack 1 "menu now"
    cmp -s "$T/pa.png" "$T/pb.png" || { EVID="the paused page changed over @1"; fail now_pause_same_pick_still; }; }
nowrun now_pause_other_pick "screen on; vol 0; color 2; @3; $NOWMENU; $PAUSE; nowmenu -3; simtap 81 279; @0.5; simtap 279 81; @0.3; simtap 180 180; @0.5; uistack; nowmenu -3" && {
    pz=$(grep '^APP now paused' <<<"$out" | tr '\n' '|')
    [ "$pz" = "APP now paused=1 audio=paused|APP now paused=0 audio=playing|" ] || { EVID="pause states: $pz"; fail now_pause_other_pick; }
    stack_is now_pause_other_pick_stack 1 "menu now"; }

# ---- spectrum page (scr_viz.c): opens over the now page, one status line format, garbage settings read as defaults, low memory = title + back only
VIZSTAT='^APP viz open=[01] kind=(bars|ring|scope|fall|vu|phase|voices|rows|roll|lyrics) var=[0-9] ramp=[a-z]+ ring=0 knob=vol period_ms=[0-9]+ stereo=0 map=(height|tip|band|level)$'
nowrun viz_opens "screen on; vol 0; track 3; @3; viz stat; viz bars; @1; uitop; uistack; viz stat; viz ring; viz var next; @1; uistack; back; uitop; back; uitop" && {
    st=$(grep '^APP viz open' <<<"$out")
    [ "$(wc -l <<<"$st")" = 5 ] && ! grep -qvE "$VIZSTAT" <<<"$st" || { EVID="stat lines: $st"; fail viz_stat_format; }
    [ "$(sed -n 1p <<<"$st" | cut -d' ' -f3)" = "open=0" ] && [ "$(sed -n 2p <<<"$st" | cut -d' ' -f3-4)" = "open=1 kind=bars" ] || { EVID="$st"; fail viz_stat_open; }
    [ "$(sed -n 5p <<<"$st" | cut -d" " -f4-5)" = "kind=ring var=1" ] || { EVID="$st"; fail viz_stat_ring_var; }
    [ "$(grep '^APP uitop' <<<"$out" | tr '\n' '|')" = "APP uitop viz|APP uitop now|APP uitop menu|" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail viz_uitop; }
    stack_is viz_stack_open 1 "menu now viz"; stack_is viz_stack_back 2 "menu now viz"; }
fresh; printf 'lang=0\nviz_style=xyz\nviz_var_bars=99\nviz_w=0\nviz_floor=1\n' > "$T/fix/_player/config/app.txt"
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; track 3; @3; viz stat; viz bars; @1; viz stat; shot $T/vg.png" 2>&1)
{ [ "$(grep -c '^APP viz open=. kind=bars var=0 ' <<<"$out")" = 3 ] && [ -s "$T/vg.png" ] && ! grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out"; } || { EVID="$(grep '^APP viz' <<<"$out" | tr '\n' '|')"; fail viz_garbage_defaults; }
fresh; printf 'lang=0\nviz_style=ring\nviz_var_ring=-3\n' > "$T/fix/_player/config/app.txt"
out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
grep -q '^APP viz open=0 kind=ring var=0 ' <<<"$out" || { EVID="$(grep '^APP viz' <<<"$out")"; fail viz_negative_var; }
nowrun viz_lowmem "screen on; vol 0; track 3; @3; lowmem 2; viz bars; @1; viz stat; viz ring; viz var next; @1; viz stat; back; uitop; back; uitop" && {
    [ "$(grep '^APP uitop' <<<"$out" | tr '\n' '|')" = "APP uitop now|APP uitop menu|" ] || fail viz_lowmem_back
    dl=$(grep '^APP viz draw' <<<"$out" | tr '\n' '|')   # no picture under lowmem 2: live=0 and nothing drawn, both times
    # every "viz ..." command and "viz stat" print the line: all of them live=0 frames=0
    [ "$(grep -c '^APP viz draw live=0 frames=0 avg_us=0 max_us=0$' <<<"$out")" -ge 4 ] && ! grep -q '^APP viz draw live=1\|^APP viz draw live=. frames=[1-9]' <<<"$out" || { EVID="$dl"; fail viz_lowmem_nopicture; }; }
nowrun viz_draw_stat "screen on; vol 0; track 3; @3; viz stat; viz bars; @2; viz stat; @1; viz stat; back; viz stat" && {   # lines: closed, "viz bars" itself, after 2 s, after 1 s more, closed
    dl=$(grep '^APP viz draw' <<<"$out"); DRAW='^APP viz draw live=[01] frames=[0-9]+ avg_us=[0-9]+ max_us=[0-9]+$'
    grep -qvE "$DRAW" <<<"$dl" && { EVID="$dl"; fail viz_draw_format; }
    [ "$(sed -n 1p <<<"$dl")" = "APP viz draw live=0 frames=0 avg_us=0 max_us=0" ] || { EVID="$dl"; fail viz_draw_closed; }
    [ "$(sed -n 3p <<<"$dl" | cut -d' ' -f4)" = "live=1" ] && [ "$(sed -n 3p <<<"$dl" | sed 's/.*frames=\([0-9]*\).*/\1/')" -gt 0 ] || { EVID="$dl"; fail viz_draw_live; }
    [ "$(sed -n 4p <<<"$dl" | sed 's/.*frames=\([0-9]*\).*/\1/')" -lt "$(sed -n 3p <<<"$dl" | sed 's/.*frames=\([0-9]*\).*/\1/')" ] || { EVID="$dl"; fail viz_draw_resets; }   # 1 s of frames < 2 s of frames: the counter restarts at each stat
    [ "$(sed -n 5p <<<"$dl" | cut -d' ' -f4)" = "live=0" ] || { EVID="$dl"; fail viz_draw_after_back; }; }
# scope and waterfall: each kind keeps its own variant and draws (frames > 0, live); stop leaves the page (Task 13, G13: the spectrum page and the cover page under it go)
# every "viz ..." command also prints the two stat lines: 1 viz scope, 2 viz var 2, 3 stat, 4 viz fall, 5 viz var 1, 6 stat, 7 viz bars, 8 viz scope, 9 stat after stop
nowrun viz_scope_fall "screen on; vol 0; track 3; @3; viz scope; viz var 2; @1; viz stat; viz fall; viz var 1; @1; viz stat; viz bars; viz scope; stop; @1; viz stat; uitop; back; uitop" && {
    st=$(grep '^APP viz open' <<<"$out"); dl=$(grep '^APP viz draw' <<<"$out")
    kv() { sed -n "$1p" <<<"$st" | cut -d' ' -f4-5; }
    [ "$(kv 3)" = "kind=scope var=2" ] && [ "$(kv 6)" = "kind=fall var=1" ] || { EVID="$st"; fail viz_scope_fall_kinds; }
    [ "$(kv 7)" = "kind=bars var=0" ] && [ "$(kv 8)" = "kind=scope var=2" ] || { EVID="$st"; fail viz_own_variant; }   # bars' own variant, then scope's own again
    for l in 3 6; do [ "$(sed -n ${l}p <<<"$dl" | sed 's/.*frames=\([0-9]*\).*/\1/')" -gt 0 ] || { EVID="$dl"; fail viz_scope_fall_draws_$l; }; done
    [ "$(sed -n 9p <<<"$dl" | cut -d' ' -f4)" = "live=0" ] && [ "$(grep '^APP uitop' <<<"$out" | tr '\n' '|')" = "APP uitop menu|APP uitop menu|" ] || { EVID="$(sed -n 9p <<<"$dl") $(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail viz_scope_stop_leaves; }; }
# stereo tap (phase): wanted only while the page is open on top with such a kind; "viz stat" prints stereo=<0|1> (the page's request to the audio task)
# stat lines (every "viz ..." command prints one): 1 phase, 2 stat, 3 var 1 (the tap stays on), 4 stat, 5 bars, 6 stat, 7 phase, 8 stat, 9 stat after back, 10 phase, 11 stat covered by settings,
# 12 stat after the cover is gone, 13 stat after theme next (full rebuild), 14 stat after stop (the page left: the tap is off)
nowrun viz_stereo "screen on; vol 0; track 3; @3; viz phase; @0.2; viz stat; viz var 1; @0.2; viz stat; viz bars; @0.2; viz stat; viz phase; @0.2; viz stat; back; viz stat; viz phase; uipush settings; @0.3; viz stat; back; @0.3; viz stat; theme next; @0.3; viz stat; uistack; stop; @1; viz stat; uitop" && {
    st=$(grep '^APP viz open' <<<"$out"); dl=$(grep '^APP viz draw' <<<"$out")
    sv=""; for l in 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do sv="$sv$(sed -n ${l}p <<<"$st" | sed 's/.*stereo=\([0-9]\).*/\1/')"; done
    [ "$sv" = 11110011010110 ] || { EVID="stereo per stat line: $sv (wanted 11110011010110): $(cut -d' ' -f4 <<<"$st" | tr '\n' ' ')"; fail viz_stereo_flag; }
    for l in 2 4 8; do [ "$(sed -n ${l}p <<<"$dl" | sed 's/.*frames=\([0-9]*\).*/\1/')" -gt 0 ] || { EVID="$dl"; fail viz_stereo_draws_$l; }; done
    stack_is viz_stereo_theme_stack 1 "menu now viz"
    [ "$(grep '^APP uitop' <<<"$out" | tr '\n' '|')" = "APP uitop menu|" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail viz_stereo_stop_leaves; }; }
# the round-screen bar looks (variants 4 Lens and 5 Arch): the page takes them, the picture is the 340x340 one, 6 wraps to 0; "viz var N" prints the stat lines twice (command + stat)
nowrun viz_round_bars "screen on; vol 0; track 3; @3; viz bars; viz var 4; @1; viz stat; shot $T/lens.png; viz var 5; @1; viz stat; shot $T/arch.png; viz var 6; viz stat" && {
    st=$(grep '^APP viz open' <<<"$out" | sed -E 's/.* kind=([a-z]+) var=([0-9]+) .*/\1 \2/' | tr '\n' '|')
    [ "$st" = "bars 0|bars 4|bars 4|bars 5|bars 5|bars 0|bars 0|" ] || { EVID="$st"; fail viz_round_bars_var; }
    [ -s "$T/lens.png" ] && [ -s "$T/arch.png" ] && ! cmp -s "$T/lens.png" "$T/arch.png" || fail viz_round_bars_shots
    grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out" && fail viz_round_bars_clean; }
# the waterfall size (box 256x200 | full 340x340): console, stored, applied at once; the axes line says it; the picture differs
nowrun viz_fall_size "screen on; vol 0; track 3; @3; viz size; viz fall; @1; viz stat; shot $T/fall_box.png; viz size full; @1; viz stat; shot $T/fall_full.png; viz size bogus; viz size next; viz stat; viz size next; viz size box" && {
    [ "$(grep '^APP viz size=' <<<"$out" | tr '\n' ' ')" = "APP viz size=box APP viz size=full APP viz size=full APP viz size=box APP viz size=full APP viz size=box " ] || { EVID="$(grep '^APP viz size=' <<<"$out" | tr '\n' ' ')"; fail viz_fall_size_console; }
    [ "$(grep '^APP viz axes' <<<"$out" | sed -E 's/.*size=([a-z]+).*/\1/' | tr '\n' ' ')" = "box box full box " ] || { EVID="$(grep '^APP viz axes' <<<"$out" | tr '\n' ' ')"; fail viz_fall_size_axes; }
    [ -s "$T/fall_box.png" ] && [ -s "$T/fall_full.png" ] && ! cmp -s "$T/fall_box.png" "$T/fall_full.png" || fail viz_fall_size_shots
    grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out" && fail viz_fall_size_clean; }
# the ring kind's order and gap: console (stored, printed), slot 4 of the style ring = order x gap, 9 values, index = gap_index * 3 + order, gap order 2, 0, 4; closes -> stored
nowrun viz_order_gap_console "screen on; vol 0; track 3; @3; viz order; viz order zigzag; viz order next; viz order bogus; viz gap; viz gap 4; viz gap next; viz gap 3; viz gap 0; viz gap next; viz stat; @4" && {
    [ "$(grep '^APP viz order=' <<<"$out" | tr '\n' ' ')" = "APP viz order=seq APP viz order=zigzag APP viz order=spread APP viz order=spread " ] || { EVID="$(grep '^APP viz order=' <<<"$out" | tr '\n' ' ')"; fail viz_order_console; }
    [ "$(grep '^APP viz gap=' <<<"$out" | tr '\n' ' ')" = "APP viz gap=2 APP viz gap=4 APP viz gap=2 APP viz gap=2 APP viz gap=0 APP viz gap=4 " ] || { EVID="$(grep '^APP viz gap=' <<<"$out" | tr '\n' ' ')"; fail viz_gap_console; }
    [ "$(og)" = "spread/4 " ] || { EVID="$(og)"; fail viz_order_gap_stat; }; }
out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
[ "$(og)" = "spread/4 " ] || { EVID="$(og)"; fail viz_order_gap_persist; }
fresh; printf 'lang=0\nviz_order=xyz\nviz_gap=7\n' > "$T/fix/_player/config/app.txt"
out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
[ "$(og)" = "seq/2 " ] || { EVID="$(og)"; fail viz_order_gap_garbage; }
nowrun viz_ring_axis4 "screen on; vol 0; track 3; @3; viz ring; viz menu; viz pick 4; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; viz pick -1; @0.5; viz stat; shot $T/ringax.png" && {
    # axes lines: 1 ring, 2 menu, 3 pick 4, then 8 steps (knob; stat), 12 pick -1, 13 stat after the close
    [ "$(og)" = "seq/2 seq/2 seq/2 zigzag/2 spread/2 seq/0 zigzag/0 spread/0 seq/4 zigzag/4 spread/4 spread/4 spread/4 " ] || { EVID="$(og)"; fail viz_ring_axis4_values; }
    grep -q '^APP viz open=1 kind=ring .* knob=order ' <<<"$out" || fail viz_ring_axis4_name
    grep -q '^APP viz ring hint=0 title=0 line=Alt, thin cells=' <<<"$out" || { EVID="$(grep '^APP viz ring' <<<"$out" | sed -n 4p)"; fail viz_ring_axis4_line; }; }
# the ring cell follows the kind: with the ring open on bars, a tap on the kind axis (ring), then the knob (fall, bars): cell 4 reads Order, Size, Bar width (no reopening); label read through uiboxes
nowrun viz_cell_relabel "screen on; vol 0; track 3; @3; viz bars; viz menu; @0.3; uiboxes; viz pick 0; @0.3; uiboxes; knob 2; @0.3; uiboxes; knob -3; @0.3; uiboxes" && {
    cl=$(grep -E '^APP uibox label .*\|(Bar width|Order|Size)$|^APP uiboxes end' <<<"$out" | sed -E 's/.*\|//' | tr '\n' ' ')
    [ "$cl" = "Bar width APP uiboxes end Order APP uiboxes end Size APP uiboxes end Bar width APP uiboxes end " ] || { EVID="$cl"; fail viz_cell_relabel; }; }
nowrun viz_fall_axis4 "screen on; vol 0; track 3; @3; viz fall; viz menu; viz pick 4; knob 1; viz stat; knob 1; viz stat; knob -1; viz stat; viz pick -1; @0.5; viz stat; viz scope; viz menu; viz pick 4; knob 1; viz stat" && {
    [ "$(grep '^APP viz axes' <<<"$out" | sed -E 's/.*size=([a-z]+).*/\1/' | tr '\n' ' ')" = "box box box full box full full full full full full full " ] || { EVID="$(grep '^APP viz axes' <<<"$out" | sed -E 's/.*size=([a-z]+).*/\1/' | tr '\n' ' ')"; fail viz_fall_axis4_values; }
    grep -q '^APP viz open=1 kind=fall .* knob=size ' <<<"$out" || fail viz_fall_axis4_name
    grep -q '^APP viz ring hint=0 title=0 line=Full cells=' <<<"$out" || fail viz_fall_axis4_line
    grep -q '^APP viz open=1 kind=scope .* knob=barw ' <<<"$out" || fail viz_scope_axis4_inert; }
# R55: the open ring's top line is the value alone, and every value of every axis fits its 160 px (the read-back text is the text shown, dots included): every kind x every axis x every value, English (no CJK font in the fixture root) and Chinese (the fonts folder linked in)
FITCMD="screen on; vol 0; track 3; @3; simvoices nes"
for kd in bars ring scope fall phase voices; do
    FITCMD="$FITCMD; viz $kd; viz menu"
    for ax in 0 1 2 3 4 5; do FITCMD="$FITCMD; viz pick $ax"; for i in 1 2 3 4 5 6 7 8 9; do FITCMD="$FITCMD; knob 1; viz stat"; done; done
    FITCMD="$FITCMD; viz pick -1; @0.5"
done
fresh; fits viz_line_fits_en "$T/fix"
fresh; mkdir -p "$T/fix/_player"; ln -s "$PWD/fonts" "$T/fix/_player/fonts"; fits viz_line_fits_zh "$T/fix"
# the same for the clock face's ring (one axis: the faces), read back through uiboxes: the label that sits on the first line (x 100..259, y 104..132)
fresh; mkdir -p "$T/fix/_player"; ln -s "$PWD/fonts" "$T/fix/_player/fonts"
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; clock; clockface classic; clockface ring; @0.3; knob 1; uiboxes; knob 1; uiboxes; knob 1; uiboxes; knob 1; uiboxes; knob 1; uiboxes; knob 1; uiboxes; knob 1; uiboxes; knob 1; uiboxes" 2>&1)
fl=$(grep '^APP uibox label 100 104 259 132 |' <<<"$out")
[ "$(wc -l <<<"$fl")" = 8 ] && ! grep -qE '(\.\.\.|…)$' <<<"$fl" || { EVID="$(tr '\n' ' ' <<<"$fl")"; fail viz_line_fits_clockface; }
# the peak marks ride the colour-map axis: eight values (the four maps with peaks, then without), the console has its own switch, "viz map next" keeps stepping the four maps only
# the size axis has two values: an even number of detents at once is no change, an odd one toggles
nowrun viz_fall_axis4_detents "screen on; vol 0; track 3; @3; viz fall; viz menu; viz pick 4; knob 2; viz stat; knob 3; viz stat; knob -4; viz stat; knob -1; viz stat" && {
    [ "$(grep '^APP viz axes' <<<"$out" | sed -E 's/.*size=([a-z]+).*/\1/' | tr '\n' ' ')" = "box box box box full full box " ] || { EVID="$(grep '^APP viz axes' <<<"$out" | sed -E 's/.*size=([a-z]+).*/\1/' | tr '\n' ' ')"; fail viz_fall_axis4_detents; }; }
# garbage in the new settings reads as the defaults: peaks on, waterfall box
fresh; printf 'lang=0\nviz_peaks=xyz\nviz_fall=7\n' > "$T/fix/_player/config/app.txt"
out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
grep -q '^APP viz axes .* size=box peaks=1$' <<<"$out" || { EVID="$(grep '^APP viz axes' <<<"$out")"; fail viz_peaks_fall_garbage; }
nowrun viz_peaks_axis "screen on; vol 0; track 3; @3; viz bars; viz menu; viz pick 3; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob -1; viz stat; knob -1; viz stat; viz pick -1; @0.5; viz stat; shot $T/nopeaks.png; viz scope; viz menu; viz pick 3; viz pick 3; viz stat; @6" && {
    # stat lines: 1 bars, 2 menu, 3 pick 3, 4..11 eight steps (a full turn), 12..13 two steps back, 14 pick -1, 15 stat, 16 scope, 17 menu, 18 pick 3, 19 pick 3 (inert), 20 stat
    [ "$(pm)" = "height/1 height/1 height/1 tip/1 band/1 level/1 height/0 tip/0 band/0 level/0 height/1 level/0 band/0 band/0 band/0 band/0 band/0 band/0 band/0 band/0 " ] || { EVID="$(pm)"; fail viz_peaks_axis_values; }
    grep -q '^APP viz open=1 kind=bars .* knob=map ' <<<"$out" || fail viz_peaks_axis_name; }
out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
[ "$(pm)" = "band/0 " ] || { EVID="$(pm)"; fail viz_peaks_axis_persist; }
nowrun viz_peaks_console "screen on; vol 0; track 3; @3; viz peaks; viz peaks off; viz peaks next; viz peaks bogus; viz peaks next; viz map tip; viz map next; viz map next; viz peaks off; viz map next; viz stat; @4" && {
    [ "$(grep '^APP viz peaks=' <<<"$out" | tr '\n' ' ')" = "APP viz peaks=1 APP viz peaks=0 APP viz peaks=1 APP viz peaks=1 APP viz peaks=0 APP viz peaks=0 " ] || { EVID="$(grep '^APP viz peaks=' <<<"$out" | tr '\n' ' ')"; fail viz_peaks_console; }
    [ "$(grep '^APP viz map=' <<<"$out" | tr '\n' ' ')" = "APP viz map=tip APP viz map=band APP viz map=level APP viz map=height " ] || { EVID="$(grep '^APP viz map=' <<<"$out" | tr '\n' ' ')"; fail viz_peaks_map_next; }; }
out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
grep -q '^APP viz axes .* peaks=0$' <<<"$out" || { EVID="$(grep '^APP viz axes' <<<"$out")"; fail viz_peaks_console_persist; }
nowrun viz_map_console "screen on; vol 0; viz map; viz map level; viz map next; viz map bogus; viz map tip; viz stat" && {
    [ "$(grep '^APP viz map=' <<<"$out" | tr '\n' ' ')" = "APP viz map=height APP viz map=level APP viz map=height APP viz map=height APP viz map=tip " ] || { EVID="$(grep '^APP viz map=' <<<"$out" | tr '\n' ' ')"; fail viz_map_console; }
    grep -q '^APP viz open=0 .* map=tip$' <<<"$out" || fail viz_map_console_stat; }
fresh; $SIM --headless --root "$T/fix" --cmd "screen on; vol 0; track 3; @3; viz map level; @4" >/dev/null 2>&1
out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
grep -q '^APP viz open=0 .* map=level$' <<<"$out" || { EVID="$(grep '^APP viz open' <<<"$out")"; fail viz_map_persists; }
fresh; printf 'lang=0\nviz_map=xyz\n' > "$T/fix/_player/config/app.txt"
out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
grep -q '^APP viz open=0 .* map=height$' <<<"$out" || { EVID="$(grep '^APP viz open' <<<"$out")"; fail viz_map_garbage; }
# the tap is off while the picture is not seen: screen off, low memory (Review Focus 2); stat lines: 1 viz phase, 2 on, 3 screen off, 4 screen on, 5 lowmem 2, 6 lowmem 0
nowrun viz_stereo_seen "screen on; vol 0; track 3; @3; viz phase; @0.2; viz stat; screen off; @0.2; viz stat; screen on; @0.2; viz stat; lowmem 2; @0.2; viz stat; lowmem 0; @0.2; viz stat; uistack" && {
    st=$(grep '^APP viz open' <<<"$out"); sv=""; for l in 2 3 4 5 6; do sv="$sv$(sed -n ${l}p <<<"$st" | sed 's/.*stereo=\([0-9]\).*/\1/')"; done
    [ "$sv" = 10101 ] || { EVID="stereo: $sv (wanted 10101: on, screen off, screen on, lowmem 2, lowmem 0)"; fail viz_stereo_seen; }
    stack_is viz_stereo_seen_stack 1 "menu now viz"; }
# the tap follows the kind, and each kind keeps its own variant: stat lines 1 phase, 2 var 1, 3 stat, 4 scope, 5 stat, 6 phase, 7 stat, 8 var 0, 9 stat, 10 scope, 11 stat, 12 phase, 13 stat
nowrun viz_stereo_kinds "screen on; vol 0; track 3; @3; viz phase; viz var 1; @1; viz stat; viz scope; @0.2; viz stat; viz phase; @1; viz stat; viz var 0; @1; viz stat; viz scope; viz stat; viz phase; viz stat" && {
    st=$(grep '^APP viz open' <<<"$out"); dl=$(grep '^APP viz draw' <<<"$out")
    sv=""; for l in 3 5 7 9 11 13; do sv="$sv$(sed -n ${l}p <<<"$st" | sed 's/.*stereo=\([0-9]\).*/\1/')"; done
    [ "$sv" = 101101 ] || { EVID="stereo: $sv"; fail viz_stereo_switch_kinds; }
    kv() { sed -n "$1p" <<<"$st" | cut -d' ' -f4-5; }
    [ "$(kv 3)" = "kind=phase var=1" ] && [ "$(kv 5)" = "kind=scope var=0" ] && [ "$(kv 7)" = "kind=phase var=1" ] && [ "$(kv 9)" = "kind=phase var=0" ] && [ "$(kv 11)" = "kind=scope var=0" ] && [ "$(kv 13)" = "kind=phase var=0" ] || { EVID="$st"; fail viz_stereo_own_variants; }; }
cyc=""; for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do cyc="$cyc; viz bars; @0.2; back"; done
nowrun viz_cycles "screen on; vol 0; track 3; @3$cyc; uistack" && stack_is viz_cycles_stack 1 "menu now"

# ---- now_open(): the one way to the playing page (cfg now_view=cover|album|viz), the spectrum item of its menu, the menu's two pages.
# Every case counts the "APP screen push=" lines: a page pushed twice and popped by the next call would leave the same stack behind.
NOWCFG="now_view=cover"
nowrun nowopen_cover "screen on; vol 0; track 3; @3; uitop; nowmenu -2; nowmenu 3; uitop; uistack; back; uistack; track 5; uistack; track 6; uistack" && {
    [ "$(grep '^APP uitop' <<<"$out" | tr '\n' '|')" = "APP uitop now|APP uitop viz|" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail nowopen_cover_top; }
    stack_is nowopen_cover_menu_viz 1 "menu now viz"; stack_is nowopen_cover_back 2 "menu now"
    stack_is nowopen_cover_track5 3 "menu now"; stack_is nowopen_cover_track6 4 "menu now"     # the menu item did not change the setting: a track still opens the cover page
    [ "$(pushes now)" = 1 ] && [ "$(pushes viz)" = 1 ] || { EVID="pushes now=$(pushes now) viz=$(pushes viz)"; fail nowopen_cover_pushes; }
    grep -qx 'now_view=cover' "$T/fix/_player/config/app.txt" || { EVID="$(tail -2 "$T/fix/_player/config/app.txt")"; fail nowopen_cover_setting; }; }
NOWCFG="now_view=viz"
nowrun nowopen_viz "screen on; vol 0; track 3; @3; uitop; uistack; track 4; mp3 all; mp3 1; uistack; theme next; uistack; back; uistack; track 5; uistack; back; uistack" && {
    [ "$(grep -m1 '^APP uitop' <<<"$out")" = "APP uitop viz" ] || fail nowopen_viz_top
    stack_is nowopen_viz_open 1 "menu now viz"; stack_is nowopen_viz_track_mp3 2 "menu now viz"; stack_is nowopen_viz_theme 3 "menu now viz"
    stack_is nowopen_viz_back 4 "menu now"; stack_is nowopen_viz_cover_stays 5 "menu now"; stack_is nowopen_viz_home 6 "menu"
    # track 4, mp3 all, mp3 1 on the spectrum page, track 5 on the cover page: no page is pushed again (this is what fails with a double push)
    [ "$(pushes now)" = 1 ] && [ "$(pushes viz)" = 1 ] || { EVID="pushes now=$(pushes now) viz=$(pushes viz)"; fail nowopen_viz_pushes; }; }
nowrun nowopen_viz_noise "screen on; vol 0; color 0; @3; uistack; back; uistack" && {
    stack_is nowopen_viz_noise 1 "menu now viz"; stack_is nowopen_viz_noise_back 2 "menu now"; }   # a source with no cover art: the spectrum page opens all the same
NOWCFG="now_view=xyz"                 # garbage or legacy value: the cover page
nowrun nowopen_garbage "screen on; vol 0; track 3; @3; uistack" && stack_is nowopen_garbage 1 "menu now"
NOWCFG="now_view=viz"
# a full stack: depth 8 = no room for any page; depth 7 = room for the cover page only (no spectrum page after it, no second message)
FULL=""; for i in 1 2 3 4 5 6 7 8 9 10 11 12; do FULL="$FULL; uipush settings"; done
nowrun nowopen_full "screen on; vol 0$FULL; uistack; track 3; uistack; mp3 all; uistack; viz bars; uistack" && {
    # 12 uipush on a stack of 8 = 5 messages (the root is layer 1), then track 3, mp3 all, viz bars: one message each
    [ "$(grep -c '^APP ui stack full' <<<"$out")" = 8 ] || { EVID="$(grep -c '^APP ui stack full' <<<"$out") messages"; fail nowopen_full_messages; }
    stack_is nowopen_full_before 1 "menu settings settings settings settings settings settings settings"
    stack_is nowopen_full_track 2 "menu settings settings settings settings settings settings settings"
    stack_is nowopen_full_mp3 3 "menu settings settings settings settings settings settings settings"; }
FULL7=""; for i in 1 2 3 4 5 6; do FULL7="$FULL7; uipush settings"; done
nowrun nowopen_full7 "screen on; vol 0$FULL7; uistack; track 3; uistack; uitop; back; back; back; back; back; back; back; uistack" && {
    stack_is nowopen_full7_now 2 "menu settings settings settings settings settings settings now"
    [ "$(grep -c '^APP ui stack full' <<<"$out")" = 1 ] || { EVID="$(grep -c '^APP ui stack full' <<<"$out") messages"; fail nowopen_full7_one_message; }
    [ "$(pushes viz)" = 0 ] || fail nowopen_full7_no_viz
    stack_is nowopen_full7_home 3 "menu"; }
NOWCFG=""
# the menu: 8 items = two pages (6 + "next page"), an item that rebuilds it (the centre = pause, the play order) keeps the page, a page turn restarts the 4 s timeout
nowrun nowmenu_pages "screen on; vol 0; track 3; @3; nowmenu -2; nowmenu -5; nowmenu page; nowmenu -5; nowmenu -1; nowmenu -5; nowmenu page; nowmenu -5; nowmenu 4; nowmenu -5; nowmenu page; nowmenu -1; nowmenu -5" && {
    mp=$(grep '^APP now menu' <<<"$out" | sed 's/^APP now //' | tr '\n' '|')
    [ "$mp" = "menu=1 page=0|menu=1 page=1|menu=1 page=1|menu=1 page=0|menu=1 page=0|menu=1 page=1|" ] || { EVID="$mp"; fail nowmenu_pages; }; }
nowrun nowmenu_timeout "screen on; vol 0; track 3; @3; nowmenu -2; @3; nowmenu page; @3; nowmenu -5; @2; nowmenu -5; nowmenu -2; @5; nowmenu -5" && {
    mp=$(grep '^APP now menu' <<<"$out" | sed 's/^APP now //' | tr '\n' '|')
    # opened at 3, turned at 6: still open at 9 (3 s after the turn), closed at 11; without a restart it would close at 7
    [ "$mp" = "menu=1 page=1|menu=0 page=0|menu=0 page=0|" ] || { EVID="$mp"; fail nowmenu_timeout_restart; }; }
nowrun nowmenu_one_page "screen on; vol 0; color 2; @3; nowmenu -2; nowmenu page; nowmenu -5" &&
    { grep -qx 'APP now menu=1 page=0' <<<"$out" || { EVID="$(grep '^APP now menu' <<<"$out")"; fail nowmenu_one_page; }; }

