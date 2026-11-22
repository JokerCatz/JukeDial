# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: mk_root)
mk_root
# ---- Task 3: fixture, fake services with behaviour ----
# the generator is reproducible and the committed fixture is its output
python3 tools/simtools/sim_fixture.py "$T/fx" || fail fixture_gen
diff -r sim/fixture "$T/fx" >/dev/null || fail fixture_reproducible
# fixture copy: the real library_svc_load reads 12 songs
fresh
out=$($SIM --headless --root "$T/fix" --cmd "libls" 2>&1)
grep -q '^APP library loaded rc=0 n=12 folders=3 ' <<<"$out" || { fail fixture_load; echo "$out"; }
# the scan task (shim: deferred to a step boundary): 12 songs afterwards, the same index bytes as the generator wrote, nothing nested, no leak
out=$($SIM --headless --root "$T/fix" --cmd "scan; @0.2; libls 0; libls 1; libls 2; uitop" 2>&1)
grep -q '^APP scan n=12 folders=3 rc=0 save=0 ' <<<"$out" || { fail scan_done; echo "$out"; }
[ "$(grep -c '^APP libls [0-9]* .* | /sdcard/export/' <<<"$out")" = 12 ] || fail scan_12_tracks
grep -q '^APP uitop menu$' <<<"$out" || fail scan_screen_left   # scr_scan pops itself when it is done; started from the main menu (console), it ends there (Task 13: only the MP3 page under it is rebuilt)
grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out" && { fail scan_sanitizer; echo "$out"; }
cmp -s "$T/fix/_player/index/library.bin" sim/fixture/_player/index/library.bin || fail scan_index_equals_generated
# playing: the track ends and player_ctl moves on (gapless: queued near the end, then taken over)
fresh
out=$($SIM --headless --root "$T/fix" --cmd "vol 0; track 1; @3; @250; uitop" 2>&1)
[ "$(grep -c '^APP track idx=' <<<"$out")" -ge 2 ] && grep -q '^APP gapless queued idx=' <<<"$out" && grep -q '^APP track idx=0 .* gapless$' <<<"$out" || { fail track_ends_next; echo "$out" | grep '^APP track\|gapless'; }
grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out" && fail play_sanitizer
# the same without gapless: the end event itself starts the next track
out=$($SIM --headless --root "$T/fix" --cmd "seti gapless 0; vol 0; track 1; @250" 2>&1)
[ "$(grep -c '^APP track idx=' <<<"$out")" -ge 2 ] && ! grep -q 'APP gapless' <<<"$out" || { fail track_ends_ended; echo "$out" | grep '^APP track'; }

# crash safety net (2026-11-18): a crash reset with a track in flight marks it on the card; it is not picked by itself (gapless or not), a deliberate play clears the mark
fresh
out=$($SIM --headless --root "$T/fix" --cmd "vol 0; track 1; @1; crashnet crash; crashnet marked 1; crashnet marked 2" 2>&1)
grep -q '^APP crashnet marks=1 new=1$' <<<"$out" && grep -q '^APP crashnet track 1 marked=1$' <<<"$out" && grep -q '^APP crashnet track 2 marked=0$' <<<"$out" || { fail crashnet_mark; echo "$out" | grep crashnet; }
[ -s "$T/fix/_player/config/crashed.txt" ] || fail crashnet_file
out=$($SIM --headless --root "$T/fix" --cmd "crashnet crash; crashnet marked 1" 2>&1)    # a second start: the mark is on the card, nothing was in flight
grep -q '^APP crashnet marks=1 new=0$' <<<"$out" && grep -q '^APP crashnet track 1 marked=1$' <<<"$out" || { fail crashnet_persist; echo "$out" | grep crashnet; }
for g in 1 0; do
    out=$($SIM --headless --root "$T/fix" --cmd "seti gapless $g; seti mp3_order 1; vol 0; crashnet crash; track 0; @3; @250" 2>&1)
    grep -q '^APP mp3 skip idx=1 reason=crash-mark$' <<<"$out" && grep -q '^APP track idx=2 ' <<<"$out" && ! grep -q '^APP track idx=1 ' <<<"$out" && ! grep -q '^APP gapless queued idx=1 ' <<<"$out" || { fail "crashnet_skip_gapless$g"; echo "$out" | grep 'APP track\|skip\|gapless'; }
