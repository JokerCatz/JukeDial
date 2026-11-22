#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Host tests of the simulator skeleton (make sim-test builds first). Any ASan/UBSan/LSan output makes a test fail.
cd "$(dirname "$0")/.." || exit 1
SIM=sim/build/sim
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
GAME_PICS=$(( $(grep -c '^GAME(' app/main/ui/games.def) + 1 ))   # the pictures of the games: the hub's and one a game of app/main/ui/games.def (games G0): no count to bump when a game is added
GAME_PICS=$(( GAME_PICS + $(python3 -c 'import sys; sys.path[:0] = ["tools", "tools/simtools", "tools/gen", "tools/check", "tools/board"]; import ui_baseline; print(len(ui_baseline.GAME_FEEL_PICS))') ))   # G0b: the game-feel pictures (tools/simtools/ui_baseline.py GAME_FEEL_PICS)
GAME_PICS=$(( GAME_PICS + $(python3 -c 'import sys; sys.path[:0] = ["tools", "tools/simtools", "tools/gen", "tools/check", "tools/board"]; import ui_baseline; print(len(ui_baseline.LANG_PICS))') ))   # i18n A: 3 sample pages in 簡中 and in English (tools/simtools/ui_baseline.py LANG_PICS)
fails=0
FAILED=; EVID=
fail() { echo "FAIL $1${EVID:+ | $EVID}"; FAILED="$FAILED $1"; EVID=; fails=$((fails + 1)); }

# The checks live in the pieces in sim/tests/, sourced here in name order (same shell: helpers and variables carry over); 00_helpers.sh (the functions) always.
# ONLY=viz,clock  runs 00_helpers + the pieces whose name contains one of the words (+ what a "# needs: NN_name" header names, if a piece has one). See the ui design notes.
# (The older case filter that nowrun and the tface block read from $ONLY is now CASE=<regex>: run only the cases whose name matches.)
# SIM_TEST_JOBS=N: every piece runs as its own bash process (own $T; a piece with "# needs:", if one ever has one, also re-runs those), N at a time; the FAIL lines are collected and the verdict below is printed once.
#   unset = the default: min(CPU count, 8) at a time. SIM_TEST_JOBS=1 (or empty) = one single bash process, the pieces one after the other (the old way). ONLY= and CASE= always run in one process.
_tp_only=$ONLY; ONLY=$CASE; CASE=
_tp_sel=()
_tp_jobs=${SIM_TEST_JOBS-$(( $(nproc 2>/dev/null || echo 1) > 8 ? 8 : $(nproc 2>/dev/null || echo 1) ))}
if [ "${_tp_jobs:-1}" -gt 1 ] 2>/dev/null && [ -z "$_tp_only" ] && [ -z "$ONLY" ]; then
    echo "sim-test: every piece in its own process, $_tp_jobs at a time (SIM_TEST_JOBS=1: one process, one piece after the other)"
    mkdir -p "$T/par"
    ls -S sim/tests/[1-9]*.sh | sed 's#.*/##; s#\.sh$##' | xargs -P "$_tp_jobs" -I{} sh -c 'ONLY={} SIM_TEST_JOBS=1 CASE= bash sim/test.sh > "$1/{}.out" 2>&1; echo $? > "$1/{}.rc"' _ "$T/par"
    for _tp_o in "$T"/par/*.out; do
        _tp_b=${_tp_o##*/}; _tp_b=${_tp_b%.out}
        grep -q '^SIM TEST \(ok\|[0-9]* failed\)' "$_tp_o" || { echo "FAIL piece_${_tp_b}_no_verdict | $(tail -2 "$_tp_o" | tr '\n' '|' | cut -c1-200)"; FAILED="$FAILED piece_${_tp_b}_no_verdict"; fails=$((fails + 1)); }
    done
    grep -ah '^FAIL ' "$T"/par/*.out | sort -u > "$T/par/fails.txt"      # a piece re-run for a "# needs:" can print the same line twice: one line each
    cat "$T/par/fails.txt"
    while read -r _tp_l; do FAILED="$FAILED $(sed 's/^FAIL \([^ ]*\).*/\1/' <<<"$_tp_l")"; fails=$((fails + 1)); done < "$T/par/fails.txt"
    _tp_ok=1
fi
[ -n "$_tp_ok" ] || for _tp_f in sim/tests/[0-9]*.sh; do
    _tp_b=${_tp_f##*/}; _tp_b=${_tp_b%.sh}
    if [ -z "$_tp_only" ] || [ "$_tp_b" = 00_helpers ]; then _tp_sel+=("$_tp_b"); continue; fi
    for _tp_w in ${_tp_only//,/ }; do [[ $_tp_b == *"$_tp_w"* ]] && { _tp_sel+=("$_tp_b"); break; }; done
done
if [ -n "$_tp_only" ]; then   # add the pieces a selected piece needs (transitively), then keep name order
    _tp_n=-1
    while [ ${#_tp_sel[@]} -ne "$_tp_n" ]; do
        _tp_n=${#_tp_sel[@]}
        for _tp_b in "${_tp_sel[@]}"; do _tp_sel+=($(sed -n 's/^# needs: *//p' "sim/tests/$_tp_b.sh" | tr ',' ' ')); done
        _tp_sel=($(printf '%s\n' "${_tp_sel[@]}" | sort -u))
    done
    echo "SIM TEST PARTIAL RUN (ONLY=$_tp_only): ${_tp_sel[*]}"
    [ ${#_tp_sel[@]} -gt 1 ] || { echo "SIM TEST ONLY=$_tp_only matches no piece (names: $(ls sim/tests | sed 's/\.sh$//' | tr '\n' ' '))"; exit 1; }
fi
for _tp_b in "${_tp_sel[@]}"; do . "sim/tests/$_tp_b.sh"; done   # (parallel mode: _tp_sel is empty)
[ $fails = 0 ] && echo "SIM TEST ok" || { echo "SIM TEST $fails failed:$FAILED"; exit 1; }
