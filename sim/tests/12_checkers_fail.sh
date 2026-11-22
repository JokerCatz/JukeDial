# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: need_base)
need_base
# ---- the checkers can fail (final review R1): each is run on a case that is wrong on purpose and must say so ----
# sim_diff: the same pages with another random seed against the simulator's own shots (the lists/fortune pages differ)
python3 tools/simtools/sim_diff.py --base "$T/base" --out "$T/dout" --shots "$T/dshots" --args "--seed 2" >"$T/seed2.log" 2>&1; rc=$?
[ $rc != 0 ] && grep -q '<-- DIFF' "$T/seed2.log" || { fail sim_diff_catches_seed; tail -5 "$T/seed2.log"; }
# sim_diff (final review M-3): a known_diffs line on a page that is now identical is named ("listed but identical") and counted, without failing the run;
# a listed page that does differ is not named; the other differences still fail
printf 'chip data (identical here)\nfortune_wheel random (differs with seed 2)\n' >"$T/known_lbi.txt"
python3 tools/simtools/sim_diff.py --base "$T/base" --out "$T/dout" --shots "$T/dshots" --known "$T/known_lbi.txt" --args "--seed 2" >"$T/lbi.log" 2>&1; rc=$?
[ $rc != 0 ] && grep -qx 'listed but identical: chip' "$T/lbi.log" && ! grep -q 'listed but identical: fortune_wheel' "$T/lbi.log" \
  && grep -q '^[0-9]* pages, [0-9]* not ok (2 listed in known_diffs, 1 listed but identical)$' "$T/lbi.log" || { fail sim_diff_listed_identical; tail -5 "$T/lbi.log"; }
# sim_diff: a baseline without any PNG
mkdir "$T/emptybase"; python3 tools/simtools/sim_diff.py --base "$T/emptybase" --out "$T/dout" --shots "$T/dshots" >/dev/null 2>&1 && fail sim_diff_empty_base
# ui_baseline (sim): an unknown command, an unknown page and a home() that cannot get back each fail the whole run, and no stale PNG survives
cat >"$T/redbase.py" <<'PY'
import os, sys
sys.path[:0] = ["tools", "tools/simtools", "tools/gen", "tools/check", "tools/board"]
import ui_baseline
which, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
open(os.path.join(out, "stale.png"), "wb").write(b"x")
def pages(c, root, outdir, only=None):
    if which == "cmd": c.cmd("nosuchcmd", 0.1)
    elif which == "page": c.cmd("uipush nosuchscreen", 0.1)
    elif which == "home": c.cmd("uipush menu", 0.1); c.home("nosuchroot")
    elif which == "ok": c.cmd("uipush menu", 0.1)
ui_baseline.pages = pages
rc = ui_baseline.sim_run(out)
if which == "ok": assert not os.path.exists(os.path.join(out, "stale.png")), "stale PNG kept"
sys.exit(rc)
PY
for w in cmd page home; do
    python3 "$T/redbase.py" $w "$T/red_$w" >"$T/red_$w.log" 2>&1; rc=$?
    [ $rc != 0 ] && grep -q '^FAILED: ' "$T/red_$w.log" || { fail "ui_baseline_red_$w"; cat "$T/red_$w.log"; }
done
python3 "$T/redbase.py" ok "$T/red_ok" >"$T/red_ok.log" 2>&1 || { fail ui_baseline_red_control; cat "$T/red_ok.log"; }
# check_font_glyphs: a missing glyph names the real repo-relative file, also under components/player_core (fix9)
if [ -d fonts ]; then
    mkdir -p "$T/cfg/tools/check" "$T/cfg/app/main" "$T/cfg/components/player_core/src"
    cp tools/check/check_font_glyphs.py "$T/cfg/tools/check/"
    printf 'const char *a = "\xe1\x97\x9c";\n' >"$T/cfg/app/main/m.c"; printf '\n\nconst char *b = "\xe1\x97\x9d";\n' >"$T/cfg/components/player_core/src/p.c"
    python3 "$T/cfg/tools/check/check_font_glyphs.py" "$PWD/fonts" /dev/null >"$T/cfg.log" 2>&1; rc=$?
    [ $rc != 0 ] && grep -q 'first used at app/main/m.c:1)' "$T/cfg.log" && grep -q 'first used at components/player_core/src/p.c:3)' "$T/cfg.log" && ! grep -q 'app/main/components' "$T/cfg.log" || { fail check_font_glyphs_real_path; cat "$T/cfg.log"; }
fi
# leak_cycles (sim): the leaking test page (uileak) and an esp_timer that is never deleted each make the verdict fail; a clean action passes
python3 tools/simtools/leak_cycles.py uileak:3 --port sim --every 1 --warm 1 >"$T/leak_ui.log" 2>&1; rc=$?
[ $rc != 0 ] && grep -q 'ui_leak_lines=[1-9]' "$T/leak_ui.log" || { fail leak_cycles_catches_uileak; tail -4 "$T/leak_ui.log"; }
python3 tools/simtools/leak_cycles.py simleaktimer:4 --port sim --every 1 --warm 1 >"$T/leak_tm.log" 2>&1; rc=$?
[ $rc != 0 ] && grep -q 'esp_timers [0-9]* -> [0-9]* CHANGES' "$T/leak_tm.log" || { fail leak_cycles_catches_timer; tail -4 "$T/leak_tm.log"; }
python3 tools/simtools/leak_cycles.py idle:3 --port sim --every 1 --warm 1 >"$T/leak_ok.log" 2>&1 || { fail leak_cycles_control; tail -4 "$T/leak_ok.log"; }

