#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# The square FakePod simulator's tests (make sim-test-fp builds sim/build-fp first): 460x460, no knob, the two keys (simkey top|bottom [hold ms]) as the knob's two directions.
# What is checked: the menu grid by keys only and by drag, a tap on a cell, the status strip's rules, a key-only run of 疊塔 / 四子棋 / 井字棋, the exit overlay's keys, long-press Back = home.
# Any ASan/UBSan/LSan output makes a test fail. Coordinates are the grid of ui_geom.h (UI_RGRID_*): cell (row 0, col 0) centre 120,136; 下一頁 (cell 6) 120,394; the strip over row 3 at y 394.
cd "$(dirname "$0")/.." || exit 1
SIM=sim/build-fp/sim
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
fail() { echo "FAIL $1${EVID:+ | $EVID}"; fails=$((fails + 1)); EVID=; }
mkdir -p "$T/root"; cp -r sim/fixture/. "$T/root/"; mkdir -p "$T/root/_player"; [ -f .local/faces.zip ] && ln -s "$PWD/.local/faces.zip" "$T/root/_player/faces.zip"; [ -d fonts ] && ln -s "$PWD/fonts" "$T/root/_player/fonts"   # the repo fonts: the labels are then the Chinese ones
run() { SIM_FP=1 $SIM --headless --root "$T/root" --cmd "screen on; @0.5; $1" 2>&1; }   # one run per test: a fresh settings state
san() { grep -q 'Sanitizer\|runtime error' <<<"$1" && { echo "$1" | grep -m3 'Sanitizer\|runtime error'; fail "$2 sanitizer"; }; }
ring_page() { grep -o 'APP ring page=[0-9]*' <<<"$1" | sed 's/APP ring page=//'; }

# the panel
out=$(run "uipush menu; shot $T/a.png")
grep -qx "SIM shot $T/a.png 460x460" <<<"$out" || fail fp_shot_size
san "$out" fp_shot

# the menu by the keys only: a click turns the page, the bottom key turns it back; a hold repeats (500 ms = the click and one repeat = two pages = back where it was)
out=$(run "uipush menu; ring; simkey top; ring; simkey bottom; ring; simkey top 500; ring")
p=($(ring_page "$out" | tr '\n' ' '))
[ "${p[0]}" = 0 ] && [ "${p[1]}" = 1 ] && [ "${p[2]}" = 0 ] && [ "${p[3]}" = 0 ] || { EVID="pages: ${p[*]}"; fail fp_menu_keys; }
out=$(run "uipush menu; simkey top 300; ring")
[ "$(ring_page "$out")" = 1 ] || fail fp_menu_key_hold_short       # held 300 ms: no repeat yet, one detent
san "$out" fp_menu_keys

# rotate: --rotate 1 turns the picture 180 degrees and the touch with it (a tap on the SCREEN reaches the same cell); the two keys trade places so the upper one stays "+"
for k in 0 1; do
    p=$(python3 -c 'import sys; k, n, x, y = map(int, sys.argv[1:]); print(*[(x, y), (n-1-x, n-1-y)][k])' $k 460 120 136)
    out=$(SIM_FP=1 $SIM --headless --rotate $k --root "$T/root" --cmd "screen on; @0.5; uipush menu; simtap $p; ring" 2>&1)
    grep -q 'APP ring page=0/2 sel=0 center=歌曲' <<<"$out" || { fail "fp_rotate_tap_$k"; echo "$out" | grep 'APP ring'; }
    san "$out" fp_rotate
done
out=$(SIM_FP=1 $SIM --headless --rotate 1 --root "$T/root" --cmd "screen on; @0.5; uipush menu; simkey bottom; ring" 2>&1)
[ "$(ring_page "$out")" = 1 ] || { EVID="pages: $(ring_page "$out")"; fail fp_rotate_180_keys; }
out=$(SIM_FP=1 $SIM --headless --rotate 0 --root "$T/root" --cmd "screen on; @0.5; uipush menu; simkey top; ring" 2>&1)
[ "$(ring_page "$out")" = 1 ] || fail fp_rotate_0_keys

# the menu by drag (left = next page, right = previous, no animation), a short drag stays a tap-sized move
out=$(run "uipush menu; simflick left; ring; simflick right; ring")   # GS: the page turn is the fast side flick; the next-page cell is gone
p=($(ring_page "$out" | tr '\n' ' '))
[ "${p[0]}" = 1 ] && [ "${p[1]}" = 0 ] || { EVID="pages: ${p[*]}"; fail fp_menu_drag; }
san "$out" fp_menu_drag

# a tap on a grid cell selects it (the centre header then shows its name), the next-page cell turns the page, the header confirms nothing while nothing is selected
out=$(run "uipush menu; simtap 120 136; ring; simflick left; ring")
grep -q 'APP ring page=0/2 sel=0 center=歌曲' <<<"$out" || { fail fp_menu_cell_tap; echo "$out" | grep 'APP ring'; }
grep -q 'APP ring page=1/2' <<<"$out" || fail fp_menu_next_cell
san "$out" fp_menu_tap

