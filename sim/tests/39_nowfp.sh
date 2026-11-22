# SPDX-License-Identifier: LGPL-2.1-or-later
# The square panel's static now-playing page (app/main/ui/now_fp.c): style A cover + buttons, style B full cover (the default since 2026-11-22), 選單格 = the older ring page, the setting 播放頁樣式.
# The round board has none of it: its row is absent and its page is the ring page (this piece's round part). The FakePod part runs only from sim/test_fp.sh (NOWFP_FP=1, $SIM is the square simulator there).
# Taps are on the 460 panel: style A play (384,160), previous (384,58), next (384,268), play order (338,356), more (410,356); style B play (230,246), previous (96,246), next (364,246), play order (50,420), more (410,420).

[ -z "$NOWFP_FP" ] && nowrun nowfp_round_none "screen on; vol 0; track 1; @3; nowfp; uipush menu; uipush settings; settings rows; back; back; nowmenu -2; @1; nowmenu -5" && {
    grep -qx 'APP nowfp style=none' <<<"$out" || grep -qx 'APP nowfp style=grid' <<<"$out" || { EVID="$(grep '^APP nowfp' <<<"$out")"; fail nowfp_round_state; }
    ! grep -qiE '^APP settings row=.*(now playing style|播放頁樣式)' <<<"$out" || { EVID="a round row"; fail nowfp_round_no_row; }
    grep -q '^APP now menu=1' <<<"$out" || fail nowfp_round_ring_opens; }

if [ -n "$NOWFP_FP" ]; then
nfp() {   # <check> <app.txt lines> <commands>: a fresh fixture with the repo fonts, the square simulator; sets out; a sanitizer line or an "APP ui leak" fails the check
    [ -n "$CASE" ] && ! [[ $1 =~ $CASE ]] && return 1
    rm -rf "$T/nf"; cp -r sim/fixture "$T/nf"; mkdir -p "$T/nf/_player"; [ -d fonts ] && ln -s "$PWD/fonts" "$T/nf/_player/fonts"
    [[ $2 == *now_fp=* || -n $NFPDEF ]] || printf 'now_fp=0\n' >> "$T/nf/_player/config/app.txt"   # the default is style B since 2026-11-22: the A tests ask for A
    [ -n "$2" ] && printf '%s\n' "$2" >> "$T/nf/_player/config/app.txt"
    out=$(SIM_FP=1 $SIM --headless --root "$T/nf" --cmd "screen on; vol 0; $3" 2>&1); local rc=$?
    if [ $rc != 0 ] || grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out"; then
        EVID="rc=$rc $(grep -m3 'ERROR: AddressSanitizer\|^SUMMARY\|runtime error\|^APP ui leak' <<<"$out" | tr '\n' ' ')"; fail "$1"; return 1
    fi
}
nfl() { grep "^APP nowfp" <<<"$out" | sed -n "${1}p"; }   # the n-th state line
# 2026-11-22: no button on the screen: style A (封面＋資訊) has passive status icons, style B (全封面) is the bare cover; the taps are the nine-grid's (sim/tests/44_ninegrid.sh)
nowl() { grep '^APP nowovl' <<<"$out" | sed -n "${1}p"; }
nfp nowfp_a_state "" "track 1; @3; nowfp; shot $T/nfa.png" && {
    l=$(nfl 1)
    grep -qE '^APP nowfp style=info paused=0 mode=0 title=.+ artist=.+ fmt=MP3 · 44\.1 kHz el=0:0[0-9] tot=[0-9]+:[0-9]+ bar=[0-9]+$' <<<"$l" || { EVID="$l"; fail nowfp_a_state_line; }
    grep -qx "SIM shot $T/nfa.png 460x460" <<<"$out" || fail nowfp_a_shot; }
nfp nowfp_b_state "now_fp=1" "track 1; @3; nowfp; shot $T/nfb.png" && {
    grep -qE '^APP nowfp style=full paused=0 mode=0 title=.+ artist=.+ fmt=MP3 · 44\.1 kHz ' <<<"$(nfl 1)" || { EVID="$(nfl 1)"; fail nowfp_b_state_line; }; }
# a tap on the page runs nothing (it only shows the grid): the status icons are not buttons
nfp nowfp_a_icons_passive "mp3_order=1" "track 1; @3; nowfp; simtap 345 160; @0.5; nowfp" && {
    [ "$(for i in 1 2; do nfl $i | grep -o 'paused=[01] mode=[0-9-]*'; done | sort -u | wc -l)" = 1 ] || { EVID="$(grep '^APP nowfp' <<<"$out" | tr '\n' '|')"; fail nowfp_a_icons_passive; }; }
