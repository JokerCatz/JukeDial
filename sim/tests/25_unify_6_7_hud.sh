# SPDX-License-Identifier: LGPL-2.1-or-later
# --- UI unify T6 ---
# the scene editor is one list: a row per layer, then add / play / favourite / save / delete; a layer row opens that layer's ring (on/off, volume axis 0..100 step 5)
nowrun t6_boxes "$T6P; uiboxes" && { read -r l1 l2 l3 l4 <<<"$(box_of '● Rain 80%' "$out")"; LR="$(((l1 + l3) / 2)) $(((l2 + l4) / 2))"; }
nowrun t6_layer_ring "$T6P; simtap $LR; @0.3; popring -5; popring 0; uiboxes; popring -5" && {
    [ "$(pst | sed -n 1p)" = "open=1 page=0 cells=2 armed=-1 centre=Rain" ] || { EVID="$(pst | tr '\n' '|')"; fail t6_layer_ring; }
    grep -q '^APP uibox button .*|・ Rain$' <<<"$out" && grep -q '^APP uibox button .*|● Thunder 80%$' <<<"$out" || { EVID="$(rows6 1)"; fail t6_layer_ring_off; }
    [ "$(pst | sed -n 3p | sed 's/ .*//')" = "open=1" ] || { EVID="$(pst | tr '\n' '|')"; fail t6_layer_ring_stays; }; }
# the volume cell: a tap at 100 % wraps to 0, the knob stops at both ends, the row follows
nowrun t6_layer_vol_ends "$T6P; simtap $LR; @0.3; popring 1; simknob 3; uiboxes; popring 1; uiboxes; simknob 30; simknob 3; uiboxes; simknob -30; uiboxes; simknob -3; uiboxes" && {
    [ "$(vrow 1)|$(vrow 2)|$(vrow 3)|$(vrow 4)|$(vrow 5)" = "● Rain 100%|● Rain 0%|● Rain 100%|● Rain 0%|● Rain 0%" ] || { EVID="$(vrow 1)|$(vrow 2)|$(vrow 3)|$(vrow 4)|$(vrow 5)"; fail t6_layer_vol_ends; }; }
# the delete row: the first tap arms it (the row says so), 4 s later a tap only arms again, two quick taps delete exactly once
nowrun t6_delete_row "$T6P; simknob 9; @0.2; uiboxes" && D6=$(box_of "$DELRE" "$out" | awk '{print int(($1 + $3) / 2), int(($2 + $4) / 2)}')
nowrun t6_delete_armed "$T6P; simknob 9; @0.2; simtap $D6; @0.3; uiboxes; @4.1; uiboxes; simtap $D6; @0.3; uiboxes" && {
    grep -q '^APP uibox button .*|\(再點一次刪除\|Tap again to delete\)$' <<<"$(awk '/^APP uiboxes end/{exit} {print}' <<<"$out")" || { EVID="$(rows6 1)"; fail t6_delete_armed_text; }
    ! grep -q '^APP uibox button .*|\(再點一次刪除\|Tap again to delete\)$' <<<"$(awk '/^APP uiboxes end/{k++; next} k==1' <<<"$out")" || { EVID="$(rows6 2)"; fail t6_delete_arm_expires; }
    ! grep -q '^SIM noise delete' <<<"$out" && grep -q '^APP uibox button .*|\(再點一次刪除\|Tap again to delete\)$' <<<"$(awk '/^APP uiboxes end/{k++; next} k==2' <<<"$out")" || { EVID="$(rows6 3)"; fail t6_delete_rearm; }; }
nowrun t6_delete_twice "$T6P; simknob 9; @0.2; simtap $D6; @0.3; simtap $D6; @0.5; uistack" && {
    [ "$(grep -c '^SIM noise delete 0$' <<<"$out")" = 1 ] || { EVID="$(grep '^SIM noise delete' <<<"$out" | tr '\n' '|')"; fail t6_delete_twice; }; }
