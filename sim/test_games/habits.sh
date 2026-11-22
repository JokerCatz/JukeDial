# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G7: 喝水提醒 (app/main/ui/scr_drink.c, drink_svc.c) and 習慣打卡 (app/main/ui/scr_habits.c) in the 時鐘 family; rules in components/player_core/src/pc_habit.c; sourced by sim/test.sh ---
# Time is the fake clock: `simtime YYYY-MM-DD HH:MM:SS` (UTC) then `@seconds`; the default zone is UTC+8 (cfg tz absent, 繁中; no DST): 01:00 UTC = 09:00 local, 16:00 UTC = 00:00 of the next local day.
# Console: `drink` dumps "APP drink on= int= st= en= goal= cups= day= next= reminded=" (int in minutes), `drink cup|undo|snooze|tick|reset|set <on> <int idx> <st> <en> <goal> <cups>|tap <i>|ring`;
# `habit` dumps "APP habit n= sel= today= h=<slot>:<preset>:<streak>:<strip>,...", `habit tap <i>|sel <i>|ring|undo|name <d>|set <n>|reset|seed <slot> <strip>`.
dk_last() { grep '^APP drink on=' <<<"$out" | tail -1; }
dk_f() { dk_last | sed -n "s/.* $1=\([^ ]*\).*/\1/p"; }
dk_all() { grep '^APP drink on=' <<<"$out" | sed 's/.* cups=\([0-9]*\) .* next=\([^ ]*\) reminded=\([0-9]*\)/\1,\2,\3/' | tr '\n' ' '; }   # "cups,next,reminded" of every dump
hb_last() { grep '^APP habit n=' <<<"$out" | tail -1; }
hb_f() { hb_last | sed -n "s/.* $1=\([^ ]*\).*/\1/p"; }
hb_slot() { hb_f h | tr ',' '\n' | sed -n "$(($1 + 1))p"; }     # slot i: "<slot>:<preset>:<streak>:<strip>"
D0="simtime 2026-11-15 01:00:00"   # 09:00 local, 2026-11-15
# off by default: no reminder however long it runs
nowrun drink_off_default "$D0; screen on; vol 0; @2; drink; @4000; drink" && {
    [ "$(dk_f on)" = 0 ] && [ "$(dk_f reminded)" = 0 ] && [ "$(dk_f cups)" = 0 ] && [ "$(dk_f goal)" = 8 ] && [ "$(dk_f int)" = 60 ] && [ "$(dk_f st)" = 9 ] && [ "$(dk_f en)" = 21 ] || { EVID="$(dk_last)"; fail drink_off_default; }; }
# the first reminder is one interval after the window is seen open, then every interval; a cup moves the next one; with the screen off nothing is pushed (the sleep rules stay)
nowrun drink_remind "$D0; screen off; vol 0; @2; drink set 1 0 9 21 8 0; @1; drink; @1805; drink; uistack; @1800; drink; drink cup; drink; @1700; drink; @200; drink" && {
    [ "$(dk_all)" = "0,--,0 0,09:30,0 0,10:00,1 0,10:30,2 1,10:30,2 1,10:30,2 1,10:30,2 1,11:00,3 " ] || { EVID="$(dk_all)"; fail drink_remind; }
    [ "$(grep -c '^APP drink remind' <<<"$out")" = 3 ] && grep -qx 'APP uistack menu' <<<"$out" || fail drink_no_page; }
# outside the window (here 23:00 local) and once the goal is reached: nothing; the window opening again next morning: the first one an interval after it opens
nowrun drink_window_goal "simtime 2026-11-15 15:00:00; screen on; vol 0; @2; drink set 1 0 9 21 8 0; @4000; drink; $D0; @2; drink set 1 0 9 21 3 3; @4000; drink" && {
    [ "$(dk_all)" = "0,--,0 0,--,0 3,--,0 3,--,0 " ] || { EVID="$(dk_all)"; fail drink_window_goal; }; }
