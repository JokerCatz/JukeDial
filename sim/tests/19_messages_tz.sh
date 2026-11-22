# SPDX-License-Identifier: LGPL-2.1-or-later
# --- Task 6: messages, help, clock exit, small texts ---
# the message page leaves by itself after 2.5 s, at once on a tap or a knob turn; it is no use for a Back button
nowrun msg_timeout "uipush menu; uipush message; @1; uistack; @2; uistack" && {
    stack_is msg_timeout_1s 1 "menu menu message"; stack_is msg_timeout_3s 2 "menu menu"; }
nowrun msg_tap "uipush menu; uipush message; @0.3; simtap 180 180; @0.2; uistack" && stack_is msg_tap 1 "menu menu"
nowrun msg_knob "screen on; @1; uipush menu; uipush message; @0.3; knob 1; @0.2; uistack" && stack_is msg_knob 1 "menu menu"
nowrun msg_no_back_button "uipush message; @0.2; uiboxes" && {
    ! grep -q '^APP uibox button' <<<"$out" || { EVID="$(grep '^APP uibox button' <<<"$out")"; fail msg_no_back_button; }; }
# back during the 2.5 s, then waiting: the timer is gone with its page, nothing else is popped
nowrun msg_back_then_wait "uipush menu; uipush message; @0.5; back; @4; uistack" && stack_is msg_back_then_wait 1 "menu menu"
# something was pushed over the message: its time runs out under that page, which stays on top; the message is gone from the stack
nowrun msg_covered "uipush menu; uipush message; @0.5; uipush help; @3; uistack; back; @0.3; uistack" && {
    stack_is msg_covered_stack 1 "menu menu explain"; stack_is msg_covered_back 2 "menu menu"; }
nowrun msg_covered_tap "uipush menu; uipush message; @0.5; uipush settings; @0.3; simtap 180 180; @0.3; uistack" && {
    [ "$(grep -c '^APP uistack menu menu message settings setgroup$' <<<"$out")" = 1 ] || { EVID="$(grep '^APP uistack' <<<"$out")"; fail msg_covered_tap_stays; }; }

# the explain page (the settings 說明 entry since fx apps T6, a scrolling list): nothing overlaps, the URL has a line of its own, no photos line, the Back button stays
NOWFRESH=helpfresh nowrun help_layout "uipush help; @0.3; uiboxes clip" && {   # `uipush help` opens the settings entry
    ov=$(uibox_overlaps <<<"$out"); [ -z "$ov" ] || { EVID="$ov"; fail help_no_overlap; }
    ! grep -qi 'photos\|相簿' <<<"$out" || { EVID="$(grep -i 'photos\|相簿' <<<"$out")"; fail help_no_photos_line; }
    # each address is a label of its own that holds only the URL (the default = GitHub + Gitee, so one or more)
    [ "$(grep -c '^APP uibox label .*|https\?://' <<<"$out")" -ge 1 ] && ! grep -q '^APP uibox label .*|https\?://[^ ]* ' <<<"$out" || { EVID="$(grep '^APP uibox' <<<"$out")"; fail help_url_own_line; }
    ! grep -q '^APP uibox button .*|\(Back\|返回\)$' <<<"$out" || fail help_back_button   # GS: no Back on the help page
    # (the old "last text line above Back" check is gone: the notes scroll in a list that ends above Back; t6_explain_back_below covers it)
}

# scene editor: the favourite button has a text label
nowrun scene_fav_label "sceneui 0; @0.3; uiboxes" && {
    grep -q '^APP uibox button .*|\(Favourite\|收藏\)$' <<<"$out" || { EVID="$(grep '^APP uibox' <<<"$out")"; fail scene_fav_label; }; }   # rows below the fold: the box rules are checked on clipped boxes (t6_no_hand_controls)
# pomodoro (UI unify T7, last part): a tap on the page opens the ring: the centre starts / pauses / resumes, the 停止 cell (armed) only while running or paused;
# "pomo stat" -> "APP pomo state=<idle|run|pause> left=<s>"; "popring N" prints "APP popring open= page= cells= armed= centre="; show pomo pushes on the top page, so clock first
nowrun pomo_pause "$PM; popring -2; popring -1; pomo stat; popring -1; pomo stat; popring -1; pomo stat" && {
    # taps on the centre: start, pause, resume
    [ "$(ps)" = "run pause run " ] || { EVID="states: $(ps) $(grep '^APP popring ' <<<"$out" | tr '\n' '|')"; fail pomo_pause; }; }