done
out=$($SIM --headless --root "$T/fix" --cmd "vol 0; crashnet crash; track 1; crashnet marked 1; crashnet crash; crashnet marked 1" 2>&1)   # a deliberate play clears it; no crash in flight afterwards that names it... (the playing track IS in flight: the simulated crash marks it again)
grep -q '^APP crashnet mark cleared by a deliberate play$' <<<"$out" && [ "$(grep -c '^APP crashnet track 1 marked=1$' <<<"$out")" = 1 ] || { fail crashnet_clear; echo "$out" | grep crashnet; }
# no crash, no mark: a clean restart (RTC taken with crashed=false) names nothing
fresh
out=$($SIM --headless --root "$T/fix" --cmd "vol 0; track 1; @1; crashnet; crashnet marked 1" 2>&1)
grep -q '^APP crashnet track 1 marked=0$' <<<"$out" || { fail crashnet_nocrash; echo "$out" | grep crashnet; }
grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out" && fail crashnet_sanitizer
# the tracks list says so
fresh
$SIM --headless --root "$T/fix" --cmd "vol 0; track 1; @1; crashnet crash; screen on; uipush menu; uipush tracks0; shot $T/crash_list.png" >/dev/null 2>&1
fresh
$SIM --headless --root "$T/fix" --cmd "vol 0; screen on; uipush menu; uipush tracks0; shot $T/plain_list.png" >/dev/null 2>&1
[ -s "$T/crash_list.png" ] && [ -s "$T/plain_list.png" ] && ! cmp -s "$T/crash_list.png" "$T/plain_list.png" || fail crashnet_list_differs

# format tiers (2026-11-18): the setting row (off by default; off -> on takes two taps, on -> off one) and the scan / open of an experimental file
fresh
tap="; settings key experimental; @0.1"
row "" | grep -qx 'APP settings row=Experimental formats: off' || fail tier_row_default_off
row "$tap" | grep -q 'row=These readers are less mature: a damaged file may reboot the player' || fail tier_row_warns
row "$tap$tap" | grep -qx 'APP settings row=Experimental formats: on' || fail tier_row_on_two_taps
row "$tap$tap$tap" | grep -qx 'APP settings row=Experimental formats: off' || fail tier_row_off_one_tap
# FPFIX: the power features are FakePod-only: no auto power-off row on the round board (hidden, not greyed)
row "" | grep -q '自動關機\|Auto power-off' && fail round_no_autooff_row
row "" | grep -qiE '常亮|永不|never|always' && fail round_no_never_row   # no screen-timeout choice on the round board either (the LCD keeps its fixed timeout)
$SIM --headless --root "$T/fix" --cmd "uipush menu; uipush settings$tap$tap; @5" >/dev/null 2>&1
grep -q '^exp_fmt=1$' "$T/fix/_player/config/app.txt" || { fail tier_row_saved; cat "$T/fix/_player/config/app.txt"; }

# an experimental file: listed by name (the scan never read it), refused with the reason while the setting is off (the fake audio ends the play with AUD_ERROR: simaudiofail)
fresh
mkdir -p "$T/fix/export/zz"; head -c 600 /dev/urandom > "$T/fix/export/zz/slow.a2m"; head -c 600 /dev/urandom > "$T/fix/export/zz/calm.rad"
out=$($SIM --headless --root "$T/fix" --cmd "scan; @0.5; libls 0; vol 0; simaudiofail 1; track 1; @1" 2>&1)
grep -q '^APP libls 1 slow | /sdcard/export/zz/slow.a2m$' <<<"$out" && grep -q '^APP skip reason=experimental path=/sdcard/export/zz/slow.a2m$' <<<"$out" || { fail tier_refused_off; echo "$out" | grep 'libls\|skip'; }
out=$($SIM --headless --root "$T/fix" --cmd "scan; @0.5; vol 0; seti exp_fmt 1; simaudiofail 1; track 1; @1" 2>&1)
grep -q 'reason=experimental' <<<"$out" && fail tier_refused_on
out=$($SIM --headless --root "$T/fix" --cmd "scan; @0.5; vol 0; simaudiofail 1; track 0; @1" 2>&1)
grep -q 'reason=experimental' <<<"$out" && fail tier_stable_not_refused

# the alarm rings: 1 minute ahead (21:31 Taipei = 13:31 UTC), @61, the ring page is on top
out=$($SIM --headless --root "$T/fix" --cmd "alarm 21 31; @61; uitop" 2>&1)
grep -q '^APP uitop ring$' <<<"$out" || { fail alarm_ring; echo "$out" | tail -5; }

