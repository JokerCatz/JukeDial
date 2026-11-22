# SPDX-License-Identifier: LGPL-2.1-or-later
# --- Task 7: axis ring shared, knob on multi-value controls ---
# "cfg <key>" prints APP cfg <key>=<value>; "clockface ?" prints APP clockface shown=<face on the clock page, - when none> stored=<cfg face> ring=<the face ring is open>
# (the screen wakes with a 400 ms knob lockout: cases that turn the knob wait 0.5 s after "screen on")
cf() { grep '^APP clockface shown=' <<<"$out" | sed 's/^APP clockface //' | tr '\n' '|'; }
# 2c(1): the 小工具 moon face keeps itself over a rebuild (console face change on top, theme change with the clock ring open) and never writes last_screen
nowrun clock_rebuild_keeps_moon "clockface classic; tools moon; @1; cfg last_screen; clockface digital; clockface ?; simtap 180 180; @0.3; theme next; @0.3; back; @0.3; clockface ?; cfg last_screen; uistack" && {
    [ "$(cf)" = "shown=moon stored=digital ring=0|shown=moon stored=digital ring=0|" ] || { EVID="$(cf)"; fail clock_rebuild_keeps_moon; }
    ls=$(cv last_screen); [ "${ls%%|*}|${ls%%|*}|" = "$ls" ] && [ "${ls%%|*}" != clock ] || { EVID="last_screen: $ls"; fail clock_rebuild_no_last_screen; }
    stack_is clock_rebuild_moon_stack 1 "menu tools clock"; }
# the normal clock still follows a rebuild to the stored face and is the last screen
nowrun clock_rebuild_normal "clock; @1; clockface digital; clockface ?; cfg last_screen" && {
    [ "$(cf)" = "shown=digital stored=digital ring=0|" ] && [ "$(cv last_screen)" = "clock|" ] || { EVID="$(cf) $(cv last_screen)"; fail clock_rebuild_normal; }; }
# 2c(3): Task 6's clock exit with the bubble ring style: the bare Back button (180,318) leaves the clock, the centre with nothing picked closes only the ring
NOWCFG="ui_style=bubble"
nowrun clock_bubble_back_leaves "clock; @1; simtap 180 180; @0.3; uistack; simflick up; @0.3; uistack" && {
    stack_is clock_bubble_ring_open 1 "menu clock clock_menu"; stack_is clock_bubble_back_leaves 2 "menu"; }
nowrun clock_bubble_centre_closes "clock; @1; simtap 180 180; @0.3; simtap 180 180; @0.3; uistack" && stack_is clock_bubble_centre_closes 1 "menu clock"
NOWCFG=""
# 2c(2): the spaces after a stripped 【artist】 go too (the fixture's songs with a space after the bracket)
fresh; mkdir "$T/fix/export/新歌"
for n in "【Aimer】 甲" "【Aimer】乙"; do cp "$T/fix/export/紙飛機工廠/04_untitled_demo.mp3" "$T/fix/export/新歌/$n.mp3"; done
out=$($SIM --headless --root "$T/fix" --cmd "scan; @0.2; uipush tracks2; @0.5; uiboxes" 2>&1)
{ grep -q '^APP uibox button .*|甲$' <<<"$out" && grep -q '^APP uibox button .*|乙$' <<<"$out"; } || { EVID="$(grep '^APP uibox button' <<<"$out" | tr '\n' ' ')"; fail tracks_prefix_space_stripped; }
# ---- Step 2 / 2b (rule R21): a control with three or more values is an axis: a tap makes it current and steps +1, the knob steps it both ways (wraps, at once),
# 4 s after the last action the knob does its normal job again and the value is the last one shown. Two-value switches stay taps. (vols: the Task 7 spectrum block above)
# the clock face: the ring's 錶面 cell (top, 180,40) opens a one-cell face ring over the face (the face visible behind it); its cell is bottom left (81,279)
nowrun clockface_axis "screen on; @0.5; vol 0; clockface classic; clock; @1; simtap 180 180; @0.3; simtap 279 279; @0.3; clockface ?; simknob 2; clockface ?; simknob -1; clockface ?; @4.5; clockface ?; simvol; simknob 1; simvol; clockface ?; back; @0.3; clock; @0.5; clockface ?" && {
    [ "$(cf)" = "shown=minimal stored=classic ring=1|shown=sun stored=classic ring=1|shown=digital stored=classic ring=1|shown=digital stored=digital ring=0|shown=digital stored=digital ring=0|shown=digital stored=digital ring=0|" ] || { EVID="$(cf)"; fail clockface_axis; }
    [ "$(vols)" = "0 2 " ] || { EVID="volumes: $(vols)"; fail clockface_axis_volume_after; }; }
nowrun clockface_axis_taps "screen on; @0.5; vol 0; clockface classic; clock; @1; simtap 180 180; @0.3; simtap 279 279; @0.3; simtap 81 279; @0.3; simtap 81 279; @0.3; clockface ?; simvol; simknob 1; simvol; clockface ?" && {
    [ "$(cf)" = "shown=sun stored=classic ring=1|shown=rings stored=classic ring=1|" ] && [ "$(vols)" = "0 0 " ] || { EVID="$(cf) volumes: $(vols)"; fail clockface_axis_taps; }; }
# the face ring's Back returns to the clock ring (the face stored), the centre closes it; leaving the page with it open stores the face too
nowrun clockface_axis_back "screen on; @0.5; clockface classic; clock; @1; simtap 180 180; @0.3; simtap 279 279; @0.3; simflick up; @0.3; uistack; clockface ?; back; @0.3; simtap 180 180; @0.3; simtap 279 279; @0.3; simtap 180 180; @0.3; uistack; clockface ?; simtap 180 180; @0.3; simtap 279 279; @0.3; uihome; @0.3; clockface ?" && {
    stack_is clockface_back_menu 1 "menu clock clock_menu"; stack_is clockface_centre_closes 2 "menu clock"
    [ "$(cf)" = "shown=minimal stored=minimal ring=0|shown=digital stored=digital ring=0|shown=- stored=sun ring=0|" ] || { EVID="$(cf)"; fail clockface_axis_back; }; }
