# SPDX-License-Identifier: LGPL-2.1-or-later
# --- Task 3: ring rules, confirmations, home ---
# Cells of a segmented ring (centre 180,180, radius 140): slot 0 bottom left (81,279), 1 left (40,180), 2 top left (81,81), 3 top (180,40), 4 top right (279,81), 5 right (320,180), 6 bottom right (279,279); Back at the bottom (180,320).
# "ring" prints the top page's ring: APP ring page=<p>/<pages> sel=<i> center=<text, / for a line break>; "settings rows" / "settings key <name>" (settings tap N is the index form); "uihome"; "simpress x y ms" holds a press.
# F2: a tap on the selected cell confirms (segmented ring and bubbles); the first tap only selects
nowrun t3_menu_second_tap "screen on; vol 0; simtap 81 279; ring; uitop; simtap 81 279; @0.3; uitop; uistack" && {
    [ "$(rg)" = "page=0/2 sel=0 center=Songs|" ] && [ "$(tp)" = "menu mp3 " ] && [ "$(us)" = "menu mp3|" ] || { EVID="$(rg) $(tp) $(us)"; fail t3_menu_second_tap; }; }
nowrun t3_menu_second_tap_bubbles "screen on; vol 0; style next; @0.3; simtap 100 275; ring; uitop; simtap 100 275; @0.3; uitop" && {
    [ "$(rg)" = "page=0/2 sel=0 center=Songs|" ] && [ "$(tp)" = "menu mp3 " ] || { EVID="$(rg) $(tp)"; fail t3_menu_second_tap_bubbles; }; }
nowrun t3_menu_other_cell_selects "screen on; vol 0; simtap 81 279; simtap 40 180; ring; uitop" && {
    [ "$(rg)" = "page=0/2 sel=1 center=Chiptune|" ] && [ "$(tp)" = "menu " ] || { EVID="$(rg) $(tp)"; fail t3_menu_other_cell_selects; }; }
# F2: the knob turns the page of a ring with several pages (wraps) and the volume stays; one page: the knob is volume
nowrun t3_knob_menu_pages "screen on; vol 0; @0.5; simknob 1; ring; simknob 1; ring; simknob -1; ring; simvol" && {
    [ "$(rg)" = "page=1/2 sel=-1 center=Pick one,/tap the centre|page=0/2 sel=-1 center=Pick one,/tap the centre|page=1/2 sel=-1 center=Pick one,/tap the centre|" ] || { EVID="$(rg)"; fail t3_knob_menu_pages; }
    [ "$(vols)" = "0 " ] || { EVID="volume $(vols)"; fail t3_knob_menu_volume; }; }
nowrun t3_knob_colors_pages "screen on; vol 0; @0.5; uipush colors; simknob 1; ring; simvol" && {
    [ "$(rg | cut -c1-8)" = "page=0/1" ] && [ "$(vols)" = "2 " ] || { EVID="$(rg) vol $(vols)"; fail t3_knob_colors_pages; }; }   # GS: eight cells a page: the colours are one page, the knob stays volume
# 小工具 has eight cells since 捏泡泡紙 joined (bubbles task): two pages again, so the knob turns the page and the volume stays (the one-page case: the next test, the fortune ring)
nowrun t3_knob_tools_two_pages_volume "screen on; vol 0; @0.5; tools; simknob 1; ring; simvol" && {
    [ "$(rg | cut -c1-8)" = "page=1/2" ] && [ "$(vols)" = "0 " ] || { EVID="$(rg) vol $(vols)"; fail t3_knob_tools_two_pages_volume; }; }
# G4c: a finger drag on a ring turns its page (left/up next, right/down previous, UI_DRAG_PX = 48 px); less is a tap; a drag never selects; the Back segment shows the page count; "ui next 0" drops the next cell
# GS (2026-11-16): the page turn is the FAST left / right flick (left = next, right = previous); a slow drag turns nothing; the up / down flicks are the back / quick settings, not a page turn
nowrun g4c_drag_tools "screen on; vol 0; @0.5; tools; ring; simflick left; ring; simflick right; ring; simdrag 250 180 130 180 900; ring; simdrag 180 100 180 200 900; ring; uitop" && {
    [ "$(rg | sed 's/center=[^|]*//g')" = "page=0/2 sel=-1 |page=1/2 sel=-1 |page=0/2 sel=-1 |page=0/2 sel=-1 |page=0/2 sel=-1 |" ] && [ "$(tp)" = "tools " ] || { EVID="$(rg) $(tp)"; fail g4c_drag_tools; }; }
