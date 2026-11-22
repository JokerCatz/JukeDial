# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: need_d6; need_dc)
need_d6; need_dc
# --- UI unify T8 ---
# Back is one button everywhere: 88 x 48, centre 138 below the screen centre (y 294..341 on the 360 screen); the hub Back (wheel, ten-pull) is the one named exception, 62 below the centre
nowrun t8_back_48 "setui; @0.3; uiboxes" && {
    read -r x1 y1 x2 y2 <<<"$(bkbox "$out")"
    [ -z "$(bkbox "$out" | tr -d ' ')" ] || { EVID="back box $(bkbox "$out")"; fail t8_back_48; }; }   # GS: no Back button on a list page
# every page of the shot set: tap targets >= 48, 8 px apart, inside the glass, and exactly one Back in its place (the pages without one are named in the checker, with a reason)
out=$(python3 tools/check/ui_boxes_check.py sim 2>&1); rc=$?
{ [ $rc = 0 ] && [ "$(tail -1 <<<"$out")" = "BOXES ok $((125 + GAME_PICS)) pages" ]; } || { EVID="rc=$rc $(grep -v '^BOX ok' <<<"$out" | head -4 | tr '\n' '|')"; fail t8_boxes_all; }
# GS: the checker's Back rule: any visible Back button fails a page, none passes
bk() { printf 'APP uibox button %s %s %s %s %s %s %s %s |Back\n' "$1" "$2" "$3" "$4" "$1" "$2" "$3" "$4"; }
{ bk 136 294 223 341; } >"$T/back_good.txt"; { printf 'APP uibox button 20 100 120 148 20 100 120 148 |Play\n'; } >"$T/back_none.txt"
o=$(python3 tools/check/ui_boxes_check.py --from-file "$T/back_none.txt" --check-back 2>&1); rc=$?
{ [ $rc = 0 ] && grep -q '^BOX ok ' <<<"$o"; } || { EVID="rc=$rc $o"; fail t8_boxes_back_good; }
o=$(python3 tools/check/ui_boxes_check.py --from-file "$T/back_good.txt" --check-back 2>&1); rc=$?
{ [ $rc = 1 ] && grep -q '^BOX fail .*: back ' <<<"$o"; } || { EVID="rc=$rc $o"; fail t8_boxes_back_visible; }
# the hub Back: 48 high, centre 62 px below the screen centre (y 242 +-1)
nowrun t8_hub_back "uipush fortune; fortune wheel; @0.5; uiboxes" && {
    read -r x1 y1 x2 y2 <<<"$(bkbox "$out")"
    [ -z "$(bkbox "$out" | tr -d ' ')" ] || { EVID="hub back $x1 $y1 $x2 $y2"; fail t8_hub_back; }; }   # GS: no Back in the hub either
# the ring that cannot be built (no memory: ring_t is about 296 bytes, 256 stops it) leaves a bare Back, the same 88 x 48 in the same place, and a tap on it pops
nowrun t8_ring_fallback_back "simallocfail 256; uipush colors; @0.3; ring; uiboxes; simallocfail 0; simflick up; @0.3; uistack" && {
    read -r x1 y1 x2 y2 <<<"$(bkbox "$out")"
    grep -qx 'APP ring none' <<<"$out" && [ "$(us)" = "menu|" ] || { EVID="ring=$(grep '^APP ring' <<<"$out") back $x1 $y1 $x2 $y2 stack=$(us)"; fail t8_ring_fallback_back; }; }
# a long press on the Back button (its centre, 318) is still home
nowrun t8_long_back_home "setui; @0.3; simpress 180 318 3100; @0.3; uistack" && { [ "$(us)" = "menu|" ] || { EVID="$(us)"; fail t8_long_back_home; }; }
# the y-variants are gone (spec 2.3): no caller and no declaration
! grep -rn 'ui_back_button_at\|ui_back_button_cb' app/main --include='*.c' --include='*.h' >"$T/yvar.txt" || { EVID="$(head -3 "$T/yvar.txt" | tr '\n' '|')"; fail t8_no_y_variants; }

# --- UI unify batch A fix ---
# pages-m1: "bt tap" takes -1 (the centre) only
nowrun fixA_bt_tap_arg "btui; @0.3; bt tap 2; bt tap 9; bt tap -2; cfg bt_mode" && {
    [ "$(grep -c '^APP cmd unknown$' <<<"$out")" = 3 ] && [ "$(nrb)" = 0 ] && [ "$(cv bt_mode)" = "|" ] || { EVID="unknown $(grep -c '^APP cmd unknown$' <<<"$out") reboots $(nrb) bt_mode $(cv bt_mode)"; fail fixA_bt_tap_arg; }; }
# pages-m3: an axis button whose step is refused (the dice tumble) keeps no borrow: the knob rolls the dice as it does without one
NOWCFG=dice_n=1 nowrun fixA_axb_inert_no_borrow "screen on; @0.5; tools dice; @0.3; simtap 180 200; @0.1; simtap $DC; @1.5; simknob 1; @1.5; tools stat" && {
    [ "$(sn rolls | tr -d ' ')" = 2 ] && [ "$(sn dice_n | tr -d ' ')" = 1 ] || { EVID="rolls $(sn rolls) dice_n $(sn dice_n)"; fail fixA_axb_inert_no_borrow; }; }