# the status strip: <= 2 lines over the bottom row; a message hides itself after 4 s; a new message renews it; a tap hides it and does nothing else; a warning stays
out=$(run "uipush menu; uimsg first; uistrip; @3; uimsg second; @3; uistrip; @1.5; uistrip")
mapfile -t s < <(grep -o 'APP strip shown=[01]' <<<"$out" | sed 's/.*=//')
[ "${s[0]}" = 1 ] && [ "${s[1]}" = 1 ] && [ "${s[2]}" = 0 ] || { EVID="strip: ${s[*]}"; fail fp_strip_timing; }
out=$(run "uipush menu; uimsg tap me; @0.3; simtap 230 394; uistrip; ring")
grep -q 'APP strip shown=0' <<<"$out" && grep -q 'sel=-1' <<<"$out" || fail fp_strip_tap_only_hides   # the tap on the strip did not select the cell under it
out=$(run "uipush menu; uiwarn stay; @30; uistrip; simtap 230 394; uistrip")
mapfile -t s < <(grep -o 'APP strip shown=[01]' <<<"$out" | sed 's/.*=//')
[ "${s[0]}" = 1 ] && [ "${s[1]}" = 0 ] || { EVID="strip: ${s[*]}"; fail fp_strip_warning; }
out=$(run "uipush menu; uimsg 一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十一二三四五六七八九十; shot $T/strip.png; uitop")
grep -q 'APP uitop menu' <<<"$out" || fail fp_strip_is_not_a_page    # a message does not push a page on this build
san "$out" fp_strip

# a key-only run: 疊塔's keys are blocks (the volume stays), 四子棋 and 井字棋 move a key cursor and place with the bottom key
out=$(run "screen on; vol 0; games open tower; @1; simkey top; simkey top; @0.5; simvol; arena")
grep -q '^SIM vol 0$' <<<"$out" && grep -q 'APP game tower state=1 n=2 ' <<<"$out" || { fail fp_tower_keys; echo "$out" | grep 'APP game tower\|SIM vol'; }
out=$(run "screen on; vol 0; games open ttt; @1; simkey top; simkey top; simkey bottom; @2; simvol; board")
grep -q '^SIM vol 0$' <<<"$out" && grep -q 'APP board row 0 - X -' <<<"$out" && grep -q 'APP board row 2 - O -' <<<"$out" || { fail fp_ttt_keys; echo "$out" | grep 'APP board row\|SIM vol'; }
out=$(run "screen on; vol 0; games open c4; @1; simkey top; simkey top; simkey bottom; @2; simvol; board")
grep -q '^SIM vol 0$' <<<"$out" && grep -q 'APP board look 5 n a n b' <<<"$out" || { fail fp_c4_keys; echo "$out" | grep 'look 5\|SIM vol'; }
san "$out" fp_games

# the exit overlay (CF2: no bubble; `exit pause`, or a tap in mode B): under it the keys are the volume only and the game does not move; the resume goes on
out=$(run "screen on; vol 0; games open tower; @1; simkey top; @0.5; exit pause; simkey top; simvol; arena; exit tap resume; exit")
grep -q 'APP exit attached=1 mode=C a=0 pause=1' <<<"$out" && grep -q '^SIM vol 2$' <<<"$out" && grep -q 'APP game tower state=1 n=1 ' <<<"$out" && grep -q 'pause=0' <<<"$(grep 'APP exit attached' <<<"$out" | tail -1)" || { fail fp_exit_overlay_keys; echo "$out" | grep 'APP exit attached\|SIM vol\|APP game tower'; }
san "$out" fp_exit

# a long press on Back goes home (the escape hatch, CF2: a list page keeps its Back pill, no popup); the hold is home_hold_ms (3000): 3100 ms goes
out=$(run "uipush settings; uitop; simpress 230 426 3100; @0.5; uitop")
mapfile -t s < <(grep -o 'APP uitop [a-z_]*' <<<"$out" | sed 's/APP uitop //')
[ "${s[0]}" = settings ] && [ "${s[1]}" = menu ] || { EVID="top: ${s[*]}"; fail fp_long_press_back_home; }
san "$out" fp_back

