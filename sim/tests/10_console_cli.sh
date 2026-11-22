# SPDX-License-Identifier: LGPL-2.1-or-later
# lvconf: values taken from app/sdkconfig, custom OS gone
python3 tools/simtools/sim_lvconf.py app/sdkconfig "$T/lv.h" || fail lvconf_gen
for want in "CONFIG_LV_COLOR_DEPTH 16" "CONFIG_LV_USE_FREETYPE 1" "CONFIG_LV_USE_CUSTOM_MALLOC 1" "CONFIG_LV_DEF_REFR_PERIOD 33" "CONFIG_LV_OS_NONE 1" "CONFIG_LV_USE_SDL 1"; do
    grep -qx "#define $want" "$T/lv.h" || fail "lvconf: $want"
done
grep '^#define' "$T/lv.h" | grep -q 'LV_OS_CUSTOM' && fail "lvconf: LV_OS_CUSTOM present"
python3 tools/simtools/sim_lvconf.py --check app/sdkconfig | grep -qx "CONFIG_LV_COLOR_DEPTH 16" || fail lvconf_check

# selftest, no sanitizer output at all
out=$($SIM --headless --selftest 2>&1); rc=$?
[ $rc = 0 ] && [ "$out" = "SELFTEST ok 14" ] || { fail selftest; echo "$out"; }

# commands: @ parsing, a page, shot
mk_root   # the data root: lang=0 (a config without it is a first boot, the language picker); mk_root is in 00_helpers.sh
out=$($SIM --headless --root "$T/root" --cmd "@0; @0.001; @abc; @-1; @999999; uipush menu; shot $T/a.png" 2>&1); rc=$?
[ $rc = 0 ] || fail "cmd_rc=$rc"
[ "$(grep -c '^SIM bad time$' <<<"$out")" = 2 ] || fail cmd_bad_time
[ "$(grep -c '^SIM ok$' <<<"$out")" = 7 ] || fail cmd_ok_lines
grep -qx "SIM shot $T/a.png 360x360" <<<"$out" || fail cmd_shot_line
[ -s "$T/a.png" ] || fail cmd_png
grep -q 'Sanitizer\|runtime error' <<<"$out" && { fail cmd_sanitizer; echo "$out"; }

# the same through stdin
printf 'uipush menu\nshot %s\n' "$T/b.png" | $SIM --headless --root "$T/root" >/dev/null 2>"$T/err"
[ ! -s "$T/err" ] || fail stdin_stderr
cmp -s "$T/a.png" "$T/b.png" || fail stdin_vs_cmd

# unknown command, quit stops the rest
out=$($SIM --headless --root "$T/root" --cmd "bogus; quit; uitop" 2>&1)
grep -qx 'APP cmd unknown' <<<"$out" && [ "$(grep -c '^SIM ok$' <<<"$out")" = 2 ] && ! grep -q 'APP uitop' <<<"$out" || { fail unknown_quit; echo "$out"; }

# a /sdcard path in shot goes under the root
$SIM --headless --root "$T/root" --cmd "uipush menu; shot /sdcard/c.png" >/dev/null 2>&1
cmp -s "$T/a.png" "$T/root/c.png" || fail shot_sdcard_path

# missing root: message + exit 2
out=$($SIM --headless --root "$T/nope" 2>&1); rc=$?
[ $rc = 2 ] && [ "$out" = "SIM root not found: $T/nope" ] || fail "root_missing rc=$rc"

# a last stdin line without newline still runs
out=$(printf 'uitop\n@0.1' | $SIM --headless --root "$T/root" 2>&1)
[ "$(grep -c '^SIM ok$' <<<"$out")" = 2 ] || { fail stdin_no_newline; echo "$out"; }

# out-of-range --time is rejected like a malformed one
out=$($SIM --headless --root "$T/root" --time "2026-13-45 10:00" --cmd "@1" 2>&1); rc=$?
[ $rc = 2 ] && grep -q 'SIM bad --time' <<<"$out" || fail "bad_time_range rc=$rc"

