# SPDX-License-Identifier: LGPL-2.1-or-later
# --- Task 11: shared list, choosers, favourites ---
# a fixture copy with N copies of the sample packs: vocab decks (/sdcard/vocab/*.zip) and lunch lists (/sdcard/lists/*.zip)
# rows of the page's list: the buttons between the title label and the Back button, one "x1 y1 x2 y2" per line
# no two rows intersect; clipped to the list's window (y 35..285 on this screen), no row intersects the Back button
# every row of a chooser can be chosen with the knob and one tap: for row i, scroll i detents, tap the middle of the row, expect the choose line
many_packs 6 6
out=$($SIM --headless --root "$T/fix" --cmd "vocab; gtfiles; vocab decks; @0.3; gtfiles; back; @0.3; gtfiles" 2>&1)
g=$(grep '^APP gt files' <<<"$out" | awk '{print $4}' | tr '\n' ' ')
[ "$g" = "1 1 1 " ] || { EVID="open packs around the deck chooser (home deck open): $g"; fail decks_open_files; }
out=$($SIM --headless --root "$T/fix" --cmd "gtfiles; tools lunch; @0.3; gtfiles; back; @0.3; gtfiles" 2>&1)
g=$(grep '^APP gt files' <<<"$out" | awk '{print $4}' | tr '\n' ' ')
[ "$g" = "0 0 0 " ] || { EVID="open packs around the list chooser: $g"; fail lists_open_files; }
o=$($SIM --headless --root "$T/fix" --cmd "vocab decks; @0.3; uiboxes" 2>&1)
[ "$(list_rows "$o" | wc -l)" = 7 ] || { EVID="deck rows: $(list_rows "$o" | wc -l)"; fail decks_seven_rows; }
rows_disjoint "$o" || { EVID="$(rows_disjoint "$o")"; fail decks_rows_disjoint; }
! grep -q '|\(下一頁\|More\)' <<<"$o" || fail decks_no_pager
o=$($SIM --headless --root "$T/fix" --cmd "tools lunch; @0.3; uiboxes" 2>&1)
[ "$(list_rows "$o" | wc -l)" = 7 ] || { EVID="list rows: $(list_rows "$o" | wc -l)"; fail lists_six_rows; }   # six lists + the end row 在電腦上編輯清單 (fx apps T14)
rows_disjoint "$o" || { EVID="$(rows_disjoint "$o")"; fail lists_rows_disjoint; }
! grep -q '|\(下一頁\|More\)' <<<"$o" || fail lists_no_pager
chooser_each decks_choose 7 "vocab decks" "vocab choose " 6 6
chooser_each lists_choose 6 "tools lunch" "lists choose " 6 6
# the current deck's row (the built-in sample, appended last) wears the selection colour and is fully inside the list window (y 35..285) when the chooser opens
many_packs 6 6
o=$($SIM --headless --root "$T/fix" --cmd "vocab decks; @0.3; vocab deckrow" 2>&1)
r=$(grep '^APP vocab deckrow ' <<<"$o")
if [[ "$r" =~ ^APP\ vocab\ deckrow\ rows=7\ sel=6\ box=([0-9-]+),([0-9-]+),([0-9-]+),([0-9-]+)$ ]]; then
    { [ "${BASH_REMATCH[2]}" -ge 35 ] && [ "${BASH_REMATCH[4]}" -le 285 ]; } || { EVID="$r"; fail decks_current_visible; }
else EVID="$r"; fail decks_current_row; fi
# 13 deck files: 11 listed + the built-in sample = MAXDECKS; the note says 2 more; 13 lists: 12 listed, 1 more
many_packs 13 13
o=$($SIM --headless --root "$T/fix" --cmd "vocab decks; @0.3; uiboxes" 2>&1)
[ "$(list_rows "$o" | wc -l)" = 12 ] && grep -q '^APP uibox label .*|\(…還有 2 份沒列出\|…2 more not shown\)$' <<<"$o" || { EVID="rows=$(list_rows "$o" | wc -l) $(grep '^APP uibox label' <<<"$o" | tr '\n' ' ')"; fail decks_more_note; }
o=$($SIM --headless --root "$T/fix" --cmd "tools lunch; @0.3; uiboxes" 2>&1)
[ "$(list_rows "$o" | wc -l)" = 13 ] && grep -q '^APP uibox label .*|\(…還有 1 份沒列出\|…1 more not shown\)$' <<<"$o" || { EVID="rows=$(list_rows "$o" | wc -l) $(grep '^APP uibox label' <<<"$o" | tr '\n' ' ')"; fail lists_more_note; }
many_packs 3 0
o=$($SIM --headless --root "$T/fix" --cmd "vocab decks; @0.3; uiboxes" 2>&1)
! grep -q '|…' <<<"$o" || fail decks_no_note_when_all_fit
# favourites read like the lists they come from
fresh
printf '%s\n' 'mp3:/sdcard/export/夜航星圖/03_late_tide.mp3=1,5' 'mp3:/sdcard/export/gone/nofile.mp3=1,1' \
    'chip:Nintendo Entertainment System.zip|Zelda (1986)/Zelda.nsf|11=1,2' 'scene:campfire=1,3' > "$T/fix/_player/config/stats.txt"