nowrun pomo_stop_only_when_paused "$PM; popring -2; popring -1; popring -1; popring -1" && {
    # the stop cell exists while running and while paused: idle 0 own cells, running 1, paused 1, running 1; every ring has the help cell too (fx apps T15): 2 3 3 3 (the Face and Colour cells are two more own cells)
    c="$(pcell 1) $(pcell 2) $(pcell 3) $(pcell 4)"; [ "$c" = "3 4 4 4" ] || { EVID="cells: $c"; fail pomo_stop_only_when_paused; }; }
nowrun pomo_paused_holds "$PM; popring -2; popring -1; @1.2; popring -1; @0.6; uiboxes; @30; uiboxes" && {
    a=$(grep '^APP uibox label .*|[0-9][0-9]:[0-9][0-9]$' <<<"$out" | sed 's/.*|//' | tr '\n' ' ')
    set -- $a; [ "$1" = "$2" ] && [ "$1" != "25:00" ] || { EVID="times: $a"; fail pomo_paused_holds; }; }
nowrun pomo_stop_button "$PM; popring -2; popring -1; popring -1; popring -2" && {
    [ "$(pcell 4)" = 4 ] || { EVID="cells: $(pcell 4) $(grep '^APP popring ' <<<"$out" | tr '\n' '|')"; fail pomo_stop_button_exists; }; }
nowrun pomo_stop_resets "$PM; popring -2; popring -1; @1.2; popring -1; popring 0; popring 0; pomo stat; @0.6; uiboxes" && {
    [ "$(ps)" = "idle " ] && grep -q '|25:00$' <<<"$out" || { EVID="state $(ps) $(grep '^APP uibox label' <<<"$out")"; fail pomo_stop_resets; }; }

# list toys: the "edit on the settings page" line is only for an empty list
nowrun lists_no_edit_hint "tools lunch; @0.5; uiboxes" && {
    ! grep -q 'Edit: settings\|在設定網頁' <<<"$out" || { EVID="$(grep '^APP uibox label' <<<"$out")"; fail lists_no_edit_hint; }; }

# the clock has a touch path back: the ring's Back leaves the clock, the centre (nothing picked) closes only the ring
nowrun clock_ring_back_leaves "clock; @1; simtap 180 180; @0.3; uistack; simflick up; @0.3; uistack" && {
    stack_is clock_ring_open 1 "menu clock clock_menu"; stack_is clock_ring_back_leaves 2 "menu"; }
nowrun clock_ring_centre_closes "clock; @1; simtap 180 180; @0.3; simtap 180 180; @0.3; uistack" && stack_is clock_ring_centre_closes 1 "menu clock"
nowrun clock_moon_from_tools_back "tools moon; @1; uistack; simtap 180 180; @0.3; simflick up; @0.3; uistack" && {
    stack_is clock_moon_tools_open 1 "menu tools clock"; stack_is clock_moon_tools_back 2 "menu tools"; }
# a picked item still confirms with the centre (the face item turns to the next face, the ring and its stack entry go)
nowrun clock_ring_pick_confirms "clock; @1; simtap 180 180; @0.3; simtap 279 279; @0.2; simtap 180 180; @0.3; uistack" && stack_is clock_ring_pick_confirms 1 "menu clock"
# the Wi-Fi sync row of the Set time page (the clock menu's last cell, south, opens it; it used to be a cell of its own): the page goes first, then the message is pushed, so it shows over the face and leaves by itself
nowrun clock_sync_message "clock; @1; simtap 180 180; @0.3; simtap 180 320; @0.2; simtap 180 180; @0.5; timeset key wifi; @0.5; uistack; @3; uistack" && {
    stack_is clock_sync_message_shown 1 "menu clock message"; stack_is clock_sync_message_left 2 "menu clock"; }

