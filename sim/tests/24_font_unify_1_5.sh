# SPDX-License-Identifier: LGPL-2.1-or-later
# --- font miss record: a glyph no font has is recorded (APP font miss, console fontmiss); plain text is not. Needs the real fonts/ (git-ignored) ---
if [ -d fonts ]; then
    NOWFRESH=helpfresh nowrun fontmiss_recorded "fontmiss; uimsg 繁體中文 音乐 ꀀ ꀁ; @0.3; fontmiss; fontmiss clear; fontmiss" && {
        [ "$(grep '^APP fontmiss' <<<"$out" | sed 's/total=[0-9]*/total=N/' | tr '\n' ' ')" = "APP fontmiss total=N list=- more=0 APP fontmiss total=N list=U+A000,U+A001 more=0 APP fontmiss total=N list=- more=0 APP fontmiss total=N list=- more=0 " ] || { EVID="$(grep '^APP fontmiss' <<<"$out" | tr '\n' ' ')"; fail fontmiss_recorded_list; }
        [ "$(grep -c '^APP font miss U+A00[01]$' <<<"$out")" = 2 ] || { EVID="$(grep '^APP font miss' <<<"$out" | tr '\n' ' ')"; fail fontmiss_printed_once_each; }
        [ "$(grep '^APP fontmiss' <<<"$out" | sed -n 3p)" = "APP fontmiss total=0 list=- more=0" ] || fail fontmiss_clear_total; }
    NOWFRESH=helpfresh nowrun fontmiss_plain_text "uimsg 繁體中文 音乐 设置 ABC 123 …; @0.3; uipush menu; @0.3; fontmiss" && {
        [ "$(grep '^APP fontmiss' <<<"$out")" = "APP fontmiss total=0 list=- more=0" ] && ! grep -q '^APP font miss' <<<"$out" || { EVID="$(grep '^APP font' <<<"$out" | tr '\n' ' ')"; fail fontmiss_plain_text_clean; }; }
    # more than 16 distinct missing characters: the first 16 are kept and printed once, the rest only counted (no print storm on every redraw)
    NOWFRESH=helpfresh nowrun fontmiss_overflow "uimsg ꀀꀁꀂꀃꀄꀅꀆꀇꀈꀉꀊꀋꀌꀍꀎꀏꀐꀑꀒꀓ; @0.3; theme next; @0.3; theme next; @0.3; uipush menu; @0.3; back; @0.3; fontmiss" && {
        [ "$(grep -c '^APP font miss ' <<<"$out")" = 16 ] || { EVID="$(grep -c '^APP font miss ' <<<"$out") lines"; fail fontmiss_overflow_16_prints; }
        l=$(grep '^APP fontmiss' <<<"$out" | tail -1)
        [ "$(sed 's/.*list=\([^ ]*\).*/\1/' <<<"$l" | tr ',' '\n' | wc -l)" = 16 ] && [ "$(sed 's/.*more=//' <<<"$l")" -gt 0 ] || { EVID="$l"; fail fontmiss_overflow_list; }; }
    # invisible characters are not misses (fix9): no-break space = a blank, zero-width / variation selectors = nothing; a real missing one still is
    NOWFRESH=helpfresh nowrun fontmiss_invisible "uimsg $(printf 'A B​C‌‍⁠﻿️︀D'); @0.3; theme next; @0.3; uipush menu; @0.3; back; @0.3; fontmiss" && {
        [ "$(grep '^APP fontmiss' <<<"$out")" = "APP fontmiss total=0 list=- more=0" ] && ! grep -q '^APP font miss' <<<"$out" || { EVID="$(grep '^APP font' <<<"$out" | tr '\n' ' ')"; fail fontmiss_invisible_not_counted; }; }
    NOWFRESH=helpfresh nowrun fontmiss_invisible_real "uimsg $(printf 'A BᗜC'); @0.3; fontmiss" && {
        l=$(grep '^APP fontmiss' <<<"$out" | tail -1)
        grep -q 'list=U+15DC more=0' <<<"$l" && ! grep -q 'total=0' <<<"$l" || { EVID="$l"; fail fontmiss_invisible_real_still_miss; }; }
fi