# paused: the picture is then still
nfp nowfp_a_paused_still "" "track 1; @3; simtap 230 230; @0.3; ninegrid tap 4; ninegrid tap 7; @1; shot $T/nfp1.png; @2; shot $T/nfp2.png" && { cmp -s "$T/nfp1.png" "$T/nfp2.png" || fail nowfp_a_paused_still; }
nfp nowfp_a_long_title "" "track 3; @3; nowfp; shot $T/nfl.png" && { [ "$(nfl 1 | grep -o 'title=.* artist' | wc -c)" -gt 20 ] || fail nowfp_a_long_title_text; }
nfp nowfp_a_nocover "" "track 11; @3; nowfp; shot $T/nfn.png" && { grep -q '^APP nowfp style=info paused=0' <<<"$out" || fail nowfp_a_nocover; }
nfp nowfp_a_noise "" "color 2; @3; nowfp; shot $T/nfz.png" && { grep -qE '^APP nowfp style=info paused=0 mode=-1 ' <<<"$(nfl 1)" || { EVID="$(nfl 1)"; fail nowfp_a_noise; }; }
nfp nowfp_a_stop "" "track 1; @3; stop; @2; uitop; nowfp" && { grep -qx 'APP uitop menu' <<<"$out" && grep -qx 'APP nowfp style=none' <<<"$out" || { EVID="$(grep '^APP uitop\|^APP nowfp' <<<"$out" | tr '\n' '|')"; fail nowfp_a_stop; }; }
for tone in bright dark; do
    out=$(SIM_COVER_TONE=$tone SIM_FP=1 $SIM --headless --root "$T/nf" --cmd "screen on; vol 0; track 3; @4; simtap 230 230; @0.5; shot $T/nft_$tone.png" 2>&1)
    grep -q 'Sanitizer\|runtime error' <<<"$out" && fail "nowfp_b_$tone"
    [ -s "$T/nft_$tone.png" ] || fail "nowfp_b_${tone}_shot"
done
# the sharp cover (A decodes 296, B 460), no cover / noise draw the record at the box size, the sleep timer on style A
nfp nowfp_big_a "" "track 1; @3; nowfp" && { grep -q '^SIM thumb big req=[0-9]* size=296 px=1$' <<<"$out" && [ "$(nowl 1)" = "APP nowovl sleep=- cov=296" ] || { EVID="$(grep '^SIM thumb big' <<<"$out") | $(nowl 1)"; fail nowfp_big_a; }; }
nfp nowfp_big_b "now_fp=1" "track 1; @3; nowfp" && { grep -q '^SIM thumb big req=[0-9]* size=460 px=1$' <<<"$out" && [ "$(nowl 1)" = "APP nowovl sleep=- cov=460" ] || { EVID="$(grep '^SIM thumb big' <<<"$out") | $(nowl 1)"; fail nowfp_big_b; }; }
nfp nowfp_big_swap_on_track_change "" "track 1; @3; next; @2; nowfp" && { [ "$(grep -c '^SIM thumb big req=[0-9]* size=296 px=1$' <<<"$out")" = 2 ] && [ "$(nowl 1)" = "APP nowovl sleep=- cov=296" ] || { EVID="$(grep -c '^SIM thumb big' <<<"$out") | $(nowl 1)"; fail nowfp_big_swap; }; }
nfp nowfp_big_nocover "" "track 11; @3; nowfp; color 2; @3; nowfp" && { [ "$(nowl 1)" = "APP nowovl sleep=- cov=296" ] && [ "$(nowl 2)" = "APP nowovl sleep=- cov=296" ] && ! grep -q '^SIM thumb big' <<<"$out" || { EVID="$(nowl 1) | $(nowl 2)"; fail nowfp_big_nocover; }; }
nfp nowfp_sleep_a "" "track 1; @3; nowfp; sleep 5; @2; nowfp; sleep 0; @2; nowfp" && { [ "$(nowl 1)" = "APP nowovl sleep=- cov=296" ] && [[ "$(nowl 2)" =~ sleep=4:5[0-9]\  ]] && [ "$(nowl 3)" = "APP nowovl sleep=- cov=296" ] || { EVID="$(nowl 1) | $(nowl 2) | $(nowl 3)"; fail nowfp_sleep_a; }; }
nfp nowfp_lab_square_default "" "lab flick_edge" && { grep -qx 'APP lab flick_edge=56 default=56 range=20..120 when=live' <<<"$out" || { EVID="$(grep '^APP lab' <<<"$out")"; fail nowfp_lab_square_default; }; }   # the square board's edge default is 56 (the round one's 50)