# 2026-11-20 (UI_FLICK_EDGE): a fast swipe that begins in the middle of the page is a scroll / drag, never a flick; the same swipe from the edge is (simflick starts 12 px inside the edge)
nowrun mid_swipe_no_flick "screen on; vol 0; @0.5; tools; simdrag 180 180 180 340 150; @0.3; simdrag 180 180 180 20 150; @0.3; simdrag 290 180 70 180 150; @0.3; ring; uitop" && {
    [ "$(rg | cut -c1-8)" = "page=0/2" ] && [ "$(tp)" = "tools " ] || { EVID="$(rg) $(tp)"; fail mid_swipe_no_flick; }; }
nowrun g4c_short_drag_is_tap "screen on; vol 0; @0.5; tools; simdrag 80 180 70 180 150; ring" && {
    [ "$(rg | cut -c1-14)" = "page=0/2 sel=1" ] || { EVID="$(rg)"; fail g4c_short_drag_is_tap; }; }
nowrun g4c_back_label "screen on; vol 0; @0.5; tools; uiboxes; simflick left; uiboxes; fortune; uiboxes" && {   # GS: the page count is the bubble (no Back label): only with more than one page
    [ "$(grep -c 'label .*|1-2$' <<<"$out")" = 1 ] && [ "$(grep -c 'label .*|2-2$' <<<"$out")" = 1 ] && [ "$(grep -c '|1-1$' <<<"$out")" = 0 ] && ! grep -q 'Back' <<<"$out" || { EVID="$(grep -c 'Back' <<<"$out")"; fail g4c_back_label; }; }
nowrun g4c_hub_pages "screen on; vol 0; @0.5; tools; @0.3; games; @1; ring; simflick left; ring; simflick left; ring; simflick left; ring; simflick left; ring" && {
    p=$(rg | tr '|' '\n' | sed -n 's|.*page=0/\([0-9]*\) .*|\1|p' | head -1)
    [ -n "$p" ] && [ "$p" -ge 2 ] && [ "$(rg | tr '|' '\n' | sed -n 's|page=\([0-9]*\)/.*|\1|p' | tr '\n' ' ')" = "$(for i in 0 1 2 3 4; do echo -n "$((i % p)) "; done)" ] || { EVID="pages=$p $(rg)"; fail g4c_hub_pages; }; }
nowrun g4c_next_cell_off "screen on; vol 0; @0.5; tools; uiboxes; simflick left; ring" && {   # GS: there is no 下一頁 cell any more (seven items a page)
    ! grep -q 'Next >>' <<<"$out" && [ "$(rg | sed 's/center=[^|]*//g')" = "page=1/2 sel=-1 |" ] || { EVID="$(rg)"; fail g4c_next_cell_off; }; }
# a drag abandoned under UI_DRAG_PX taps nothing where it ends (it left a cell and ends on the centre: no confirm); a slow drag that stays on Back does not go home at 400 ms
nowrun g4c_abandoned_drag_no_tap "screen on; vol 0; @0.5; tools; simtap 60 180; ring; simdrag 180 300 180 270 150; ring; uitop" && {
    [ "$(rg | sed 's/center=[^|]*//g')" = "page=0/2 sel=1 |page=0/2 sel=1 |" ] && [ "$(tp)" = "tools " ] || { EVID="$(rg) $(tp)"; fail g4c_abandoned_drag_no_tap; }; }
nowrun g4c_slow_drag_on_back_stays "screen on; vol 0; @0.5; tools; simdrag 180 290 180 330 700; @0.3; uitop" && {
    [ "$(tp)" = "tools " ] || { EVID="$(tp)"; fail g4c_slow_drag_on_back_stays; }; }
nowrun g4c_long_press_back_home "screen on; vol 0; @0.5; tools; simflick left; simpress 180 318 3100; @0.3; uitop" && {
    [ "$(tp)" = "menu " ] || { EVID="$(tp)"; fail g4c_long_press_back_home; }; }