# a snooze puts the next reminder 10 minutes away; a cup undone is a cup less
nowrun drink_snooze_undo "$D0; screen on; vol 0; @2; drink set 1 2 9 21 8 2; drink tick; drink; drink snooze; drink; drink undo; drink undo; drink undo; drink" && {
    [ "$(dk_all)" = "2,--,0 2,10:00,0 2,10:00,0 2,09:10,0 2,09:10,0 1,09:10,0 0,09:10,0 0,09:10,0 0,09:10,0 " ] || { EVID="$(dk_all)"; fail drink_snooze_undo; }; }
# midnight: the cups go back to 0 on the new local day while the clock runs (23:59 local, 2 minutes later)
nowrun drink_midnight "simtime 2026-11-15 15:59:00; screen off; vol 0; @2; drink set 1 2 9 21 8 3; drink; @120; drink" && {
    d=$(grep '^APP drink on=' <<<"$out" | sed 's/.* cups=\([0-9]*\) day=\([0-9]*\).*/\1:\2/' | tr '\n' ' ')
    [ "$d" = "3:20772 3:20772 0:20773 " ] && grep -qx 'APP drink new day=20773' <<<"$out" || { EVID="$d"; fail drink_midnight; }; }
# the page: two sectors (a tap on 0 is a cup, on 1 a snooze), the hub says cups/goal, the ring has 7 cells (the axes, 說明 last), the axes step, the armed 少一杯 needs two taps
nowrun drink_page "$D0; screen on; vol 0; @2; drink set 1 2 9 21 8 0; show drink; @0.5; uitop; drink tap 0; drink tap 0; drink tap 1; drink; drink ring; @0.3; popring -5; popring 1; popring 1; popring 2; popring 3; popring 3; popring 4; drink; popring 5; popring -5; drink; popring 5; popring -5; drink" && {
    grep -qx 'APP uitop clock_drink' <<<"$out" || fail drink_page_open
    [ "$(grep '^APP drink cup' <<<"$out" | wc -l)" = 2 ] && grep -qx 'APP drink snooze' <<<"$out" || fail drink_page_taps
    [ "$(grep '^APP popring open=1' <<<"$out" | sed -n 1p | sed 's/.* cells=\([0-9]*\) .*/\1/')" = 7 ] || fail drink_ring_cells
    s=$(grep '^APP drink setup' <<<"$out" | sed 's/APP drink setup //' | tr '\n' '|')
    [ "$s" = "on=1 int=90 st=9 en=21 goal=8|on=1 int=30 st=9 en=21 goal=8|on=1 int=30 st=10 en=21 goal=8|on=1 int=30 st=10 en=22 goal=8|on=1 int=30 st=10 en=23 goal=8|on=1 int=30 st=10 en=23 goal=9|" ] || { EVID="$s"; fail drink_ring_axes; }
    grep -q 'armed=5 ' <<<"$out" && grep -qx 'APP drink undo cups=1' <<<"$out" && [ "$(grep -c '^APP drink undo' <<<"$out")" = 1 ] && [ "$(dk_f cups)" = 1 ] || { EVID="$(grep '^APP drink undo\|armed=' <<<"$out" | tr '\n' '|')"; fail drink_undo_armed; }; }
# the toggle cell: 提醒 on / off, off by the first tap (the reminders were on); 說明 is the last cell and opens the explain page
nowrun drink_toggle_help "$D0; screen on; vol 0; @2; drink set 1 2 9 21 8 0; show drink; @0.5; drink ring; @0.3; popring -5; popring 0; drink; popring 0; drink; drink ring; popring 6; @0.5; uitop" && {
    [ "$(grep '^APP drink on=' <<<"$out" | sed 's/ int=.*//' | tr '\n' ' ')" = "APP drink on=1 APP drink on=1 APP drink on=0 APP drink on=1 APP drink on=1 " ] || { EVID="$(grep '^APP drink on=' <<<"$out" | sed 's/ int=.*//' | tr '\n' ' ')"; fail drink_toggle; }
    grep -qx 'APP uitop explain' <<<"$out" || fail drink_help_last; }