# the grid style is the older page: no static layout, a tap on the picture opens the ring
nfp nowfp_grid "now_fp=2" "track 1; @3; nowfp; simtap 230 230; @0.5; nowmenu -5" && {
    grep -qx 'APP nowfp style=none' <<<"$out" && grep -q '^APP now menu=1' <<<"$out" || { EVID="$(grep '^APP nowfp\|^APP now menu' <<<"$out" | tr '\n' '|')"; fail nowfp_grid; }; }
# the setting (NFPDEF=1: no now_fp line, the real default = B): default value, the steps (the key stands in for the knob), a garbage value reads as the default, the next page follows it
NFPDEF=1 nfp nowfp_setting "" "uipush menu; uipush settings; settings key nowfp; settings key nowfp; @0.1; cfg now_fp; axishalf -1; settings key nowfp; @0.1; cfg now_fp; settings key nowfp; @0.1; cfg now_fp; axishalf 1; settings key nowfp; @0.1; cfg now_fp; settings key nowfp; @0.1; cfg now_fp; settings rows" && {
    [ "$(grep '^APP cfg now_fp' <<<"$out" | tr '\n' ' ')" = "APP cfg now_fp=2 APP cfg now_fp=1 APP cfg now_fp=0 APP cfg now_fp=1 APP cfg now_fp=2 " ] || { EVID="$(grep '^APP cfg' <<<"$out" | tr '\n' '|')"; fail nowfp_setting_steps; }
    grep -q '^APP settings row=.*播放頁樣式: 選單格$' <<<"$out" || { EVID="$(grep '^APP settings row' <<<"$out" | head -6 | tr '\n' '|')"; fail nowfp_setting_row; }; }
NFPDEF=1 nfp nowfp_setting_default_row "" "uipush menu; uipush settings; settings group display; settings rows" && { grep -q '^APP settings row=.*播放頁樣式: 滿版封面$' <<<"$out" || { EVID="$(grep '^APP settings row' <<<"$out" | head -6 | tr '\n' '|')"; fail nowfp_setting_default_row; }; }
nfp nowfp_setting_garbage "now_fp=9" "track 1; @3; nowfp" && { grep -q '^APP nowfp style=full ' <<<"$out" || fail nowfp_setting_garbage; }
nfp nowfp_setting_follows "" "uipush menu; uipush settings; settings key nowfp; settings key nowfp; @0.1; back; back; track 1; @3; nowfp" && { grep -q '^APP nowfp style=full ' <<<"$out" || { EVID="$(grep '^APP nowfp' <<<"$out")"; fail nowfp_setting_follows; }; }
# English: the row and its values
nfp nowfp_english "" "lang en; uipush menu; uipush settings; settings group display; settings rows; settings key nowfp; settings key nowfp; @0.1; settings key nowfp; @0.1; settings rows" && {
    grep -q '^APP settings row=.*Now playing: Cover + info$' <<<"$out" && grep -q '^APP settings row=.*Now playing: Menu grid$' <<<"$out" || { EVID="$(grep '^APP settings row.*Now playing' <<<"$out" | tr '\n' '|')"; fail nowfp_english; }; }
# fpux2 (2026-11-22): the popup over the now page pages with the left flick on long frames too (a 100 ms frame made it a "late drag"), the keys stay volume (the ring's rule), the cover does not redraw under it
for F in 16 100; do
nfp nowfp_popup_flick_frame_$F "" "track 1; @3; simtap 230 230; @0.4; simtap 230 76; @0.5; nowmenu -5; simframe $F; simflick left; simframe 0; @0.3; nowmenu -5" && {
    [ "$(grep '^APP now menu' <<<"$out" | tr '\n' ' ')" = "APP now menu=1 page=0 APP now menu=1 page=1 " ] || { EVID="$(grep '^APP now menu\|^APP flick' <<<"$out" | tr '\n' '|')"; fail nowfp_popup_flick_frame_$F; }; }
done
nfp nowfp_popup_keys_volume "" "track 1; @3; simtap 230 230; @0.4; simtap 230 76; @0.5; vol 40; simkey top; @0.3; nowmenu -5; vol" && {
    grep -q '^APP now menu=1 page=0' <<<"$out" || { EVID="$(grep '^APP now menu\|^APP vol' <<<"$out" | tr '\n' '|')"; fail nowfp_popup_keys_volume; }; }
# the two keys keep their function: a click of the top key on the now page is the knob's detent = volume (no change of the layout)
nfp nowfp_keys "" "track 1; @3; simkey top; @0.3; nowfp; simkey bottom; @0.3; nowfp" && { [ "$(grep -c '^APP nowfp style=info' <<<"$out")" = 2 ] || fail nowfp_keys; }
fi