# bad console arguments: sceneui out of range (either sign) must not crash the scene page; show of an unknown name reports it
fresh
for n in 99 -1; do
    out=$($SIM --headless --root "$T/fix" --cmd "sceneui $n; @0.2; uitop" 2>&1); rc=$?
    [ $rc = 0 ] && ! grep -q 'Sanitizer\|runtime error' <<<"$out" && grep -q '^APP uitop ' <<<"$out" || { fail "sceneui_bounds_$n rc=$rc"; echo "$out" | tail -4; }
done
out=$($SIM --headless --root "$T/fix" --cmd "show nosuch; @0.2; uitop" 2>&1)
grep -q '^APP show unknown nosuch$' <<<"$out" && grep -q '^APP uitop menu$' <<<"$out" || { fail show_unknown; echo "$out" | tail -3; }
out=$($SIM --headless --root "$T/fix" --cmd "show alarm; @0.2; uitop" 2>&1)
grep -q '^APP uitop alarm$' <<<"$out" || { fail show_alarm_still_works; echo "$out" | tail -3; }
# vocab stat / reset with no deck open: after the vocabulary page closed, and from a cold start
for cmds in "vocab; @0.5; back; @0.5; vocab stat; vocab reset" "vocab stat; vocab reset"; do
    out=$($SIM --headless --root "$T/fix" --cmd "$cmds" 2>&1); rc=$?
    [ $rc = 0 ] && ! grep -q 'Sanitizer\|runtime error' <<<"$out" && grep -q '^APP vocab stat deck=.* total=0 ' <<<"$out" || { fail "vocab_no_deck rc=$rc ($cmds)"; echo "$out" | tail -4; }
    grep -q '^APP vocab reset no deck$' <<<"$out" || fail "vocab_reset_no_deck_msg ($cmds)"
done

# timer races (ui_timer_bind / ui_timer_guard, spec 2026-11-11-ui-kit-diagnostics 3.2): leave or rebuild pages while their timers run
# or are about to fire (2 rounds here, ~7 s; tools/simtools/sim_timer_races.py --reps 10 for the full run)
python3 tools/simtools/sim_timer_races.py --reps 2 >"$T/races.log" 2>&1 || { fail timer_races; tail -8 "$T/races.log"; }
# the rebuild tripwire (ui_rebuild_all destroys every layer, then checks): a screen whose forever-timer and object outlive it across a
# theme change is reported once, by name; an animation that the rebuild stops is not a leak
fresh
out=$($SIM --headless --root "$T/fix" --cmd "uipush settings; uileak; theme next; uitop" 2>&1 | sed '/^APP uitop/q')   # up to the rebuild (the exit pops the rest)
n=$(grep -c '^APP ui leak' <<<"$out")
grep -q '^APP ui leak screen=leaktest timers=+1 objs=+1$' <<<"$out" && [ "$n" = 1 ] || { EVID="$n leak lines: $(grep '^APP ui leak' <<<"$out" | tr '\n' ' ')"; fail rebuild_tripwire_fires; }
fresh
out=$($SIM --headless --root "$T/fix" --cmd "screen on; tools dice; @0.5; tools roll; @0.1; simtimers; theme next; simtimers; @0.5; fortune wheel; @0.5; fortune draw; @0.1; simtimers; theme next; simtimers; uitop" 2>&1)
! grep -q '^APP ui leak' <<<"$out" && grep -q '^APP uitop fortune_wheel$' <<<"$out" || { EVID="$(grep '^APP ui leak\|^APP uitop' <<<"$out" | tr '\n' ' ')"; fail rebuild_tripwire_quiet_on_stopped_animation; }
# ...and the animation really ran into each rebuild: fewer 33 ms timers (paused or not) right after the theme change than right before
python3 -c "
import re, sys
c = [sum(1 for x in m.split(',') if x.rstrip('p') == '33') for m in re.findall(r'^SIM timers n=\d+ forever=\d+ periods=(\S*)$', sys.stdin.read(), re.M)]
sys.exit(0 if len(c) == 4 and c[0] > c[1] and c[2] > c[3] else 'counts of 33 ms timers before/after each rebuild: %s' % c)" <<<"$out" || fail rebuild_quiet_case_hit_running_animation
# a LOWER screen's own forever timer, started while a child is open and deleted by its destroy: no leak on a rebuild (simowner, sim/sim_cmd.c)
fresh
out=$($SIM --headless --root "$T/fix" --cmd "simowner; uipush settings; simowner start; simtimers; theme next; simtimers; uitop" 2>&1 | sed '/^APP uitop/q')
! grep -q '^APP ui leak' <<<"$out" && grep -q '^SIM owner timer=1 leak=0$' <<<"$out" || { EVID="$(grep '^APP ui leak\|^SIM owner' <<<"$out" | tr '\n' ' ')"; fail rebuild_lower_owned_timer_quiet; }
# the same timer leaked: reported once, by the layer that was on top when it appeared (settings), or by the owner when started before the child
for case in "simowner; uipush settings; simowner leak; simowner start|settings" "simowner; simowner leak; simowner start; uipush settings|simowner"; do
    fresh
    out=$($SIM --headless --root "$T/fix" --cmd "${case%|*}; theme next; uitop" 2>&1 | sed '/^APP uitop/q')
    n=$(grep -c '^APP ui leak' <<<"$out")
    grep -q "^APP ui leak screen=${case#*|} timers=+1 objs=+0\$" <<<"$out" && [ "$n" = 1 ] || { EVID="$n lines: $(grep '^APP ui leak' <<<"$out" | tr '\n' ' ')"; fail "rebuild_lower_timer_leak_named (${case#*|})"; }
done