nowrun t3_knob_one_page_volume "screen on; vol 0; @0.5; fortune; simknob 1; ring; simvol" && {
    [ "$(rg | cut -c1-8)" = "page=0/1" ] && [ "$(vols)" != "0 " ] || { EVID="$(rg) vol $(vols)"; fail t3_knob_one_page_volume; }; }
# the MP3 page is a list since Task 13 (ruling R23): the knob is the list's, also when it is too short to scroll (no ring, no volume change)
nowrun t3_knob_mp3_list "screen on; vol 0; @0.5; mp3ui; simknob 1; ring; simvol" && {
    [ "$(rg)" = "none|" ] && [ "$(vols)" = "0 " ] || { EVID="$(rg) vol $(vols)"; fail t3_knob_mp3_list; }; }
# Ruling P1: the playing ring (two pages on the cover page) keeps the knob as volume
nowrun t3_knob_playing_ring_volume "screen on; vol 0; track 3; @3; nowmenu -2; ring; nowmenu -5; simknob 1; ring; nowmenu -5; simvol" && {
    [ "$(rg)" = "page=0/2 sel=-1 center=Pause|page=0/2 sel=-1 center=Pause|" ] && [ "$(nm)" = "menu=1 page=0|menu=1 page=0|" ] && [ "$(vols)" != "0 " ] || { EVID="$(rg) $(nm) vol $(vols)"; fail t3_knob_playing_ring_volume; }; }
# F1: the Bluetooth ring selects first (the centre shows the item's name); one tap changes nothing, the second tap (or the centre) runs it
nowrun t3_bt_select_first "screen on; vol 0; btui; @0.3; simtap 40 180; ring; uitop; simtap 40 180; @0.3; uitop" && {
    [ "$(rg | head -c 100)" = "page=0/1 sel=1 center=Reboot into Speaker|" ] || { EVID="$(rg)"; fail t3_bt_name_in_centre; }   # the centre states what the cell does (its hint), not just its name
    [ "$(nrb)" = 1 ] || { EVID="reboots: $(nrb)"; fail t3_bt_second_tap_runs; }; }
# fx apps T15: the Bluetooth ring is two pages (6 cells + next, then the last cells: 清除配對 = the cell before the last, 說明 = the last). Its cells are no uiboxes buttons: the cells of
# page 2 are tapped by ring slot (cell k is on slot k % 6, see the slot table above); the cell numbers come from the ring's n= (a cell added before them at the merge moves nothing here)
nowrun t15_bt_ring_n "btui; @0.3; ring" && { [ "$(rn)" = "8 " ] || { EVID="n $(rn)"; fail t15_bt_ring_n; }; }
BTN=$(rn | tr -d ' '); BTN=${BTN:-8}
BTFT=${BTSLOT[$(( (BTN - 2) % 8 ))]}; BTHT=${BTSLOT[$(( (BTN - 1) % 8 ))]}
BTFP=$(( (BTN - 2) / 8 )); BTHP=$(( (BTN - 1) / 8 ))   # the page of 清除配對 (clear pairing) / 說明
BTFPRE=$(pre $BTFP); BTHPRE=$(pre $BTHP)
nowrun t15_bt_page2_cells "btui; @0.3; $BTHPRE ring; simtap $BTHT; @0.3; uitop" && {
    { [ "$(rg | head -c 20)" = "page=$BTHP/1 sel=-1 cent" ] && [ "$(tp)" = "explain " ]; } || { EVID="$(rg) top $(tp)"; fail t15_bt_page2_cells; }; }
nowrun t3_bt_one_tap_nothing "screen on; vol 0; btui; @0.3; simtap 40 180; @0.3; $BTFPRE simtap $BTFT; @0.3; uitop; ring" && {
    [ "$(nrb)" = 0 ] && [ "$(tp)" = "bt " ] || { EVID="reboots $(nrb) top $(tp)"; fail t3_bt_one_tap_nothing; }; }