# pages-m4: the gacha with an empty library has no pull cells (Back stays) and says so
NOWFRESH=efresh nowrun fixA_gacha_empty "screen on; @0.5; fortune gacha; @0.3; ring; uiboxes; simtap 81 81; simtap 180 180; @0.3; uitop" && {
    [ "$(rn)" = "0 " ] && [ "$(rg)" = "page=0/0 sel=-1 center=The library is empty|" ] && [ "$(grep '^APP uitop' <<<"$out" | tail -1)" = "APP uitop fortune_gacha" ] || { EVID="n=$(rn) $(rg)"; fail fixA_gacha_empty; }; }
# pages-m5: scrolling the scene page cancels an armed delete row
nowrun fixA_scene_scroll_disarms "$T6P; simknob 9; @0.2; simtap $D6; @0.3; uiboxes; simknob -1; uiboxes" && {
    ! grep -q '^APP uibox button .*|\(再點一次刪除\|Tap again to delete\)$' <<<"$(awk '/^APP uiboxes end/{k++; next} k==1' <<<"$out")" && grep -q '^APP uibox button .*|\(再點一次刪除\|Tap again to delete\)$' <<<"$(awk '/^APP uiboxes end/{exit} {print}' <<<"$out")" || { EVID="$(rows6 1)|$(rows6 2)"; fail fixA_scene_scroll_disarms; }; }
# pages-m6: the pairing page pops when the prompt ends elsewhere; with no memory for its ring a bare Back rejects the prompt
nowrun fixA_pair_ends_elsewhere "screen on; @0.5; simbt pair 123456; uipush pair; @0.3; simbt pair off; @1.5; uistack" && { [ "$(us)" = "menu|" ] || { EVID="$(us)"; fail fixA_pair_ends_elsewhere; }; }
nowrun fixA_pair_ring_fallback "screen on; @0.5; simbt pair 123456; simallocfail 256; uipush pair; @0.3; ring; uiboxes; simallocfail 0; simflick up; @0.5; uistack" && {
    read -r x1 y1 x2 y2 <<<"$(bkbox "$out")"
    grep -qx 'APP ring none' <<<"$out" && grep -qx 'SIM bt pair reply 0' <<<"$out" && [ "$(us)" = "menu message|" ] || { EVID="ring=$(grep '^APP ring' <<<"$out") back $x1 $y1 $x2 $y2 $(grep '^SIM bt' <<<"$out" | tr '\n' '|') stack=$(us)"; fail fixA_pair_ring_fallback; }; }
# T3-M5: an armed cell whose text cannot be copied (no memory for a 15 byte string, at the open and at the first tap: the text is asked again then, batch C fix M-1) still needs two taps
nowrun fixA_arm_no_memory "screen on; vol 0; @0.6; uipush poptest; simallocfail 15 15; popring -2; popring 0; simallocfail 0; popring 0" && {
    [ "$(pstn 2 | sed 's/ centre=.*//')" = "open=1 page=0 cells=5 armed=0" ] && [ "$(pstn 2 | sed 's/.*centre=//')" = "Tap again" ] && [ "$(ran "$out" 0)" = 1 ] || { EVID="$(pst | tr '\n' '|') ran $(ran "$out" 0)"; fail fixA_arm_no_memory; }; }
# T3-M6: a cell run through ui_popring_pick (the console of the pages that use it) goes through the ring's tap path (an armed cell asks for its second tap):
# the t3_arm_* tests above reach it through "popring N", which is ui_popring_pick
# T3-M8: a stored value below the axis' range is clamped before the tap steps it
nowrun fixA_axis_tap_below_lo "seti brightness 7; setui; @0.3; settings key bright; settings key bright; cfg brightness" && { [ "$(cv brightness)" = "7|12|" ] || { EVID="brightness $(cv brightness)"; fail fixA_axis_tap_below_lo; }; }
nowrun fixA_axis_tap_above_hi "seti brightness 140; setui; @0.3; settings key bright; settings key bright; cfg brightness" && { [ "$(cv brightness)" = "140|100|" ] || { EVID="brightness $(cv brightness)"; fail fixA_axis_tap_above_hi; }; }
# kit-m5: a choice row keeps its cells UI_TAP_MIN wide, the unset tail of a grid's cells[] is NULL, a plain list has no title
nowrun fixA_kit_edges "uipush kit6; @0.3; uiboxes" && {
    [ "$(grep '^APP kit6 ' <<<"$out")" = "APP kit6 row=5 tail_null=1 title=0" ] || { EVID="$(grep '^APP kit6' <<<"$out")"; fail fixA_kit_edges; }
    narrow=$(grep '^APP uibox button ' <<<"$out" | awk '$6 - $4 + 1 < 48 {print}'); [ -z "$narrow" ] || { EVID="$narrow"; fail fixA_kit_edges_width; }; }