# no hand-built controls: every button is a list row (>= 48 wide and high) except Back; the box rules hold on the clipped boxes (4 layers: scene_custom)
nowrun t6_no_hand_controls "simnoise 30 4 custom; sceneui 0; @0.3; uiboxes" && {
    small=$(grep '^APP uibox button ' <<<"$out" | grep -v '|\(Back\|返回\)$' | awk '$6 - $4 + 1 < 48 || $7 - $5 + 1 < 48 {print}'); [ -z "$small" ] || { EVID="$small"; fail t6_no_hand_controls; }; }
out=$(python3 tools/check/ui_boxes_check.py sim --only scene,scene_custom 2>&1); rc=$?
{ [ $rc = 0 ] && [ "$(tail -1 <<<"$out")" = "BOXES ok 2 pages" ]; } || { EVID="rc=$rc $(tail -3 <<<"$out" | tr '\n' '|')"; fail t6_boxes_scene; }

# --- UI unify T7 ---
# dice: "顆數：N" is an axis button in the HEAD slot (1..3, wraps; the tap steps, the knob steps both ways while borrowed, 4 s later the knob rolls again)
NOWCFG=dice_n=1 nowrun t7_dice_boxes "tools dice; @0.5; frame hold; uiboxes" && DC=$(box_of '顆數：1\|Dice: 1' "$out" | awk '{print int(($1 + $3) / 2), int(($2 + $4) / 2)}')
NOWCFG=dice_n=1 nowrun t7_dice_count "screen on; @0.5; tools dice; @0.3; frame hold; tools stat; simtap $DC; @0.2; tools stat; simtap $DC; @0.2; tools stat; simtap $DC; @0.2; tools stat" && {
    [ "$(sn dice_n)" = "1 2 3 1 " ] && [ "$(sn rolls | tr -d ' ')" = 0000 ] || { EVID="dice_n $(sn dice_n) rolls $(sn rolls)"; fail t7_dice_count; }; }
NOWCFG=dice_n=1 nowrun t7_dice_knob "screen on; @0.5; tools dice; @0.3; frame hold; simtap $DC; @0.2; tools stat; simknob 1; @0.1; tools stat; simknob -1; simknob -1; @0.1; tools stat; @4.5; simknob 1; @1.5; tools stat" && {
    [ "$(sn dice_n)" = "2 3 1 1 " ] && [ "$(sn rolls)" = "0 0 0 1 " ] || { EVID="dice_n $(sn dice_n) rolls $(sn rolls)"; fail t7_dice_knob; }; }
# slot: the score is a pill in HEAD, a tap opens the pay table
nowrun t7_slot_pay "tools slot; @0.5; uiboxes" && {
    SP=$(box_of '分數 0 · 賠率表\|Score 0 · Odds' "$out" | awk '{print int(($1 + $3) / 2), int(($2 + $4) / 2)}'); [ -n "$SP" ] || { EVID="$(grep '^APP uibox' <<<"$out" | cut -d'|' -f2 | tr '\n' ' ')"; fail t7_slot_pay_pill; } }
nowrun t7_slot_pay "tools slot; @0.5; simtap ${SP:-180 40}; @0.3; uistack" && stack_is t7_slot_pay 1 "menu tools tools_slot tools_slot_pay"
# list toys: the two answer buttons are one choice row in FOOT (>= 48 high, 8 apart), their words change per phase; people: the group is an axis button in HEAD
nowrun t7_lists_buttons "tools lunch; @0.5; tools spin; @3; uiboxes; tools btn 1; @3; uiboxes" && {
    fb() { grep '^APP uibox button ' <<<"$(awk -v b="$1" '/^APP uiboxes end/{k++; next} k==b-1' <<<"$out")" | grep -v '|\(Back\|返回\)$'; }
    [ "$(fb 1 | wc -l)" = 2 ] && [ "$(fb 2 | wc -l)" = 2 ] && [ "$(fb 1 | cut -d'|' -f2)" != "$(fb 2 | cut -d'|' -f2)" ] || { EVID="$(fb 1 | cut -d'|' -f2 | tr '\n' ' ')/ $(fb 2 | cut -d'|' -f2 | tr '\n' ' ')"; fail t7_lists_words; }
    bad=$(fb 1 | awk '$6 - $4 + 1 < 48 || $7 - $5 + 1 < 48 || ($5 + $7) / 2 < 258 || ($5 + $7) / 2 > 266 {print}'); [ -z "$bad" ] || { EVID="$bad"; fail t7_lists_buttons; }; }