nowrun t3_bt_centre_runs "screen on; vol 0; btui; @0.3; simtap 40 180; simtap 180 180; @0.3" && { [ "$(nrb)" = 1 ] || { EVID="reboots: $(nrb)"; fail t3_bt_centre_runs; }; }
nowrun t3_bt_forget_needs_confirm "screen on; vol 0; btui; @0.3; $BTFPRE simtap $BTFT; @0.3; uitop; ring; simtap $BTFT; @0.3; uitop" && {
    [ "$(tp)" = "bt message " ] || { EVID="top: $(tp)"; fail t3_bt_forget_confirm; }
    [ "$(rg | head -c 80)" = "page=$BTFP/1 sel=$((BTN - 2)) center=Forget paired|" ] || { EVID="$(rg)"; fail t3_bt_forget_centre; }; }
# F1: settings, USB drive mode: the first tap only rewrites the row, a second tap within 4 s runs it; 4 s, another row, the knob or leaving restore it
nowrun t3_usb_rows "setui; @0.3; settings group card; settings rows" && USBN=$(( $(grep '^APP settings row=' <<<"$out" | grep -n 'USB drive mode' | cut -d: -f1) - 1 ))
nowrun t3_usb_one_tap "setui; @0.3; settings key usb; settings rows; @3; settings rows; @2; settings rows" && {
    [ "$(nreq)" = 0 ] || { EVID="requests: $(nreq)"; fail t3_usb_one_tap_nothing; }
    [ "$(usb_rows)" = 2 ] || { EVID="armed dumps: $(usb_rows)"; fail t3_usb_armed_text; }   # armed at 0 s and 3 s, restored at 5 s
    [ "$(grep -c '^APP settings row=USB drive mode$' <<<"$out")" = 1 ] || { EVID="$(grep '^APP settings row=.*USB' <<<"$out")"; fail t3_usb_restored_4s; }; }   # the plain row only after the 4 s
nowrun t3_usb_second_tap "setui; @0.3; settings key usb; @1; settings key usb" && { [ "$(nreq)" = 1 ] || { EVID="requests: $(nreq)"; fail t3_usb_second_tap; }; }
nowrun t3_usb_late_second_tap "setui; @0.3; settings key usb; @4.5; settings key usb; settings rows" && {
    [ "$(nreq)" = 0 ] && [ "$(usb_rows)" = 1 ] || { EVID="requests $(nreq) armed $(usb_rows)"; fail t3_usb_late_second_tap; }; }
nowrun t3_usb_other_row "setui; @0.3; settings key usb; settings key toysfx; settings rows; settings key usb; settings rows" && {
    [ "$(nreq)" = 0 ] && [ "$(usb_rows)" = 1 ] || { EVID="requests $(nreq) armed $(usb_rows)"; fail t3_usb_other_row; }; }   # re-armed by the tap after the other row, not run
nowrun t3_usb_knob "setui; @0.3; settings key usb; @0.5; simknob 1; settings rows; settings key usb" && {
    [ "$(nreq)" = 0 ] && [ "$(usb_rows)" = 0 ] || { EVID="requests $(nreq) armed $(usb_rows)"; fail t3_usb_knob; }; }   # the knob restored it: the next tap arms again, nothing runs
nowrun t3_usb_leave "setui; @0.3; settings key usb; back; setui; @0.3; settings key usb; settings rows" && {
    [ "$(nreq)" = 0 ] && [ "$(usb_rows)" = 1 ] || { EVID="requests $(nreq) armed $(usb_rows)"; fail t3_usb_leave; }; }
# F7: the album rows are on the album page's own ring (interval, order, playing view); the settings page does not list them
nowrun t3_settings_no_album_rows "setui; @0.3; settings group display; settings rows; back; settings group sound; settings rows" && {
    ! grep '^APP settings row=' <<<"$out" | grep -qi 'Photo every\|Photo order\|Now playing\|相簿間隔\|相簿順序\|播放畫面' || { EVID="$(grep '^APP settings row=' <<<"$out")"; fail t3_settings_no_album_rows; }; }
# 3b: the main menu reopens the now-playing page of the source that owns playback; another source or nothing playing: the source's page as before
nowrun t3_menu_mp3_playing "screen on; vol 0; track 3; @3; back; uistack; simtap 81 279; simtap 81 279; @0.5; uistack; back; @0.3; uistack" && {
    [ "$(us)" = "menu|menu mp3 now|menu mp3|" ] || { EVID="$(us)"; fail t3_menu_mp3_playing; }; }