# kit-m2: the box checker's exceptions are as wide as their reasons (two stacked 120 wide buttons 4 px apart are not a list; the dice waiver is not for Back)
{ bx "100 100 219 147"; bx "100 151 219 198"; } >"$T/gap_nolist.txt"
o=$(python3 tools/check/ui_boxes_check.py --from-file "$T/gap_nolist.txt" 2>&1); rc=$?
{ [ $rc = 1 ] && grep -q '^BOX fail .*: gap ' <<<"$o"; } || { EVID="rc=$rc $o"; fail fixA_boxes_list_rows_width; }
{ bk 136 302 223 341; } >"$T/fortune_dice.txt"
o=$(python3 tools/check/ui_boxes_check.py --from-file "$T/fortune_dice.txt" --check-back 2>&1); rc=$?
{ [ $rc = 1 ] && grep -q '^BOX fail fortune_dice: size ' <<<"$o"; } || { EVID="rc=$rc $o"; fail fixA_boxes_dice_back; }
# the ten-pull (hub Back) and the ringing alarm are in no shot (the pull saves the card collection, the alarm rings the board): their boxes are checked here
nowrun fixA_boxes_ten "screen on; @0.5; vol 0; uipush fortune; fortune ten; @3; uiboxes clip" && {
    grep '^APP uibox ' <<<"$out" >"$T/fortune_ten.txt"
    o=$(python3 tools/check/ui_boxes_check.py --from-file "$T/fortune_ten.txt" --check-back 2>&1); rc=$?
    { [ $rc = 0 ] && grep -q '^BOX ok fortune_ten' <<<"$o"; } || { EVID="rc=$rc $(grep -v '^BOX ok' <<<"$o" | head -3 | tr '\n' '|')"; fail fixA_boxes_ten; }; }
nowrun fixA_boxes_ring "screen on; vol 0; alarm 21 31; @61; uitop; uiboxes clip" && {
    grep '^APP uibox ' <<<"$out" >"$T/ring.txt"
    o=$(python3 tools/check/ui_boxes_check.py --from-file "$T/ring.txt" --check-back 2>&1); rc=$?
    { [ $rc = 0 ] && grep -q '^BOX ok ring' <<<"$o" && grep -q '^APP uitop ring$' <<<"$out"; } || { EVID="rc=$rc $o"; fail fixA_boxes_ring; }; }
# pages-I2: the board run skips the pages whose commands only the simulator has; no other page of the set uses a sim command
python3 - <<'PY' || fail fixA_baseline_sim_only
import sys; sys.path[:0] = ["tools", "tools/simtools", "tools/gen", "tools/check", "tools/board"]
import ui_baseline
class Board:                                   # no page_start: the board's Con
    def __init__(s): s.sent = []
    def cmd(s, c, w=1.0): s.sent.append(c); return b"APP cfg %s=\n" % c[4:].encode() if c.startswith("cfg ") else b""   # cfg_keep reads each kept setting
    def drain(s, n): pass
    def shot(s, p): s.sent.append("shot " + p)
    def home(s, r): pass
for n in ("tools_tuner_listen", "btaudio_idle", "btaudio_aac", "btaudio_aac_end", "wired_page", "wired_play", "meta_out_wired", "meta_out_bt", "tool_counter", "tool_counter8", "clock_drink", "clock_habits", "scene_custom", "scene_layer_ring", "scene_delete_armed", "pair", "status_wired", "ratetest_tier", "ratetest_done",
          "settings_card", "thumbcache_page", "thumbcache_page_paused", "card_gpt", "cardfmt_warn", "cardfmt_code", "cardfmt_arm", "cardfmt_run", "cardfmt_done", "speaker_meta", "cardtest_idle", "cardtest_done", "cardtest_ladder"):
    b = Board(); ui_baseline.pages(b, "menu", "/tmp", {n})
    assert not [x for x in b.sent if x.startswith("sim") or n in x], (n, b.sent)
assert ui_baseline.SIM_ONLY == {"tools_tuner_listen", "btaudio_idle", "btaudio_aac", "btaudio_aac_end", "wired_page", "wired_play", "meta_out_wired", "meta_out_bt", "tool_counter", "tool_counter8", "clock_drink", "clock_habits", "hud_cd_last10", "hud_sw_laps", "hud_pomo", "scene_custom", "scene_layer_ring", "scene_delete_armed", "pair", "status_wired", "bt_p2", "ratetest_tier", "ratetest_done",
                               "settings_card", "thumbcache_page", "thumbcache_page_paused", "card_gpt", "cardfmt_warn", "cardfmt_code", "cardfmt_arm", "cardfmt_run", "cardfmt_done", "speaker_meta", "cardtest_idle", "cardtest_done", "cardtest_ladder",
                               "nowfp_a", "nowfp_a_paused", "nowfp_a_long", "nowfp_a_nocover", "nowfp_b", "nowfp_b_paused", "nowfp_b_long", "nowfp_b_nocover", "nowfp_b_hidden", "nowfp_a_sleep", "nowfp_a_noise", "nowfp_grid"} | set(ui_baseline.LANG_PICS) | set(ui_baseline.LANG_INST_PICS)