# habits: a tap = done today, once a day (a second tap changes nothing), the streak and the 7-day strip; the next day continues it; a missed day breaks it
nowrun habit_marks "$D0; screen on; vol 0; @2; show habits; @0.5; uitop; habit reset; habit tap 0; habit tap 0; habit tap 1; habit; $D0; simtime 2026-11-16 01:00:00; @3; habit; habit tap 0; habit; simtime 2026-11-18 01:00:00; @3; habit; habit tap 0; habit" && {
    grep -qx 'APP uitop clock_habits' <<<"$out" || fail habit_open
    r=$(grep '^APP habit n=' <<<"$out" | sed -n 's/.* h=\([^,]*\),.*/\1/p' | uniq | tr '\n' ' ')      # slot 0 after every command, repeats folded
    [ "$r" = "0:0:0:....... 0:0:1:......# 0:0:1:.....#. 0:0:2:.....## 0:0:0:...##.. 0:0:1:...##.# " ] || { EVID="$r"; fail habit_marks; }; }
# midnight with the page open: the 1 s timer moves the strip and keeps the streak (23:59 local, 70 s later); F1a2: the streak line is the sectors' status (the page has no title label any more)
nowrun habit_midnight_page "simtime 2026-11-15 15:59:00; screen on; vol 0; @2; show habits; @0.5; habit reset; habit tap 0; habit; sectors; @70; habit; sectors" && {
    s=$(grep '^APP habit n=' <<<"$out" | sed -n 's/.* h=0:[0-9]*:[0-9]*:\([^,]*\),.*/\1/p' | uniq | tail -2 | tr '\n' ' ')            # the 7-day strip of slot 0 (the HEAD cell that shows it is overlay chrome now: not on screen at rest)
    [ "$s" = "......# .....#. " ] && [ "$(grep -c '^APP sectors status Move · streak 1$\|^APP sectors status Move · 連續 1$' <<<"$out")" = 2 ] || { EVID="$s"; fail habit_midnight_page; }; }
# the ring: 數量 steps 4 -> 8 -> 2 -> 4, 這格 steps the name past the ones on other sectors (0 -> 4), 取消今天 needs two taps and takes today's tick away, 說明 is the last cell
nowrun habit_ring "$D0; screen on; vol 0; @2; show habits; @0.5; habit reset; habit tap 0; habit tap 1; habit sel 0; habit ring; @0.3; popring -5; popring 0; habit; popring 0; habit; popring 0; habit; popring 1; habit; habit name -1; habit; popring 2; popring -5; habit; popring 2; popring -5; habit" && {
    n=$(grep '^APP habit n=' <<<"$out" | sed 's/^APP habit n=\([0-9]\) sel=\([0-9]\) .* h=\([^,]*\),.*/\1:\2:\3/' | uniq | tr '\n' ' ')
    [ "$n" = "4:0:0:0:0:....... 4:0:0:0:1:......# 4:1:0:0:1:......# 4:0:0:0:1:......# 8:0:0:0:1:......# 2:0:0:0:1:......# 4:0:0:0:1:......# 4:0:0:4:0:....... 4:0:0:0:1:......# 4:0:0:0:0:....... " ] || { EVID="$n"; fail habit_ring; }
    [ "$(grep '^APP popring open=1' <<<"$out" | sed -n 1p | sed 's/.* cells=\([0-9]*\) .*/\1/')" = 4 ] || fail habit_ring_cells
    grep -q 'armed=2 ' <<<"$out" || fail habit_undo_armed; }