out=$($SIM --headless --root "$T/fix" --cmd "favui; @0.3; uiboxes" 2>&1)
for want in '遲到的潮汐' 'nofile.mp3' 'Zelda #12' '營火'; do
    grep -q "^APP uibox button .*|$want\$" <<<"$out" || { EVID="$(grep '^APP uibox button' <<<"$out" | tr '\n' ' ')"; fail "favs_row_$want"; }
done
! grep -q '|\(03_late_tide.mp3\|campfire\|chip:\)' <<<"$out" || fail favs_no_raw_ids
cp sim/fixture/_player/config/stats.txt "$T/fix/_player/config/stats.txt"; printf '%s\n' 'scene:campfire=1,3' >> "$T/fix/_player/config/stats.txt"
out=$($SIM --headless --root "$T/fix" --cmd "favui; @0.3; uiboxes" 2>&1)
ny=$(grep '^APP uibox button .*|營火$' <<<"$out" | awk '{print int(($4 + $6) / 2), int(($5 + $7) / 2)}')
out=$($SIM --headless --root "$T/fix" --cmd "favui; @0.3; simtap $ny; @0.5; uistack" 2>&1)
[ "$(grep '^APP uistack' <<<"$out")" = "APP uistack menu favs now" ] || { EVID="$ny: $(grep '^APP uistack' <<<"$out")"; fail favs_noise_opens_now; }
# --- end Task 11 ---

# --- Task 12: slot wins, alarm page, tool spacing ---
# slot: the result label says what paid. "tools grid ABCDEFGHI" shows exactly that grid (row by row, symbols 0..7) and spins to it.
fresh
RULE_ZH='三個一樣才中 · 幸運草百搭'; RULE_EN='3 alike win · clover wild'   # one line in UI_SLOT_FOOT, clear of Back (board batch A F3); the cherry rule is on the pay table
slot_label 222343531 '葡萄×3 +15' 'Grape×3 +15'
slot_label 227343531 '葡萄×3（幸運草代替）+15' 'Grape×3 (wild) +15'
slot_label 001343531 '左起兩顆櫻桃 +2' 'Two cherries from the left +2'
slot_label 072343531 '左起兩顆櫻桃 +2' 'Two cherries from the left +2'
slot_label 777343531 '幸運草×3 +120' 'Clover×3 +120'
slot_label 100343531 "$RULE_ZH" "$RULE_EN"
slot_label 331343531 "$RULE_ZH" "$RULE_EN"
slot_label 222343000 '共 +20（2 條線）' '+20 on 2 lines'     # two or more lines paid: the total and how many (one line: the room under the reels, board batch A F3)
slot_label 222333000 '共 +40（3 條線）' '+40 on 3 lines'
o=$($SIM --headless --root "$T/fix" --cmd "tools slot; @0.5; uiboxes" 2>&1)
grep -q "^APP uibox label .*|\($RULE_ZH\|$RULE_EN\)\$" <<<"$o" || { EVID="$(grep '^APP uibox label' <<<"$o" | cut -d'|' -f2 | tr '\n' ' ')"; fail slot_rule_line_before_first_spin; }
# the stroke of a two-cell win covers its two cells only (x from the block's left to the second cell's right edge); three cells run to the right edge
o=$($SIM --headless --root "$T/fix" --cmd "tools grid 001343531; @6; tools lines" 2>&1)
grep -qx 'APP tools line 0 x=84,y=124 x2=206,y2=124' <<<"$o" && [ "$(grep -c '^APP tools line ' <<<"$o")" = 1 ] || { EVID="$(grep '^APP tools line' <<<"$o" | tr '\n' ' ')"; fail slot_stroke_two_cells; }
o=$($SIM --headless --root "$T/fix" --cmd "tools grid 222343531; @6; tools lines" 2>&1)
grep -qx 'APP tools line 0 x=84,y=124 x2=276,y2=124' <<<"$o" || { EVID="$(grep '^APP tools line' <<<"$o" | tr '\n' ' ')"; fail slot_stroke_three_cells; }
# a tap on the score line opens the pay table (8 symbol rows; the rule sentences are behind 說明 since fx apps T15, test t15_slot_help); the knob scrolls it; a tap or Back leaves
o=$($SIM --headless --root "$T/fix" --cmd "tools slot; @0.5; simtap 180 68; @0.3; uitop; uiboxes" 2>&1)
grep -qx 'APP uitop tools_slot_pay' <<<"$o" || { EVID="$(grep '^APP uitop' <<<"$o")"; fail slot_pay_opens; }
for want in '櫻桃 ×3 = 5\|Cherry ×3 = 5' '檸檬 ×3 = 10\|Lemon ×3 = 10' '葡萄 ×3 = 15\|Grape ×3 = 15' '鈴鐺 ×3 = 20\|Bell ×3 = 20'; do
    grep -q "^APP uibox button .*|\($want\)\$" <<<"$o" || { EVID="$(grep '^APP uibox button' <<<"$o" | cut -d'|' -f2 | tr '\n' ' ')"; fail "slot_pay_row_$want"; }