b = Board(); ui_baseline.inst_pages(b, "/tmp")       # batch B fix S-m1: the board's instrument run sends no simulator command and no virtual-time wait
assert not [x for x in b.sent if x.startswith("sim") or x.startswith("@")], b.sent
PY
bad=$(grep -E '"sim(tap|knob|noise|bt|allocfail|inst)' tools/simtools/ui_baseline.py | grep -Ev 'snap\("(scene_custom|scene_layer_ring|scene_delete_armed|pair|speaker_meta|btaudio_idle|btaudio_aac|btaudio_aac_end|meta_out_bt)"' | grep -v '^SIM_ONLY\|^# \|if hasattr(c, "page_start") else\|for st, nm, play in')   # the last: the FakePod-only now-playing pictures (if FP: they only run in the simulator) ; [ -z "$bad" ] || { EVID="$bad"; fail fixA_baseline_no_other_sim_cmd; }
# kit-m3: the geometry asserts hold on a square panel too (a fake 460 square bsp_geom.h)
mkdir -p "$T/fb/bsp"; printf '#pragma once\n#define BSP_LCD_H_RES 460\n#define BSP_LCD_V_RES 460\n#define BSP_LCD_ROUND 0\n#define BSP_HAS_KNOB 0\n' >"$T/fb/bsp/bsp_geom.h"
printf '#include "ui_geom.h"\n' | gcc -fsyntax-only -I app/main/ui -I "$T/fb" -x c - >/dev/null 2>&1 || fail fixA_geom_square
# pages-m7: the slot score is a pill whose text goes through the kit (no lv_ call, not the choice setter on a pill)
! grep -n 'ui_choice_set(s_score' app/main/ui/scr_tools.c >/dev/null || fail fixA_slot_score_setter
# pages-m10: the alarm rows read "時: 07" / "分: 30" like every other row (zh, with the real fonts when the checkout has them)
helpfresh; out=$($SIM --headless --root "$T/fix" --cmd "alarm 7 30; show alarm; @0.3; uiboxes" 2>&1)
if [ -s fonts/ui-tc.ttf ]; then grep -q '^APP uibox button .*|時: 07$' <<<"$out" && grep -q '^APP uibox button .*|分: 30$' <<<"$out" || { EVID="$(grep '^APP uibox button' <<<"$out" | cut -d'|' -f2 | tr '\n' ' ')"; fail fixA_alarm_colon_zh; }
else grep -q '^APP uibox button .*|Hour: 07$' <<<"$out" || fail fixA_alarm_colon_en; fi

# --- UI unify T9 ---
# the instrument app in the simulator: --mode inst boots inst_ui_start (a data root without fonts is enough: the labels are plain text)
mkdir -p "$T/t9root"
# t9_inst_boot: the app starts, a page switch answers on the console and a picture is made, no sanitizer line
out=$(t9 "inst page 0; @0.5; shot $T/i0.png"); rc=$?
{ [ $rc = 0 ] && grep -qx 'INST ui page=0' <<<"$out" && [ -s "$T/i0.png" ] && ! grep -q 'Sanitizer\|runtime error' <<<"$out"; } || { EVID="rc=$rc $(head -3 <<<"$out" | tr '\n' '|')"; fail t9_inst_boot; }
# t9_inst_pages: the start shows page 0 (one line), then every page 0..5 answers with its own line
out=$(t9 "inst page 0; inst page 1; inst page 2; inst page 3; inst page 4; inst page 5; @0.3")
got=$(grep -o '^INST ui page=[0-9]*$' <<<"$out" | tr '\n' ' ')
[ "$got" = "INST ui page=0 INST ui page=0 INST ui page=1 INST ui page=2 INST ui page=3 INST ui page=4 INST ui page=5 " ] || { EVID="$got"; fail t9_inst_pages; }
# t9_inst_note: a note from the fake engine reaches the play page's note label (it reads "-" until one was played); uiboxes works in this mode
out=$(t9 "inst page 0; @0.3; uiboxes; siminst note 60 100; @0.2; uiboxes")
first=$(awk '/^APP uiboxes end/{exit} /^APP uibox label /' <<<"$out"); second=$(awk '/^APP uiboxes end/{k++; next} k==1 && /^APP uibox label /' <<<"$out")
{ grep -q '^APP uibox label .*|-$' <<<"$first" && ! grep -q '^APP uibox label .*|-$' <<<"$second" && grep -q '^APP uibox label .*|C4$' <<<"$second" && grep -qx 'SIM inst note on 60' <<<"$out"; } || { EVID="first=$(cut -d'|' -f2 <<<"$first" | tr '\n' ' ') second=$(cut -d'|' -f2 <<<"$second" | tr '\n' ' ')"; fail t9_inst_note; }
# t9_inst_exit: the console's inst exit says it, the shim reports the restart and the simulator goes on
out=$(t9 "inst exit; uitop"); rc=$?
{ [ $rc = 0 ] && grep -qx 'INST exit to player' <<<"$out" && grep -qx 'SIM restart' <<<"$out" && grep -q '^APP uitop ' <<<"$out"; } || { EVID="rc=$rc $(head -5 <<<"$out" | tr '\n' '|')"; fail t9_inst_exit; }
# t9_mode_bad: an unknown mode is a usage error (usb: T12, safe: T13 made them real)
out=$($SIM --headless --mode nope --root "$T/t9root" 2>&1); rc=$?
{ [ $rc = 2 ] && grep -q '^usage: sim .*--mode player|inst|ext|cast|safe' <<<"$out"; } || { EVID="rc=$rc $out"; fail t9_mode_bad; }