# --- CF2: the content-first frame is only for CONTENT pages (games, toys, counter ...); list / menu / settings pages keep title and 返回 on the square panel (no popup, no bubble) ---
fr_d() { grep '^APP frame armed=' <<<"$out" | sed -n "${1}p"; }
fr_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
out=$(run "uipush settings; frame; uiboxes; simflick up; @0.3; uitop")
[ "$(fr_v "$(fr_d 1)" armed)" = 0 ] && ! grep -q '^APP uibox button .*|\(返回\|Back\)$' <<<"$out" || { EVID="$(fr_d 1)"; fail fp_list_page_keeps_chrome; }   # GS: the title stays, the 返回 is gone (the up flick)
grep -q 'APP uitop menu' <<<"$out" || fail fp_list_page_back
san "$out" fp_list
# a content page that takes no taps (mode A, the exit's mode override): a tap on the content shows the popup; it hides by itself after 4 s; a tap beside the chrome hides it at once
out=$(run "tools ambient; @0.5; exit mode a; frame; simtap 230 230; @0.1; frame; @5; frame; simtap 230 230; @0.1; simtap 60 448; @0.1; frame")
[ "$(fr_v "$(fr_d 1)" shown)" = 0 ] && [ "$(fr_v "$(fr_d 2)" shown)" = 1 ] && [ "$(fr_v "$(fr_d 3)" shown)" = 0 ] && [ "$(fr_v "$(fr_d 4)" shown)" = 0 ] || { EVID="$(fr_d 1) | $(fr_d 2) | $(fr_d 3) | $(fr_d 4)"; fail fp_frame_a_tap_timeout; }
san "$out" fp_frame_a
# knob free (a content page with no borrowed keys): a key falls to the volume AND shows the popup; the bubble sheet borrows nothing
out=$(run "vol 0; tools bubbles; @0.3; frame; simkey top; @0.1; frame; simvol")
[ "$(fr_v "$(fr_d 1)" shown)" = 0 ] && [ "$(fr_v "$(fr_d 2)" shown)" = 1 ] && grep -q '^SIM vol 2$' <<<"$out" || { EVID="$(fr_d 1) | $(fr_d 2)"; fail fp_frame_key_volume_overlay; }
san "$out" fp_frame_key
# the keys are a content page's own for 4 s after a touch or a key it took (the borrow's analogue): then a key is the volume and shows the popup
out=$(run "vol 0; tools ambient; @0.5; simtap 230 230; @0.2; simkey top; @0.2; frame; simvol; @4.5; simkey top; @0.2; frame; simvol")
[ "$(fr_v "$(fr_d 1)" shown)" = 0 ] && [ "$(fr_v "$(fr_d 2)" shown)" = 1 ] && [ "$(grep '^SIM vol' <<<"$out" | sed -n 1p)" = "SIM vol 0" ] && [ "$(grep '^SIM vol' <<<"$out" | sed -n 2p)" != "SIM vol 0" ] || { EVID="$(fr_d 1) | $(fr_d 2) $(grep '^SIM vol' <<<"$out" | tr '\n' '|')"; fail fp_frame_keys_borrow_timeout; }
san "$out" fp_frame_keys
# an option axis through the popup: the counter's 選單 cell opens its ring (the popup hides: the ring is the page's face), the ring's last cell is 說明
out=$(run "tools counter; @0.3; frame show; @0.2; frame tap opt 0; @0.3; popring -5; popring 4; @0.3; uitop")
grep -q '^APP popring open=1 ' <<<"$out" && grep -qx 'APP uitop explain' <<<"$out" || { EVID="$(grep 'APP popring\|APP uitop' <<<"$out" | tr '\n' '|')"; fail fp_frame_option_ring_help; }
san "$out" fp_frame_option
# a game with the shared exit and options (ambient): a key turn shows the popup (the HEAD row's cells and 返回), an axis cell borrows the keys and holds it, 返回 leaves
out=$(run "tools ambient; @5; frame; simkey top; @0.2; frame; frame tap opt 0 0; @4.5; frame; frame tap back; @0.3; uitop")
[ "$(fr_v "$(fr_d 1)" mode)" = X ] && [ "$(fr_v "$(fr_d 1)" chrome)" = 1 ] && [ "$(fr_v "$(fr_d 1)" shown)" = 0 ] && [ "$(fr_v "$(fr_d 2)" shown)" = 1 ] && [ "$(fr_v "$(fr_d 4)" shown)" = 1 ] || { EVID="$(fr_d 1) | $(fr_d 2) | $(fr_d 4)"; fail fp_frame_game_overlay; }
grep -qx 'APP uitop tools' <<<"$out" || fail fp_frame_game_back
san "$out" fp_frame_game
# the pause overlay coming up over a popup that is already shown: the options stay (above it), the overlay's own 返回 pill goes (the pause overlay has one)
out=$(run "tools ambient; @0.5; frame show; exit pause; @0.2; frame; exit; exit tap resume; frame")
grep -q 'APP exit attached=1 mode=C a=0 pause=1' <<<"$out" && [ "$(fr_v "$(fr_d 2)" shown)" = 1 ] && [ "$(fr_v "$(fr_d 3)" shown)" = 0 ] || { EVID="$(fr_d 2) | $(fr_d 3)"; fail fp_frame_pause_over_shown; }
san "$out" fp_frame_pause
# CF2 fix round: while a round is in flight the keys are the game's however long ago the last touch / key was (timing games by key alone); between rounds the 4 s rule holds
out=$(run "vol 0; games open tower; @0.5; simtap 230 230; @6; simkey top; @0.2; frame; simvol; arena")
[ "$(fr_v "$(fr_d 1)" shown)" = 0 ] && grep -q '^SIM vol 0$' <<<"$out" && grep -q 'APP game tower state=1 n=2 ' <<<"$out" || { EVID="$(fr_d 1) $(grep 'SIM vol\|game tower' <<<"$out" | tr '\n' '|')"; fail fp_keys_in_flight_tower; }
out=$(run "vol 0; games open tower; @5; simkey top; @0.2; frame; simvol")
[ "$(fr_v "$(fr_d 1)" shown)" = 1 ] && ! grep -q '^SIM vol 0$' <<<"$out" || { EVID="$(fr_d 1)"; fail fp_keys_between_rounds_volume; }
out=$(run "vol 0; games open simon; @0.5; sectors hub; @8; simkey top; @0.2; frame; simvol")
grep -q '^SIM vol 0$' <<<"$out" && [ "$(fr_v "$(fr_d 1)" shown)" = 0 ] || { EVID="$(fr_d 1) $(grep 'SIM vol' <<<"$out")"; fail fp_keys_in_flight_simon; }
san "$out" fp_keys_flight
# the fast up flick (CF2) shows a content page's popup on the FakePod too; a slow drag does not
out=$(run "tools dice; @0.6; frame; simdrag 230 420 230 120 150; @0.2; frame; simdrag 230 420 230 120 1200; @0.2; frame")
[ "$(fr_v "$(fr_d 1)" shown)" = 0 ] && [ "$(fr_v "$(fr_d 2)" shown)" = 1 ] && grep -qx 'APP flick up' <<<"$out" || { EVID="$(fr_d 1) | $(fr_d 2)"; fail fp_flick_up; }
san "$out" fp_flick
# a hold anywhere on a content page goes home (3000 ms): 2000 does not, 3100 does
out=$(run "tools ambient; @0.5; simpress 230 230 2000; @0.3; uitop; simpress 230 230 3100; @0.3; uistack")
grep -q 'APP uitop tools_ambient' <<<"$out" && grep -qx 'APP uistack menu' <<<"$out" || { EVID="$(grep 'APP uitop\|APP uistack' <<<"$out" | tr '\n' '|')"; fail fp_hold_anywhere_home; }
san "$out" fp_hold