# the album ring: 間隔 (left, 45,180) and 播放畫面 (top, 180,45) are axes; 順序 (two values, upper left 87,87) stays a tap and leaves the knob to the volume
nowrun album_now_axis "screen on; @0.5; vol 0; album; @0.5; albummenu -2; @0.3; albummenu 2; @0.2; cfg now_view; simknob 2; cfg now_view; simknob -1; cfg now_view; @4.5; simvol; simknob 1; simvol; cfg now_view" && {
    [ "$(cv now_view)" = "album|cover|viz|viz|" ] && [ "$(vols)" = "0 2 " ] || { EVID="view $(cv now_view) vol $(vols)"; fail album_now_axis; }; }
nowrun album_order_tap "screen on; @0.5; vol 0; album; @0.5; albummenu -2; @0.3; albummenu 1; @0.2; cfg album_order; simvol; simknob 1; simvol; cfg album_order" && {
    [ "$(cv album_order)" = "order|order|" ] && [ "$(vols)" = "0 2 " ] || { EVID="order $(cv album_order) vol $(vols)"; fail album_order_tap; }; }
# the playing ring's play order (MP3: three orders, cell 4): the knob is the order's for 4 s after the tap, the volume otherwise; another cell gives it back at once
nowrun nowmenu_order_axis "screen on; @0.5; vol 0; track 3; @3; nowmenu -2; nowmenu 4; cfg mp3_order; simknob 2; cfg mp3_order; simknob -1; cfg mp3_order; @4.5; simvol; simknob 1; simvol; cfg mp3_order" && {
    [ "$(cv mp3_order)" = "1|0|2|2|" ] && [ "$(vols)" = "0 2 " ] || { EVID="order $(cv mp3_order) vol $(vols)"; fail nowmenu_order_axis; }; }
nowrun nowmenu_order_taps "screen on; @0.5; vol 0; track 3; @3; nowmenu -2; nowmenu 4; nowmenu 4; cfg mp3_order; nowmenu -1; simvol; simknob 1; simvol; cfg mp3_order; nowmenu -2; simknob 1; simvol; cfg mp3_order" && {
    [ "$(cv mp3_order)" = "2|2|2|" ] && [ "$(vols)" = "0 2 4 " ] || { EVID="order $(cv mp3_order) vol $(vols)"; fail nowmenu_order_taps; }; }
# the settings rows with three or more values: tap the row, the knob changes it instead of scrolling for 4 s, the row in the selection colour; then the knob scrolls again
settings_axis set_sleep_axis sleep "Sleep: " "15 min" "60 min" "30 min" "30 min"
# the theme rebuilds every page: the borrowed row survives it (the knob keeps changing the theme, not scrolling), then the list scrolls again
nowrun set_theme_names "setui; @0.3; settings group display; settings rows; theme next; settings rows; theme next; settings rows; theme next; settings rows" && TH=$(srow "Theme: ")
settings_axis set_theme_axis theme "Theme: " "$(cut -d'|' -f2 <<<"$TH")" "$(cut -d'|' -f4 <<<"$TH")" "$(cut -d'|' -f3 <<<"$TH")" "$(cut -d'|' -f3 <<<"$TH")"
# two-value rows stay taps: 震動 toggles on/off (the strength and the length are on the web page), the knob scrolls
nowrun set_haptic_tap "screen on; @0.5; setui; @0.3; settings group sound; uiboxes; settings key haptic; settings rows; cfg haptic_on; simknob 1; uiboxes; settings key haptic; settings rows" && {
    set -- $(sy); [ "$(srow "Haptics: ")" = "off|on|" ] && [ "$(grep '^APP cfg haptic_on' <<<"$out" | tr '\n' ' ')" = "APP cfg haptic_on=0 " ] && [ "$1" != "$2" ] || { EVID="$(srow "Haptics: ") tops $* $(grep '^APP cfg haptic' <<<"$out")"; fail set_haptic_tap; }; }
# the vocab home's direction cell is an axis too (t7_vocab_dir); the alarm page's 重複 row (four choices) is one
nowrun alarm_days_axis "screen on; @0.5; vol 0; show alarm; @0.5; uiboxes" && {
    set -- $(grep '^APP uibox button .*|\(Repeat\|重複\): ' <<<"$out" | awk '{print int(($4 + $6) / 2), int(($5 + $7) / 2)}')
    nowrun alarm_days_axis "screen on; @0.5; vol 0; show alarm; @0.5; simtap $1 $2; @0.2; uiboxes; simknob 2; uiboxes; simknob -1; uiboxes; @4.5; simknob 1; uiboxes" && {
        [ "$(ar | cut -d'|' -f1-4)" = "Every day|Once|Weekend|Weekend" ] || { EVID="$(ar)"; fail alarm_days_axis; }; }; }