# --- UI unify T10 ---
# the instrument on the shared page stack (ui_host_t): the play page with its ring, the chips and kits lists, one settings list; the fixture has two kits (demo, tabla)
mk_t10root
# t10_stack: the play page is the bottom of the stack; a tap on it opens its ring with six cells
out=$(t10 "uistack; popring -2")
{ grep -qx 'APP uistack inst_play' <<<"$out" && grep -q '^APP popring open=1 .*cells=6 ' <<<"$out" && ! t10bad; } || { EVID="$(grep '^APP uistack\|^APP popring' <<<"$out" | tr '\n' '|')"; fail t10_stack; }
# t10_chip_axis (Q1): the chip cell is an axis; two taps step two chips, applied 350 ms after the last step; then the knob steps it for 4 s; after that the knob is the volume
out=$(t10 "inst page 4; inst uistat; inst page 0; uitop")
c0=$(cur_of "$out"); nch=11
out=$(t10 "popring -2; popring 3; popring 3; @0.2; uiboxes; uitop; @0.4; uitop; simknob 1; @0.1; uiboxes; @0.5; uitop; @4.5; simknob 1; @0.5; uitop")
lb() { ph "$1" | grep '^APP uibox label ' | cut -d'|' -f2- | sort | tr '\n' ' '; }
{ [ -n "$c0" ] && ! ph 0 | grep -q '^SIM inst chip' && [ "$(ph 1 | grep '^SIM inst chip')" = "SIM inst chip $(( (c0 + 2) % nch ))" ] \
  && [ "$(ph 2 | grep '^SIM inst chip')" = "SIM inst chip $(( (c0 + 3) % nch ))" ] && ! ph 2 | grep -q '^SIM inst vol' && [ "$(lb 0)" != "$(lb 2)" ] \
  && [ "$(ph 3 | grep -c '^SIM inst vol ')" = 1 ] && ! ph 3 | grep -q '^SIM inst chip' && ! t10bad; } || { EVID="c0=$c0 $(grep '^SIM inst' <<<"$out" | tr '\n' '|')"; fail t10_chip_axis; }
# t10_exit_armed (Q4): the last cell asks for a second tap (system colour), the second tap leaves for the player; after the ring's 4 s a tap is a first tap again (here: no ring)
out=$(t10 "popring -2; popring 5; popring 5")
{ grep -q '^APP popring open=1 .*armed=5 centre=\(再點一次：重開機回播放器\|Tap again: reboot into the player\)$' <<<"$out" && grep -qx 'INST exit to player' <<<"$out" && grep -qx 'SIM restart' <<<"$out"; } || { EVID="$(grep '^APP popring\|^INST exit\|^SIM restart' <<<"$out" | tr '\n' '|')"; fail t10_exit_armed; }
out=$(t10 "popring -2; popring 5; @4.1; popring 5")
{ ! grep -q '^INST exit' <<<"$out" && ! grep -q '^SIM restart' <<<"$out" && grep -q '^APP popring .*armed=5 ' <<<"$out"; } || { EVID="$(grep '^APP popring\|^INST exit' <<<"$out" | tr '\n' '|')"; fail t10_exit_armed_timeout; }
# t10_chips_list: ring -> settings -> the chip row -> the chips list; the current chip's row is read from inst uistat (cur=), a tap on another row chooses it and pops
out=$(t10 "popring -2; popring 4; uistack; inst row 0; uistack; inst uistat")
c1=$(cur_of "$out"); k=$(( (${c1:-0} + 1) % nch ))
s1=$(grep '^APP uistack' <<<"$out" | tr '\n' '|')
out=$(t10 "popring -2; popring 4; inst row 0; inst row $k; @0.1; uistack; inst uistat")
{ [ "$s1" = "APP uistack inst_play inst_set|APP uistack inst_play inst_set inst_chips|" ] && [ "$c1" = "$c0" ] && grep -qx "SIM inst chip $k" <<<"$out" \
  && grep -qx 'APP uistack inst_play inst_set' <<<"$out" && [ "$(cur_of "$out")" = -1 ] && ! t10bad; } || { EVID="s1=$s1 c0=$c0 c1=$c1 k=$k $(grep '^APP uistack\|^SIM inst chip' <<<"$out" | tr '\n' '|')"; fail t10_chips_list; }
# t10_kits_list: the kit row -> "none" + the fixture's two kits, the loaded one ("demo", the default) is current; another kit row loads it (the list says so) and pops once it is ready
out=$(t10 "inst page 3; inst row 5; uistack; uiboxes; uitop; inst uistat; inst row 2; uiboxes; uitop; @0.3; uistack")
rows=$(ph 0 | grep '^APP uibox button ' | cut -d'|' -f2 | grep -v '^\(返回\|Back\)$' | tr '\n' ',')
{ grep -qx 'APP uistack inst_play inst_set inst_kits' <<<"$out" && [ "$rows" = "無,demo,tabla," -o "$rows" = "None,demo,tabla," ] && [ "$(cur_of "$(ph 1)")" = 1 ] \
  && ph 1 | grep -q '^APP uibox label .*|\(載入中…\|Loading…\)$' && ph 1 | grep -qx "INST ui kit='tabla'" \
  && [ "$(ph 2 | grep '^APP uistack')" = 'APP uistack inst_play inst_set' ] && ! t10bad; } || { EVID="rows=$rows $(grep '^APP uistack\|^INST ui kit\|^INST uistat' <<<"$out" | tr '\n' '|')"; fail t10_kits_list; }