done
o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; tools slot; @0.5; simtap 180 68; @0.3; uiboxes; simknob 8; @0.1; uiboxes" 2>&1)
before=$(sed -n '1,/^APP uiboxes end/p' <<<"$o"); after=$(sed '1,/^APP uiboxes end/d' <<<"$o")
y0=$(grep '^APP uibox button .*|\(櫻桃 ×3 = 5\|Cherry ×3 = 5\)$' <<<"$before" | awk '{print $5}'); y1=$(grep '^APP uibox button .*|\(櫻桃 ×3 = 5\|Cherry ×3 = 5\)$' <<<"$after" | awk '{print $5}')
{ [ -n "$y0" ] && [ -n "$y1" ] && [ "$y1" -lt "$y0" ]; } || { EVID="first row y before=$y0 after=$y1"; fail slot_pay_scrolls; }
o=$($SIM --headless --root "$T/fix" --cmd "tools slot; @0.5; simtap 180 68; @0.3; simtap 180 200; @0.3; uistack" 2>&1)
grep -q '^APP uistack .* tools_slot$' <<<"$o" || { EVID="$(grep '^APP uistack' <<<"$o")"; fail slot_pay_tap_leaves; }
o=$($SIM --headless --root "$T/fix" --cmd "tools slot; @0.5; simtap 180 68; @0.3; back; @0.3; uistack" 2>&1)
grep -q '^APP uistack .* tools_slot$' <<<"$o" || { EVID="$(grep '^APP uistack' <<<"$o")"; fail slot_pay_back_leaves; }
# alarm page: with no row holding the knob, the knob scrolls the column; 試聽鈴聲 comes into view and a tap starts the test
fresh
printf '%s\n' 'alarm_sound=/sdcard/export/夜航星圖/03_late_tide.mp3' >> "$T/fix/_player/config/app.txt"
TEST_RE='試聽鈴聲（會停止目前播放）\|Test the alarm (stops playback)'
SOUND_ARM='再點一次：改回內建鐘聲\|Tap again: back to the built-in chime'
o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; show alarm; @0.3; simknob 3; @0.2; uiboxes" 2>&1)
read -r x1 y1 x2 y2 <<<"$(box_of "$TEST_RE" "$o")"
if [ -n "$y1" ] && [ "$y1" -ge 35 ] && [ "$y2" -le 285 ]; then
    o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; show alarm; @0.3; simknob 3; @0.2; simtap $(((x1 + x2) / 2)) $(((y1 + y2) / 2)); @0.3" 2>&1)
    grep -qx 'APP clock alarm test on' <<<"$o" || { EVID="$(grep '^APP clock' <<<"$o")"; fail alarm_test_row_tap_starts_test; }