# empty root = a new card: every page opens, the track list is empty
rm -rf "$T/empty"; mkdir "$T/empty"; mkdir -p "$T/empty/_player/config"; echo lang=0 > "$T/empty/_player/config/app.txt"
out=$($SIM --headless --root "$T/empty" --cmd "uipush menu; mp3ui; list; @0.5; shot $T/empty_list.png; setui; sceneui 0; uipush radio; @0.5" 2>&1); rc=$?
[ $rc = 0 ] || fail "empty_root_rc=$rc"
grep -q '^APP library loaded rc=-2 n=0 folders=0 ' <<<"$out" || { fail empty_root_library; echo "$out" | head -20; }
grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out" && { fail empty_root_sanitizer; echo "$out"; }
fresh
$SIM --headless --root "$T/fix" --cmd "uipush menu; mp3ui; list; @0.5; shot $T/fix_list.png" >/dev/null 2>&1
[ -s "$T/empty_list.png" ] && [ -s "$T/fix_list.png" ] && ! cmp -s "$T/empty_list.png" "$T/fix_list.png" || fail empty_list_differs_from_fixture
# no card: pages open, scan refuses, no sanitizer output
out=$(SIM_STATE="sd=0" $SIM --headless --root "$T/fix" --cmd "uipush menu; mp3ui; list; @0.5; scan; @0.5; setui; uipush radio" 2>&1); rc=$?
[ $rc = 0 ] || fail "nocard_rc=$rc"
grep -q '^APP library loaded rc=-2 n=0 ' <<<"$out" && grep -q '^APP scan refused (no card)$' <<<"$out" || { fail nocard_behaviour; echo "$out" | head -20; }
grep -q 'Sanitizer\|runtime error' <<<"$out" && fail nocard_sanitizer
# SIM_STATE shots differ from the default ones
cmd="vol 0; track 1; @3; shot"
fresh; $SIM --headless --root "$T/fix" --cmd "$cmd $T/st_def.png; btui; @1; shot $T/bt_def.png" >/dev/null 2>&1
fresh; SIM_STATE="batt=3;charging=0" $SIM --headless --root "$T/fix" --cmd "$cmd $T/st_batt.png" >/dev/null 2>&1
fresh; SIM_STATE="bt=connected" $SIM --headless --root "$T/fix" --cmd "btui; @1; shot $T/bt_con.png" >/dev/null 2>&1
[ -s "$T/st_batt.png" ] && ! cmp -s "$T/st_def.png" "$T/st_batt.png" || fail state_batt_differs
[ -s "$T/bt_con.png" ] && ! cmp -s "$T/bt_def.png" "$T/bt_con.png" || fail state_bt_differs
# two runs of the same commands give the same pixels
fresh; $SIM --headless --root "$T/fix" --cmd "$cmd $T/st_def2.png" >/dev/null 2>&1
cmp -s "$T/st_def.png" "$T/st_def2.png" || fail shots_repeat
# the drivers' crash report also covers the first commands, prints FAILED and leaves no temp root behind
# (its own TMPDIR: counting /tmp/sim_root_* globally raced with any other simulator run on the machine, e.g. another agent's make sim-test)
mkdir "$T/tmp0"
TMPDIR="$T/tmp0" SIM_CRASH_ON="mem" python3 tools/simtools/ui_baseline.py sim "$T/crash0" >"$T/crash0.log" 2>&1; rc=$?
left=$(ls "$T/tmp0" | wc -l)
[ $rc != 0 ] && grep -q '^FAILED: simulator died at command #1' "$T/crash0.log" && [ "$left" = 0 ] || { EVID="rc=$rc leftover temp dirs=$left"; fail crash_first_command; cat "$T/crash0.log"; }

# shutdown with screens open (the fonts must outlive the screens): commands done / stdin EOF / quit all end 0 with no sanitizer output
for c in "uipush menu; uipush settings" "uipush menu; vocab; vocab start" "uipush menu; list" "track 1; @3; nowmenu -2" "uipush menu; vocab; quit"; do
    $SIM --headless --root "$T/root" --cmd "$c" >/dev/null 2>"$T/err"; rc=$?
    [ $rc = 0 ] && ! grep -q 'Sanitizer\|runtime error' "$T/err" || { fail "shutdown_cmd: $c (rc=$rc)"; head -5 "$T/err"; }
    printf '%s\n' "${c//; /$'\n'}" | $SIM --headless --root "$T/root" >/dev/null 2>"$T/err"; rc=$?
    [ $rc = 0 ] && ! grep -q 'Sanitizer\|runtime error' "$T/err" || { fail "shutdown_stdin: $c (rc=$rc)"; head -5 "$T/err"; }