# t10_long_back: a long press on Back in the chips list goes to the bottom of this mode's stack, the play page
out=$(t10 "inst page 4; uistack; simpress 180 318 3100; @0.3; uistack")
{ [ "$(grep '^APP uistack' <<<"$out" | tr '\n' '|')" = "APP uistack inst_play inst_chips|APP uistack inst_play|" ] && ! t10bad; } || { EVID="$(grep '^APP uistack' <<<"$out" | tr '\n' '|')"; fail t10_long_back; }
# t10_notes_during_push: the note label of the play page shows the last note after pages were pushed over it and popped
out=$(t10 "inst page 3; siminst note 60 100; inst page 0; siminst note 64 100; inst page 4; inst page 0; @0.1; uiboxes")
{ grep -q '^APP uibox label .*|E4$' <<<"$out" && ! grep -q '^APP uibox label .*|C4$' <<<"$out" && ! t10bad; } || { EVID="$(grep '^APP uibox label' <<<"$out" | cut -d'|' -f2 | tr '\n' ' ')"; fail t10_notes_during_push; }
# t10_popring_dim: the console override of the dim layer (debug, not saved): 1 draws it, 0 does not, auto = the ring's own (the instrument: none)
out=$(t10 "popring dim 1; popring -2; @0.1; shot $T/d1.png; popring -3; popring dim 0; popring -2; @0.1; shot $T/d0.png; popring -3; popring dim auto; popring -2; @0.1; shot $T/da.png")
{ grep -qx 'APP popring dim=1' <<<"$out" && grep -qx 'APP popring dim=auto' <<<"$out" && [ -s "$T/d1.png" ] && ! cmp -s "$T/d1.png" "$T/d0.png" && cmp -s "$T/d0.png" "$T/da.png"; } || { EVID="$(grep '^APP popring dim' <<<"$out" | tr '\n' '|')"; fail t10_popring_dim; }
# t10_boxes_inst: every instrument picture page passes the tap-target rules (the play page and its ring have no Back button: the ring's cell is Back; T11: 21 pages, 24 with the three 簡中 width pictures (i18n B) with pads, metronome, looper)
out=$(python3 tools/check/ui_boxes_check.py sim --mode inst 2>&1); rc=$?
{ [ $rc = 0 ] && [ "$(tail -1 <<<"$out")" = "BOXES ok 24 pages" ]; } || { EVID="rc=$rc $(grep -v '^BOX ok' <<<"$out" | head -4 | tr '\n' '|')"; fail t10_boxes_inst; }

