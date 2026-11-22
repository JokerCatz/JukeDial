# SPDX-License-Identifier: LGPL-2.1-or-later
# The FakePod's nine-grid of invisible tap zones (app/main/ui/ui_ninegrid.c). FakePod only: runs from sim/test_fp.sh after 39_nowfp.sh (NOWFP_FP=1; nfp / nfl come from there).
# Zones on the 460 panel (thirds): 0 (76,76) 1 (230,76) 2 (383,76) / 3 (76,230) 4 (230,230) 5 (383,230) / 6 (76,383) 7 (230,383) 8 (383,383).
if [ -n "$NOWFP_FP" ]; then
ngl() { grep '^APP ninegrid' <<<"$out" | sed -n "${1}p"; }
ngs() { ngl "$1" | grep -o 'shown=[01]'; }
# hidden: a tap on the centre zone only shows the labels, it does not pause; shown: the same tap pauses; the labels stay (the timer restarts)
nfp ng_hidden_runs_nothing "now_fp=1" "track 1; @3; simtap 230 230; @0.4; ninegrid; nowfp; simtap 230 230; @0.4; ninegrid; nowfp" && {
    [ "$(ngs 1)" = shown=1 ] && [ "$(nfl 1 | grep -o 'paused=[01]')" = paused=0 ] || { EVID="$(ngl 1) $(nfl 1)"; fail ng_hidden_tap_only_shows; }
    [ "$(ngs 2)" = shown=1 ] && [ "$(nfl 2 | grep -o 'paused=[01]')" = paused=1 ] || { EVID="$(ngl 2) $(nfl 2)"; fail ng_shown_tap_runs; }; }
# a hidden tap on a corner zone (next track) changes nothing; shown, it runs
nfp ng_hidden_corner "now_fp=1
mp3_order=1" "track 1; @3; nowfp; simtap 383 383; @1; nowfp; ninegrid; simtap 383 383; @1; nowfp" && {
    t0=$(nfl 1 | grep -o 'title=.* artist'); t1=$(nfl 2 | grep -o 'title=.* artist'); t2=$(nfl 3 | grep -o 'title=.* artist')
    [ "$t0" = "$t1" ] && [ "$t1" != "$t2" ] || { EVID="$t0 | $t1 | $t2"; fail ng_hidden_corner_runs_nothing; }; }
# close hides at once; the console's tap obeys the same rule (hidden: the first tap only shows, the second on 關閉 hides, a real tap shows again)
nfp ng_close "now_fp=1" "track 1; @3; ninegrid tap 7; ninegrid; ninegrid tap 7; ninegrid; simtap 230 383; @0.3; ninegrid" && {
    [ "$(ngs 1)" = shown=1 ] && [ "$(ngs 2)" = shown=0 ] && [ "$(ngs 3)" = shown=1 ] || { EVID="$(grep '^APP ninegrid' <<<"$out" | grep -o 'shown=[01]' | tr '\n' ' ')"; fail ng_close; }; }
# the labels: nine of them, the toggle states in them, the set cycle (cover page: one set 播放模式 1/1)
nfp ng_labels_state "now_fp=1
mp3_order=1" "track 1; @3; simtap 230 230; @0.3; ninegrid; ninegrid tap 3; ninegrid; ninegrid tap 5; ninegrid; ninegrid tap 0; ninegrid" && {
    grep -q '^APP ninegrid shown=1 set=1/1 name=播放模式 cells: 0=播放模式 1/1 | 1=選單 | 2=下一頁 | 3=隨機：關 | 4=暫停 | 5=循環：全部 | 6=上一首 | 7=關閉 | 8=下一首$' <<<"$(ngl 1)" || { EVID="$(ngl 1)"; fail ng_labels_set1; }
    grep -q '3=隨機：開 ' <<<"$(ngl 2)" || { EVID="$(ngl 2)"; fail ng_shuffle_on; }
    grep -q '5=循環：單曲 ' <<<"$(ngl 3)" || { EVID="$(ngl 3)"; fail ng_repeat_one; }
    grep -q 'set=1/1 ' <<<"$(ngl 4)" || fail ng_cover_one_set; }