# --- Task 13: pickers as lists, rescan, stop ---
# Stations, scenes, systems and folders are scrolling lists (ruling R23): every row reachable with the knob and one tap, rows never intersect, the current
# entry in the selection colour and in view at open, long names one line. "APP pick <page> <index>" is printed by a row's tap (index: station, scene,
# system or folder). The list window on this screen is y 35..285 (ui_list: 250 high at centre offset -20).
# rows of the list in the n-th "uiboxes" dump (1-based): "x1 y1 x2 y2|text" per button, Back left out
# every row from <first> (0-based among the list's rows) to <first>+<count>-1 is reached: the knob until the row is inside the window, then one tap on it
# (one run per row: a pick makes its entry the current one, which the next open scrolls to); PICKS = the "APP pick <page> <i>" indices, in row order;
# <verify cmds> run after the last row's tap, LASTOUT = that run's output (what really plays / opens)
# the colour of each list row (a pixel inside its left padding), in row order: "<colour>|<text>"; "------" for a row not wholly inside the list window
pickers_fix
# --- Step 2: stations ---
picker_each t13_radio_each "uipush radio" 0 20 radio @0 '@0.3; uitop; {"op":"status"}'
grep -qx 'APP uitop now' <<<"$LASTOUT" && grep -q '^CTL .*"playing":"Sim 電台 19\\n".*"radio":19,' <<<"$LASTOUT" ||
    { EVID="$(grep '^APP uitop\|^CTL' <<<"$LASTOUT" | cut -c1-120 | tr '\n' ' ')"; fail t13_radio_last_plays; }   # the last row: station 19 plays, the playing page is on top
[ "$PICKS" = "$(seq -s ' ' 0 19) " ] || { EVID="picks: $PICKS"; fail t13_radio_each; }
rows_disjoint "$(awk '/^APP uiboxes end/{exit} {print}' <<<"$BOXES")" || { EVID="$(rows_disjoint "$BOXES")"; fail t13_radio_disjoint; }
r=$(block_rows "$BOXES" 1); h_long=$(grep '|Retro Lofi Chip' <<<"$r" | awk '{print $4 - $2}'); h_short=$(sed -n 1p <<<"$r" | awk '{print $4 - $2}')   # uiboxes prints the label's text, which the dots replaced
[ -n "$h_long" ] && [ "$h_long" = "$h_short" ] || { EVID="row heights long=$h_long short=$h_short"; fail t13_radio_long_one_line; }
pfresh; out=$($SIM --headless --root "$T/fix" --cmd "screen on; radio 13; @0.3; uihome; uipush radio; @0.3; uiboxes; shot $T/radio_cur.png" 2>&1)
rc_=$(row_colours "$T/radio_cur.png" "$out"); cur=$(sed -n 14p <<<"$rc_" | cut -d'|' -f1); oth=$(sed -n 13p <<<"$rc_" | cut -d'|' -f1)
[ -n "$cur" ] && [ "$cur" != "------" ] && [ "$oth" != "------" ] && [ "$cur" != "$oth" ] || { EVID="$(tr '\n' ' ' <<<"$rc_")"; fail t13_radio_current_marked; }
pfresh; out=$($SIM --headless --root "$T/fix" --cmd "screen on; uipush radio; @0.3; uiboxes; shot $T/radio_none.png" 2>&1)
[ "$(row_colours "$T/radio_none.png" "$out" | cut -d'|' -f1 | grep -v -- '------' | sort -u | wc -l)" = 1 ] || { EVID="$(row_colours "$T/radio_none.png" "$out" | tr '\n' ' ')"; fail t13_radio_none_marked; }
fresh; : > "$T/fix/_player/config/radio.txt"; out=$($SIM --headless --root "$T/fix" --cmd "uipush radio; @0.3; uiboxes" 2>&1)
grep -q '^APP uibox label .*|\(沒有電台/\|No stations/\)/sdcard/_player/config/radio.txt$' <<<"$out" && [ "$(list_rows "$out" | wc -l)" = 2 ] || { EVID="$(grep '^APP uibox' <<<"$out" | tr '\n' ' ')"; fail t13_radio_empty_note; }   # no stations: the 廣播電台 row (BR) and the end row 在電腦上編輯電台 (fx apps T14)
# --- Step 2: scenes (噪音顏色 first, then the scenes; the fixture's favourite campfire leads) ---
picker_each t13_noise_each "noiseui" 2 30 noise "simnoise 30 3" "@0.3; uitop; simnoise ?"
grep -qx 'APP uitop now' <<<"$LASTOUT" && grep -qx 'SIM noise current=29' <<<"$LASTOUT" || { EVID="$(grep '^APP uitop\|^SIM noise current' <<<"$LASTOUT" | tr '\n' ' ')"; fail t13_noise_last_plays; }   # the last row: scene 29 (campfire, the favourite, leads)
[ "$(tr ' ' '\n' <<<"$PICKS" | grep -c .)" = 30 ] && [ "$(tr ' ' '\n' <<<"$PICKS" | grep . | sort -n | uniq | tr '\n' ' ')" = "$(seq -s ' ' 0 29) " ] || { EVID="picks: $PICKS"; fail t13_noise_each; }
rows_disjoint "$(awk '/^APP uiboxes end/{exit} {print}' <<<"$BOXES")" || fail t13_noise_disjoint
r=$(block_rows "$BOXES" 1); [ "$(sed -n 1p <<<"$r" | cut -d'|' -f2)" = "噪音顏色" ] || [ "$(sed -n 1p <<<"$r" | cut -d'|' -f2)" = "Noise colours" ] || { EVID="first row: $(sed -n 1p <<<"$r")"; fail t13_noise_colours_first; }
[ "$(sed -n 2p <<<"$r" | cut -d'|' -f2)" = "呼吸教練" ] || [ "$(sed -n 2p <<<"$r" | cut -d'|' -f2)" = "Breathing coach" ] || { EVID="second row: $(sed -n 2p <<<"$r")"; fail t13_noise_breath_second; }   # G7 (owner): the breathing coach is the second row
pfresh; out=$($SIM --headless --root "$T/fix" --cmd "screen on; simnoise 30 3; noise 22; noiseui; @0.3; uiboxes; shot $T/noise_cur.png" 2>&1)
rc_=$(row_colours "$T/noise_cur.png" "$out"); cur=$(grep '|情境 22$' <<<"$rc_" | cut -d'|' -f1); oth=$(grep '|情境 21$' <<<"$rc_" | cut -d'|' -f1)
[ -n "$cur" ] && [ "$cur" != "------" ] && [ "$oth" != "------" ] && [ "$cur" != "$oth" ] || { EVID="$(tr '\n' ' ' <<<"$rc_")"; fail t13_noise_current_marked; }
out=$($SIM --headless --root "$T/fix" --cmd "screen on; simnoise 30 3; noiseui; @0.3; uiboxes; shot $T/noise_none.png" 2>&1)
rc_=$(row_colours "$T/noise_none.png" "$out"); sys=$(sed -n 1p <<<"$rc_" | cut -d'|' -f1)
[ "$sys" != "------" ] && [ "$(sed 1,2d <<<"$rc_" | cut -d'|' -f1 | grep -v -- '------' | sort -u | wc -l)" = 1 ] && [ "$(sed -n 2p <<<"$rc_" | cut -d'|' -f1)" = "$sys" ] && [ "$(sed -n 3p <<<"$rc_" | cut -d'|' -f1)" != "$sys" ] || { EVID="$(tr '\n' ' ' <<<"$rc_")"; fail t13_noise_none_marked; }
h_long=$(grep '|Rain on a tin roof' <<<"$(block_rows "$out" 1)" | awk '{print $4 - $2}'); h_short=$(grep '|情境 05$' <<<"$(block_rows "$out" 1)" | awk '{print $4 - $2}')
[ -n "$h_long" ] && [ "$h_long" = "$h_short" ] || { EVID="row heights long=$h_long short=$h_short"; fail t13_noise_long_one_line; }
# --- Step 3: systems (the play order last, a tap switches it) and folders (全部隨機 first, 重新掃描 last) ---
picker_each t13_chip_each "chipui" 0 12 chip @0 "@0.3; uitop"
grep -qx 'APP uitop chip_games' <<<"$LASTOUT" && grep -q '^APP chip games system=ZX Spectrum n=' <<<"$LASTOUT" || { EVID="$(grep '^APP uitop\|^APP chip games' <<<"$LASTOUT" | tr '\n' ' ')"; fail t13_chip_last_opens; }   # the last system's games page
[ "$PICKS" = "$(seq -s ' ' 0 11) " ] || { EVID="picks: $PICKS"; fail t13_chip_each; }
rows_disjoint "$(awk '/^APP uiboxes end/{exit} {print}' <<<"$BOXES")" || fail t13_chip_disjoint
pfresh; out=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; chipui; @0.3; simknob 9; @0.1; uiboxes" 2>&1)
ob=$(block_rows "$out" 1 | tail -1); set -- ${ob%%|*} 0 0 0 0
out=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; chipui; @0.3; simknob 9; @0.1; uiboxes; simtap $((($1 + $3) / 2)) $((($2 + $4) / 2)); @0.2; uiboxes; uitop" 2>&1)
o1=$(block_rows "$out" 1 | tail -1 | cut -d'|' -f2); o2=$(block_rows "$out" 2 | tail -1 | cut -d'|' -f2)
case "$o1|$o2" in "Play order: In order|Play order: Random"|"Play order: Random|Play order: In order"|"播放順序: 循序|播放順序: 隨機"|"播放順序: 隨機|播放順序: 循序") ;; *) EVID="$o1 -> $o2"; fail t13_chip_order_row;; esac
grep -qx 'APP uitop chip' <<<"$out" || fail t13_chip_order_stays
picker_each t13_mp3_each "mp3ui" 1 15 mp3 @0 "@0.3; uitop; libls 14; uiboxes"
lt=$(grep '^APP libls [0-9]' <<<"$LASTOUT" | head -1 | sed 's/^APP libls [0-9]* \(.*\) | .*/\1/')
grep -qx 'APP uitop mp3' <<<"$LASTOUT" && [ -n "$lt" ] && grep -q "^APP uibox button .*|$lt\$" <<<"$LASTOUT" || { EVID="title '$lt' $(grep '^APP uitop' <<<"$LASTOUT")"; fail t13_mp3_last_opens; }   # the last folder's song list: the next level of the tree opens IN the page
[ "$PICKS" = "$(seq -s ' ' 0 14) " ] || { EVID="picks: $PICKS"; fail t13_mp3_each; }
rows_disjoint "$(awk '/^APP uiboxes end/{exit} {print}' <<<"$BOXES")" || fail t13_mp3_disjoint
r=$(block_rows "$BOXES" 1); { grep -q '^.*|\(全部隨機\|Shuffle all\)$' <<<"$(sed -n 1p <<<"$r")" && block_rows "$BOXES" 17 | tail -1 | grep -q '|\(重新掃描曲庫\|Rescan library\)$'; } ||
    { EVID="first $(sed -n 1p <<<"$r") last $(block_rows "$BOXES" 17 | tail -1)"; fail t13_mp3_rows; }