# --- UI unify T1 ---
# ui_geom.h compiles on its own (only bsp/bsp_geom.h, no IDF paths); its static asserts are really compiled (the selftest macro trips one)
printf '#include "ui_geom.h"\n' | gcc -fsyntax-only -I app/main/ui -I components/jc3636f118_bsp/include -x c - >/dev/null 2>&1 || fail t1_geom_header
printf '#include "ui_geom.h"\n' | gcc -fsyntax-only -DUI_GEOM_SELFTEST_BAD=1 -I app/main/ui -I components/jc3636f118_bsp/include -x c - >/dev/null 2>&1 && fail t1_geom_header_asserts_compiled
# the header never pulls bsp_pins.h; the panel size is defined once, in bsp_geom.h
{ [ ! -e app/main/ui/ui_geom.h ] || [ -z "$(grep -n 'bsp_pins' app/main/ui/ui_geom.h)" ]; } && [ -e app/main/ui/ui_geom.h ] && [ "$(grep -rn 'define BSP_LCD_H_RES' components app --include='*.h' --include='*.c' | grep -v 'managed_components\|fakepod_bsp' | wc -l)" = 1 ] && [ -n "$(grep -l 'define BSP_LCD_H_RES' components/jc3636f118_bsp/include/bsp/bsp_geom.h 2>/dev/null)" ] || fail t1_geom_idf_free

# --- UI unify T2 ---
# ui_lint: its built-in cases (one per rule must report in a page and not in a piece, comments/strings/lv_timer_create/0x360/1360/180.0f must not, the ratchet both ways)
[ "$(python3 tools/check/ui_lint.py --selftest 2>&1 | tail -1 | sed 's/(\([0-9]*\) cases)/(N cases)/')" = "ui_lint selftest: ok (N cases)" ] || { EVID="$(python3 tools/check/ui_lint.py --selftest 2>&1 | tail -3 | tr '\n' '|')"; fail t2_lint_selftest; }
# the real tree passes against ui/ui_lint.allow
python3 tools/check/ui_lint.py >"$T/lint.log" 2>&1 || { EVID="$(tail -3 "$T/lint.log" | tr '\n' '|')"; fail t2_lint_tree; }
grep -q '^ui_lint: ok (' "$T/lint.log" || fail t2_lint_tree_ok_line
# the checker can fail: a copy of the tree with one hand-built button added to scr_meta.c, and one allowance lowered too far
mkdir "$T/lt"; (cd app/main && find . \( -name '*.c' -o -path './ui/*.h' -o -name 'ui_lint.allow' \) -print0 | xargs -0 cp --parents -t "$T/lt")
python3 tools/check/ui_lint.py --root "$T/lt" >/dev/null 2>&1 || fail t2_lint_copy_clean
echo 'static void t2_bad(lv_obj_t *p) { lv_button_create(p); }' >>"$T/lt/ui/scr_meta.c"
python3 tools/check/ui_lint.py --root "$T/lt" >"$T/lint_bad.log" 2>&1; rc=$?
{ [ $rc = 1 ] && grep -q '^ui/scr_meta.c:[0-9]*: R1: ' "$T/lint_bad.log" && grep -q 'ui/scr_meta.c: R1: count 3, allowance 2 (new hand-built UI)' "$T/lint_bad.log"; } || { EVID="rc=$rc $(tail -3 "$T/lint_bad.log" | tr '\n' '|')"; fail t2_lint_catches; }
sed -i 's/^ui\/scr_meta.c\( *page *\)2 /ui\/scr_meta.c\1 3 /' "$T/lt/ui/ui_lint.allow"
cp app/main/ui/scr_meta.c "$T/lt/ui/scr_meta.c"
python3 tools/check/ui_lint.py --root "$T/lt" >"$T/lint_low.log" 2>&1; rc=$?
{ [ $rc = 1 ] && grep -q 'ui/scr_meta.c: R1: count 2, allowance 3 (lower the allowance to 2 in ui_lint.allow)' "$T/lint_low.log"; } || { EVID="rc=$rc $(tail -3 "$T/lint_low.log" | tr '\n' '|')"; fail t2_lint_catches_lower; }

# --- UI unify T3 ---
# the test page "poptest" (console.c): a tap on the page opens a popring: cell 0 armed (two taps), 1 press, 2 bounded axis 0..4, 3 plain
# "popring N" prints "APP popring open= page= cells= armed= centre="; the page prints "APP poptest open t=" and "APP poptest ran <i> v= t= tap="
PT="screen on; vol 0; @0.6; uipush poptest; popring -2"
# the armed cell: the first tap shows the armed text in the centre and marks the cell, the second tap (before 4 s) runs it
nowrun t3_arm_second_tap "$PT; @2; popring 0; @3.9; popring 0" && {
    [ "$(ran "$out" 0)" = 1 ] || { EVID="$(grep '^APP poptest' <<<"$out" | tr '\n' '|')"; fail t3_arm_second_tap; }
    [ "$(pstn 2)" = "open=1 page=0 cells=5 armed=0 centre=Tap again: run" ] || { EVID="$(pst | tr '\n' '|')"; fail t3_arm_first_status; }
    [ "$(pstn 3 | sed 's/ centre=.*//')" = "open=1 page=0 cells=5 armed=-1" ] || { EVID="$(pst | tr '\n' '|')"; fail t3_arm_after_run; }; }