# F2: quick settings shows the FakePod's battery percentage (the AXP2101 gauge; sim state batt=): the picture differs between 62 % and 63 %, and between a battery and none (the round simulator has no gauge)
qs_shot() { SIM_STATE="$1" SIM_FP=1 $SIM --headless --root "$T/root" --cmd "screen on; @0.5; qs; @0.5; shot $T/$2.png" >/dev/null 2>&1; }
qs_shot "batt=62;charging=0" qs62; qs_shot "batt=63;charging=0" qs63; qs_shot "charging=0" qsnone; qs_shot "batt=62;charging=1" qs62c
[ -s "$T/qs62.png" ] && ! cmp -s "$T/qs62.png" "$T/qs63.png" && ! cmp -s "$T/qs62.png" "$T/qsnone.png" && ! cmp -s "$T/qs62.png" "$T/qs62c.png" || fail fp_qs_battery_pct

# DACFS (amended 2026-11-17): >= 32 kHz is native on the FakePod (the CS43131 is reprogrammed, never resampled); 16 .. 32 kHz is upsampled by the rate stage to the same family;
# below 16 kHz is refused with its reason on the strip
out=$(run "mp3ui; @0.3; simaudiorate 22050 1; mp3 1; @0.6; uistrip")
{ ! grep -q '^APP dac rate:' <<<"$out" && ! grep -q '^APP skip reason=rate_low' <<<"$out" && grep -q '^APP pipe src=22050 pipe=44100 ' <<<"$out" && grep -q 'APP strip shown=0' <<<"$out"; } || { EVID="$(grep 'APP pipe\|dac rate\|skip reason' <<<"$out" | tr '\n' '|')"; fail fp_22k_upsampled_plays; }
out=$(run "mp3ui; @0.3; simaudiorate 16000 1; mp3 1; @0.6")
{ ! grep -q '^APP dac rate:' <<<"$out" && grep -q '^APP pipe src=16000 pipe=32000 ' <<<"$out"; } || fail fp_16k_upsampled
out=$(run "mp3ui; @0.3; simaudiorate 24000 1; mp3 1; @0.6")
{ ! grep -q '^APP dac rate:' <<<"$out" && grep -q '^APP pipe src=24000 pipe=48000 ' <<<"$out"; } || { EVID="$(grep 'APP pipe\|dac rate' <<<"$out" | tr '\n' '|')"; fail fp_24k_to_48k; }
out=$(run "mp3ui; @0.3; simaudiorate 11025 1; mp3 1; @0.6; uistrip")
{ grep -q '^APP dac rate: 11025 Hz' <<<"$out" && grep -q '^APP skip reason=rate_low rate=11025$' <<<"$out" && grep -q 'APP strip shown=1' <<<"$out" && ! grep -q '^APP pipe src=11025 pipe=44100' <<<"$out"; } || { EVID="$(grep 'APP pipe\|dac rate\|skip reason\|strip' <<<"$out" | tr '\n' '|')"; fail fp_11k_refused; }
out=$(run "mp3ui; @0.3; simaudiorate 12000 1; mp3 1; @0.6")
{ grep -q '^APP dac rate: 12000 Hz' <<<"$out" && ! grep -q '^APP pipe src=12000 pipe=32000' <<<"$out"; } || fail fp_12k_refused
for r in 32000 44100 48000 88200 96000 176400 192000; do
    out=$(run "seti dac_max_rate 384000; mp3ui; @0.3; simaudiorate $r 1; mp3 1; @0.6")
    { ! grep -q '^APP dac rate:' <<<"$out" && { grep -q "^APP pipe src=$r pipe=$r halvings=0 " <<<"$out" || ! grep -q '^APP pipe src=' <<<"$out"; }; } || { EVID="$(grep 'APP pipe\|dac rate' <<<"$out" | tr '\n' '|')"; fail fp_native_$r; }
done
san "$out" fp_low_rate
# 352.8 / 384 kHz stay native (no halving to 192, no stage); the FLAC itself is the host test's (test_flac 384k: generated at test time) and the unity digital gain is test_cs43131's (pc_cs_digital_gain)
for r in 352800 384000; do
    out=$(run "seti dac_max_rate 384000; mp3ui; @0.3; simaudiorate $r 1; mp3 1; @0.6")
    { ! grep -q '^APP dac rate:' <<<"$out" && grep -q "^APP pipe src=$r pipe=$r halvings=0 " <<<"$out"; } || { EVID="$(grep 'APP pipe\|dac rate' <<<"$out" | tr '\n' '|')"; fail fp_${r}_native; }