pfresh; out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; mp3 11; @1; uihome; mp3ui; @0.3; uiboxes; shot $T/mp3_cur.png" 2>&1)
rc_=$(row_colours "$T/mp3_cur.png" "$out"); cur=$(sed -n 13p <<<"$rc_" | cut -d'|' -f1); oth=$(sed -n 12p <<<"$rc_" | cut -d'|' -f1)   # row 1 全部隨機, row k+2 folder k
[ "$oth" != "------" ] || { EVID="$(tr '\n' ' ' <<<"$rc_")"; fail t13_mp3_current_neighbour; }
[ -n "$cur" ] && [ "$cur" != "------" ] && [ "$cur" != "$oth" ] || { EVID="$(tr '\n' ' ' <<<"$rc_")"; fail t13_mp3_current_marked; }
# R17 still holds: the main menu's MP3 entry with MP3 playing opens the now-playing page, not the list
pfresh; out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; track 3; @2; uihome; simtap 81 279; simtap 81 279; @0.5; uistack" 2>&1)
[ "$(grep '^APP uistack' <<<"$out")" = "APP uistack menu mp3 now" ] || { EVID="$(grep '^APP uistack' <<<"$out")"; fail t13_mp3_r17; }
# --- Step 4: rescan asks twice; one tap never stops playback; the scan ends on the page it was started from ---
pfresh; out=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; vol 0; track 3; @2; uihome; mp3ui; @0.3; simknob 20; @0.1; uiboxes" 2>&1)   # the same state as the runs below
RS=$(block_rows "$out" 1 | tail -1); set -- ${RS%%|*} 0 0 0 0; RX=$((($1 + $3) / 2)); RY=$((($2 + $4) / 2))
nowrun t13_rescan_mp3_one_tap "screen on; @0.5; vol 0; track 3; @2; uihome; mp3ui; @0.3; simknob 20; @0.1; simtap $RX $RY; @0.2; nowmenu -3; uiboxes; @4.5; nowmenu -3; uiboxes; uistack" && {
    [ "$(pa)" = "playing playing " ] || { EVID="audio after one tap / +4.5 s: $(pa)"; fail t13_rescan_mp3_one_tap; }
    [ "$(block_rows "$out" 1 | tail -1 | cut -d'|' -f2)" != "$(block_rows "$out" 2 | tail -1 | cut -d'|' -f2)" ] && grep -q '|\(再點一次：重新掃描\|Tap again: rescan\)$' <<<"$(block_rows "$out" 1 | tail -1)" ||
        { EVID="$(block_rows "$out" 1 | tail -1) then $(block_rows "$out" 2 | tail -1)"; fail t13_rescan_mp3_armed_text; }
    stack_is t13_rescan_mp3_one_tap_stack 1 "menu mp3"; }