nowrun t3_arm_timeout "$PT; popring 0; @4.1; popring -5; popring 0" && {
    [ "$(ran "$out" 0)" = 0 ] && [ "$(pstn 4 | sed 's/ .*//')" = "open=0" ] || { EVID="$(pst | tr '\n' '|')"; fail t3_arm_timeout; }; }
nowrun t3_arm_other_cell "$PT; popring 0; popring 3; popring 0" && {
    [ "$(ran "$out" 3)" = 1 ] && [ "$(ran "$out" 0)" = 0 ] && [ "$(pstn 4 | sed 's/ cells.*armed=/ armed=/; s/ centre.*//')" = "open=1 page=0 armed=0" ] || { EVID="$(pst | tr '\n' '|')"; fail t3_arm_other_cell; }; }
# a press cell runs at the touch-down (600 ms hold: it ran well before the release), once; cell 1 is at 180 deg: (40,180)
nowrun t3_press "$PT; @1; simpress 40 180 600; @0.3" && {
    to=$(sed -n 's/^APP poptest open t=//p' <<<"$out" | head -1); tr=$(sed -n 's/^APP poptest ran 1 .* t=\([0-9]*\) tap=.*/\1/p' <<<"$out")
    [ "$(ran "$out" 1)" = 1 ] && [ -n "$to" ] && [ -n "$tr" ] && [ $((tr - to)) -ge 1000 ] && [ $((tr - to)) -lt 1050 ] || { EVID="open=$to ran=$tr $(grep '^APP poptest' <<<"$out" | tr '\n' '|')"; fail t3_press; }; }
nowrun t3_press_console_once "$PT; popring 1; popring 1" && { [ "$(ran "$out" 1)" = 2 ] || fail t3_press_console_once; }
# a bounded axis: taps go 0..4 and wrap to 0; the knob clamps. ui_axis_from_tap is 1 for the taps, 0 for the knob
nowrun t3_bounded_tap_wraps "$PT; popring 2; popring 2; popring 2; popring 2; popring 2" && {
    [ "$(grep '^APP poptest ran 2 ' <<<"$out" | sed 's/.* v=\([0-9]*\) .* tap=\([01]\)$/\1\/\2/' | tr '\n' ' ')" = "1/1 2/1 3/1 4/1 0/1 " ] || { EVID="$(grep '^APP poptest ran 2' <<<"$out" | tr '\n' '|')"; fail t3_bounded_tap_wraps; }; }
nowrun t3_bounded_knob_clamps "$PT; popring 2; popring 2; popring 2; popring 2; simknob 3; popring 2; simknob -2" && {
    [ "$(grep '^APP poptest ran 2 ' <<<"$out" | sed 's/.* v=\([0-9]*\) .* tap=\([01]\)$/\1\/\2/' | tr '\n' ' ')" = "1/1 2/1 3/1 4/1 4/0 0/1 0/0 " ] || { EVID="$(grep '^APP poptest ran 2' <<<"$out" | tr '\n' '|')"; fail t3_bounded_knob_clamps; }; }
# ui_axis_step: the one rule for a bounded axis (axisstep <v> <lo> <hi> <step> <detents> <tap 0|1>)
nowrun t3_axis_step "axisstep 95 10 100 5 0 1; axisstep 100 10 100 5 0 1; axisstep 63 10 100 5 0 1; axisstep 10 10 100 5 3 0; axisstep 25 10 100 5 30 0; axisstep 100 10 100 5 -40 0; axisstep 90 20 100 5 -6 0" && {
    [ "$(grep '^APP axisstep ' <<<"$out" | sed 's/APP axisstep //' | tr '\n' ' ')" = "100 10 68 25 100 10 60 " ] || { EVID="$(grep '^APP axisstep' <<<"$out" | tr '\n' '|')"; fail t3_axis_step; }; }