done
# the hard limit (dac_hard_rate): a source above it is refused before it opens; the halve threshold (dac_max_rate) is a separate setting (default 384k = no halving)
out=$(run "seti dac_hard_rate 192000; seti dac_max_rate 384000; mp3ui; @0.3; simaudiorate 384000 1; mp3 1; @0.6")
{ grep -q '^APP dac rate: 384000 Hz.*hard limit' <<<"$out" && ! grep -q '^APP pipe src=384000' <<<"$out"; } || { EVID="$(grep 'APP pipe\|dac rate' <<<"$out" | tr '\n' '|')"; fail fp_hard_limit_refuses; }
out=$(run "seti dac_hard_rate 192000; seti dac_max_rate 192000; mp3ui; @0.3; simaudiorate 192000 1; mp3 1; @0.6")
{ ! grep -q '^APP dac rate:' <<<"$out" && grep -q '^APP pipe src=192000 pipe=192000 halvings=0 ' <<<"$out"; } || fail fp_hard_limit_allows_equal
out=$(run "mp3ui; @0.3; simaudiorate 384000 1; mp3 1; @0.6")      # the defaults: hard 384k, halve 384k: pure passthrough
{ ! grep -q '^APP dac rate:' <<<"$out" && grep -q '^APP pipe src=384000 pipe=384000 halvings=0 ' <<<"$out"; } || { EVID="$(grep 'APP pipe\|dac rate' <<<"$out" | tr '\n' '|')"; fail fp_default_384k_passthrough; }
# the user's ceiling still halves (a setting, not a resampler): 384 kHz under a 192 kHz ceiling plays at 192 kHz
out=$(run "seti dac_max_rate 192000; mp3ui; @0.3; simaudiorate 384000 1; mp3 1; @0.6")
{ ! grep -q '^APP dac rate:' <<<"$out" && grep -q '^APP pipe src=384000 pipe=192000 halvings=1 ' <<<"$out"; } || { EVID="$(grep 'APP pipe\|dac rate' <<<"$out" | tr '\n' '|')"; fail fp_ceiling_halves; }
# an odd rate >= 32 kHz (99.999 kHz) has no code: refused, never resampled to 96 kHz
out=$(run "seti dac_max_rate 384000; mp3ui; @0.3; simaudiorate 99999 1; mp3 1; @0.6")
{ grep -q '^APP dac rate: 99999 Hz\|^APP play refused rate=99999' <<<"$out" && ! grep -q '^APP pipe src=99999 pipe=96000' <<<"$out"; } || { EVID="$(grep 'APP pipe\|dac rate' <<<"$out" | tr '\n' '|')"; fail fp_odd_rate_refused; }
# the fallback (honest degrade, FIX2 I2): a rate the stage cannot take (below 8 kHz) is refused with its reason on the status strip, the track is skipped and the next one plays
out=$(run "mp3ui; @0.3; simaudiorate 7000 1; mp3 1; @0.6; uistrip")
{ grep -q '^APP dac rate: 7000 Hz' <<<"$out" && grep -q '^APP skip reason=rate_low rate=7000$' <<<"$out" && grep -q 'APP strip shown=1' <<<"$out" && [ "$(grep -c '^APP track idx=' <<<"$out")" = 2 ]; } || { EVID="$(grep -c 'min rate' <<<"$out") refused, $(grep -c '^APP track idx=' <<<"$out") tracks"; fail fp_below_8k_refused_shown_skipped; }
san "$out" fp_low_rate_refused

# BTAUDIO: the Wired output page and the reasons of the wired stages on the FakePod (the owner's examples, 2026-11-17)
out=$(run "wired open; @0.3; wired rows")
{ grep -qx 'APP wired rows=6' <<<"$out" && [ "$(grep -c '^APP wired row=' <<<"$out")" = 6 ]; } || { EVID="$(grep '^APP wired' <<<"$out" | tr '\n' '|')"; fail fp_wired_rows; }
grep -q '^APP wired row=\([^ ]* \)\?\(輸出推力：1.0 V・高音質（預設）\|Output drive: 1.0 V · best quality (default)\)$' <<<"$out" || { EVID="$(grep '^APP wired row=' <<<"$out" | tr '\n' '|')"; fail fp_dacfs_label_best; }
out=$(run "wired open; @0.3; wired key fs; wired rows; cfg dac_fs; wired key slot; wired key slot; cfg dac_slot24_from; wired key hpf; cfg dac_hpf; wired key amute; cfg dac_amute")
{ grep -q '^APP wired row=\([^ ]* \)\?\(輸出推力：1.41 V・中推力\|Output drive: 1.41 V · more drive\)$' <<<"$out" && grep -q '^APP cfg dac_fs=1$' <<<"$out" && grep -q '^APP cfg dac_slot24_from=1$' <<<"$out" && grep -q '^APP cfg dac_hpf=0$' <<<"$out" && grep -q '^APP cfg dac_amute=0$' <<<"$out"; } || { EVID="$(grep '^APP wired row=\|^APP cfg' <<<"$out" | tr '\n' '|')"; fail fp_wired_options; }
san "$out" fp_wired_page
# DAC173: the output drive row cycles 1.0 -> 1.41 -> 1.73 -> 1.0 V; 1.73 V is experimental (a disclaimer tap first); raising the drive caps the volume at 20, lowering never changes it
out=$(run "wired open; @0.3; wired key fs; cfg dac_fs; wired key fs; wired rows; cfg dac_fs; wired key fs; wired rows; cfg dac_fs; wired key fs; wired rows; cfg dac_fs")
{ [ "$(grep -c '^APP cfg dac_fs=1$' <<<"$out")" = 2 ] \
    && grep -q '^APP wired row=\([^ ]* \)\?\(實驗性：1.73 V\|Experimental: 1.73 V high-voltage mode\)' <<<"$out" && [ "$(grep -c '^APP cfg dac_fs=2$' <<<"$out")" = 1 ] \
    && grep -q '^APP wired row=\([^ ]* \)\?\(輸出推力：1.73 V・高電壓（實驗性）\|Output drive: 1.73 V · high voltage (experimental)\)$' <<<"$out" && [ "$(grep -c '^APP cfg dac_fs=0$' <<<"$out")" = 1 ]; } || { EVID="$(grep '^APP wired row=\|^APP cfg' <<<"$out" | cut -c1-90 | tr '\n' '|')"; fail fp_dacfs_three_values_gate; }