nowrun t13_rescan_mp3_two_taps "screen on; @0.5; vol 0; track 3; @2; uihome; mp3ui; @0.3; simknob 20; @0.1; simtap $RX $RY; @0.5; simtap $RX $RY; @1; uistack; nowmenu -3" && {
    stack_is t13_rescan_mp3_two_taps 1 "menu mp3"; grep -q '^APP scan n=' <<<"$out" || { EVID="no scan line"; fail t13_rescan_mp3_scanned; }; }
nowrun t13_rows "setui; @0.3; settings group card; settings rows" && SRESCAN=$(( $(grep '^APP settings row=' <<<"$out" | grep -n 'Rescan library' | cut -d: -f1) - 1 ))   # the rescan row's place in the dump (tapped by key)
nowrun t13_rescan_settings_one_tap "screen on; @0.5; vol 0; track 3; @2; uihome; setui; @0.3; settings key rescan; nowmenu -3; settings rows; @4.5; nowmenu -3; settings rows; uistack" && {
    [ "$(pa)" = "playing playing " ] || { EVID="audio: $(pa)"; fail t13_rescan_settings_one_tap; }
    [ "$(grep '^APP settings row=' <<<"$out" | sed -n "$((SRESCAN + 1))p" | sed 's/^APP settings row=//')" = "Tap again: rescan" ] || { EVID="$(grep '^APP settings row=' <<<"$out" | sed -n "$((SRESCAN + 1))p")"; fail t13_rescan_settings_armed_text; }
    [ "$(grep -c '^APP settings row=Tap again: rescan$' <<<"$out")" = 1 ] || fail t13_rescan_settings_restored
    stack_is t13_rescan_settings_one_tap_stack 1 "menu settings setgroup"; }
nowrun t13_rescan_settings_two_taps "screen on; @0.5; vol 0; track 3; @2; uihome; setui; @0.3; settings key rescan; @0.5; settings key rescan; @1; uistack" && {
    stack_is t13_rescan_settings_two_taps 1 "menu settings setgroup"; grep -q '^APP scan n=' <<<"$out" || { EVID="no scan line"; fail t13_rescan_settings_scanned; }; }
# another row or the knob in between: the first tap is forgotten
nowrun t13_rescan_settings_other_row "screen on; @0.5; vol 0; track 3; @2; uihome; setui; @0.3; settings key rescan; settings key toysfx; settings key rescan; @0.5; uistack; nowmenu -3" && {
    stack_is t13_rescan_settings_other_row 1 "menu settings setgroup"; [ "$(pa)" = "playing " ] || { EVID="audio: $(pa)"; fail t13_rescan_settings_other_row_audio; }; }
# --- Step 5: when the source stops with the cover or spectrum page on top, the page leaves by itself ---
nowrun t13_stop_cells "screen on; vol 0; setui; track 3; @2; nowmenu -2; nowmenu -5" && STOPI=$(( $(stopidx) - 1 ))
nowrun t13_stop_cover "screen on; vol 0; setui; track 3; @2; uistack; nowmenu -2; nowmenu $STOPI; @0.5; uistack; uitop" && {
    stack_is t13_stop_cover_before 1 "menu settings now"; stack_is t13_stop_cover 2 "menu settings"; }
nowrun t13_stop_viz "screen on; vol 0; setui; track 3; @2; viz bars; @0.5; uistack; stop; @0.5; uistack" && {
    stack_is t13_stop_viz_before 1 "menu settings now viz"; stack_is t13_stop_viz 2 "menu settings"; }