# the playing ring's rule survives: a popring on a page that is popped goes silently, the knob is volume again, nothing leaks
nowrun t3_ring_under_pop "$PT; back; popring -5; uipush poptest; popring -5; simknob 1; simvol" && {
    [ "$(pstn 2 | sed 's/ .*//')" = "open=0" ] && [ "$(pstn 3 | sed 's/ .*//')" = "open=0" ] && [ "$(vols)" = "2 " ] || { EVID="$(pst | tr '\n' '|') vol $(vols)"; fail t3_ring_under_pop; }; }
# the ring open, nothing borrowed: the knob is still volume (the playing ring's rule), and the ring stays
nowrun t3_pop_knob_volume "$PT; popring 3; simknob 1; simvol; popring -5" && {
    [ "$(vols)" = "2 " ] && [ "$(pstn 3 | sed 's/ .*//')" = "open=1" ] || { EVID="$(pst | tr '\n' '|') vol $(vols)"; fail t3_pop_knob_volume; }; }
# a theme change (and a menu-style change) rebuilds every page: the ring is gone, the page stack is the same
nowrun t3_ring_rebuild "$PT; uistack; theme next; @0.3; popring -5; uistack; popring -2; style next; @0.3; popring -5; uistack" && {
    [ "$(pstn 2 | sed 's/ .*//')" = "open=0" ] && [ "$(pstn 4 | sed 's/ .*//')" = "open=0" ] && [ "$(pstn 3 | sed 's/ .*//')" = "open=1" ] || { EVID="$(pst | tr '\n' '|')"; fail t3_ring_rebuild_closed; }
    [ "$(uis)" = "menu poptest|menu poptest|menu poptest|" ] || { EVID="$(uis)"; fail t3_ring_rebuild_stack; }; }
nowrun t3_ring_screen_off "$PT; screen off; popring -5" && { [ "$(pstn 2 | sed 's/ .*//')" = "open=0" ] || { EVID="$(pst | tr '\n' '|')"; fail t3_ring_screen_off; }; }
# a page pushed over the popring page closes the ring
nowrun t3_ring_push_closes "$PT; uipush message; popring -5; back; popring -5" && {
    [ "$(pstn 2 | sed 's/ .*//')" = "open=0" ] && [ "$(pstn 3 | sed 's/ .*//')" = "open=0" ] || { EVID="$(pst | tr '\n' '|')"; fail t3_ring_push_closes; }; }
# the 4 s rule: every tap restarts it, the ring closes 4 s after the last one; Back closes (poptest: no back handler)
nowrun t3_pop_timeout_restart "$PT; @3; popring 3; @3; popring -5; @1.5; popring -5" && {
    [ "$(pstn 3 | sed 's/ .*//')" = "open=1" ] && [ "$(pstn 4 | sed 's/ .*//')" = "open=0" ] || { EVID="$(pst | tr '\n' '|')"; fail t3_pop_timeout_restart; }; }
nowrun t3_pop_back_closes "$PT; popring -3" && { [ "$(pstn 2 | sed 's/ .*//')" = "open=0" ] || { EVID="$(pst | tr '\n' '|')"; fail t3_pop_back_closes; }; }

# T3 fix round 1 (review I1, M1, M2, M3)
# I1: an armed first tap ends the borrowed axis: one cell lit, the knob is volume again (no "ran 2 ... tap=0")
nowrun t3_arm_ends_borrow "$PT; popring 2; popring 0; simknob 1; simvol" && {
    [ "$(grep -c '^APP poptest ran 2 .* tap=0$' <<<"$out")" = 0 ] && [ "$(vols)" = "2 " ] && [ "$(pstn 2 | sed 's/ centre=.*//')" = "open=1 page=0 cells=5 armed=-1" ] && [ "$(pstn 3 | sed 's/ centre=.*//')" = "open=1 page=0 cells=5 armed=0" ] || { EVID="$(grep '^APP poptest ran 2' <<<"$out" | tr '\n' '|') vol $(vols)"; fail t3_arm_ends_borrow; }; }
# M1: opening another def closes the old ring with its borrow: the knob no longer steps the old axis
nowrun t3_new_def_closes_old "$PT; popring 2; poptest2; popring -5; simknob 1; simvol" && {
    [ "$(grep -c '^APP poptest ran 2 .* tap=0$' <<<"$out")" = 0 ] && [ "$(vols)" = "2 " ] && [ "$(pstn 3 | sed 's/ page=.*cells=/ cells=/; s/ armed.*//')" = "open=1 cells=2" ] || { EVID="$(grep '^APP poptest ran 2' <<<"$out" | tr '\n' '|') vol $(vols) $(pst | tr '\n' '|')"; fail t3_new_def_closes_old; }; }
