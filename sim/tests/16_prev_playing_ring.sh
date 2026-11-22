# SPDX-License-Identifier: LGPL-2.1-or-later
# --- Task 1: previous track ---
# track indices of the "APP track" lines of a run, in order, one line: "0 1 2 1"
fresh
# sequential: next twice, prev -> the second song, prev -> the first, next -> the second again (from the history, not a new pick)
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 1; vol 0; mp3 0; next; next; prev; prev; next" 2>&1)
[ "$(tr_ids "$out")" = "0 1 2 1 0 1 " ] || { EVID="got $(tr_ids "$out")"; fail prev_sequential_walk; }
# shuffle with a one-entry history: prev starts the current song again (the same index twice); the fake stats count no play
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 0; vol 0; mp3 0; prev" 2>&1)
set -- $(tr_ids "$out"); { [ $# = 2 ] && [ "$1" = "$2" ]; } || { EVID="got $(tr_ids "$out")"; fail prev_shuffle_restart; }
# sequential, one entry: prev goes to the folder's previous song (wraps to the last, folder 0 has 4) and that becomes the OLDEST entry; prev again goes one further back
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 1; vol 0; mp3 0; prev; prev" 2>&1)
[ "$(tr_ids "$out")" = "0 3 2 " ] || { EVID="got $(tr_ids "$out")"; fail prev_sequential_wrap; }
# a pick made while behind the newest song drops the newer entries: 0 1 2, prev (1), track 3 -> history 0 1 3; prev -> 1
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 1; vol 0; mp3 0; next; next; prev; track 3; prev" 2>&1)
[ "$(tr_ids "$out")" = "0 1 2 1 3 1 " ] || { EVID="got $(tr_ids "$out")"; fail prev_pick_drops_newer; }
# hires T8e: a rescan keeps the history (its entries follow their songs into the new library): 0 1, rescan, the folder again (2), prev walks back to 1
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 1; vol 0; mp3 0; next; scan; @0.2; mp3 0; prev" 2>&1)
[ "$(tr_ids "$out")" = "0 1 2 1 " ] || { EVID="got $(tr_ids "$out")"; fail prev_kept_by_scan; }
# sequential, started on the 3rd song: prev walks 2nd, 1st, then wraps to the folder's last (4th); next walks the three old entries again, then picks anew
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 1; vol 0; track 2; prev; prev; prev; next; next; next; next" 2>&1)
[ "$(tr_ids "$out")" = "2 1 0 3 0 1 2 3 " ] || { EVID="got $(tr_ids "$out")"; fail prev_oldest_end_inserts_oldest; }
# a source without prev: the console says so
out=$($SIM --headless --root "$T/fix" --cmd "prev" 2>&1)
grep -qx 'APP prev n/a' <<<"$out" || fail prev_without_source
# gapless: the history decides what follows. Visit 0, 3, 1, go back to 3 (prev): when 3 ends, 1 (the history) follows, not 0 (the folder order)
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 1; vol 0; mp3 0; track 3; track 1; prev; @3; @300" 2>&1)
{ grep -q '^APP gapless queued idx=1 ' <<<"$out" && grep -q '^APP track idx=1 .* gapless$' <<<"$out"; } || { EVID="got $(tr_ids "$out")"; fail prev_gapless_follows_history; }
# the same without gapless: the end event takes the history's next
out=$($SIM --headless --root "$T/fix" --cmd "seti gapless 0; seti mp3_order 1; vol 0; mp3 0; track 3; track 1; prev; @3; @300" 2>&1)
[ "$(tr_ids "$out" | cut -c1-12)" = "0 3 1 3 1 " ] || { EVID="got $(tr_ids "$out")"; fail prev_ended_follows_history; }
# a pre-queued pick is dead after a prev: the queue is made (idx=1 after 0), prev, and the old queued song never starts "gapless"; the new queue follows the new current song
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 1; vol 0; trackat 0 985; @1; prev; @1; @30" 2>&1)
{ grep -q '^APP gapless queued idx=1 ' <<<"$out" && [ "$(tr_ids "$out")" = "0 3 " ] && ! grep -q '^APP track idx=1 ' <<<"$out"; } || { EVID="got $(tr_ids "$out")"; fail prev_stale_queue; }
# repeat-one: a track that ends by itself repeats the same song; the repeat is not a history entry, so the entries newer than the cursor stay
# (0 3 1, prev -> 3, repeat-one, 3 ends and repeats, user next -> 1 from the history; folder order would give 0)
rep1="seti mp3_order 1; vol 0; track 0; track 3; track 1; prev; seti mp3_order 2; @3; @330; next"
out=$($SIM --headless --root "$T/fix" --cmd "$rep1" 2>&1)
[ "$(tr_ids "$out")" = "0 3 1 3 3 1 " ] || { EVID="got $(tr_ids "$out")"; fail prev_repeat_one_gapless_history_kept; }
grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out" && fail prev_sanitizer_repeat_gapless
out=$($SIM --headless --root "$T/fix" --cmd "seti gapless 0; $rep1" 2>&1)
[ "$(tr_ids "$out")" = "0 3 1 3 3 1 " ] || { EVID="got $(tr_ids "$out")"; fail prev_repeat_one_ended_history_kept; }
grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out" && fail prev_sanitizer_repeat_ended
# repeat-one, user next while behind the newest: the history
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 1; vol 0; mp3 0; next; prev; seti mp3_order 2; next" 2>&1)
[ "$(tr_ids "$out")" = "0 1 0 1 " ] || { EVID="got $(tr_ids "$out")"; fail prev_repeat_one_user_next_history; }
grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out" && fail prev_sanitizer_repeat_next
# a folder start while behind the newest song is a fresh pick from the new folder (4 = its first), not the history's next (1); the old newer entries are gone
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 1; vol 0; mp3 0; next; prev; mp3 1; prev" 2>&1)
[ "$(tr_ids "$out")" = "0 1 0 4 0 " ] || { EVID="got $(tr_ids "$out")"; fail prev_folder_start_fresh; }
grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out" && fail prev_sanitizer_folder
# the same for the all-library shuffle: the pick is not the history's next (recent songs have weight 0), and prev goes back to the first song
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 0; vol 0; track 0; next; prev; mp3 all; prev" 2>&1)
set -- $(tr_ids "$out"); { [ $# = 5 ] && [ "$4" != "$2" ] && [ "$5" = "$1" ]; } || { EVID="got $(tr_ids "$out")"; fail prev_all_shuffle_start_fresh; }
grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out" && fail prev_sanitizer_all
# repeat-one applies to a song that ends by itself, not to a folder / all start: the started song comes from the new candidates, and prev returns to the first song
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 2; vol 0; mp3 0; mp3 1; prev" 2>&1)
set -- $(tr_ids "$out"); { [ $# = 3 ] && [ "$2" -ge 4 ] && [ "$2" -le 7 ] && [ "$3" = "$1" ]; } || { EVID="got $(tr_ids "$out")"; fail prev_repeat_one_folder_start; }
grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out" && fail prev_sanitizer_repeat_folder
out=$($SIM --headless --root "$T/fix" --cmd "seti mp3_order 2; vol 0; mp3 0; mp3 all; prev" 2>&1)
set -- $(tr_ids "$out"); { [ $# = 3 ] && [ "$2" != "$1" ] && [ "$3" = "$1" ]; } || { EVID="got $(tr_ids "$out")"; fail prev_repeat_one_all_start; }
grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out" && fail prev_sanitizer_repeat_all
# --- end Task 1 ---

# --- Task 2: one playing ring ---
# The playing ring is shared by the cover page and the spectrum page (now_menu.c). Taps: the page centre (180,180) opens it; its centre (180,180) = pause / resume;
# Back (180,318). MP3 cells in order: fav, prev, list, host (cover: spectrum, viz: style), order, next, info, stop (cover only); clockwise from the bottom left: slot 0 (81,279) ... slot 5 (right, 312,180).
# "nowmenu -5" prints "APP now menu=<0|1> page=<n>" and "APP now cells=<n> items=<names, | between the pages>"; "nowmenu -3": the pause state.
ci() { grep '^APP now cells' <<<"$out" | sed 's/^APP now cells=//; s/ items=/ /' | tr '\n' ' '; }
SEQ="seti mp3_order 1; screen on; vol 0; track 3; @3; viz bars; @1"
# open: a tap on the page, ring open, 7 MP3 cells on one page
nowrun t2_viz_open "$SEQ; simtap 180 180; @0.3; nowmenu -5; uitop" && {
    [ "$(nm)" = "menu=1 page=0|" ] || { EVID="$(nm)"; fail t2_viz_open; }
    [ "$(ci)" = "8 fav,prev,list,host,order,next,info,speed " ] || { EVID="$(ci)"; fail t2_viz_cells; }
    [ "$(grep '^APP uitop' <<<"$out")" = "APP uitop viz" ] || fail t2_viz_open_top; }
# next and prev act and keep the ring open: next (right, 312,180) then prev (left, 48,180) -> tracks 3 0 3
nowrun t2_viz_next_prev "$SEQ; simtap 180 180; @0.3; simtap 312 180; @0.3; nowmenu -5; simtap 48 180; @0.3; nowmenu -5" && {
    [ "$(tr_ids "$out")" = "3 0 3 " ] || { EVID="$(tr_ids "$out")"; fail t2_viz_next_prev_tracks; }   # folder 0 has 4 songs: next from the last wraps to the first
    [ "$(nm)" = "menu=1 page=0|menu=1 page=0|" ] || { EVID="$(nm)"; fail t2_viz_next_prev_open; }; }
# the centre pauses (the audio and the page agree), the ring stays open, the second tap resumes
nowrun t2_viz_pause "$SEQ; simtap 180 180; @0.3; simtap 180 180; @0.3; nowmenu -3; nowmenu -5; simtap 180 180; @0.3; nowmenu -3; nowmenu -5" && {
    [ "$(pz)" = "paused=1 audio=paused|paused=0 audio=playing|" ] || { EVID="$(pz)"; fail t2_viz_pause_state; }
    [ "$(nm)" = "menu=1 page=0|menu=1 page=0|" ] || { EVID="$(nm)"; fail t2_viz_pause_open; }; }
# the style cell (slot 3 = top, 180,48) closes the playing ring and opens the style ring; its Back returns to the playing ring, the page stays on top
nowrun t2_viz_style_back "$SEQ; simtap 180 180; @0.3; simtap 180 48; @0.3; viz stat; nowmenu -5; viz pick -3; @0.3; nowmenu -5; uitop; simflick up; @0.3; uitop; uistack" && {
    [ "$(grep '^APP viz open' <<<"$out" | sed -E 's/.* ring=([01]) .*/\1/' | tr '\n' ' ')" = "0 1 0 " ] || { EVID="$(grep '^APP viz open' <<<"$out" | sed -E 's/.* ring=([01]) .*/\1/' | tr '\n' ' ')"; fail t2_viz_style_ring; }
    [ "$(nm)" = "menu=0 page=0|menu=1 page=0|" ] || { EVID="$(nm)"; fail t2_viz_style_back_playing; }
    [ "$(grep '^APP uitop' <<<"$out" | tr '\n' '|')" = "APP uitop viz|APP uitop now|" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail t2_viz_style_back_top; }
    stack_is t2_viz_style_back_stack 1 "menu now"; }
# the playing ring's Back leaves the spectrum page
nowrun t2_viz_back_leaves "$SEQ; simtap 180 180; @0.3; simflick up; @0.3; uitop; nowmenu -5" && {
    [ "$(grep '^APP uitop' <<<"$out")" = "APP uitop now" ] && [ "$(nm)" = "menu=0 page=0|" ] || { EVID="$(grep '^APP uitop' <<<"$out") $(nm)"; fail t2_viz_back_leaves; }; }
# 4 s closes it, every tap restarts the 4 s (open at 0, pause at 3, still open at 6, closed at 8)
nowrun t2_viz_timeout "$SEQ; simtap 180 180; @3; simtap 180 180; @3; nowmenu -5; @2; nowmenu -5; uitop" && {
    [ "$(nm)" = "menu=1 page=0|menu=0 page=0|" ] && [ "$(grep '^APP uitop' <<<"$out")" = "APP uitop viz" ] || { EVID="$(nm)"; fail t2_viz_timeout; }; }
# a closed ring and the knob: volume
nowrun t2_viz_knob_volume "$SEQ; simtap 180 180; @0.3; knob 1; simvol; viz stat" && {
    [ "$(vols)" = "2 " ] || { EVID="volumes: $(vols)"; fail t2_viz_knob_volume; }; }
# nothing playing: a tap opens the style ring directly (no playing ring); its Back leaves the page
nowrun t2_viz_nosource "$SEQ; stop; @1; viz bars; @1; simtap 180 180; @0.3; viz stat; nowmenu -5; viz pick -3; uitop" && {
    [ "$(grep '^APP viz open' <<<"$out" | sed -E 's/.* ring=([01]) .*/\1/' | tr '\n' ' ')" = "0 0 1 0 " ] || fail t2_viz_nosource_ring   # SEQ's viz bars, the second viz bars (the page reopened after the stop), the tap, the Back cell
    [ "$(nm)" = "menu=0 page=0|" ] && [ "$(grep '^APP uitop' <<<"$out")" = "APP uitop now" ] || { EVID="$(nm) $(grep '^APP uitop' <<<"$out")"; fail t2_viz_nosource_back; }; }
# the cover page: the centre pauses, the ring stays; MP3 has 8 cells, page 1 has 6 and page 2 has details and stop
nowrun t2_cover_pause_pages "seti mp3_order 1; screen on; vol 0; track 3; @3; simtap 180 180; @0.3; simtap 180 180; @0.3; nowmenu -3; nowmenu -5; nowmenu page; nowmenu -5" && {
    [ "$(pz)" = "paused=1 audio=paused|" ] || { EVID="$(pz)"; fail t2_cover_pause; }
    [ "$(nm)" = "menu=1 page=0|menu=1 page=1|" ] || { EVID="$(nm)"; fail t2_cover_pause_open; }
    [ "$(ci)" = "9 fav,prev,list,host,order,next,info,stop|speed 9 fav,prev,list,host,order,next,info,stop|speed " ] || { EVID="$(ci)"; fail t2_cover_cells; }; }
# the cover page: next (right) and prev (left) keep the ring open; the spectrum cell (top) pushes the spectrum page; details (page 2) and stop close the ring (stop: and the page leaves, Task 13)
nowrun t2_cover_cells_act "seti mp3_order 1; screen on; vol 0; track 3; @3; simtap 180 180; @0.3; simtap 312 180; @0.3; simtap 48 180; @0.3; nowmenu -5; simtap 180 48; @0.3; uitop; back; @0.3; nowmenu -2; nowmenu page; nowmenu 7; @0.5; nowmenu -5; uitop" && {
    [ "$(tr_ids "$out")" = "3 0 3 " ] || { EVID="$(tr_ids "$out")"; fail t2_cover_next_prev; }
    [ "$(grep '^APP uitop' <<<"$out" | tr '\n' '|')" = "APP uitop viz|APP uitop menu|" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail t2_cover_spectrum_cell; }
    [ "$(nm)" = "menu=1 page=0|menu=0 page=0|" ] || { EVID="$(nm)"; fail t2_cover_stop_closes; }; }
# colour noise (list, spectrum, next, stop): no empty cell, the centre pauses and an out-of-range pick closes (white noise needs library.muk: not in the fixture)
nowrun t2_cover_noise_cells "screen on; vol 0; color 2; @3; nowmenu -2; nowmenu -5; nowmenu -1; nowmenu -5; nowmenu 99; nowmenu -5" && {
    [ "$(ci)" = "4 list,host,next,stop 4 list,host,next,stop 0 - " ] || { EVID="$(ci)"; fail t2_cover_colour_cells; }
    [ "$(nm)" = "menu=1 page=0|menu=1 page=0|menu=0 page=0|" ] || { EVID="$(nm)"; fail t2_cover_colour_open; }; }
# the spectrum page, colour noise: next, list, style (no stop, no details)
nowrun t2_viz_noise_cells "screen on; vol 0; color 2; @3; viz bars; @1; simtap 180 180; @0.3; nowmenu -5" && {
    [ "$(ci)" = "3 list,host,next " ] || { EVID="$(ci)"; fail t2_viz_colour_cells; }; }
# the two new style axes: 4 bar width (bars only) and 5 sensitivity (all kinds), written when the ring closes. aw: "barw floor" of every "APP viz axes" line
nowrun t2_axis_barw "screen on; vol 0; track 3; @3; viz bars; viz menu; viz pick 4; knob 1; viz stat; viz barw; knob 1; viz stat; knob 1; viz stat; knob -1; viz stat; viz pick -1; @0.5; viz barw" && {
    # open 7: +1 -> 9, +1 -> 5 (wraps), +1 -> 7, -1 -> 5; the stored value only changes when the ring closes
    [ "$(aw)" = "7:-48 7:-48 7:-48 9:-48 5:-48 7:-48 5:-48 5:-48 " ] || { EVID="$(aw)"; fail t2_axis_barw; }
    [ "$(vv barw)" = "7 5 " ] || { EVID="stored: $(vv barw)"; fail t2_axis_barw_stored; }
    [ "$(ax | awk '{print $3}')" = "bars:0:theme:1:barw:height" ] || { EVID="$(ax)"; fail t2_axis_barw_knob; }; }
nowrun t2_axis_barw_notbars "screen on; vol 0; track 3; @3; viz scope; viz menu; viz pick 4; knob 1; viz stat; viz pick -1; @0.5; viz barw" && {
    [ "$(aw | awk '{print $NF}')" = "7:-48" ] && [ "$(vv barw)" = "7 " ] || { EVID="$(aw) stored $(vv barw)"; fail t2_axis_barw_notbars; }
    grep '^APP viz ring' <<<"$out" | grep -q 'line=— cells=' || { EVID="$(grep '^APP viz ring' <<<"$out" | tail -2)"; fail t2_axis_barw_notbars_line; }; }
nowrun t2_axis_floor "screen on; vol 0; track 3; @3; viz scope; viz menu; viz pick 5; knob 1; viz stat; knob 1; viz stat; viz floor; viz pick -1; @0.5; viz floor" && {
    [ "$(aw)" = "7:-48 7:-48 7:-48 7:-36 7:-60 7:-60 " ] || { EVID="$(aw)"; fail t2_axis_floor; }
    [ "$(vv floor)" = "-48 -60 " ] || { EVID="stored: $(vv floor)"; fail t2_axis_floor_stored; }
    [ "$(ax | awk '{print $3}')" = "scope:0:theme:1:floor:height" ] || { EVID="$(ax)"; fail t2_axis_floor_knob; }; }
# the settings page no longer lists the four spectrum rows (colours, style, bar width, sensitivity): asked by label, not by a total
nowrun t2_settings_rows "$ALLROWS" && {
    [ "$(grep -c '^APP settings row=' <<<"$out")" -ge 5 ] || { EVID="$(grep '^APP settings' <<<"$out")"; fail t2_settings_rows_listed; }
    ! grep '^APP settings row=' <<<"$out" | grep -qi 'Spectrum\|頻譜\|Bar width\|柱寬\|Sensitiv\|靈敏\|Colour ramp\|Spectrum colours' || { EVID="$(grep '^APP settings row=' <<<"$out")"; fail t2_settings_rows; }; }
# the page prints nothing by itself when it opens (the count is a query now)
nowrun t2_settings_quiet "setui; @0.5" && { ! grep -q '^APP settings' <<<"$out" || { EVID="$(grep '^APP settings' <<<"$out")"; fail t2_settings_quiet; }; }
# the style ring opened by a direct tap on the spectrum page with nothing playing: also under low memory and with the ring kind
nowrun viz_noback_stopped_lowmem "screen on; vol 0; track 3; @3; lowmem 2; stop; @1; viz bars; @1; $NB" && noback_check stopped_lowmem
nowrun viz_noback_stopped_ringkind "screen on; vol 0; track 3; @3; stop; @1; viz ring; @1; $NB" && noback_check stopped_ringkind
# --- end Task 2 ---

# --- fpux2 (2026-11-22): the pictures that animate on their own stop while a popup ring is open over them (ui_visual_paused), and the popup's own page flick still works when frames are long ---
# the spectrum: 30 frames a second before the ring, none while it is up (two samples), drawing again after its 4 s run out
nowrun fpux2_viz_freezes "screen on; vol 0; track 3; @3; viz bars; @1; viz stat; nowmenu -2; @1; viz stat; @1; viz stat; @5; nowmenu -5; viz stat; @1; viz stat" && {
    f=$(grep '^APP viz draw' <<<"$out" | sed 's/.*frames=\([0-9]*\).*/\1/' | tr '\n' ' ')
    [ "$f" = "0 30 0 0 91 30 " ] || [ "$(cut -d' ' -f3,4 <<<"$f")" = "0 0" ] && [ "$(cut -d' ' -f6 <<<"$f")" -gt 0 ] || { EVID="frames: $f"; fail fpux2_viz_freezes; }; }
# the round cover: it turns (an "APP rot" line every 100 steps, here 16 ms steps) without the ring, not at all with it open for 3.5 s
if [ -z "$NOWFP_FP" ]; then
nowrun fpux2_cover_turns "screen on; vol 0; rot 3 16; track 1; @5" && { grep -q '^APP rot step' <<<"$out" || { EVID="no rot line"; fail fpux2_cover_turns; }; }
nowrun fpux2_cover_freezes "screen on; vol 0; rot 3 16; track 1; @0.3; nowmenu -2; @1; nowmenu -5; @2.5; nowmenu -5" && { ! grep -q '^APP rot step' <<<"$out" && [ "$(grep -c '^APP now menu=1' <<<"$out")" = 2 ] || { EVID="$(grep '^APP rot\|^APP now menu' <<<"$out" | tr '\n' '|')"; fail fpux2_cover_freezes; }; }
fi
# the popup's left / right flick on frames of 16 / 60 / 100 / 150 ms: the page turns (before the fix a 100 ms frame made every flick a "late drag")
for F in 16 60 100 150; do
nowrun fpux2_flick_frame_$F "screen on; vol 0; track 1; @3; nowmenu -2; @0.5; simframe $F; simflick left; simframe 0; @0.3; nowmenu -5" && {
    grep -q '^APP now menu=1 page=1' <<<"$out" || { EVID="$(grep '^APP flick\|^APP now menu' <<<"$out" | tr '\n' '|')"; fail fpux2_flick_frame_$F; }; }
done
# a slow drag is still no flick on long frames (the stricter window): 900 ms across the panel turns nothing
nowrun fpux2_slow_drag_frame_100 "screen on; vol 0; track 1; @3; nowmenu -2; @0.5; simframe 100; simdrag 340 180 120 180 900; simframe 0; @0.3; nowmenu -5" && {
    grep -q '^APP now menu=1 page=0' <<<"$out" && ! grep -q '^APP flick' <<<"$out" || { EVID="$(grep '^APP flick\|^APP now menu' <<<"$out" | tr '\n' '|')"; fail fpux2_slow_drag_frame_100; }; }