# with the playing ring open and the play order holding the knob: everything goes, the knob is the volume again, no timer is left over
# (on Settings the knob scrolls the list once nothing borrows it: the first row's top moves (sy, Task 7 block) and the play order stays)
nowrun t13_stop_viz_ring "screen on; @0.5; vol 0; setui; @0.3; simtimers; track 3; @2; viz bars; @0.5; nowmenu -2; nowmenu 4; nowmenu -5; cfg mp3_order; stop; @0.5; uistack; uiboxes; simknob 1; uiboxes; cfg mp3_order; @2; simtimers" && {
    stack_is t13_stop_viz_ring 1 "menu settings"; set -- $(sy); o=$(grep '^APP cfg mp3_order=' <<<"$out" | sed 's/.*=//' | tr '\n' ' ')
    [ "$1" != "$2" ] && [ "$(cut -d' ' -f1 <<<"$o")" = "$(cut -d' ' -f2 <<<"$o")" ] || { EVID="tops $* orders $o"; fail t13_stop_viz_ring_knob; }
    [ "$(grep '^SIM timers' <<<"$out" | sed -n 1p)" = "$(grep '^SIM timers' <<<"$out" | sed -n 2p)" ] || { EVID="$(grep '^SIM timers' <<<"$out" | tr '\n' ' ')"; fail t13_stop_viz_ring_timers; }; }
nowrun t13_stop_viz_style "screen on; @0.5; vol 0; setui; @0.3; simtimers; track 3; @2; viz bars; @0.5; viz menu; stop; @0.5; uistack; uiboxes; simknob 1; uiboxes; viz stat; @2; simtimers" && {
    stack_is t13_stop_viz_style 1 "menu settings"; set -- $(sy)
    [ "$1" != "$2" ] && grep -q '^APP viz open=0 .* ring=0 knob=vol ' <<<"$out" || { EVID="tops $* $(grep '^APP viz open' <<<"$out" | tail -1)"; fail t13_stop_viz_style_knob; }
    [ "$(grep '^SIM timers' <<<"$out" | sed -n 1p)" = "$(grep '^SIM timers' <<<"$out" | sed -n 2p)" ] || { EVID="$(grep '^SIM timers' <<<"$out" | tr '\n' ' ')"; fail t13_stop_viz_style_timers; }; }
# pause is not stop; a page opened with nothing playing stays
nowrun t13_pause_stays "screen on; vol 0; setui; track 3; @2; nowmenu -2; nowmenu -1; @1; uistack" && stack_is t13_pause_stays 1 "menu settings now"
nowrun t13_idle_viz_stays "screen on; vol 0; setui; viz bars; @1; uistack" && stack_is t13_idle_viz_stays 1 "menu settings now viz"
# --- Step 6: scene editor (7 layers: the knob scrolls the layer column until "+ 加聲音" can be tapped; the delete arm ends with its hint) ---
nowrun t13_scene_add_reach "screen on; @0.5; vol 0; simnoise 30 7 custom; sceneui 0; @0.3; uiboxes; simknob 7; @0.1; uiboxes; simvol" && {
    ab=$(grep '^APP uibox button .*|＋ \(加聲音\|Add a sound\)$' <<<"$(awk '/^APP uiboxes end/{k++; next} k==1' <<<"$out")" | awk '{print $4, $5, $6, $7}')
    a0=$(grep '^APP uibox button .*|＋ \(加聲音\|Add a sound\)$' <<<"$(awk '/^APP uiboxes end/{exit} {print}' <<<"$out")" | awk '{print $5}')
    read -r x1 y1 x2 y2 <<<"$ab"
    if [ -n "$ab" ] && [ "$y1" -ge 50 ] && [ "$y2" -le 290 ] && [ "$a0" -gt 290 ]; then
        nowrun t13_scene_add_tap "screen on; @0.5; vol 0; simnoise 30 7 custom; sceneui 0; @0.3; simknob 7; @0.1; simtap $(((x1 + x2) / 2)) $(((y1 + y2) / 2)); @0.3; uitop" &&
            { grep -qx 'APP uitop sound_cats' <<<"$out" || { EVID="$(grep '^APP uitop' <<<"$out")"; fail t13_scene_add_tap; }; }
    else EVID="add row before $a0, after knob: $ab"; fail t13_scene_add_reach; fi; }
nowrun t13_scene_save_text "sceneui 0; @0.3; uiboxes" && {   # the rows (the knob scrolls the ones below the fold into view)
    grep -q '^APP uibox button .*|\(存成新情境\|Save as new\)$' <<<"$out" || { EVID="$(grep '^APP uibox button' <<<"$out" | tr '\n' ' ')"; fail t13_scene_save_text; }
    }
nowrun t13_scene_del_boxes "screen on; @0.5; simnoise 30 7 custom; sceneui 0; @0.3; simknob 20; @0.2; uiboxes" && DEL=$(box_of "$DELRE" "$out" | awk '{print int(($1 + $3) / 2), int(($2 + $4) / 2)}')
nowrun t13_scene_del_expires "screen on; @0.5; simnoise 30 7 custom; noiseui; sceneui 0; @0.3; simknob 20; @0.2; simtap $DEL; @4.5; simtap $DEL; @0.3; uistack" && {
    ! grep -q '^SIM noise delete' <<<"$out" || { EVID="deleted after 4.5 s"; fail t13_scene_del_expires; }; }
nowrun t13_scene_del_quick "screen on; @0.5; simnoise 30 7 custom; noiseui; sceneui 0; @0.3; simknob 20; @0.2; simtap $DEL; @0.5; simtap $DEL; @0.3" && {
    grep -q '^SIM noise delete 0' <<<"$out" || { EVID="DEL=$DEL not deleted"; fail t13_scene_del_quick; }; }
# --- LIBTREE: the song tree (sub-folders, songs at every level, depth first), paged lists, the shuffle scope ---
# root: root1 root2 | deep (no songs of its own) / a/x b/y | many: s001..s100 + sub/t1..t3 | the fixture's three albums. Folder numbers (pre-order): deep 0, a 1, b 2, many 3, sub 4, then the albums.
NOWFRESH=treefresh nowrun lt_scan_tree "scan; @1; libls 3; libls 4; libls" && {
    { grep -q '^APP scan n=119 folders=8 rc=0 ' <<<"$out" && grep -q '^APP libls folder=3 name=many count=100 sub=103 parent=-1$' <<<"$out" && grep -q '^APP libls folder=4 name=sub count=3 sub=3 parent=3$' <<<"$out" &&
      grep -q '^APP libls folder 3 many$' <<<"$out" && grep -q '^APP libls folder 4 sub$' <<<"$out"; } || { EVID="$(grep '^APP scan\|^APP libls folder' <<<"$out" | head -6 | tr '\n' '|')"; fail lt_scan_tree; }; }