nowrun t3_menu_mp3_nothing "screen on; vol 0; simtap 81 279; simtap 81 279; @0.5; uistack" && { [ "$(us)" = "menu mp3|" ] || { EVID="$(us)"; fail t3_menu_mp3_nothing; }; }
nowrun t3_menu_mp3_playing_pick_chip "screen on; vol 0; track 3; @3; back; simtap 40 180; simtap 40 180; @0.5; uistack" && { [ "$(us)" = "menu chip|" ] || { EVID="$(us)"; fail t3_menu_chip_other_source; }; }
nowrun t3_menu_mp3_playing_pick_noise "screen on; vol 0; track 3; @3; back; simtap 81 81; simtap 81 81; @0.5; uistack" && { [ "$(us)" = "menu noise|" ] || { EVID="$(us)"; fail t3_menu_noise_other_source; }; }
nowrun t3_menu_noise_playing "screen on; vol 0; color 2; @2; back; uistack; simtap 81 81; simtap 81 81; @0.5; uistack; back; @0.3; uistack" && {
    [ "$(us)" = "menu|menu noise now|menu noise|" ] || { EVID="$(us)"; fail t3_menu_noise_playing; }; }
nowrun t3_menu_noise_playing_pick_mp3 "screen on; vol 0; color 2; @2; back; simtap 81 279; simtap 81 279; @0.5; uistack" && { [ "$(us)" = "menu mp3|" ] || { EVID="$(us)"; fail t3_menu_mp3_other_source; }; }
# 3c: a long press on a Back goes to the main menu (ui_home); the short tap is one level; the playing song goes on
nowrun t3_home_spectrum_ring "screen on; vol 0; track 3; @3; viz bars; @1; simtap 180 180; @0.3; nowmenu -5; uistack; simpress 180 320 3100; @0.3; uitop; uistack; nowmenu -3" && {
    [ "$(us)" = "menu now viz|menu|" ] && [ "$(tp)" = "menu " ] || { EVID="$(us) $(tp)"; fail t3_home_spectrum_ring; }
    grep -qx 'APP now paused=0 audio=playing' <<<"$out" || { EVID="$(grep '^APP now paused' <<<"$out")"; fail t3_home_song_plays; }; }
nowrun t3_home_spectrum_style_ring "screen on; vol 0; track 3; @3; stop; @1; viz bars; @1; simflick up; @0.3; simpress 180 320 3100; @0.3; uistack" && { [ "$(us)" = "menu|" ] || { EVID="$(us)"; fail t3_home_style_ring; }; }
nowrun t3_home_list_back_button "mp3ui; list; @0.5; uistack; simpress 180 318 3100; @0.3; uistack; uitop" && { [ "$(us)" = "menu mp3 tracks|menu|" ] && [ "$(tp)" = "menu " ] || { EVID="$(us) $(tp)"; fail t3_home_list_back; }; }
nowrun t3_back_short_press "mp3ui; list; @0.5; simflick up; @0.3; uistack; simflick up; @0.3; uistack" && { [ "$(us)" = "menu mp3|menu|" ] || { EVID="$(us)"; fail t3_back_short_press; }; }
nowrun t3_home_ring_back_deep "screen on; tools; uipush colors; uistack; @0.3; simpress 180 320 3100; @0.3; uistack" && { [ "$(us)" = "menu tools colors|menu|" ] || { EVID="$(us)"; fail t3_home_ring_back_deep; }; }
nowrun t3_home_ring_back_deep_short "screen on; tools; uipush colors; @0.3; simflick up; @0.3; uistack" && { [ "$(us)" = "menu tools|" ] || { EVID="$(us)"; fail t3_home_ring_back_short; }; }
nowrun t3_home_bubbles "screen on; style next; @0.3; tools; uipush colors; @0.3; simpress 180 318 3100; @0.3; uistack" && { [ "$(us)" = "menu|" ] || { EVID="$(us)"; fail t3_home_bubbles; }; }
nowrun t3_press_cell_is_tap "screen on; simpress 81 279 600; ring; simpress 81 279 600; @0.3; uistack" && {
    [ "$(rg)" = "page=0/2 sel=0 center=Songs|" ] && [ "$(us)" = "menu mp3|" ] || { EVID="$(rg) $(us)"; fail t3_press_cell_is_tap; }; }