nowrun habit_help_last "$D0; screen on; vol 0; @2; show habits; @0.5; habit ring; popring 3; @0.5; uitop" && { grep -qx 'APP uitop explain' <<<"$out" || fail habit_help_last; }
# a name is never on two sectors, and a rename keeps both records (they belong to the names): slot 0 ticked as name 0, renamed to 4 and ticked, back to 0: its tick is still there
nowrun habit_names "$D0; screen on; vol 0; @2; show habits; @0.5; habit reset; habit tap 0; habit name 1; habit tap 0; habit name -1; habit set 8; habit name 1; habit set 2; habit" && {
    r=$(grep '^APP habit n=' <<<"$out" | sed -n 's/.* h=\([^,]*\),.*/\1/p' | uniq | tr '\n' ' ')
    [ "$r" = "0:0:0:....... 0:0:1:......# 0:4:0:....... 0:4:1:......# 0:0:1:......# 0:8:0:....... " ] || { EVID="$r"; fail habit_names; }; }
# saved on the card (games.txt): the settings and today's cups come back after a restart, the next day's start has 0 cups; the habits, their names and the count come back, the streak goes on
fresh
out=$($SIM --headless --root "$T/fix" --cmd "$D0; screen on; vol 0; @2; drink set 1 1 8 20 6 4; show habits; @0.5; habit reset; habit set 8; habit tap 0; habit tap 5; habit name 1; gamesave flush" 2>&1)
grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out" && fail habits_save_run1
grep -q '^drink.cups=4$' "$T/fix/_player/config/games.txt" && grep -q '^drink.goal=6$' "$T/fix/_player/config/games.txt" && grep -q '^hab.n=8$' "$T/fix/_player/config/games.txt" || { EVID="$(grep -c . "$T/fix/_player/config/games.txt")"; fail habits_saved_file; }
out=$($SIM --headless --root "$T/fix" --cmd "$D0; screen on; vol 0; @2; drink; show habits; @0.5; habit" 2>&1)
[ "$(dk_f cups)" = 4 ] && [ "$(dk_f goal)" = 6 ] && [ "$(dk_f int)" = 45 ] && [ "$(dk_f st)" = 8 ] && [ "$(dk_f en)" = 20 ] || { EVID="$(dk_last)"; fail drink_restart; }
[ "$(hb_f n)" = 8 ] && [ "$(hb_slot 0)" = "0:0:1:......#" ] && [ "$(hb_slot 5)" = "5:8:0:......." ] || { EVID="$(hb_last)"; fail habit_restart; }
out=$($SIM --headless --root "$T/fix" --cmd "simtime 2026-11-16 01:00:00; screen on; vol 0; @2; drink; show habits; @0.5; habit tap 0; habit" 2>&1)
[ "$(dk_f cups)" = 0 ] && [ "$(dk_f day)" = 20773 ] && [ "$(hb_slot 0)" = "0:0:2:.....##" ] || { EVID="$(dk_last) $(hb_slot 5)"; fail habits_next_day; }
# the clock family ring: 8 cells (stopwatch and countdown at NW and N), 喝水 and 習慣 open their pages and Back comes back to the clock
nowrun clockmenu_g7 "screen on; vol 0; clock; @1; simtap 180 180; @0.3; ring; simtap 279 81; @0.3; simtap 180 180; @0.5; uistack; back; @0.3; uistack; simtap 180 180; @0.3; simtap 320 180; @0.3; simtap 180 180; @0.5; uistack" && {
    grep -q '^APP ring .* n=8$' <<<"$out" || { EVID="$(grep '^APP ring ' <<<"$out" | head -1)"; fail clockmenu_seven; }
    stack_is clockmenu_drink 1 "menu clock clock_drink"; stack_is clockmenu_drink_back 2 "menu clock"; stack_is clockmenu_habits 3 "menu clock clock_habits"; }
# HUD task: the ring's NW cell opens the stopwatch, its N cell the countdown (each a page of its own: the clock stays below)
nowrun clockmenu_timers "screen on; vol 0; clock; @1; simtap 180 180; @0.3; simtap 88 88; @0.3; simtap 180 180; @0.5; uistack; back; @0.3; simtap 180 180; @0.3; simtap 180 50; @0.3; simtap 180 180; @0.5; uistack" && {
    stack_is clockmenu_stopwatch 1 "menu clock clock_stopwatch"; stack_is clockmenu_countdown 2 "menu clock clock_countdown"; }