# the music root: the songs lying directly in it, then its folders (songs first, the playing order), Rescan last
NOWFRESH=treefresh nowrun lt_root_rows "scan; @1; mp3ui; @0.3; uiboxes" && {
    r=$(block_rows "$(awk '/^APP cmd mp3ui/{f=1} f' <<<"$out")" 1 | cut -d'|' -f2 | tr '\n' '/')
    [ "$r" = "Shuffle all/root1/root2/deep/many/夜航星圖/山間小火車/紙飛機工廠/Rescan library/" ] || [ "$r" = "全部隨機/root1/root2/deep/many/夜航星圖/山間小火車/紙飛機工廠/重新掃描曲庫/" ] || { EVID="$r"; fail lt_root_rows; }; }
# a level is paged: 40 items a page ("many": sub-folder + 100 songs = 3 pages), the side flick turns the page (wrapping), the bubble says n-N, rows are built for one page only, no "…n more"
NOWFRESH=treefresh nowrun lt_paging "scan; @1; uipush tracks3; @0.3; uiboxes; flick left; @0.2; uiboxes; flick left; @0.2; uiboxes; flick left; @0.2; uiboxes; flick right; @0.2; uiboxes; knob 100000; simknob 1; @0.2; uiboxes" && {
    d=$(awk '/^APP cmd uipush tracks3/{f=1} f' <<<"$out")
    bub=$(grep '^APP uibox label .*|[0-9]*-[0-9]*$' <<<"$d" | sed 's/.*|//' | tr '\n' ' ')
    cnt=$(for k in 1 2 3 4 5 6; do block_rows "$d" $k | wc -l; done | tr '\n' ' ')
    { [ "$bub" = "1-3 2-3 3-3 1-3 3-3 3-3 " ] && [ "$cnt" = "41 41 22 41 22 22 " ]; } || { EVID="bubbles '$bub' rows '$cnt'"; fail lt_paging; }
    ! grep -q '^APP uibox label .*|…' <<<"$d" || { EVID="a cut note"; fail lt_paging_no_cut; }; }
# the knob that runs off the last row of a page goes on to the next page
NOWFRESH=treefresh nowrun lt_paging_knob "screen on; scan; @1; uipush tracks3; @0.3; simknob -1; @0.1; simknob 80; @0.2; uiboxes" && {
    bub=$(grep '^APP uibox label .*|[0-9]*-[0-9]*$' <<<"$out" | sed 's/.*|//' | tail -1)
    [ "$bub" = "2-3" ] || [ "$bub" = "3-3" ] || { EVID="bubble '$bub'"; fail lt_paging_knob; }; }
# tapping a folder opens its level IN the page (stack unchanged); Back / the up flick goes up one level and, from the root level, leaves
NOWFRESH=treefresh nowrun lt_navigate "scan; @1; mp3ui; @0.3; uiboxes; simtap 175 239; @0.3; uistack; uiboxes; simtap 175 135; @0.3; uiboxes; flick up; @0.3; uistack; uiboxes; flick up; @0.3; uistack; uiboxes; flick up; @0.3; uistack" && {   # root row 3 = deep (sub-folders a, b only); its row 1 = a (song x)
    d=$(awk '/^APP cmd mp3ui/{f=1} f' <<<"$out")
    l1=$(block_rows "$d" 2 | cut -d'|' -f2 | head -3 | tr '\n' '/'); l2=$(block_rows "$d" 3 | cut -d'|' -f2 | tr '\n' '/')
    l3=$(block_rows "$d" 4 | cut -d'|' -f2 | head -3 | tr '\n' '/'); l4=$(block_rows "$d" 5 | cut -d'|' -f2 | head -3 | tr '\n' '/')
    st=$(grep '^APP uistack' <<<"$d" | tr '\n' '|')
    case "$l1" in "Shuffle this folder/a/b/"|"隨機播放這個資料夾/a/b/") ;; *) EVID="deep: $l1"; fail lt_navigate_open;; esac
    case "$l2" in "Shuffle this folder/x/"|"隨機播放這個資料夾/x/") ;; *) EVID="a: $l2"; fail lt_navigate_sub;; esac
    case "$l3" in "Shuffle this folder/a/b/"|"隨機播放這個資料夾/a/b/") ;; *) EVID="back to deep: $l3"; fail lt_navigate_back_level;; esac
    case "$l4" in "Shuffle all/root1/root2/"|"全部隨機/root1/root2/") ;; *) EVID="back to root: $l4"; fail lt_navigate_back_root;; esac
    [ "$st" = "APP uistack menu mp3|APP uistack menu mp3|APP uistack menu mp3|APP uistack menu|" ] || { EVID="$st"; fail lt_navigate_stack; }; }
# shuffle scope: a folder with no song of its own plays the songs of its sub-folders (and nothing else); a folder with sub-folders includes them
NOWFRESH=treefresh nowrun lt_scope "vol 0; scan; @1; mp3 0; @0.3; mp3cur; next; mp3cur; next; mp3cur; next; mp3cur; next; mp3cur; next; mp3cur; next; mp3cur; mp3 3; @0.3; mp3cur; next; mp3cur; next; mp3cur" && {
    a=$(awk '/^APP cmd mp3 0/{f=1} /^APP cmd mp3 3/{f=0} f && /^APP mp3 cur=/{sub(/cur=/,"",$3); print $3}' <<<"$out" | sort -u | tr '\n' ' ')
    b=$(awk '/^APP cmd mp3 3/{f=1} f && /^APP mp3 cur=/{sub(/cur=/,"",$3); print $3}' <<<"$out" | tr '\n' ' ')
    ok=1; for x in $b; do [ "$x" -ge 4 ] && [ "$x" -le 106 ] || ok=0; done
    { [ "$a" = "2 3 " ] && [ $ok = 1 ] && grep -q '^APP mp3 cur=[0-9]* folder=0 ' <<<"$out" && grep -q '^APP mp3 cur=[0-9]* folder=3 ' <<<"$out"; } || { EVID="deep $a many $b"; fail lt_scope; }; }