# the English labels
nfp ng_labels_en "now_fp=1" "lang en; track 1; @3; simtap 230 230; @0.3; ninegrid" && {
    grep -q 'cells: 0=Play mode 1/1 | 1=Menu | 2=Next page | 3=Shuffle: .. | 4=Pause | 5=Repeat: all | 6=Previous | 7=Close | 8=Next' <<<"$(ngl 1)" || grep -q 'Next page' <<<"$(ngl 1)" || { EVID="$(ngl 1)"; fail ng_labels_en; }; }
# the centre follows the state; radio / noise have no previous / next (those zones are off) and no play order
nfp ng_noise "now_fp=1" "color 2; @3; simtap 230 230; @0.3; ninegrid; ninegrid tap 4; ninegrid" && {
    grep -q '3=-- (off)' <<<"$(ngl 1)" && grep -q '6=.*上一首 (off)' <<<"$(ngl 1)" || { EVID="$(ngl 1)"; fail ng_noise_off; }
    grep -q '4=.*播放' <<<"$(ngl 2)" || { EVID="$(ngl 2)"; fail ng_noise_centre; }; }
# the menu zone opens the page's own ring and hides the labels
nfp ng_menu "" "track 1; @3; simtap 230 230; @0.3; simtap 230 76; @0.5; ninegrid; nowmenu -5" && {
    [ "$(ngs 1)" = shown=0 ] && grep -q '^APP now menu=1' <<<"$out" || { EVID="$(ngl 1) $(grep '^APP now menu' <<<"$out")"; fail ng_menu_opens_ring; }; }
# next page: cover -> spectrum (a pushed page) -> (lyrics when the track has them) -> cover; no deeper stack than before
nfp ng_next_page "now_fp=1" "track 1; @3; simtap 230 230; @0.3; simtap 383 76; @1; uitop; uistack; simtap 230 230; @0.3; ninegrid; simtap 383 76; @1; uitop; uistack; ninegrid" && {
    [ "$(grep '^APP uitop' <<<"$out" | tr '\n' ' ')" = "APP uitop viz APP uitop now " ] || { EVID="$(grep '^APP uitop\|^APP uistack' <<<"$out" | tr '\n' '|')"; fail ng_next_page_cycle; }
    grep -q 'set=1/3 name=樣式' <<<"$(ngl 1)" || { EVID="$(ngl 1)"; fail ng_viz_sets; }
    [ "$(ngs 2)" = shown=0 ] || fail ng_cover_grid_hidden_again; }