nowrun t7_lists_group "tools people; @0.5; uiboxes" && {
    read -r gx1 gy1 gx2 gy2 <<<"$(grep '^APP uibox button ' <<<"$out" | grep -v '|\(Back\|返回\)$' | awk '$5 < 120 {print $4, $5, $6, $7}')"
    nowrun t7_lists_group "tools people; @0.5; simtap $(((gx1 + gx2) / 2)) $(((gy1 + gy2) / 2)); @0.3; tools stat" && {
        grep -q '^APP lists group=[1-9]' <<<"$out" && [ $((gy2 - gy1 + 1)) -ge 48 ] || { EVID="box $gx1 $gy1 $gx2 $gy2: $(grep '^APP lists group' <<<"$out")"; fail t7_lists_group; }; }; }
out=$(python3 tools/check/ui_boxes_check.py sim --only tools_dice,tools_slot,tools_lunch,tools_people 2>&1); rc=$?
{ [ $rc = 0 ] && [ "$(tail -1 <<<"$out")" = "BOXES ok 4 pages" ]; } || { EVID="rc=$rc $(tail -3 <<<"$out" | tr '\n' '|')"; fail t7_boxes_toys; }

# vocab home is a menu ring (開始, direction, decks; the Back cell is not counted): the centre starts the quiz, the direction cell is an axis (the knob steps it for 4 s, then volume again)
nowrun t7_vocab_ring "screen on; @0.5; vol 0; vocab; @0.5; ring; uitop; simtap 180 180; @0.5; uitop" && {
    [ "$(rn)" = "3 " ] && [ "$(tp)" = "vocab vocab_quiz " ] || { EVID="n=$(rn) top=$(tp)"; fail t7_vocab_ring; }; }
nowrun t7_vocab_dir "screen on; @0.5; vol 0; vocab; @0.5; simtap 40 180; @0.2; cfg vocab_dir; simknob 2; cfg vocab_dir; simknob -1; cfg vocab_dir; @4.5; simvol; simknob 1; simvol; cfg vocab_dir" && {
    [ "$(cv vocab_dir)" = "1|0|2|2|" ] && [ "$(vols)" = "0 2 " ] || { EVID="dir $(cv vocab_dir) vol $(vols)"; fail t7_vocab_dir; }; }