# a list opens on the page that holds the playing song, and a song of the root plays with the whole library as its pool
NOWFRESH=treefresh nowrun lt_playing_page "vol 0; scan; @1; track 54; @0.5; uihome; uipush tracks3; @0.3; uiboxes" && {
    bub=$(grep '^APP uibox label .*|[0-9]*-[0-9]*$' <<<"$out" | sed 's/.*|//' | tail -1)
    [ "$bub" = "2-3" ] || { EVID="bubble '$bub'"; fail lt_playing_page; }; }
NOWFRESH=treefresh nowrun lt_root_song "vol 0; scan; @1; track 0; @0.5; mp3cur" && { grep -q '^APP mp3 cur=0 folder=-1 ' <<<"$out" || { EVID="$(grep '^APP mp3 cur' <<<"$out")"; fail lt_root_song; }; }
# wave-5 fix D1: a library.bin whose folder 0 says nsub=2 (3 root folders, root_nchild 3: the hop finds 2) used to pass the loader and crash the tree page (ASan heap overflow); now refused -> rescan, page builds clean
NOWFRESH=corruptfresh nowrun lt_corrupt_index "mp3ui; @0.3; uiboxes; scan; @1; libls" && {
    grep -q '^APP library loaded rc=-22 ' <<<"$out" && grep -q '^APP library rescan reason=index' <<<"$out" && grep -q '^APP uiboxes end' <<<"$out" || { EVID="$(grep '^APP library' <<<"$out" | head -3 | tr '\n' '|')"; fail lt_corrupt_index; }; }
# wave-5 fix D3: revisiting the music page (other page with a list on top, then Back) adds no LVGL event descriptor to its list
NOWFRESH=treefresh nowrun lt_resume_events "scan; @1; mp3ui; @0.3; uipush tracks3; @0.3; back; @0.3; uievents; uipush tracks3; @0.3; back; @0.3; uievents; uipush tracks3; @0.3; back; @0.3; uievents; uipush tracks3; @0.3; back; @0.3; uievents" && {
    ev=$(grep '^APP uievents' <<<"$out" | sed 's/.* //' | sort -u | tr '\n' ' ')
    [ "$(grep -c '^APP uievents' <<<"$out")" = 4 ] && [ "$(wc -w <<<"$ev")" = 1 ] || { EVID="events $(grep '^APP uievents' <<<"$out" | sed 's/.* //' | tr '\n' ' ')"; fail lt_resume_events; }; }
# wave-5 fix D5: the console `list` / `tracks N` pushed twice share one instance no more (the second replaces the first); popping leaves a sane page below
NOWFRESH=treefresh nowrun lt_console_twice "scan; @1; mp3ui; @0.3; uipush tracks3; @0.3; uipush tracks3; @0.3; uistack; back; @0.3; uistack; uiboxes" && {
    st=$(grep '^APP uistack' <<<"$out" | tr '\n' '|')
    [ "$st" = "APP uistack menu mp3 tracks|APP uistack menu mp3|" ] || { EVID="$st"; fail lt_console_twice; }; }
# --- fold-in: the variant axis with voices stored and nothing voiced playing steps the bars that are drawn ---
NOWCFG=viz_style=voices nowrun t13_voices_variant_bars "screen on; @0.5; vol 0; color 0; @3; viz menu; viz pick 1; viz stat; simknob 1; @0.1; viz stat" && {
    v=$(grep '^APP viz voices' <<<"$out" | tail -2 | sed 's/^APP viz voices //' | tr '\n' '|'); l=$(grep '^APP viz ring hint' <<<"$out" | tail -2 | sed 's/.* line=//; s/ cells=.*//' | tr '\n' '|')
    [ "$v" = "n=0 var=0|n=0 var=1|" ] && [ "$(cut -d'|' -f1 <<<"$l")" != "$(cut -d'|' -f2 <<<"$l")" ] || { EVID="drawn $v lines $l"; fail t13_voices_variant_bars; }; }   # the bars' variant is drawn and named
# experimental CPU clock (cpu_clock.h): the first tap selects, from 320 the second only arms (nothing applied), then 240 -> 160 -> 320 each applied once (the sim stub prints it); the console `cpuclk N` sets and reports
nowrun cpuclk_cycle "setui; @0.3; settings key cpuclk; settings key cpuclk; settings key cpuclk; @0.1; cfg cpu_mhz; settings key cpuclk; @0.1; cfg cpu_mhz; settings key cpuclk; @0.1; cfg cpu_mhz; axishalf -1; settings key cpuclk; @0.1; cfg cpu_mhz; axishalf 1; cpuclk 160; cpuclk" && {
    c=$(grep '^APP cpuclk set' <<<"$out" | sed 's/^APP cpuclk set //; s/ MHz (sim)//' | tr '\n' ' ')
    [ "$c" = "240 160 240 160 " ] || { EVID="applied: $c"; fail cpuclk_cycle; }
    [ "$(cv cpu_mhz)" = "240|160|160|240|" ] || { EVID="cfg: $(cv cpu_mhz)"; fail cpuclk_cfg; }
    grep -q '^APP cpuclk want=160 real=160$' <<<"$out" || { EVID="$(grep '^APP cpuclk want' <<<"$out" | tr '\n' '|')"; fail cpuclk_cmd; }; }