done

# simtime: the wall clock reads the given UTC instant after the command (Task 4, tools/simtools/sim_diff.py)
out=$($SIM --headless --root "$T/root" --cmd "simtime 2026-11-11 07:05:04.5; simtime nonsense; clock" 2>&1)
[ "$(grep -c '^SIM bad time$' <<<"$out")" = 1 ] && [ "$(grep -c '^SIM ok$' <<<"$out")" = 3 ] || { fail simtime_bad; echo "$out"; }   # only the nonsense one is refused
# the good path: the wall clock reads that instant (clock page minute) and the same command with another instant gives another picture
$SIM --headless --root "$T/root" --cmd "simtime 2026-11-11 07:05:04; clock; shot $T/t1.png; simtime 2026-11-11 07:35:04; shot $T/t2.png" >/dev/null 2>&1
[ -s "$T/t1.png" ] && [ -s "$T/t2.png" ] && ! cmp -s "$T/t1.png" "$T/t2.png" || fail simtime_good

# tools: sim_diff refuses a directory that is not a fixture; sim_root takes only the main board's by-id path
mkdir "$T/notfix"; touch "$T/notfix/keep.txt"
python3 tools/simtools/sim_fixture.py "$T/notfix" >/dev/null 2>&1; rc=$?
[ $rc != 0 ] && [ -e "$T/notfix/keep.txt" ] || fail fixture_refuses_foreign_dir
# a directory that holds only config/.index/export but not the generator's marker is not wiped either
mkdir -p "$T/lookalike/_player/config" "$T/lookalike/export"; touch "$T/lookalike/_player/config/mine.txt"
python3 tools/simtools/sim_fixture.py "$T/lookalike" >/dev/null 2>&1; rc=$?
[ $rc != 0 ] && [ -e "$T/lookalike/_player/config/mine.txt" ] || fail fixture_refuses_lookalike_dir
python3 tools/simtools/sim_root.py /dev/ttyACM0 "$T/r" >/dev/null 2>&1 && fail sim_root_port
[ ! -e "$T/r" ] || fail sim_root_port_dir
mkdir -p "$T/evil/_player/index"
python3 -c "
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; import sim_fixture as f
open(sys.argv[1], 'wb').write(f.build_index([('../..', 'a.mp3', 't', 'a', 'b', 20240101, 1, 0, 0)]))" "$T/evil/_player/index/library.bin"
python3 tools/simtools/sim_root.py --placeholders "$T/evil" >"$T/evil.log" 2>&1; rc=$?
[ $rc != 0 ] && grep -q 'not accepted' "$T/evil.log" || fail placeholders_dotdot
[ -z "$(find "$T/evil" -type f ! -name library.bin)" ] && [ ! -e "$T/xport" ] || fail placeholders_dotdot_files
# "/sdcard//etc/x": the part after /sdcard/ is absolute, os.path.join would leave the output directory
mkdir -p "$T/evil2/_player/index"
python3 -c "
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; import sim_fixture as f
f.EXPORT = '/sdcard/'
open(sys.argv[1], 'wb').write(f.build_index([('etc', 'sim-escape.mp3', 't', 'a', 'b', 20240101, 1, 0, 0)]))" "$T/evil2/_player/index/library.bin"
python3 tools/simtools/sim_root.py --placeholders "$T/evil2" >"$T/evil2.log" 2>&1; rc=$?
[ $rc != 0 ] && grep -q 'not accepted' "$T/evil2.log" && [ ! -e /etc/sim-escape.mp3 ] && [ -z "$(find "$T/evil2" -type f ! -name library.bin)" ] || { fail placeholders_absolute_rel; cat "$T/evil2.log"; }

# a command line over 511 bytes is reported and skipped (never run cut short); the next command still runs
out=$($SIM --headless --root "$T/root" --cmd "$(printf 'a%.0s' $(seq 1 600)); uitop" 2>&1)
grep -q '^SIM line too long (600 bytes, max 511): skipped$' <<<"$out" && grep -q '^APP uitop ' <<<"$out" && ! grep -q 'APP cmd unknown' <<<"$out" && [ "$(grep -c '^SIM ok$' <<<"$out")" = 2 ] || { fail long_line; echo "$out"; }

# a path with .. under /sdcard fails like a missing file and nothing leaves the root (selftest checks every wrapped call)
$SIM --headless --root "$T/root" --cmd "shot /sdcard/../sim-escape.png" >/dev/null 2>&1
[ ! -e "$T/sim-escape.png" ] || fail shot_dotdot_escape