out=$(run "wired open; @0.3; seti dac_fs 1; wired key fs; wired rows; cfg dac_fs")                 # the first tap on 1.41 V only shows the disclaimer: the stored value stays 1
{ grep -qx 'APP cfg dac_fs=1' <<<"$out" && ! grep -q 'APP cfg dac_fs=2' <<<"$out"; } || { EVID="$(grep '^APP cfg' <<<"$out" | tr '\n' '|')"; fail fp_dacfs_gate_holds; }
out=$(run "wired open; @0.3; wired key fs; wired key hpf; wired key fs; cfg dac_fs; wired rows")     # another row between the two taps cancels the disclaimer: 1.41 V -> 1.73 V needs two taps in a row
grep -q '^APP wired help=.*\(Chip drive now: 1.73 V\|晶片實際推力：1.73 V\)' <<<"$out" && fail fp_dacfs_gate_cancelled
out=$(run "vol 50; wired open; @0.3; wired key fs; cfg dac_fs; cfg volume; uistrip")
{ grep -qx 'APP cfg dac_fs=1' <<<"$out" && grep -qx 'APP cfg volume=20' <<<"$out" && grep -q 'APP strip shown=1' <<<"$out"; } || { EVID="$(grep '^APP cfg\|^APP strip' <<<"$out" | tr '\n' '|')"; fail fp_dacfs_raise_clamps_volume; }
out=$(run "vol 50; seti dac_fs 1; wired open; @0.3; wired key fs; wired key fs; cfg dac_fs; cfg volume")  # 1.41 V: disclaimer, confirm -> 1.73 V and the volume is capped too
{ grep -qx 'APP cfg dac_fs=2' <<<"$out" && grep -qx 'APP cfg volume=20' <<<"$out"; } || { EVID="$(grep '^APP cfg' <<<"$out" | tr '\n' '|')"; fail fp_dacfs_confirm_clamps_volume; }
# BTVOL: the drive cap is a rule of the WIRED output: with a headset as the output it caps the wired value (50 -> 20) and leaves the headset's (45) and the sound alone; back on the wired output 20 is in force at once
out=$(run "vol 50; simbt hp a; @0.1; vol 45; wired open; @0.3; wired key fs; cfg dac_fs; cfg volume; vol; simaudiovol; simbt hp off; simaudiovol; @0.1; simaudiovol; vol")
{ grep -qx 'APP cfg dac_fs=1' <<<"$out" && grep -qx 'APP cfg volume=20' <<<"$out" && [ "$(grep -c '^APP vol out=bt:020000000001 value=45 wired=20$' <<<"$out")" = 1 ] \
    && [ "$(grep '^SIM audiovol' <<<"$out" | tr '\n' '|')" = 'SIM audiovol 45|SIM audiovol 20|SIM audiovol 20|' ] && grep -qx 'APP vol out=wired value=20 wired=20' <<<"$out"; } || { EVID="$(grep '^APP cfg\|^APP vol out\|^SIM audiovol' <<<"$out" | tr '\n' '|')"; fail fp_dacfs_cap_is_wired_rule; }
san "$out" fp_dacfs_cap_wired
# PMVOL: the cap is a rule of the jack, so it caps the speaker-mode value too (wired 50, speaker 70 -> both 20), and the jack in speaker mode plays 20 at once
out=$(run "vol 50; simbt mode 1; @0.2; vol 70; wired open; @0.3; wired key fs; cfg dac_fs; cfg volume; cfg volume_spk; vol; simaudiovol")
{ grep -qx 'APP cfg volume=20' <<<"$out" && grep -qx 'APP cfg volume_spk=20' <<<"$out" && grep -qx 'APP vol out=speaker value=20 wired=20' <<<"$out" && [ "$(grep '^SIM audiovol' <<<"$out" | tail -1)" = 'SIM audiovol 20' ]; } || { EVID="$(grep '^APP cfg\|^APP vol out\|^SIM audiovol' <<<"$out" | tr '\n' '|')"; fail fp_dacfs_cap_speaker_too; }
out=$(run "vol 15; seti dac_fs 0; wired open; @0.3; wired key fs; cfg volume")                       # volume already at or below 20: left alone
grep -qx 'APP cfg volume=15' <<<"$out" || { EVID="$(grep '^APP cfg' <<<"$out" | tr '\n' '|')"; fail fp_dacfs_raise_low_volume_kept; }
out=$(run "vol 60; seti dac_fs 2; wired open; @0.3; wired key fs; cfg dac_fs; cfg volume; uistrip")  # 1.73 V -> 1.0 V lowers the drive: the volume stays and no message
{ grep -qx 'APP cfg dac_fs=0' <<<"$out" && grep -qx 'APP cfg volume=60' <<<"$out" && ! grep -q 'APP strip shown=1' <<<"$out"; } || { EVID="$(grep '^APP cfg\|^APP strip' <<<"$out" | tr '\n' '|')"; fail fp_dacfs_lower_keeps_volume; }
out=$(run "seti dac_fs 1; wired open; @0.3; wired key fs; wired key fs; @0.3; wired rows")                                              # honest state: the line shows what the (simulated) chip reads back
grep -q '^APP wired help=\(Chip drive now: 1.73 V\|晶片實際推力：1.73 V\)' <<<"$out" || { EVID="$(grep '^APP wired help' <<<"$out" | cut -c1-80)"; fail fp_dacfs_readback_line; }
out=$(run "mp3ui; @0.3; simaudiorate 384000 1; mp3 1; @0.6; outstat tw")
grep -q '^APP outstat wired=輸入 384k → 輸出 384k・直通（原始）' <<<"$out" || { EVID="$(grep '^APP outstat wired' <<<"$out")"; fail fp_outstat_384_passthrough; }
out=$(run "seti dac_max_rate 192000; mp3ui; @0.3; simaudiorate 384000 1; mp3 1; @0.6; outstat tw; outstat en")
{ grep -q '^APP outstat wired=輸入 384k → 輸出 192k・折半（設定：折半 192k）' <<<"$out" && grep -q 'In 384k → out 192k・halved (setting: halve at 192k)' <<<"$out" && grep -qx 'APP outstat block 處理：折半 384k→192k（設定：折半 192k）' <<<"$out"; } || { EVID="$(grep '^APP outstat' <<<"$out" | tr '\n' '|')"; fail fp_outstat_halve_192; }
out=$(run "mp3ui; @0.3; simaudiorate 22050 1; mp3 1; @0.6; outstat tw")
grep -q '^APP outstat wired=輸入 22.05k → 輸出 44.1k・升頻（DAC：最低 32k，升頻）' <<<"$out" || { EVID="$(grep '^APP outstat wired\|^APP pipe\|^APP dac' <<<"$out" | tr '\n' '|')"; fail fp_outstat_upsample_22k; }
# the 24-bit slot: off by default; at or above the threshold the live line and the block say it, with its reason (the setting)
out=$(run "mp3ui; @0.3; simaudiorate 384000 1; mp3 1; @0.6; outstat tw")
! grep -q '24-bit 槽' <<<"$out" || fail fp_slot24_default_off
out=$(run "seti dac_slot24_from 1; mp3ui; @0.3; simaudiorate 384000 1; mp3 1; @0.6; outstat tw")
{ grep -q '^APP outstat wired=.*・24-bit 槽' <<<"$out" && grep -qx 'APP outstat block 槽寬：24-bit（設定）' <<<"$out"; } || { EVID="$(grep '^APP outstat' <<<"$out" | tr '\n' '|')"; fail fp_slot24_352_on_384; }
out=$(run "seti dac_slot24_from 1; mp3ui; @0.3; simaudiorate 192000 1; mp3 1; @0.6; outstat tw")
! grep -q '24-bit 槽' <<<"$out" || fail fp_slot24_352_not_on_192
out=$(run "seti dac_slot24_from 2; mp3ui; @0.3; simaudiorate 192000 1; mp3 1; @0.6; outstat tw")
grep -q '24-bit 槽' <<<"$out" || fail fp_slot24_192_on_192
# the DAC's own volume is the wired volume: the live line says it in dB
out=$(run "vol 50; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.6; outstat tw")
grep -q '^APP outstat wired=.*・DAC -[0-9.]* dB' <<<"$out" || { EVID="$(grep '^APP outstat wired' <<<"$out")"; fail fp_outstat_dac_db; }
# a change while a 384k file plays re-plans it at once
out=$(run "mp3ui; @0.3; simaudiorate 384000 1; mp3 1; @0.6; wired open; @0.3; wired key dacmax; axishalf -1; wired key dacmax; @4.5; outstat tw")
{ grep -q '^APP audio wired reopen' <<<"$out" && grep -q '^APP outstat wired=輸入 384k → 輸出 \(48\|96\|192\)k・折半' <<<"$out"; } || { EVID="$(grep '^APP outstat wired\|^APP audio' <<<"$out" | tr '\n' '|')"; fail fp_wired_change_replans; }
san "$out" fp_wired_replan