# fixed headless clock and seed defaults do not depend on the host
$SIM --headless --root "$T/root" --time "2026-11-11 13:30" --cmd "@1" >/dev/null 2>&1 || fail time_arg

# the real UI: screen off after the timeout, shot still works, the knob wakes it
out=$(SIM_TRACE=1 $SIM --headless --root "$T/root" --cmd "uipush menu; @3600; shot $T/off.png; simknob 1; @0.2; shot $T/on.png" 2>&1); rc=$?
[ $rc = 0 ] && [ -s "$T/off.png" ] && [ -s "$T/on.png" ] || fail "screen_off_shots rc=$rc"
grep -qx 'SIM backlight 0' <<<"$out" || fail screen_off_backlight
[ "$(grep '^SIM backlight' <<<"$out" | tail -1)" != 'SIM backlight 0' ] || fail knob_wakes
grep -q 'Sanitizer\|runtime error' <<<"$out" && fail screen_off_sanitizer

# a tap reaches the UI; board-only commands answer SIM n/a
out=$($SIM --headless --root "$T/root" --cmd "simtap 180 180; @0.2; uitop" 2>&1)
grep -q '^APP uitop ' <<<"$out" || { fail simtap_uitop; echo "$out"; }
out=$($SIM --headless --root "$T/root" --cmd "btscan; heapwalk" 2>&1)
[ "$(grep -c '^SIM n/a ' <<<"$out")" = 2 ] && ! grep -q 'APP cmd' <<<"$out" || { fail na_commands; echo "$out"; }
out=$($SIM --headless --root "$T/root" --cmd "stack" 2>&1)
[ "$(grep '^SIM n/a ' <<<"$out")" = "SIM n/a stack" ] && ! grep -q 'APP cmd' <<<"$out" || { fail na_stack; echo "$out"; }
out=$($SIM --headless --root "$T/root" --cmd "fxbench" 2>&1)
[ "$(grep '^SIM n/a ' <<<"$out")" = "SIM n/a fxbench" ] && ! grep -q 'APP cmd' <<<"$out" || { fail t11_fxbench_na; echo "$out"; }

# no font anywhere: warning once, every page still opens (the empty root has no fonts/)
out=$($SIM --headless --root "$T/root" --cmd "uitop" 2>&1)
[ "$(grep -c '^SIM warn no CJK font$' <<<"$out")" = 1 ] || fail no_font_warning
SIM_NOFONT=1 python3 tools/simtools/ui_baseline.py sim "$T/nofont" >"$T/nofont.log" 2>&1 || { fail nofont_baseline; cat "$T/nofont.log"; }
[ "$(ls "$T/nofont" | wc -l)" = $((125 + GAME_PICS)) ] || fail nofont_pngs
SIM_NOFONT=1 python3 -c "
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']
from sim_con import SimCon
c = SimCon(); out = c.cmd('fontinfo', 0); assert b'cjk=0' in out, out; assert c.close() == 0" || fail nofont_simcon

# the whole baseline set opens: every name has a PNG, exit 0, no sanitizer output
need_base || { fail baseline; cat "$T/base.log"; }
[ "$(ls "$T/base" | wc -l)" = $((125 + GAME_PICS)) ] || fail baseline_pngs

# the leak tripwire of ui.c is alive in the simulator
out=$($SIM --headless --root "$T/root" --cmd "uileak; back" 2>&1)
grep -q '^APP ui leak screen=leaktest timers=+1 objs=+1$' <<<"$out" || { fail ui_leak_tripwire; echo "$out"; }

# a crash mid-script is reported with the command that did it (and the driver exits non-zero)
SIM_CRASH_ON="uipush colors" python3 tools/simtools/ui_baseline.py sim "$T/crash" >"$T/crash.log" 2>&1; rc=$?
[ $rc != 0 ] && grep -q "'uipush colors'" "$T/crash.log" && grep -q 'command #' "$T/crash.log" || { fail crash_reported; cat "$T/crash.log"; }