# M2: a def without its own centre text shows Close and the centre closes the ring after pick(-1); with an own centre text pick decides (not tested here)
nowrun t3_close_centre_closes "$PT; poptest2; popring -1; popring -5" && {
    [ "$(grep -c '^APP poptest ran -1 ' <<<"$out")" = 1 ] && [ "$(pstn 2 | sed 's/ .*//')" = "open=0" ] || { EVID="$(grep '^APP poptest' <<<"$out" | tr '\n' '|') $(pst | tr '\n' '|')"; fail t3_close_centre_closes; }; }
# M3: a knob turn on a borrowed axis restarts the ring's 4 s even when the step does not relabel (the poptest step relabels on taps only)
nowrun t3_knob_restarts_4s "$PT; popring 2; @3; simknob 1; @3; popring -5" && {
    [ "$(pstn 3 | sed 's/ .*//')" = "open=1" ] || { EVID="$(pst | tr '\n' '|')"; fail t3_knob_restarts_4s; }; }

# --- UI unify T4 ---
# the page kit (ui_kit.c) on five test pages of console.c (kit .. kit5, composed of shared pieces only); taps print "APP kit <piece> <i>"
# kit: HEAD axis button (3 values "val 0..2", y 68), the big button (centre), progress bar, Back; kit2: 4-cell choice row (y 118) + pill (y 242); kit3: 2x2 grid; kit4: hub; kit5: titled list of marks + note + FOOT pill + dots
# every page's buttons obey the box rules (tools/check/ui_boxes_check.py: >= 48 px, >= 8 px apart, 48 px square of the centre inside the glass; named exceptions inside the checker)
out=$(python3 tools/check/ui_boxes_check.py sim --only kit,kit2,kit3,kit4,kit5 2>&1); rc=$?
{ [ $rc = 0 ] && [ "$(tail -1 <<<"$out")" = "BOXES ok 5 pages" ]; } || { EVID="rc=$rc $(tail -3 <<<"$out" | tr '\n' '|')"; fail t4_boxes_kit; }
# the checker catches what it must: a 40 px high button, two buttons 4 px apart, a button whose centre square leaves the glass; a clean file passes
{ bx "100 100 200 139"; } >"$T/bad_h.txt"
{ bx "100 100 200 147"; bx "110 152 210 199"; } >"$T/bad_gap.txt"
{ bx "326 326 374 374"; } >"$T/bad_glass.txt"
{ bx "100 100 200 147"; bx "100 160 200 207"; } >"$T/good.txt"
for k in h gap glass; do
    o=$(python3 tools/check/ui_boxes_check.py --from-file "$T/bad_$k.txt" 2>&1); rc=$?
    { [ $rc = 1 ] && grep -q '^BOX fail ' <<<"$o"; } || { EVID="$k rc=$rc $o"; fail t4_boxes_catch_$k; }
done
o=$(python3 tools/check/ui_boxes_check.py --from-file "$T/good.txt" 2>&1); rc=$?
{ [ $rc = 0 ] && grep -q '^BOX ok ' <<<"$o"; } || { EVID="rc=$rc $o"; fail t4_boxes_catch_good; }
# the axis button: a tap borrows the knob and steps once (label follows), the knob steps both ways for 4 s, then the knob is volume again
nowrun t4_axis_button "screen on; vol 0; uipush kit; @0.3; uiboxes; simtap 180 68; @0.2; uiboxes; simknob 1; @0.2; uiboxes; simvol; @4.5; simknob 1; simvol; uiboxes" && {
    ax=$(grep '^APP kit axis ' <<<"$out" | sed 's/^APP kit axis //' | tr '\n' ' ')
    lb=$(grep '^APP uibox button .*|val ' <<<"$out" | sed 's/.*|//' | tr '\n' ' ')
    [ "$ax" = "1 2 " ] && [ "$lb" = "val 0 val 1 val 2 val 2 " ] || { EVID="axis=$ax labels=$lb"; fail t4_axis_button; }
    v=($(vols)); [ "${v[0]}" = 0 ] && [ "${v[1]}" -gt 0 ] || { EVID="vols=$(vols)"; fail t4_axis_button_volume; }; }