else EVID="test row box: $x1 $y1 $x2 $y2"; fail alarm_knob_brings_test_row_into_view; fi
# the knob still belongs to the hour row when it holds it (the time is an hour row and a minute row since UI unify T5)
o=$($SIM --headless --root "$T/fix" --cmd "show alarm; @0.3; uiboxes" 2>&1)
read -r x1 y1 x2 y2 <<<"$(box_of '時: .*\|Hour: .*' "$o")"
o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; show alarm; @0.3; simtap $(((x1 + x2) / 2)) $(((y1 + y2) / 2)); @0.2; simknob 2; @0.2; uiboxes" 2>&1)
grep -q '^APP uibox button .*|\(時: 10\|Hour: 10\)$' <<<"$o" && grep -q "^APP uibox button $x1 $y1 " <<<"$o" || { EVID="$(grep '^APP uibox button' <<<"$o" | cut -d'|' -f2 | tr '\n' ' ')"; fail alarm_time_row_keeps_knob; }
# the sound row: one tap shows the question and changes nothing; two taps reset; one tap and 5 s restore the text
o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; show alarm; @0.3; simknob 3; @0.2; uiboxes" 2>&1)
read -r x1 y1 x2 y2 <<<"$(box_of '鈴聲: .*\|Sound: .*' "$o")"
tapc="simtap $(((x1 + x2) / 2)) $(((y1 + y2) / 2))"
o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; show alarm; @0.3; simknob 3; @0.2; $tapc; @0.3; uiboxes" 2>&1)
grep -q "^APP uibox button .*|\($SOUND_ARM\)\$" <<<"$o" && ! grep -q '^APP clock alarm sound=' <<<"$o" || { EVID="$(grep '^APP uibox button\|^APP clock' <<<"$o" | cut -d'|' -f2 | tr '\n' ' ')"; fail alarm_sound_first_tap_asks; }
o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; show alarm; @0.3; simknob 3; @0.2; $tapc; @0.3; $tapc; @0.3; uiboxes" 2>&1)
grep -qx 'APP clock alarm sound=' <<<"$o" && grep -q '^APP uibox button .*|\(鈴聲: 內建鐘聲\|Sound: Built-in chime\)$' <<<"$o" || { EVID="$(grep '^APP uibox button\|^APP clock' <<<"$o" | cut -d'|' -f2 | tr '\n' ' ')"; fail alarm_sound_second_tap_resets; }
o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; show alarm; @0.3; simknob 3; @0.2; $tapc; @5; uiboxes; $tapc; @0.3; uiboxes" 2>&1)
first=$(sed -n '1,/^APP uiboxes end/p' <<<"$o"); second=$(sed '1,/^APP uiboxes end/d' <<<"$o")
grep -q '^APP uibox button .*|\(鈴聲: 03_late_tide.mp3\|Sound: 03_late_tide.mp3\)$' <<<"$first" && ! grep -q '^APP clock alarm sound=' <<<"$o" && grep -q "^APP uibox button .*|\($SOUND_ARM\)\$" <<<"$second" || { EVID="$(grep '^APP uibox button' <<<"$o" | cut -d'|' -f2 | tr '\n' ' ')"; fail alarm_sound_arm_times_out; }
# the built-in chime is already the default: a tap on the sound row asks nothing
fresh
o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; show alarm; @0.3; simknob 3; @0.2; uiboxes" 2>&1)
read -r x1 y1 x2 y2 <<<"$(box_of '鈴聲: .*\|Sound: .*' "$o")"
o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; show alarm; @0.3; simknob 3; @0.2; simtap $(((x1 + x2) / 2)) $(((y1 + y2) / 2)); @0.3; uiboxes" 2>&1)
{ [ -n "$x1" ] && ! grep -q "^APP uibox button .*|\($SOUND_ARM\)\$" <<<"$o" && ! grep -q '^APP clock alarm sound=' <<<"$o"; } || { EVID="$(grep '^APP uibox button' <<<"$o" | cut -d'|' -f2 | tr '\n' ' ')"; fail alarm_sound_builtin_no_arm; }
# --- end Task 12 alarm ---
# fortune wheel: 12 long entries: each label is one line, no two label boxes (nor the hub's text and Back) intersect; the hub says 點一下轉
fresh
mkdir -p "$T/fix/fortune"
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do echo "A rather long choice number $i of twelve"; done > "$T/fix/fortune/wheel.txt"
o=$($SIM --headless --root "$T/fix" --cmd "fortune wheel; @0.5; uiboxes" 2>&1)
EVID=$(python3 -c "
import re, sys
b = [(int(m[0]), int(m[1]), int(m[2]), int(m[3]), m[4]) for m in re.findall(r'^APP uibox (?:label|button) (-?\d+) (-?\d+) (-?\d+) (-?\d+) \|(.*)$', sys.stdin.read(), re.M)]
if len(b) < 13: sys.exit('only %d boxes' % len(b))   # 12 labels + the hub text (no Back since GS)
bad = [(a[4], c[4]) for i, a in enumerate(b) for c in b[i + 1:] if a[0] <= c[2] and a[2] >= c[0] and a[1] <= c[3] and a[3] >= c[1]]
tall = [x[4] for x in b if x[3] - x[1] > 30 and 'choice' in x[4]]
sys.exit('intersect: %s tall: %s' % (bad, tall) if bad or tall else 0)" 2>&1 <<<"$o") && EVID= || fail wheel_12_labels_disjoint_one_line
grep -q '^APP uibox label .*|\(點一下轉\|Tap to spin\)$' <<<"$o" || { EVID="$(grep '^APP uibox label' <<<"$o" | tail -3 | cut -d'|' -f2 | tr '\n' ' ')"; fail wheel_hub_text; }
# spacing: gaps between boxes (inclusive pixel coordinates, gap = next.x1 - prev.x2 - 1), read from the pages' own boxes
# (vocabulary home: no buttons left, it is a menu ring since UI unify T7; t7_vocab_ring and t7_boxes_vocab)
# quiz: the bottom answer boxes keep 8 px from Back
o=$($SIM --headless --root "$T/fix" --cmd "vocab start; @0.5; frame hold; uiboxes; vocab quizgeom" 2>&1)
bt=$(back_top "$o"); low=$(grep '^APP vocab quizbox ' <<<"$o" | sed 's/.* box=//' | awk -F, '{print $4}' | sort -n | tail -1)
[ "$(grep -c '^APP vocab quizbox ' <<<"$o")" = 4 ] && [ $((bt - low - 1)) -ge 8 ] || { EVID="Back.y1=$bt answers.y2=$low"; fail quiz_gap_to_back; }
# 抽人: the two buttons keep 8 px apart, Back 8 px below; 新一輪 is "剩 n/m · 重來" and needs two taps within 4 s
fresh
printf '%s\n' 'pick_norep=1' >> "$T/fix/_player/config/app.txt"
o=$($SIM --headless --root "$T/fix" --cmd "tools people; @0.5; tools spin; @3; frame hold; uiboxes" 2>&1)
read -r a1 b1 a2 b2 <<<"$(grep '^APP uibox button' <<<"$o" | grep -v '|\(Back\|返回\)$' | awk '$5 > 200 {print $4, $5, $6, $7}' | sort -n | head -1)"
read -r c1 d1 c2 d2 <<<"$(grep '^APP uibox button' <<<"$o" | grep -v '|\(Back\|返回\)$' | awk '$5 > 200 {print $4, $5, $6, $7}' | sort -n | tail -1)"
bt=$(back_top "$o")
{ [ $((c1 - a2 - 1)) -ge 8 ] && [ $((bt - b2 - 1)) -ge 8 ]; } || { EVID="left=$a1,$a2 right=$c1,$c2 back.y1=$bt b2=$b2"; fail people_button_gaps; }
grep -q '^APP uibox button .*|\(重來 [0-9]*/[0-9]*\|Reset [0-9]*/[0-9]*\)$' <<<"$o" || { EVID="$(grep '^APP uibox button' <<<"$o" | cut -d'|' -f2 | tr '\n' ' ')"; fail people_restart_label; }
tapr="simtap $(((c1 + c2) / 2)) $(((d1 + d2) / 2))"
o=$($SIM --headless --root "$T/fix" --cmd "tools people; @0.5; tools spin; @3; uiboxes; $tapr; @0.3; uiboxes; @5; uiboxes; $tapr; @0.3; $tapr; @0.3; uiboxes" 2>&1)
labs=$(awk '/^APP uibox button/ { split($0, a, "|"); if (a[2] !~ /^(Back|返回)$/ && $4 + 0 > 100 && $5 + 0 > 200) print a[2] }' <<<"$o" | tr '\n' '#')
IFS='#' read -r l_after_draw l_armed l_timeout l_reset _ <<<"$labs"
ARM='再點重來\|Tap again'
n=$(sed -n 's/^[^0-9]*\([0-9]*\)\/\([0-9]*\).*/\2/p' <<<"$l_after_draw")
{ [ -n "$n" ] && grep -q "^\($ARM\)\$" <<<"$l_armed" && [ "$l_timeout" = "$l_after_draw" ] && grep -q "^\(重來 $n/$n\|Reset $n/$n\)\$" <<<"$l_reset" && ! grep -q "^\(重來 $n/$n\|Reset $n/$n\)" <<<"$l_after_draw"; } || { EVID="labels: $labs"; fail people_restart_two_taps; }
# gacha: the collection text is the ring centre (168 px wide): the Collected line + the rates wrapped = three lines at most
o=$($SIM --headless --root "$T/fix" --cmd "fortune gacha; @0.5; uiboxes" 2>&1)
h=$(grep '^APP uibox label .*|\(圖鑑\|Collected\)' <<<"$o" | awk '{print $7 - $5 + 1}')
[ -n "$h" ] && [ "$h" -le 72 ] || { EVID="info label height=$h"; fail gacha_rate_line_two_lines; }
# --- end Task 12 ---