# --- UI unify T11 ---
# pads, metronome and looper on shared pieces (inst_pads, tap-revealed rings, choice row, big button); the looper's more list and file ring. Metro ring cells:
# 0 速度 (bounded axis 40..240), 1 拍號, 2 Tap (press cell, at 225 deg: 81 81), 3 時脈, 4 節拍聲; looper ring: 0 播放/停止, 1 靜音, 2 清除這軌 (armed), 3 復原, 4 更多
mk_t11root
# t11_pads: a pad tap (console) on the pads page is one note on and one note off of the same note
out=$(t11 "@0.2; inst page 1; inst pad 3; @0.1")
on=$(grep '^SIM inst note on ' <<<"$out"); off=$(grep '^SIM inst note off ' <<<"$out")
{ [ "$(grep -c . <<<"$on")" = 1 ] && [ "$(grep -c . <<<"$off")" = 1 ] && [ "${on##* }" = "${off##* }" ] && ! t11bad; } || { EVID="on=$on off=$off"; fail t11_pads; }
# t11_pad_held_pop: a pad held down when its page goes gets its note off (inst_pads deletion), the stack is the play page; a pad released before gets only one
out=$(t11 "@0.2; inst page 1; siminst paddown 3; @0.1; back; @0.1; uistack")
o1=$(grep -c '^SIM inst note off ' <<<"$out"); s1=$(grep '^APP uistack' <<<"$out")
bad1=$(t11bad && echo 1)
out=$(t11 "@0.2; inst page 1; siminst paddown 3; @0.1; siminst padup 3; @0.1; back; @0.1")
o2=$(grep -c '^SIM inst note off ' <<<"$out")
{ [ "$o1" = 1 ] && [ "$s1" = 'APP uistack inst_play' ] && [ "$o2" = 1 ] && [ -z "$bad1" ] && ! t11bad; } || { EVID="o1=$o1 s1=$s1 o2=$o2"; fail t11_pad_held_pop; }
# t11_pads_ring: a tap on the hub opens the pads ring with two cells (kit, layer) and plays no note
out=$(t11 "@0.2; inst page 1; simtap 180 180; popring -5")
{ grep -q '^APP popring open=1 .*cells=2 ' <<<"$out" && ! grep -q '^SIM inst note' <<<"$out" && ! t11bad; } || { EVID="$(grep '^APP popring\|^SIM inst note' <<<"$out" | tr '\n' '|')"; fail t11_pads_ring; }
# t11_bpm_ends: the tempo cell is a bounded axis 40..240: a tap at the top wraps to 40, the knob +5 -> 45; at 240 the knob clamps
out=$(t11 "@0.5; inst bpm 240; inst page 2; popring -2; popring 0; simknob 5; inst bpm 240; simknob 3; @0.1")
b=$(bpms)
{ [ "$b" = "240 40 45 240 240 " ] && ! grep -q '^SIM inst vol' <<<"$out" && ! t11bad; } || { EVID="bpm=$b"; fail t11_bpm_ends; }
# t11_bpm_ext_inert: with an external clock the tempo cell does nothing, the centre says why ("外部"), and the knob stays the volume (the knob 0.5 s after the boot: the wake lockout);
# the beats cell still steps (batch B fix A-m5: a MIDI clock carries no metre)
out=$(t11 "@0.5; inst clock ext; inst page 2; popring -2; popring 0; popring -5; simknob 1; @0.1; popring 1; @0.1")
m=$(grep '^SIM inst metro ' <<<"$out" | tr '\n' '|')
{ [ "$m" = "SIM inst metro running=0 bpm=120 bpb=5|" ] && grep -q '^APP popring open=1 .*centre=.*\(外部\|xternal\)' <<<"$out" && grep -q '^SIM inst vol ' <<<"$out" && ! t11bad; } || { EVID="metro=$m $(grep '^APP popring\|^SIM inst vol' <<<"$out" | tr '\n' '|')"; fail t11_bpm_ext_inert; }
# t11_tap_tempo: four presses on the Tap cell 0.4 s apart set 150 BPM (+-1); the ring stays open. The ring is opened by the console's page tap and the first
# press follows with no time in between: it must count (batch B fix A-m1: the ring is laid out when it opens)
tp="simpress 81 81 50; @0.317"
out=$(t11 "@0.5; inst page 2; popring -2; $tp; $tp; $tp; simpress 81 81 50; popring -5")
b=$(bpms); last=${b% }; last=${last##* }
{ [ -n "$last" ] && [ "$last" -ge 149 ] && [ "$last" -le 151 ] && grep -q '^APP popring open=1 ' <<<"$out" && ! t11bad; } || { EVID="bpm=$b $(grep '^APP popring\|^INST ui tap' <<<"$out" | tr '\n' '|')"; fail t11_tap_tempo; }
# t11_metro_centre (Q2): the ring's centre starts the metronome, a second tap stops it
out=$(t11 "inst page 2; popring -2; popring -1; popring -1; @0.1")
{ [ "$(grep -o '^SIM inst metro running=[01]' <<<"$out" | tr '\n' ' ')" = "SIM inst metro running=1 SIM inst metro running=0 " ] && ! t11bad; } || { EVID="$(grep '^SIM inst metro\|^APP popring' <<<"$out" | tr '\n' '|')"; fail t11_metro_centre; }
# t11_loop_ring_clear: one recorded track; the ring's clear cell needs two taps (the second clears it); after the ring's 4 s a tap is a first tap again
rec="inst loop rec; @2.1; siminst note 60 100; siminst note 60 0; inst loop rec; @0.1"
out=$(t11 "$rec; inst loop; inst page 5; popring -2; popring 2; inst loop; popring 2; @0.1; inst loop")
c1=$(cnt); bad1=$(t11bad && echo 1)
out=$(t11 "$rec; inst page 5; popring -2; popring 2; @4.1; popring -2; popring 2; @0.1; inst loop; popring -5")
c2=$(cnt)
{ [ "$c1" = "1/0/0/0 1/0/0/0 0/0/0/0 " ] && [ "$c2" = "1/0/0/0 " ] && grep -q '^APP popring open=1 .*armed=2 ' <<<"$out" && [ -z "$bad1" ] && ! t11bad; } || { EVID="c1=$c1 c2=$c2"; fail t11_loop_ring_clear; }
# t11_loopmore: 更多 pushes the more list (save, export, the fixture's three loop files, ...); `inst loop more N` taps row N: a file row (from 2) opens the
# file ring (load, delete armed); delete needs two taps and removes the file once, the list then shows the two others
out=$(t11 "inst page 5; popring -2; popring 4; @0.1; uistack; uiboxes; uitop; inst loop more 2; popring -5; popring 1; popring 1; @0.1; uistack; uiboxes")
r0=$(ph 0 | grep '^APP uibox button ' | cut -d'|' -f2 | grep '^loop_' | tr '\n' ','); r1=$(ph 1 | grep '^APP uibox button ' | cut -d'|' -f2 | grep '^loop_' | tr '\n' ',')
{ grep -qx 'APP uistack inst_play inst_loop inst_loopmore' <<<"$out" && [ "$r0" = "loop_001,loop_002,loop_003," ] && grep -q '^APP popring open=1 .*cells=2 ' <<<"$out" \
  && [ "$(grep -c '^SIM inst loopfile rm ' <<<"$out")" = 1 ] && grep -qx 'SIM inst loopfile rm loop_001' <<<"$out" && [ "$r1" = "loop_002,loop_003," ] && ! t11bad; } \
  || { EVID="r0=$r0 r1=$r1 $(grep '^APP uistack\|^APP popring\|^SIM inst loopfile' <<<"$out" | tr '\n' '|')"; fail t11_loopmore; }

# --- UI unify batch B fixes ---
# fixB_kitstat (A-I1): "inst kits" / "inst kitstat" print their own lines and do not load a kit named "s" / "stat" (the saved kit stays)
out=$(t11 "@0.6; inst knob 1; inst cfg; inst kits; inst kitstat; @0.2; inst knob -1; inst cfg; inst kitstat")
{ [ "$(kitof)" = "demo demo " ] && grep -qx 'INST kits n=2' <<<"$out" && [ "$(grep -c "^INST kitstat .*name='demo'" <<<"$out")" = 2 ] && ! grep -q "^INST ui kit\|name='s'\|name='stat'" <<<"$out" && ! t11bad; } \
  || { EVID="kit=$(kitof) $(grep '^INST' <<<"$out" | head -6 | tr '\n' '|')"; fail fixB_kitstat; }
# fixB_bpm_loop_inert (S-I2, A-m5): with a loop the tempo and the beats cells do not step and the centre names the loop
out=$(t11 "@0.5; $rec; inst page 2; popring -2; popring 0; popring 1; popring -5; @0.1")
{ ! grep -q '^SIM inst metro ' <<<"$out" && grep -q '^APP popring open=1 .*centre=.*\(循環\|loop\)' <<<"$out" && ! t11bad; } || { EVID="$(grep '^SIM inst metro\|^APP popring' <<<"$out" | tr '\n' '|')"; fail fixB_bpm_loop_inert; }
# fixB_loop_files (S-I3, A-I2): with no loop 存新檔 and 匯出 MOD are refused (the note says why, no row); with a recorded loop 存新檔 adds a row and says so,
# 匯出 MOD names the file it wrote; 載入 goes back to the looper, whose TITLE says so
out=$(t11 "@0.3; inst loop more 0; inst loop more 1; @0.1; uiboxes")
n1=$(notes); r1=$(lrows); bad1=$(t11bad && echo 1)
out=$(t11 "@0.3; $rec; inst loop more 0; @0.1; uiboxes")
n2=$(notes); r2=$(lrows); bad2=$(t11bad && echo 1)
out=$(t11 "@0.3; $rec; inst loop more 1; @0.1; uiboxes")
n3=$(notes); bad3=$(t11bad && echo 1)
out=$(t11 "@0.3; inst loop more 2; popring 0; @0.1; uistack; uiboxes")
n4=$(notes)
{ [[ "$n1" == *"沒有東西可存"* || "$n1" == *"Nothing to save"* ]] && [ "$r1" = 3 ] && [[ "$n2" == *"loop_004"* ]] && [ "$r2" = 4 ] && [[ "$n3" == *"loop.mod"* ]] \
  && grep -qx 'APP uistack inst_play inst_loop' <<<"$out" && [[ "$n4" == *"已載入"* || "$n4" == *"Loaded"* ]] && [ -z "$bad1$bad2$bad3" ] && ! t11bad; } \
  || { EVID="n1=$n1 r1=$r1 n2=$n2 r2=$r2 n3=$n3 n4=$n4"; fail fixB_loop_files; }
# fixB_pad_touch (S-I4): a finger on a pad (the top one, note 40) gives its note on and, on the release, its note off; a press in the gap below the pads gives nothing
out=$(t11 "@0.3; inst page 1; simpress 180 70 50; @0.1; simpress 180 290 50; @0.1")
{ [ "$(grep '^SIM inst note ' <<<"$out" | tr '\n' '|')" = "SIM inst note on 40|SIM inst note off 40|" ] && ! t11bad; } || { EVID="$(grep '^SIM inst note' <<<"$out" | tr '\n' '|')"; fail fixB_pad_touch; }

# fix1_pad_hold_swipe (I3 / I2): a finger held 3.3 s on a pad is a sustained note (the page stays, one note on and off); the hub (no pad) still goes home on the hold; a fast swipe that starts on a pad is no flick and
# its note ends at the release; the same swipe from the hub is the quick settings' flick
out=$(t11 "@0.3; inst page 1; simpress 180 70 3300; @0.3; uistack; simpress 180 180 3300; @0.4; uistack")
{ [ "$(grep '^SIM inst note ' <<<"$out" | tr '\n' '|')" = "SIM inst note on 40|SIM inst note off 40|" ] && [ "$(grep '^APP uistack' <<<"$out" | tr '\n' '|')" = "APP uistack inst_play inst_pads|APP uistack inst_play|" ] && ! t11bad; } \
  || { EVID="$(grep '^SIM inst note\|^APP uistack' <<<"$out" | tr '\n' '|')"; fail fix1_pad_hold; }
out=$(t11 "@0.3; inst page 1; simdrag 180 70 180 290 150; @0.3; uistack")
{ [ "$(grep '^SIM inst note ' <<<"$out" | tr '\n' '|')" = "SIM inst note on 40|SIM inst note off 40|" ] && ! grep -q '^APP flick' <<<"$out" && ! t11bad; } || { EVID="$(grep '^SIM inst note\|^APP flick' <<<"$out" | tr '\n' '|')"; fail fix1_pad_swipe_veto; }
out=$(t11 "@0.3; inst page 1; simdrag 180 130 180 350 150; @0.3; uistack")
# 2026-11-20 (UI_FLICK_EDGE): the swipe from the hub begins in the middle of the page, so it is no flick either; the edge flick itself is the mid_swipe_no_flick case in 17_ring_rules_home.sh
{ ! grep -q '^APP flick' <<<"$out" && ! t11bad; } || { EVID="$(grep '^APP flick\|^APP uistack' <<<"$out" | tr '\n' '|')"; fail fix1_hub_swipe_flicks; }
# fixB_chip_table (S-m5): the fake chip table has the real chips' ids in the real order (inst_chips.c's chip_at -> the adapter file's .id)
python3 - <<'PY' || fail fixB_chip_table
import glob, re
order = re.findall(r"case \d+: return (midi_chip_\w+)\(\);", open("app/main/inst_chips.c").read())
ids = {}
for f in glob.glob("app/main/inst_chip_*.cpp"):
    t = open(f).read()
    for fn in re.findall(r"_t \*(midi_chip_\w+)\(void\)", t): ids[fn] = re.search(r'\.id = "(\w+)"', t).group(1)
fake = re.findall(r'\{ "(\w+)", "[^"]*", \d+ \}', open("sim/fake_inst_chip.c").read())
assert len(order) == 11 and [ids[f] for f in order] == fake, ([ids.get(f) for f in order], fake)
PY