# a tap on a control prints only its own line; the page's tap is for the bare page
nowrun t4_page_tap_only_page "screen on; vol 0; uipush kit; @0.3; simtap 180 180; @0.2; simtap 40 180; @0.2" && {
    [ "$(grep '^APP kit ' <<<"$out" | tr '\n' '|')" = "APP kit big 0|APP kit page 0|" ] || { EVID="$(grep '^APP kit ' <<<"$out" | tr '\n' '|')"; fail t4_page_tap_only_page; }; }
# the other pieces answer their taps with their index
nowrun t4_kit_taps "screen on; vol 0; uipush kit2; @0.3; simtap 144 118; simtap 288 118; simtap 180 242; back; uipush kit3; @0.3; simtap 104 92; simtap 256 252; back; uipush kit4; @0.3; simtap 180 180; back; uipush kit5; @0.3; simtap 180 150; simtap 180 262" && {
    [ "$(grep '^APP kit ' <<<"$out" | sed 's/^APP kit //' | tr '\n' '|')" = "choice 1|choice 3|pill 0|grid 0|grid 3|hub 0|row 1|pill 0|" ] || { EVID="$(grep '^APP kit ' <<<"$out" | tr '\n' '|')"; fail t4_kit_taps; }; }
# ui_word_knob(): the knob word of the axis ring's hint line, in Chinese (the fonts linked in) and English (no CJK font in the fixture root)
KW="screen on; vol 0; track 3; @3; viz bars; viz menu; knob 1; uiboxes"
fresh; mkdir -p "$T/fix/_player"; ln -s "$PWD/fonts" "$T/fix/_player/fonts"; out=$($SIM --headless --root "$T/fix" --cmd "$KW" 2>&1)
grep -q '^APP uibox label .*|旋鈕＝' <<<"$out" || { EVID="$(grep '^APP uibox label' <<<"$out" | tail -2 | tr '\n' '|')"; fail t4_word_knob_zh; }
nowrun t4_word_knob_en "$KW" && { grep -q '^APP uibox label .*|Knob: ' <<<"$out" || { EVID="$(grep '^APP uibox label' <<<"$out" | tail -2 | tr '\n' '|')"; fail t4_word_knob_en; }; }
# the alarm-ringing hint and the dice hint take the word from the same place
nowrun t4_word_knob_ring_en "screen on; vol 0; alarm 21 31; @61; uiboxes" && { grep -q '^APP uibox label .*Turn the Knob: again in 9 min' <<<"$out" || { EVID="$(grep '^APP uibox label' <<<"$out" | tr '\n' '|')"; fail t4_word_knob_ring_en; }; }
fresh; mkdir -p "$T/fix/_player"; ln -s "$PWD/fonts" "$T/fix/_player/fonts"; out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; alarm 21 31; @61; uiboxes; back; tools dice; @0.5; frame hold; uiboxes" 2>&1)
{ grep -q '^APP uibox label .*轉旋鈕：9 分鐘後再叫' <<<"$out" && grep -q '^APP uibox label .*|點一下或轉旋鈕擲骰' <<<"$out"; } || { EVID="$(grep '^APP uibox label' <<<"$out" | tr '\n' '|')"; fail t4_word_knob_ring_zh; }
# list rows are 48 high and the knob moves exactly one row per detent (row + gap = UI_LIST_STEP): a long song list
# (the first knob after "screen on" only wakes: one detent back first). After 3 detents the 4th row stands where the 1st stood.
for pg in "scan; @1; uipush tracks3"; do   # a level of 100 songs (LIBTREE's "many")
    NOWFRESH=treefresh nowrun t4_list_rows_48 "screen on; vol 0; $pg; @0.3; simknob -1; @0.2; uiboxes clip; simknob 3; @0.2; uiboxes clip" && {
        bad=$(awk '/^APP uiboxes end/{k++} k==0 && /^APP uibox button/ && !/\|(Back|返回)$/ && n < 3 { n++; if ($7 - $5 + 1 != 48) bad = bad " " ($7 - $5 + 1) } END { print bad }' <<<"$out")
        [ -z "$bad" ] || { EVID="$pg: first rows not 48 high:$bad"; fail t4_list_rows_48; }
        r1=$(awk '/^APP uiboxes end/{k++} k==0 && /^APP uibox button/ && !/\|(Back|返回)$/ { n++; if (n == 1) y1 = $5; if (n == 4) { t = substr($0, index($0, "|")); print y1, t; exit } }' <<<"$out")
        y1=${r1%% *}; t=${r1#* }
        y4=$(awk -v t="$t" '/^APP uiboxes end/{k++} k==1 && /^APP uibox button/ && substr($0, index($0, "|")) == t { print $5; exit }' <<<"$out")
        d=$((y1 - ${y4:-999})); [ ${d#-} -le 1 ] || { EVID="$pg: after 3 detents row 4 ($t) is at y=$y4, row 1 was at y=$y1"; fail t4_list_step; }; }
done

# --- UI unify T5 ---
# settings: brightness and volume limit are bounded axes on the shared list (ui_axis_step: the tap wraps at the top, the knob stops); "seti" prints its own APP cfg line, so the first number of cv is the start value
nowrun t5_bright_tap_clamps "seti brightness 95; setui; @0.3; settings key bright; settings key bright; cfg brightness; settings key bright; cfg brightness" && {
    [ "$(cv brightness)" = "95|100|100|" ] || { EVID="brightness $(cv brightness)"; fail t5_bright_tap_clamps; }; }
nowrun t5_bright_odd "seti brightness 63; setui; @0.3; settings key bright; settings key bright; cfg brightness" && { [ "$(cv brightness)" = "63|68|" ] || { EVID="brightness $(cv brightness)"; fail t5_bright_odd; }; }
nowrun t5_bright_knob_clamps "screen on; @0.5; seti brightness 100; setui; @0.3; settings key bright; settings key bright; cfg brightness; simknob 3; cfg brightness; simknob 30; cfg brightness; simknob -40; cfg brightness" && {
    [ "$(cv brightness)" = "100|100|100|100|10|" ] || { EVID="brightness $(cv brightness)"; fail t5_bright_knob_clamps; }; }
# bt-remote fix H: the web's volume limit applies at once: the stored volume and the audio path's volume (wired gain; the headset through the same hook)
# settings move: every key that left the device (or never had a control) is set and read back through config_set / config_get, a value outside its steps is refused
nowrun setmove_web_keys '{"op":"config_set","id":1,"values":{"volume_max":85,"rg":2,"rg_untag":-6,"cover_smooth":1,"cover_auto":0,"haptic_mv":850,"haptic_ms":15,"cast_q":2,"album_sec":60,"album_fade":0,"vocab_new":20,"gapless":0,"dac_bits":16,"ldac_kbps":660,"cstep":0,"font_src":1,"splash":0}}; {"op":"config_get","id":2}; {"op":"config_set","id":3,"values":{"album_sec":45,"dac_bits":24,"ldac_kbps":500,"haptic_mv":0}}' && {
    g=$(grep '^CTL' <<<"$out"); bad=
    grep -q '"id":1.*"changed":17.*"rejected":0.*"reboot":true' <<<"$g" || bad="$bad set1"
    for kv in volume_max:85 rg:2 rg_untag:-6 cover_smooth:1 cover_auto:0 haptic_mv:850 haptic_ms:15 cast_q:2 album_sec:60 album_fade:0 vocab_new:20 gapless:0 dac_bits:16 ldac_kbps:660 cstep:0 font_src:1 splash:0; do
        grep '"id":2' <<<"$g" | grep -q "\"${kv%%:*}\":{\"value\":${kv##*:}[,}]" || bad="$bad get:$kv"
    done
    grep -q '"id":3.*"changed":0.*"rejected":4' <<<"$g" || bad="$bad refuse"
    [ -z "$bad" ] || { EVID="$bad | $(grep '"id":[13]' <<<"$g" | cut -c1-200)"; fail setmove_web_keys; }; }
nowrun setmove_haptic_switch "setui; @0.3; settings key haptic; cfg haptic_on; settings key haptic; cfg haptic_on" && {
    [ "$(cv haptic_on)" = "0|1|" ] || { EVID="haptic_on $(cv haptic_on)"; fail setmove_haptic_switch; }; }
nowrun t_volmax_web_applies 'seti volume_max 100; vol 80; {"op":"config_set","id":1,"values":{"volume_max":60}}; simvol; simaudiovol' && {
    [ "$(vols)" = "60 " ] && grep -q '^SIM audiovol 60$' <<<"$out" || { EVID="vol $(vols) $(grep '^SIM audiovol' <<<"$out")"; fail t_volmax_web_applies; }; }
# every value row changes by a tap alone (sound: the fixture has no preset; USB, help, rescan are actions): its label differs after the tap. The row count R comes from the first dump
# (APP settings rows=<R>), so a task that adds or removes a row changes no line here; the 說明 row is not in the dump
for k in sleep theme style bright haptic; do
    sk=$k; case $k in sleep|theme|bright) sk="$k; settings key $k";; esac   # a multi-value row: the first tap only selects it
    nowrun t5_settings_row_$k "setui; @0.3; settings group $(key_group $k); settings rows; settings key $sk; settings rows" && {
        R=$(grep -m1 '^APP settings rows=' <<<"$out" | sed 's/.*rows=//')
        d=$(grep '^APP settings row=' <<<"$out" | awk -v r="${R:-0}" 'NR <= r { a[NR] = $0; next } NR <= 2 * r && a[NR - r] != $0 { n++ } END { print n + 0 }')
        pre=$(case $k in sleep) echo 'Sleep: ';; theme) echo 'Theme: ';; style) echo 'Menu style: ';; bright) echo 'Brightness: ';; haptic) echo 'Haptics: ';; esac)
        w=$(grep '^APP settings row=' <<<"$out" | awk -v r="${R:-0}" 'NR <= r { a[NR] = $0; next } NR <= 2 * r && a[NR - r] != $0 { print substr(a[NR - r], 18) }')   # the first-dump text of the row that changed
        [ "${R:-0}" -gt 0 ] && [ "$d" = 1 ] && [ "${w#"$pre"}" != "$w" ] || { EVID="key $k: R=$R, rows changed by the tap: $d, the changed one: '$w' (want prefix '$pre')"; fail t5_settings_no_knob_only; }; }