# lyrics: a track with an .lrc (track 0) has the extra page: cover -> spectrum -> lyrics -> cover; the stored style is given back
nfp ng_next_page_lyrics "now_fp=1" "track 0; @3; simtap 230 230; @0.3; simtap 383 76; @1; uitop; viz stat; simtap 230 230; @0.3; simtap 383 76; @1; uitop; viz stat; cfg viz_style; simtap 230 230; @0.3; simtap 383 76; @1; uitop; cfg viz_style" && {
    [ "$(grep '^APP uitop' <<<"$out" | tr '\n' ' ')" = "APP uitop viz APP uitop viz APP uitop now " ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' ' ')"; fail ng_lyrics_cycle; }
    [ "$(grep -o '^APP viz open=1 kind=[a-z]*' <<<"$out" | tr '\n' ' ')" = "APP viz open=1 kind=bars APP viz open=1 kind=lyrics " ] || { EVID="$(grep '^APP viz open' <<<"$out" | cut -c1-40 | tr '\n' '|')"; fail ng_lyrics_kinds; }
    [ "$(grep '^APP cfg viz_style' <<<"$out" | tr '\n' ' ')" = "APP cfg viz_style=lyrics APP cfg viz_style=bars " ] || { EVID="$(grep '^APP cfg viz_style' <<<"$out" | tr '\n' ' ')"; fail ng_lyrics_restores_style; }; }
# the spectrum page's sets: 樣式 -> 配色 -> 播放模式, the style / colour setters act (the stored cfg changes)
nfp ng_viz_sets "now_fp=1" "track 1; @3; simtap 230 230; @0.3; simtap 383 76; @1; simtap 230 230; @0.3; cfg viz_style; ninegrid tap 5; cfg viz_style; ninegrid tap 3; cfg viz_style; ninegrid tap 0; ninegrid; cfg viz_ramp; ninegrid tap 5; cfg viz_ramp; ninegrid tap 0; ninegrid" && {
    c1=$(grep '^APP cfg viz_style' <<<"$out" | sed -n 1p); c2=$(grep '^APP cfg viz_style' <<<"$out" | sed -n 2p); c3=$(grep '^APP cfg viz_style' <<<"$out" | sed -n 3p)
    [ "$c2" = "APP cfg viz_style=ring" ] && [ "$c3" = "APP cfg viz_style=bars" ] || { EVID="$c1 | $c2 | $c3"; fail ng_viz_style_steps; }
    grep -q 'set=2/3 name=配色 cells: 0=配色 2/3 | 1=選單 | 2=下一頁 | 3=.*上一配色' <<<"$(ngl 1)" || { EVID="$(ngl 1)"; fail ng_viz_set2; }
    r1=$(grep '^APP cfg viz_ramp' <<<"$out" | sed -n 1p); r2=$(grep '^APP cfg viz_ramp' <<<"$out" | sed -n 2p)
    [ "$r1" != "$r2" ] || { EVID="$r1 | $r2"; fail ng_viz_ramp_steps; }
    grep -q 'set=3/3 name=播放模式' <<<"$(ngl 2)" || { EVID="$(ngl 2)"; fail ng_viz_set3; }; }
# the picture under the labels stands still (ui_visual_paused): no frame is drawn while the labels are up (viz stat frames=0), frames are drawn without them
nfp ng_visual_paused "now_fp=1" "track 1; @3; simtap 230 230; @0.3; simtap 383 76; @1.5; viz stat; @1.5; viz stat; simtap 230 230; @0.3; viz stat; @1.5; viz stat" && {
    fr() { grep '^APP viz draw' <<<"$out" | sed -n "${1}p" | grep -o 'frames=[0-9]*'; }
    [ "$(fr 2)" != frames=0 ] && [ "$(fr 4)" = frames=0 ] || { EVID="$(grep '^APP viz draw' <<<"$out" | tr '\n' '|')"; fail ng_visual_paused_still; }; }
# the timer: the labels hide after lab nowb_hide_ms (default 4 s) with no touch; a tap on a zone restarts it; 0 = never
nfp ng_timer "now_fp=1" "track 1; @2; simtap 230 230; @0.3; ninegrid; @2.5; simtap 230 230; @2.5; ninegrid; @2.5; ninegrid" && {
    [ "$(ngs 1)" = shown=1 ] && [ "$(ngs 2)" = shown=1 ] && [ "$(ngs 3)" = shown=0 ] || { EVID="$(grep '^APP ninegrid' <<<"$out" | grep -o 'shown=[01]' | tr '\n' ' ')"; fail ng_timer_hides; }; }
nfp ng_timer_lab "now_fp=1
lab_nowb_hide_ms=1500" "track 1; @2; simtap 230 230; @0.3; ninegrid; @2; ninegrid" && { [ "$(ngs 1)" = shown=1 ] && [ "$(ngs 2)" = shown=0 ] || fail ng_timer_lab_1500; }
nfp ng_timer_never "now_fp=1
lab_nowb_hide_ms=0" "track 1; @2; simtap 230 230; @15; ninegrid" && { [ "$(ngs 1)" = shown=1 ] || fail ng_timer_never; }
# 封面＋資訊: the same grid, no bands needed; the grid dims the page and the status icons are not buttons
nfp ng_info_grid "now_fp=0" "track 1; @3; simtap 230 230; @0.3; ninegrid; shot $T/ngi.png" && { [ "$(ngs 1)" = shown=1 ] || fail ng_info_grid_shows; }
# the flicks and the keys stay: a down flick from the top edge with the labels up still opens quick settings, (the keys are the page rule: volume)
nfp ng_keys_flick "now_fp=1" "track 1; @3; simtap 230 230; @0.3; simflick down; @1; uitop" && { grep -qx 'APP uitop qs' <<<"$out" || { EVID="$(grep '^APP uitop' <<<"$out")"; fail ng_flick_down_qs; }; }
fi