# --- tz: the time zone replaces the city list; sunrise and sunset only after the web page stored a location ---
# the clock ring has no time-zone or sunrise cell: the zone and the location are set on the web page only (8 cells with the stopwatch and the countdown, one page)
nowrun tz_ring_six "clock; @1; simtap 180 180; @0.3; ring" && { grep -q '^APP ring page=0/1 .* n=8$' <<<"$out" || { EVID="$(grep '^APP ring' <<<"$out")"; fail tz_ring_six; }; }
nowrun tz_no_axis "clock; @1; clockface tz; @0.3; uitop" && { ! grep -q '^APP cfg tz\|axis' <<<"$out" || fail tz_no_axis; }
# the sunrise line: nothing without a location, the times with one (Taipei 25.03 N 121.57 E on 2026-06-21: 05:04 / 18:46)
nowrun tz_sun_absent "simtime 2026-06-21 05:00:00; clockface info; clock; @1.5; uiboxes" && { ! grep -q 'Rise\|日出' <<<"$out" || { EVID="$(grep '^APP uibox' <<<"$out" | tr '\n' '|')"; fail tz_sun_absent; }; }
NOWCFG=$'sun_lat=2503\nsun_lon=12157' nowrun tz_sun_present "simtime 2026-06-21 05:00:00; clockface info; clock; @1.5; uiboxes" && { grep -q 'Rise 05:04 · Set 18:46' <<<"$out" || { EVID="$(grep '^APP uibox' <<<"$out" | tr '\n' '|')"; fail tz_sun_present; }; }
# the zone: no cfg tz = UTC+8 in every language; a stored string with rules shows 夏令 / DST while it is in force and the plain offset otherwise
nowrun tz_default_en "lang en; clockface info; clock; @1.5; uiboxes" && { grep -q "|.*UTC+8\$" <<<"$out" || { EVID="$(grep '^APP uibox' <<<"$out" | tr '\n' '|')"; fail tz_default_en; }; }
NOWCFG='tz=<-05>5<-04>,M3.2.0,M11.1.0' nowrun tz_dst_summer "simtime 2026-06-21 05:00:00; clockface info; clock; @1.5; uiboxes" && { grep -q 'UTC-4 DST' <<<"$out" || { EVID="$(grep '^APP uibox' <<<"$out" | tr '\n' '|')"; fail tz_dst_summer; }; }
NOWCFG='tz=<-05>5<-04>,M3.2.0,M11.1.0' nowrun tz_dst_winter "simtime 2026-01-10 05:00:00; clockface info; clock; @1.5; uiboxes" && { grep -q '|.*UTC-5$' <<<"$out" && ! grep -q 'DST' <<<"$out" && grep -q '|00:00$' <<<"$(grep 'uibox label' <<<"$out")" || { EVID="$(grep '^APP uibox' <<<"$out" | tr '\n' '|')"; fail tz_dst_winter; }; }
# the web ops: clock_get carries tz / sun; clock_set takes a valid string and a location in range, refuses the rest, sun_clear drops the location
nowrun tz_web '{"op":"clock_set","id":1,"tz":"<+0530>-5:30","sun_lat":2503,"sun_lon":12157}; {"op":"clock_get","id":2}; {"op":"clock_set","id":3,"tz":"Taipei"}; {"op":"clock_set","id":4,"sun_lat":9100,"sun_lon":0}; {"op":"clock_set","id":5,"sun_clear":true}; {"op":"clock_get","id":6}' && {
    g=$(grep '^CTL' <<<"$out")
    { [ "$(grep -c '"id":2.*"tz":"<+0530>-5:30".*"tz_min":330.*"sun":true.*"sun_lat":2503.*"sun_lon":12157' <<<"$g")" = 1 ] && grep -q '"id":3.*"ok":false' <<<"$g" && grep -q '"id":4.*"ok":false' <<<"$g" && grep -q '"id":6.*"sun":false' <<<"$g"; } || { EVID="$g"; fail tz_web; }; }
# the first-boot language pick writes no zone (the default UTC+8 is the same for every language)
for pk in 95 180; do
    rm -rf "$T/lp"; mkdir -p "$T/lp/_player/config"
    out=$($SIM --headless --root "$T/lp" --cmd "@0.5; simtap 180 $pk; @0.5; cfg tz" 2>&1)
    grep -qx "APP cfg tz=" <<<"$out" || { EVID="$pk: $(grep '^APP cfg' <<<"$out")"; fail tz_first_boot_no_zone; }
done
# --- end tz ---

# track rows: when every row starts with the same 【artist】 the rows show the title only; one row without it keeps all prefixes
fresh; bracket_fix
out=$($SIM --headless --root "$T/fix" --cmd "scan; @0.2; uipush tracks2; @0.5; uiboxes" 2>&1)
{ grep -q '^APP uibox button .*|甲$' <<<"$out" && grep -q '^APP uibox button .*|乙$' <<<"$out" && ! grep -q '^APP uibox button .*|【' <<<"$out"; } || { EVID="$(grep '^APP uibox button' <<<"$out" | tr '\n' ' ')"; fail tracks_common_prefix_stripped; }
fresh; bracket_fix "丙"
out=$($SIM --headless --root "$T/fix" --cmd "scan; @0.2; uipush tracks2; @0.5; uiboxes" 2>&1)
{ grep -q '^APP uibox button .*|【Aimer】甲$' <<<"$out" && grep -q '^APP uibox button .*|丙$' <<<"$out"; } || { EVID="$(grep '^APP uibox button' <<<"$out" | tr '\n' ' ')"; fail tracks_mixed_prefix_kept; }
# --- end Task 6 ---