done
# alarm: 時 and 分 rows; the tap steps, the knob steps both ways for 4 s, then the knob scrolls the list again
nowrun t5_alarm_rows "alarm 7 10; show alarm; @0.3; uiboxes" && AUX="$out"
read -r hx1 hy1 hx2 hy2 <<<"$(box_of '時: .*\|Hour: .*' "$AUX")"; read -r mx1 my1 mx2 my2 <<<"$(box_of '分: .*\|Minute: .*' "$AUX")"
nowrun t5_alarm_hour "screen on; @0.5; alarm 7 10; show alarm; @0.3; simtap $(((hx1 + hx2) / 2)) $(((hy1 + hy2) / 2)); @0.2; cfg alarm_min; simknob -9; cfg alarm_min; uiboxes; @4.5; uiboxes; simknob 2; @0.2; uiboxes" && {
    [ "$(cv alarm_min)" = "490|1390|" ] || { EVID="alarm_min $(cv alarm_min)"; fail t5_alarm_hour; }
    d1=$(sed -n '1,/^APP uiboxes end/p' <<<"$out"); d2=$(sed '1,/^APP uiboxes end/d' <<<"$out" | sed -n '1,/^APP uiboxes end/p'); d3=$(sed '1,/^APP uiboxes end/d' <<<"$out" | sed '1,/^APP uiboxes end/d')
    grep -q '^APP uibox button .*|\(時: 23\|Hour: 23\)$' <<<"$d1" || { EVID="$(grep '^APP uibox button' <<<"$d1" | cut -d'|' -f2 | tr '\n' ' ')"; fail t5_alarm_hour_label; }
    { [ "$(hy "$d1")" = "$(hy "$d2")" ] && [ -n "$(hy "$d3")" ] && [ "$(hy "$d3")" != "$(hy "$d2")" ]; } || { EVID="hour row y: $(hy "$d1") $(hy "$d2") $(hy "$d3") (still while borrowed, scrolled after 4.5 s)"; fail t5_alarm_hour_scrolls_after; }; }
nowrun t5_alarm_min_snap "screen on; @0.5; alarm 21 31; show alarm; @0.3; simtap $(((mx1 + mx2) / 2)) $(((my1 + my2) / 2)); cfg alarm_min; simknob -1; cfg alarm_min; alarm 21 55; simknob 1; cfg alarm_min" && {
    [ "$(cv alarm_min)" = "1295|1290|1260|" ] || { EVID="alarm_min $(cv alarm_min)"; fail t5_alarm_min_snap; }; }
nowrun t5_alarm_min_down_snap "screen on; @0.5; alarm 21 31; show alarm; @0.3; alarm tap 2; cfg alarm_min; alarm 21 32; simknob -2; cfg alarm_min; alarm 21 0; simknob -1; cfg alarm_min" && {
    [ "$(cv alarm_min)" = "1295|1285|1315|" ] || { EVID="alarm_min $(cv alarm_min)"; fail t5_alarm_min_down_snap; }; }   # 32 -2 -> 25 (the first step aligns to 30); 00 -1 -> 55 in the same hour