# vizb (2026-11-17): the square board's size switch. Default zoomed to 440 px (zoom 375/256 on the 300 wide pictures), "original" shows them 1:1 with no scaling; the setting applies to bars, ring, scope
# and the voice pictures, the waterfall and the phase picture are never enlarged; the style ring has 8 cells (6 axes, size, help) and the size cell is the 7th (index 6)
out=$(run "vol 0; track 0; @1; viz bars; viz stat; viz zoom orig; viz stat; viz zoom zoom; viz stat; viz fall; viz stat")
z=$(grep '^APP viz extra' <<<"$out" | sed -E 's/^APP viz extra zoom=([0-9]+) size=([a-z]+) .*$/\1:\2/' | tr '\n' ' ')
[ "$z" = "375:zoom 375:zoom 256:orig 375:zoom 256:zoom 256:zoom " ] || { EVID="$z"; fail fp_viz_zoom_values; }
san "$out" fp_viz_zoom
out=$(run "vol 0; track 0; @1; viz bars; viz menu; viz pick 6; viz pick 6; viz stat; viz pick 6; viz stat")
z=$(grep '^APP viz extra' <<<"$out" | sed -E 's/^APP viz extra zoom=([0-9]+) size=([a-z]+) .*$/\1:\2/' | tr '\n' ' ')
{ [ "$z" = "375:zoom 375:zoom 375:zoom 256:orig 256:orig 375:zoom 375:zoom " ] && grep -q '^APP viz ring .* cells=8 ' <<<"$out"; } || { EVID="$z $(grep '^APP viz ring' <<<"$out" | head -2 | tr '\n' '|')"; fail fp_viz_zoom_axis; }
# the ring's last cell is the help, one tap
out=$(run "vol 0; track 0; @1; viz bars; viz menu; viz pick 7; uitop")
[ "$(grep '^APP uitop' <<<"$out")" = "APP uitop explain" ] || { EVID="$(grep '^APP uitop' <<<"$out")"; fail fp_viz_help_cell; }
# the lyrics view, voice rows and the piano roll on the square panel: not empty shots, no sanitizer text
out=$(run "vol 0; track 0; @1; viz lyrics; @17.5; shot $T/fl.png; simvoices spc; viz rows; @2; shot $T/fr.png; viz roll; @4; shot $T/fo.png")
for f in fl fr fo; do [ -s "$T/$f.png" ] || fail "fp_viz_shot_$f"; done
san "$out" fp_viz_kinds

