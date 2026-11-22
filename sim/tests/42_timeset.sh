# SPDX-License-Identifier: LGPL-2.1-or-later
# --- Set time: the page from Settings > System and from the clock menu; axis rows; Apply sets the clock (LOCAL time, default tz UTC+8) ---
# simtime is UTC: 2026-11-22 07:00 UTC = 15:00 local
TS0="simtime 2026-11-22 07:00:00"
nowrun timeset_from_settings "$TS0; setui; @0.3; settings key time; @0.3; uistack; timeset draft" && {
    stack_is timeset_from_settings_stack 1 "menu settings setgroup timeset"
    grep -q '^APP timeset draft=2026-11-22 15:00$' <<<"$out" || { EVID="$(grep '^APP timeset' <<<"$out")"; fail timeset_draft_is_now; }; }
nowrun timeset_system_row "setui; @0.3; settings group system; settings rows" && {
    grep -q '^APP settings row=.*\(Set time\|設定時間\)$' <<<"$out" || { EVID="$(grep '^APP settings row' <<<"$out")"; fail timeset_system_row; }; }
nowrun timeset_from_clock_menu "show clockmenu; ring; uistack; ringpick 7; @0.5; uistack" && {
    grep -q '^APP ring .*n=8$' <<<"$out" || { EVID="$(grep '^APP ring' <<<"$out")"; fail timeset_clock_menu_cell; }; }
# the first tap of a row only selects it, the second steps; apply sets the clock; apply sets the clock: 2027-12-23 16:01 local = 08:01 UTC = 1829548860 s
nowrun timeset_apply "$TS0; setui; @0.3; settings key time; @0.3; timeset key year; timeset key year; timeset key month; timeset key month; timeset key day; timeset key day; timeset key hour; timeset key hour; timeset key min; timeset key min; timeset draft; timeset key apply; @0.3; timenow; uistack" && {
    grep -q '^APP timeset draft=2027-12-23 16:01$' <<<"$out" || { EVID="$(grep '^APP timeset' <<<"$out")"; fail timeset_taps_step; }
    grep -q '^APP clock set manual 2027-12-23 16:01 utc=1829548860$' <<<"$out" || { EVID="$(grep '^APP clock' <<<"$out")"; fail timeset_apply_utc; }
    grep -q '^APP time utc=18295488[6-9][0-9] valid=1$' <<<"$out" || { EVID="$(grep '^APP time' <<<"$out")"; fail timeset_clock_reads_new; }
    stack_is timeset_apply_message 1 "menu settings setgroup timeset message"; }
# the knob steps the borrowed axis both ways: the tap 11 -> 12, +3 wraps to 3, -1 to 2 ; the day clamps to the month: Jan 31 -> Feb 28
nowrun timeset_knob_and_clamp "simtime 2026-01-31 04:00:00; setui; @0.3; settings key time; @0.3; timeset key month; timeset key month; timeset draft" && {
    grep -q '^APP timeset draft=2026-02-28 12:00$' <<<"$out" || { EVID="$(grep '^APP timeset' <<<"$out")"; fail timeset_day_clamps; }; }
nowrun timeset_knob "$TS0; setui; @0.3; settings key time; @0.3; timeset key month; timeset key month; simknob 3; simknob -1; timeset draft" && {
    grep -q '^APP timeset draft=2026-02-22 15:00$' <<<"$out" || { EVID="$(grep '^APP timeset' <<<"$out")"; fail timeset_knob_wraps; }; }
# the console form: LOCAL time; leap day accepted in 2028, refused in 2027; out of range refused
nowrun timeset_console "$TS0; clock set 2028-02-29 23:59; timenow; clock set 2027-02-29 10:00; clock set 2026-13-01 10:00; clock set 2024-01-01 00:00" && {
    grep -q '^APP cmd clock set rc=0$' <<<"$out" || { EVID="$(grep '^APP cmd' <<<"$out")"; fail timeset_console_leap; }
    [ "$(grep -c '^APP cmd clock set rc=-1$' <<<"$out")" = 3 ] || { EVID="$(grep '^APP cmd' <<<"$out")"; fail timeset_console_refuses; }; }
# the time zone is the stored one: New York in summer (EDT, UTC-4)
NOWCFG='tz=EST5EDT,M3.2.0,M11.1.0' nowrun timeset_tz "clock set 2026-07-01 12:00; timenow" && {
    grep -q '^APP time utc=1782921600 valid=1$' <<<"$out" || { EVID="$(grep '^APP time' <<<"$out")"; fail timeset_tz_dst; }; }