# vocab quiz: the four answers are one 2 x 2 choice grid (136 x 64, 8 px or more apart)
nowrun t7_vquiz_grid "vocab start; @0.5; uiboxes clip" && {
    g=$(grep '^APP uibox button ' <<<"$out" | grep -v '|\(Back\|返回\)$' | awk '{print $6 - $4 + 1 "x" $7 - $5 + 1}' | sort | uniq -c | tr -s ' ' | tr '\n' ';')
    [ "$g" = " 4 136x64;" ] || { EVID="boxes: $g"; fail t7_vquiz_grid; }
    xs=$(grep '^APP uibox button ' <<<"$out" | grep -v '|\(Back\|返回\)$' | awk '{print $4}' | sort -n | uniq | tr '\n' ' ')
    set -- $xs; [ $# = 2 ] && [ $(($2 - $1 - 136)) -ge 8 ] || { EVID="x1s: $xs"; fail t7_vquiz_gap; }; }
out=$(python3 tools/check/ui_boxes_check.py sim --only vhome,vquiz 2>&1); rc=$?
{ [ $rc = 0 ] && [ "$(tail -1 <<<"$out")" = "BOXES ok 2 pages" ]; } || { EVID="rc=$rc $(tail -3 <<<"$out" | tr '\n' '|')"; fail t7_boxes_vocab; }
# the configured deck is missing (the built-in sample stands in): the quiz must not reopen it and free the pack under the home title (found when the home ring changed the title width)
NOWCFG="vocab_deck=/sdcard/vocab/nope.zip" nowrun t7_vocab_missing_deck "vocab decks; @1.2; back; back; vocab start; @1.2; uitop" && {
    [ "$(tp)" = "vocab_quiz " ] || { EVID="top $(tp)"; fail t7_vocab_missing_deck; }; }
# pairing: a two-cell select-then-confirm ring (no Back cell), the digits in the centre and in the hints; the centre confirms the selected cell; the knob does nothing
nowrun t7_pair_ring "screen on; @0.5; vol 0; simbt pair 123456; uipush pair; @0.5; ring; uiboxes; simtap 81 279; ring; simvol; simknob 2; simvol; simtap 180 180; @0.5; uitop" && {
    [ "$(rn)" = "2 2 " ] && ! grep -q '^APP uibox .*|\(Back\|返回\)$' <<<"$out" || { EVID="n=$(rn) $(grep -c '|返回$\||Back$' <<<"$out")"; fail t7_pair_cells; }
    [ "$(rg | cut -d'|' -f1 | grep -c 123456)" = 1 ] && [ "$(rg | cut -d'|' -f2 | grep -c 123456)" = 1 ] || { EVID="$(rg)"; fail t7_pair_digits; }
    grep -qx 'SIM bt pair reply 1' <<<"$out" && [ "$(vols)" = "0 0 " ] || { EVID="$(grep '^SIM bt' <<<"$out" | tr '\n' ' ') vols $(vols)"; fail t7_pair_reply; }; }
nowrun t7_pair_reject "screen on; @0.5; simbt pair 654321; uipush pair; @0.3; simtap 40 180; simtap 40 180; @0.5; uitop" && {
    grep -qx 'SIM bt pair reply 0' <<<"$out" || { EVID="$(grep '^SIM bt' <<<"$out" | tr '\n' ' ')"; fail t7_pair_reject; }; }
# gacha: a menu ring (single, ten; Back cell not counted), the centre shows the collection
nowrun t7_gacha_ring "uipush fortune; fortune gacha; @0.5; ring; uitop" && {
    [ "$(rn)" = "2 " ] && [ "$(tp)" = "fortune_gacha " ] && grep -q '^APP ring .*center=\(圖鑑\|Collected\)' <<<"$out" || { EVID="n=$(rn) top=$(tp) $(grep '^APP ring' <<<"$out")"; fail t7_gacha_ring; }; }
nowrun t7_gacha_pull "screen on; uipush fortune; fortune gacha; @0.5; simtap 81 279; @0.5; uitop; back; @0.3; simtap 40 180; @0.5; uitop" && {
    [ "$(tp)" = "fortune_card fortune_ten " ] || { EVID="$(tp)"; fail t7_gacha_pull; }; }
out=$(python3 tools/check/ui_boxes_check.py sim --only fortune_gacha,pair 2>&1); rc=$?
{ [ $rc = 0 ] && [ "$(tail -1 <<<"$out")" = "BOXES ok 2 pages" ]; } || { EVID="rc=$rc $(tail -3 <<<"$out" | tr '\n' '|')"; fail t7_boxes_gacha_pair; }
# bt_devs: the status line is the title (TITLE slot), "scan again" and the devices are list rows (>= 48 high), Back where every page has it
SIM_STATE="bt=headphones" nowrun t7_btdevs_list "screen on; @0.5; btui; @0.3; simtap 81 81; simtap 81 81; @0.5; uiboxes" && SC=$(box_of '重新掃描\|Scan again' "$out" | awk '{print int(($1 + $3) / 2), int(($2 + $4) / 2)}')
SIM_STATE="bt=headphones" nowrun t7_btdevs_list "screen on; @0.5; btui; @0.3; simtap 81 81; simtap 81 81; @0.5; uitop; simtap ${SC:-180 74}; @1.5; uiboxes" && {
    [ "$(tp)" = "bt_devs " ] || { EVID="top $(tp)"; fail t7_btdevs_top; }
    L=$(grep '^APP uibox' <<<"$out")
    t=$(grep '^APP uibox label .*|\(掃描完成\|Scan done\)' <<<"$L" | awk '{print int(($5 + $7) / 2)}')
    [ -n "$t" ] && [ "$t" -ge 15 ] && [ "$t" -le 45 ] || { EVID="title y=$t"; fail t7_btdevs_title; }
    rows=$(grep '^APP uibox button ' <<<"$L" | grep -v '|\(Back\|返回\)$')
    [ "$(wc -l <<<"$rows")" -ge 3 ] && [ -z "$(awk '$6 - $4 + 1 < 200 || $7 - $5 + 1 < 48 {print}' <<<"$rows")" ] || { EVID="rows: $rows"; fail t7_btdevs_rows; }
    ! grep -q '^APP uibox button .*|\(Back\|返回\)$' <<<"$L" || fail t7_btdevs_back; }   # GS: no Back on a list page

# the pomodoro ring: the idle page has no button; centre start / pause; the armed stop cell (a second tap after 4 s is a first tap); Back = pop
nowrun t7_pomo_ring "$PM; uiboxes; pomo stat; simtap 180 180; popring -5; popring -1; pomo stat; popring -5; popring -1; pomo stat; popring 0; @4.1; popring -2; popring 0; pomo stat; popring 0; pomo stat; popring -2; popring -3; uitop" && {
    grep -q '^APP uibox button ' <<<"$out" && { EVID="$(grep '^APP uibox button' <<<"$out")"; fail t7_pomo_idle_no_button; }
    [ "$(ps)" = "idle run pause pause idle " ] || { EVID="states: $(ps)"; fail t7_pomo_states; }
    c="$(pcell 1) $(pcell 3) $(pcell 6) $(pcell 9)"; [ "$c" = "3 4 4 3" ] || { EVID="cells: $c"; fail t7_pomo_cells; }
    grep -q '^APP popring .*centre=\(開始\|Start\)$' <<<"$out" && [ -n "$(pcen 3 | grep '暫停\|Pause')" ] && [ -n "$(pcen 4 | grep '繼續\|Resume')" ] || { EVID="$(grep '^APP popring ' <<<"$out" | tr '\n' '|')"; fail t7_pomo_centre; }
    [ -n "$(grep '^APP popring ' <<<"$out" | sed -n 5p | grep 'armed=0')" ] && [ -n "$(grep '^APP popring ' <<<"$out" | sed -n 7p | grep 'armed=0')" ] || { EVID="$(grep '^APP popring ' <<<"$out" | tr '\n' '|')"; fail t7_pomo_armed; }
    [ "$(grep '^APP uitop' <<<"$out" | tail -1)" = "APP uitop clock" ] || { EVID="$(grep '^APP uitop' <<<"$out")"; fail t7_pomo_back; }; }

# --- HUD task (2026-11-18): the shared timing faces, the stopwatch, the countdown ---
# "timers" prints "APP timers sw=<idle|run|pause> el=<ms> laps= cd=<idle|run|pause|done> left=<ms> set=<s> done= msgs= beeps="; "timers dump" the face's data (APP tface ...); "clockface dump" the clock's HUD data
HT="screen on; vol 0; @0.5; clock"
# stopwatch: start / lap / pause / resume / reset; the laps are listed newest first with the split and the total
nowrun hud_sw_laps "$HT; show stopwatch; timers sw start; @1.5; timers sw lap; @1; timers sw lap; timers sw laps; timers sw start; @2; timers sw reset" && {
    [ "$(grep '^APP timers sw=' <<<"$out" | sed 's/^APP timers sw=\([a-z]*\) .*/\1/' | tr '\n' ' ')" = "run run run run pause idle " ] || { EVID="$(grep '^APP timers sw=' <<<"$out" | tr '\n' '|')"; fail hud_sw_states; }
    [ "$(grep -c '^APP timers lap ' <<<"$out")" = 2 ] || fail hud_sw_lap_count
    grep -q '^APP timers lap k=0 no=2 split=1[0-9][0-9][0-9] total=2[0-9][0-9][0-9] best=1' <<<"$out" || { EVID="$(grep '^APP timers lap' <<<"$out" | tr '\n' '|')"; fail hud_sw_lap_values; }
    [ "$(tv el | tail -1)" = 0 ] && [ "$(tv laps | tail -1)" = 0 ] || fail hud_sw_reset; }
# it goes on when the page is left (and the music is not touched: nothing here pauses or starts a track)
nowrun hud_sw_keeps_running "$HT; show stopwatch; timers sw start; uihome; @2.5; timers" && {
    [ "$(tv sw)" = run ] && [ "$(tv el)" -ge 2400 ] || { EVID="$(grep '^APP timers sw=' <<<"$out" | tail -1)"; fail hud_sw_keeps_running; }; }
# countdown: set, start, the last 10 s warn (triangles blink, red), zero fires once with a message when the page is not open
nowrun hud_cd_flow "$HT; show countdown; timers cd set 75; timers; timers cd start; @1.2; timers cd jump 12; @0.6; timers dump; timers cd jump 9; @0.6; timers dump; timers cd jump 3; @4; timers" && {
    [ "$(grep '^APP timers sw=' <<<"$out" | sed 's/.* set=\([0-9]*\) .*/\1/' | head -1)" = 75 ] || fail hud_cd_set
    [ "$(grep '^APP tface ' <<<"$out" | sed 's/.* red=\([01]\) .*/\1/' | tr '\n' ' ')" = "0 1 " ] || { EVID="$(grep '^APP tface ' <<<"$out" | tr '\n' '|')"; fail hud_cd_red_last10; }
    [ "$(tv cd)" = done ] && [ "$(tv done)" = 1 ] && [ "$(tv beeps)" -ge 1 ] || { EVID="$(grep '^APP timers sw=' <<<"$out" | tail -1)"; fail hud_cd_done_once; }
    [ "$(tv msgs)" = 0 ] || fail hud_cd_no_message_on_its_page; }
nowrun hud_cd_message_off_page "$HT; show countdown; timers cd set 20; timers cd start; uihome; timers cd jump 2; @4; timers; uitop" && {
    [ "$(tv done)" = 1 ] && [ "$(tv msgs)" = 1 ] || { EVID="$(grep '^APP timers sw=' <<<"$out" | tail -1)"; fail hud_cd_message_off_page; }
    grep -q '^APP uitop message' <<<"$out" || { EVID="$(grep '^APP uitop' <<<"$out" | tail -1)"; fail hud_cd_message_shown; }; }
# pause holds the time; the set time is stored; a running countdown refuses a new set time
nowrun hud_cd_pause_set "$HT; timers cd set 90; timers cd start; @2; timers cd start; @5; timers; timers cd set 10; timers cd reset; timers cd set 10; cfg cd_set" && {
    a=$(grep '^APP timers sw=' <<<"$out" | sed 's/.* left=\([0-9]*\) .*/\1/' | tr '\n' ' '); set -- $a
    [ "$4" -ge 87000 ] && [ "$4" -le 88100 ] && [ "$5" = 87999 -o "$5" = "$4" ] || { EVID="left: $a"; fail hud_cd_pause_holds; }
    grep -q '^APP cfg cd_set=10' <<<"$out" || fail hud_cd_set_stored; }
# the face of each feature is its own choice (cfg key per feature); the stopwatch's look does not move the countdown's
nowrun hud_face_own_choice "$HT; show stopwatch; timers face hud; uihome; clock; show countdown; timers face rings; uihome; cfg swface; cfg cdface; cfg pomoface" && {
    grep -q '^APP cfg swface=hud' <<<"$out" && grep -q '^APP cfg cdface=rings' <<<"$out" && grep -q '^APP cfg pomoface=$' <<<"$out" || { EVID="$(grep '^APP cfg' <<<"$out" | tr '\n' '|')"; fail hud_face_own_choice; }; }
# the HUD data: triangles lit only for a real state (clock: time not synced; countdown: last 10 s), the status labels from real data
nowrun hud_clock_data "$HT; clockface hud; @1.2; clockface dump; clockface ?" && {
    grep -q '^APP clockface shown=hud' <<<"$out" || fail hud_clock_face_shown
    grep -q '^APP tface kind=hud feat=clock big=[0-9][0-9]:[0-9][0-9]:[0-9][0-9] .* time_hm=[0-9][0-9]:[0-9][0-9] time_s=[0-9][0-9] date=20[0-9][0-9]-[01][0-9]-[0-3][0-9] wday=[0-6] alarm=[01] ' <<<"$out" || { EVID="$(grep '^APP tface' <<<"$out")"; fail hud_clock_data; }
    : ; }
nowrun hud_clock_unsynced "$HT; simtime 2000-01-01 00:00:00; @1.2; clockface hud; @3.5; clockface dump" && {
    [ "$(tf big)" = "--:--:--" ] && [ "$(tf time_hm)" = "--:--" ] && [ "$(tf sync)" = 0 ] && [ "$(tf alert)" = 1 ] || { EVID="$(grep '^APP tface' <<<"$out")"; fail hud_clock_unsynced_warn; }; }
nowrun hud_cd_warn_data "$HT; show countdown; timers face hud; timers cd set 30; timers cd start; timers dump; timers cd jump 9; @0.3; timers dump" && {
    [ "$(grep '^APP tface ' <<<"$out" | sed 's/.* alert=\([01]\) .*/\1/' | tr '\n' ' ')" = "0 1 " ] || { EVID="$(grep '^APP tface' <<<"$out" | tr '\n' '|')"; fail hud_cd_warn_data; }
    [ "$(tf set)" = "00:30" ] && [ "$(tf state)" = 1 ] && [ "$(tf main)" = "00:09" ] && [ "$(tf prog)" = 6 ] || { EVID="$(grep '^APP tface' <<<"$out" | tail -1)"; fail hud_cd_slots; }; }
# TFACE / FACEZIP: the HUD art is one STORE-method zip on the card (_player/faces.zip, read in place); a zip with a home-made rd_sw (a P5 layer and a P5 digit sheet) replaces the fixture's;
# the redraw counter moves only by slot rectangles while the stopwatch runs
if [ -z "$ONLY" ] || [[ tface_card_face =~ $ONLY ]]; then
    fresh; rm -f "$T/fix/_player/faces.zip"; mkzip "$T/fix/_player/faces.zip" store
    out=$($SIM --headless --root "$T/fix" --cmd "$HT; show stopwatch; timers face hud; @0.3; timers dump; shot $T/tface_card.png" 2>&1)
    grep -q '^APP tface kind=hud feat=sw .* main=00:00' <<<"$out" || { EVID="$(grep '^APP tface' <<<"$out")"; fail tface_card_face; }
    python3 -c "
from PIL import Image; im = Image.open('$T/tface_card.png').convert('RGB')
assert sum(im.getpixel((110, 105))) > 150, 'the zip layer is not drawn'
assert sum(im.getpixel((30, 30))) < 30, 'outside the zip art is not dark'" 2>/dev/null || fail tface_card_pixels
    # a COMPRESSED zip is refused (the firmware inflates nothing): the face falls back like a missing one
    fresh; rm -f "$T/fix/_player/faces.zip"; mkzip "$T/fix/_player/faces.zip" deflate
    out=$($SIM --headless --root "$T/fix" --cmd "$HT; show stopwatch; timers face hud; @0.3; timers dump" 2>&1)
    grep -q '^APP tface kind=info feat=sw ' <<<"$out" || { EVID="$(grep '^APP tface' <<<"$out")"; fail tface_deflated_zip_refused; }
fi
# no faces.zip: HUD is not offered (stepping skips it), a stored HUD choice shows the plain face (info; the clock's info page), nothing breaks, the reason is on the clock's face ring
NOFACES=1 nowrun tface_no_zip "$HT; show stopwatch; timers face hud; @0.3; timers dump; timers face rings; timers face next; timers face next; timers face next; timers dump; uihome; clock; clockface hud; clockface ?; clockface ring; @0.3; uiboxes" && {
    grep -q '^APP tface kind=info feat=sw ' <<<"$out" || { EVID="$(grep '^APP tface' <<<"$out")"; fail tface_no_zip_fallback; }
    [ "$(grep '^APP timers face=' <<<"$out" | tr '\n' ' ')" = "APP timers face=hud APP timers face=rings APP timers face=info APP timers face=rings APP timers face=info " ] || { EVID="$(grep '^APP timers face' <<<"$out" | tr '\n' '|')"; fail tface_no_zip_not_offered; }
    grep -q '^APP clockface shown=info stored=hud ' <<<"$out" || { EVID="$(grep '^APP clockface' <<<"$out")"; fail tface_no_zip_clock_info; }
    grep -q '^APP uibox label .*|No faces.zip' <<<"$out" || { EVID="$(grep '^APP uibox' <<<"$out" | head -20)"; fail tface_no_zip_reason; }
    # the hint stays one line inside its zone (a long hint once wrapped to three lines over the hands): its box is a single line tall (< 30 px) and ends above the Back-free bottom edge
    read -r hy1 hy2 <<<"$(grep '^APP uibox label .*|No faces.zip' <<<"$out" | head -1 | awk '{print $5, $7}')"
    [ -n "$hy1" ] && [ $((hy2 - hy1)) -lt 30 ] || { EVID="hint box y $hy1..$hy2"; fail tface_no_zip_reason_oneline; }; }
for lg in 0 1 2; do   # the hint stays one clipped line in every language (zh-TW, zh-CN, English)
    NOFACES=1 NOWCFG="lang=$lg" nowrun "tface_no_zip_hint_lang$lg" "$HT; uihome; clock; clockface ring; @0.3; uiboxes" && {
        read -r hy1 hy2 <<<"$(grep '^APP uibox label .*faces.zip' <<<"$out" | head -1 | awk '{print $5, $7}')"
        [ -n "$hy1" ] && [ $((hy2 - hy1)) -lt 30 ] || { EVID="lang $lg hint box y $hy1..$hy2: $(grep '^APP uibox label' <<<"$out" | tr '\n' '|')"; fail "tface_no_zip_hint_lang$lg"; }; }
done
nowrun tface_redraw_small "$HT; show stopwatch; timers face hud; timers sw start; @1; timers stats; @2; timers stats" && {
    a=($(grep '^APP tface stats' <<<"$out" | sed 's/.*redraw_px=\([0-9]*\) .*/\1/')); d=$(( ${a[1]:-0} - ${a[0]:-0} ))
    [ "$d" -gt 0 ] && [ "$d" -lt 129600 ] || { EVID="2 s of the stopwatch invalidated $d px (one 360 x 360 frame = 129600)"; fail tface_redraw_small; }; }
# the default HUD colour is tri (the EVA look): the first step of the axis wraps to green, then red
nowrun hud_colour_axis "$HT; show countdown; timers face hud; popring -2; popring 4; cfg hudcol; popring 4; cfg hudcol" && {
    [ "$(grep '^APP cfg hudcol=' <<<"$out" | sed 's/.*=//' | tr '\n' ' ')" = "green red " ] || { EVID="$(grep '^APP cfg hudcol' <<<"$out" | tr '\n' '|')"; fail hud_colour_axis; }; }
nowrun hud_alert_follows_state "$HT; show countdown; timers face hud; timers cd set 30; timers cd start; timers dump; timers cd jump 9; @0.3; timers dump; timers cd reset; @0.3; timers dump" && {
    [ "$(grep '^APP tface ' <<<"$out" | sed 's/.* alert=\([01]\) .*/\1/' | tr '\n' ' ')" = "0 1 0 " ] || { EVID="$(grep '^APP tface' <<<"$out" | tr '\n' '|')"; fail hud_alert_follows_state; }; }
nowrun hud_menu_cells "$HT; show clockmenu; ring" && {
    grep -q '^APP ring .*cells=8\|^APP ring page=0/1' <<<"$out" || { EVID="$(grep '^APP ring' <<<"$out")"; fail hud_menu_8_cells; }; }
# --- end HUD ---