# HUD task (2026-11-18): the HUD face on the square panel (the grid layout), the countdown's last 10 s, the stopwatch laps, the pomodoro's face cell; the data and no sanitizer text
out=$(run "vol 0; clock; clockface hud; @1.2; clockface dump; shot $T/hc.png; show countdown; timers face hud; timers cd set 30; timers cd start; timers cd jump 9; @0.6; timers dump; shot $T/hd.png; uihome; show stopwatch; timers face hud; timers sw start; @1.2; timers sw lap; @1.1; timers sw lap; timers dump; shot $T/hs.png")
for f in hc hd hs; do [ -s "$T/$f.png" ] || fail "fp_hud_shot_$f"; done
grep -q '^APP tface kind=hud feat=clock big=[0-9][0-9]:[0-9][0-9]:[0-9][0-9] .*time_hm=[0-9][0-9]:[0-9][0-9] ' <<<"$out" || { EVID="$(grep '^APP tface' <<<"$out" | head -1)"; fail fp_hud_clock_data; }
grep '^APP tface ' <<<"$out" | sed -n 2p | grep -q 'feat=cd .*red=1 alert=1 .*main=00:0[89]' || { EVID="$(grep '^APP tface' <<<"$out" | sed -n 2p)"; fail fp_hud_cd_last10; }
grep '^APP tface ' <<<"$out" | tail -1 | grep -q 'feat=sw .*laps=02 .*lap1_no=02 lap1=00:01\.[0-9][0-9] lap2_no=01 ' || { EVID="$(grep '^APP tface' <<<"$out" | tail -1)"; fail fp_hud_sw_laps; }
python3 -c "
from PIL import Image; im = Image.open('$T/hc.png').convert('RGB')
assert im.size == (460, 460) and sum(im.getpixel((230, 30))) > 0" 2>/dev/null || fail fp_hud_square_art
san "$out" fp_hud
out=$(run "vol 0; clock; show pomo; pomo; popring -2; popring -1; popring 1; popring 1; cfg pomoface; popring -3; uitop")
grep -q '^APP cfg pomoface=\(info\|hud\|rings\)' <<<"$out" || { EVID="$(grep '^APP cfg' <<<"$out")"; fail fp_hud_pomo_face_axis; }
san "$out" fp_hud_pomo

# FPFIX: the FakePod's power features. Auto power-off row: default 30 min, the first tap selects, then 30 -> 60 (right half; the ends stop) and 30 -> 10 -> off (left half); the charge screen (sim state batt= charging= lowbat=)
out=$(run "uipush menu; uipush settings; settings group power; settings rows")
grep -q 'APP settings row=.*自動關機: 30 分$' <<<"$out" || { EVID="$(grep '^APP settings row' <<<"$out" | head -3)"; fail fp_autooff_row_default; }
out=$(run "uipush menu; uipush settings; settings key autooff; settings key autooff; @0.1; settings rows")
grep -q 'APP settings row=.*自動關機: 60 分$' <<<"$out" || fail fp_autooff_step
out=$(run "uipush menu; uipush settings; settings key autooff; axishalf -1; settings key autooff; settings key autooff; settings key autooff; @0.1; settings rows")
grep -q 'APP settings row=.*自動關機: 關$' <<<"$out" || fail fp_autooff_off
chg() { SIM_STATE="$1" SIM_FP=1 $SIM --headless --root "$T/root" --cmd "screen on; @0.5; uipush charge; @0.6; ring; shot $T/$2.png" 2>&1; }
out=$(chg "batt=87;charging=1" ch87); grep -q 'center=87 % · 充電中' <<<"$out" || { EVID="$(grep 'APP ring' <<<"$out")"; fail fp_charge_screen_text; }
out=$(chg "batt=100;charging=2" chfull); grep -q '已充飽' <<<"$out" || fail fp_charge_screen_full
out=$(chg "batt=1;charging=0;lowbat=1" chlow); grep -q '電量太低 (1 %)' <<<"$out" || { EVID="$(grep 'APP ring' <<<"$out")"; fail fp_lowbat_screen_text; }
[ -s "$T/ch87.png" ] && ! cmp -s "$T/ch87.png" "$T/chfull.png" && ! cmp -s "$T/ch87.png" "$T/chlow.png" || fail fp_charge_screen_differs

# OLEDOFF: the FakePod's screen always times out. No screen-timeout "never" row on either board; the instrument-style never-host and a stuck touch are capped;
# on USB power (the sim default) an idle player's screen is off after the timeout and the unit stays up; a page that asks for full light (the torch) still times out
out=$(run "uipush menu; uipush settings; settings group 1; settings rows; back; settings group 2; settings rows; back; settings group 3; settings rows; back; settings group 4; settings rows; back; settings group 5; settings rows; back; settings group 6; settings rows; back"); grep -qiE '常亮|永不|never|always' <<<"$out" && { EVID="$(grep -iE '常亮|永不|never|always' <<<"$out" | head -2)"; fail fp_no_never_screen_row; }
out=$(SIM_TRACE=1 SIM_FP=1 $SIM --headless --root "$T/root" --cmd "screen on; @0.5; uipush menu; @61; uitop" 2>&1)
grep -qx 'SIM backlight 0' <<<"$out" && ! grep -q 'power off' <<<"$out" || fail fp_usb_idle_screen_off
out=$(SIM_TRACE=1 SIM_FP=1 $SIM --headless --root "$T/root" --cmd "screen on; @0.5; tools torch; @0.3; @61" 2>&1)
grep -qx 'SIM backlight 0' <<<"$out" || fail fp_torch_times_out
out=$(SIM_TRACE=1 SIM_FP=1 $SIM --headless --root "$T/root" --cmd "screen on; @0.5; uipush menu; simpress 230 230 130000; @2" 2>&1)
[ "$(grep '^SIM backlight' <<<"$out" | tail -1)" = 'SIM backlight 0' ] || fail fp_stuck_touch_screen_off
san "$out" fp_oled

# the static now-playing page (sim/tests/39_nowfp.sh: its round part runs in make sim-test, the FakePod part here)
NOWFP_FP=1
. sim/tests/39_nowfp.sh
. sim/tests/44_ninegrid.sh   # the FakePod nine-grid (needs nfp / nfl from 39)

# the geometry file holds on a square panel too (its static asserts are compiled in the square build already; this keeps the claim honest)
[ "$(grep -c 'UI_RGRID_' app/main/ui/ui_geom.h)" -ge 8 ] || fail fp_geom_grid_macros

if [ "$fails" = 0 ]; then echo "sim-test-fp: all passed"; else echo "sim-test-fp: $fails failed"; exit 1; fi