nowrun t3_home_bottom_noop "screen on; simpress 180 320 3100; simpress 180 180 3100; @0.3; uistack; ring" && { [ "$(us)" = "menu|" ] || { EVID="$(us)"; fail t3_home_bottom_noop; }; }
nowrun t3_uihome "mp3ui; list; uihome; uistack; uihome; uistack" && { [ "$(us)" = "menu|menu|" ] || { EVID="$(us)"; fail t3_uihome; }; }
# Addition A: bubble style ("style next" switches 選單樣式): no next-page cell, so the knob is what reaches page 2 of the main menu (設定 is idx 11 = bubble slot 4 there since 音效 joined)
nowrun t3_bubbles_knob_menu_page2 "screen on; vol 0; style next; @0.5; ring; simknob 1; ring; simtap $(bub 4); simtap $(bub 4); @0.5; uitop; simvol" && {
    [ "$(rg | cut -d'|' -f1 | cut -c1-8)" = "page=0/2" ] && [ "$(rg | cut -d'|' -f2 | cut -c1-8)" = "page=1/2" ] || { EVID="$(rg)"; fail t3_bubbles_knob_page; }
    [ "$(tp)" = "settings " ] && [ "$(vols)" = "0 " ] || { EVID="top $(tp) vol $(vols)"; fail t3_bubbles_settings_reachable; }; }
# 小工具 in bubbles (no next-page cell): page 1 holds seven cells (the moon left it), 捏泡泡紙 is the eighth on page 2, so 單字 is still reachable without the knob
nowrun t3_bubbles_tools_last_cell "screen on; vol 0; style next; @0.5; tools; ring; simtap $(bub 6); simtap $(bub 6); @0.5; uitop" && {
    [ "$(rg | cut -c1-8)" = "page=0/2" ] && [ "$(tp)" = "vocab " ] || { EVID="$(rg) top $(tp)"; fail t3_bubbles_tools_last_cell; }; }
# Addition B: a cell with a consequence says it in the centre while selected (ui_ring_set_item_hint); one tap never runs it, the second does
SIM_STATE="bt=on" nowrun t3_hint_bt "screen on; vol 0; btui; @0.3; simtap 81 81; ring; simtap 40 180; ring; $BTFPRE simtap $BTFT; ring" && {
    [ "$(rg)" = "page=0/1 sel=2 center=Reboot into Headphones|page=0/1 sel=1 center=Speaker|page=$BTFP/1 sel=$((BTN - 2)) center=Forget 2 pairings|" ] || { EVID="$(rg)"; fail t3_hint_bt; }   # the running mode (Speaker) has no consequence: its name
    [ "$(nrb)" = 0 ] || { EVID="reboots $(nrb)"; fail t3_hint_bt_one_tap; }; }
SIM_STATE="bt=on" nowrun t3_hint_bt_second_tap "screen on; vol 0; btui; @0.3; simtap 81 81; simtap 81 81; @0.3" && { [ "$(nrb)" = 1 ] || { EVID="reboots $(nrb)"; fail t3_hint_bt_second_tap; }; }
# the quality cell is an axis since UI unify T5: broadcasting, a tap changes a pending value (the centre says so, nothing reboots); the centre confirms
nowrun t3_hint_instrument "screen on; vol 0; @0.5; simtap 180 320; ring; uitop" && {
    [ "$(rg)" = "page=0/2 sel=7 center=Reboots into the instrument/(music stops)|" ] && ! grep -q '^APP instrument request' <<<"$out" || { EVID="$(rg)"; fail t3_hint_instrument; }; }
nowrun t3_hint_instrument_second "screen on; vol 0; @0.5; simtap 180 320; simtap 180 320; @0.3" && { [ "$(grep -c '^APP instrument request' <<<"$out")" = 1 ] || { fail t3_hint_instrument_second; }; }
# --- end Task 3 ---

