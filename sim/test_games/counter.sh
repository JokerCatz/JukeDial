# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G2: 多組計數器 (app/main/ui/scr_counter.c) and the saved numbers behind it (app/main/gamesave_svc.c, components/player_core/src/pc_gamesave.c); sourced by sim/test.sh ---
# Console: `counter` dumps "APP counter n= sel= name= style= v=<values>", `counter tap <i>` a touch on counter i, `counter sel <i>`, `counter ring`, `counter reset`,
# `counter set <n> <name> <style>`; `gamesave` dumps "APP gamesave status= keys= dirty= writes= card= bad= full=" ( `dump`, `get <k>`, `set <k> <v>`, `flush`, `clear` ).
ct_last() { grep '^APP counter n=' <<<"$out" | tail -1; }
ct_v() { ct_last | sed 's/.* v=//'; }
ct_f() { ct_last | sed -n "s/.* $1=\([^ ]*\).*/\1/p"; }
gs_f() { grep '^APP gamesave status=' <<<"$out" | tail -1 | sed -n "s/.* $1=\([^ ]*\).*/\1/p"; }
nowrun counter_tap_knob "screen on; vol 0; tools counter; @0.5; uistack; counter; counter tap 1; counter tap 1; counter tap 3; counter; simknob 3; counter; simknob -9; counter" && {
    stack_is counter_open 1 "menu tools tools_counter"
    v=$(grep '^APP counter n=' <<<"$out" | sed 's/.* v=//' | tr '\n' ' ')
    [ "$v" = "0,0,0,0 0,1,0,0 0,2,0,0 0,2,0,1 0,2,0,1 0,2,0,4 0,2,0,0 " ] || { EVID="$v"; fail counter_tap_knob; }      # every command dumps: taps +1 each; the knob (borrowed by the last tap) adds 3 to counter 3, then clamps at 0
    [ "$(grep '^APP counter n=' <<<"$out" | sed -n 4p | sed -n 's/.* sel=\([0-9]\).*/\1/p')" = 3 ] || fail counter_selected; }
# 4 s after the last tap the knob is the volume again
nowrun counter_knob_returns "screen on; vol 5; tools counter; @0.5; counter tap 0; @5; simknob 2; counter; vol" && {
    [ "$(ct_v)" = "1,0,0,0" ] || { EVID="$(ct_v)"; fail counter_knob_returns; }; }
# every 10th count plays the bank's tick, others do not
nowrun counter_tick "screen on; vol 0; tools counter; @0.5; counter tap 0; counter tap 0; counter tap 0; counter tap 0; counter tap 0; counter tap 0; counter tap 0; counter tap 0; counter tap 0; counter tap 0" && {
    [ "$(grep -c ' sound=tick' <<<"$out")" = 1 ] && grep -q '^APP counter 0=10 n=4 sound=tick' <<<"$out" || { EVID="$(grep '^APP counter 0=' <<<"$out" | tail -2 | tr '\n' '|')"; fail counter_tick; }; }
# the ring: 計數器數 steps 4 -> 8 -> 2 (the sectors are rebuilt), 名稱 steps, 歸零 needs two taps, 說明 is the last cell
nowrun counter_ring "screen on; vol 0; tools counter; @0.5; counter tap 2; counter ring; @0.3; popring -5; popring 0; counter; popring 0; counter; popring 1; popring 1; counter; popring 4; @0.5; uitop; back; @0.3; counter ring; popring 3; popring -5; counter; popring 3; popring -5; counter" && {
    n=$(grep '^APP counter n=' <<<"$out" | sed -n 's/^APP counter n=\([0-9]\) sel=\([0-9]\) name=\([0-9]\).* v=\(.*\)/\1:\2:\3:\4/p' | tr '\n' ' ')
    [ "$n" = "4:2:0:0,0,1,0 4:2:0:0,0,1,0 8:2:0:0,0,1,0,0,0,0,0 2:0:0:0,0 2:0:2:0,0 2:0:2:0,0 2:0:2:0,0 2:0:2:0,0 " ] || { EVID="$n"; fail counter_ring; }       # (an 8 -> 2 rebuild keeps the values of the first two: counter 2's 1 is cut off the page but kept)
    grep -qx 'APP uitop explain' <<<"$out" || fail counter_help_last
    [ "$(grep '^APP popring open=1' <<<"$out" | sed -n 1p | sed 's/.* cells=\([0-9]*\) .*/\1/')" = 5 ] || fail counter_ring_cells; }
nowrun counter_zero_armed "screen on; vol 0; tools counter; @0.5; counter tap 0; counter tap 1; counter ring; @0.3; popring 3; popring -5; counter; popring 3; popring -5; counter" && {
    grep -q 'armed=3 centre=' <<<"$out" || fail counter_armed
    [ "$(grep '^APP counter n=' <<<"$out" | sed 's/.* v=//' | tr '\n' ' ')" = "1,0,0,0 1,1,0,0 1,1,0,0 1,1,0,0 0,0,0,0 " ] || { EVID="$(grep '^APP counter n=' <<<"$out" | sed 's/.* v=//' | tr '\n' ' ')"; fail counter_zero_armed; }; }
nowrun counter_names "screen on; vol 0; tools counter; @0.5; counter set 4 3 1; counter reset; counter sel 2; sectors" && {
    grep -q '^APP sectors status C: 0' <<<"$out" || { EVID="$(grep '^APP uibox label' <<<"$out" | tail -2 | tr '\n' ' ')"; fail counter_names; }; }
# saved: the values, the count and the names come back after a restart (the same root run twice)
fresh
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; tools counter; @0.5; counter set 8 3 2; counter reset; counter tap 0; counter tap 0; counter tap 6; @2; gamesave" 2>&1)
grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out" && fail counter_save_run1
[ "$(gs_f writes)" -ge 1 ] 2>/dev/null && [ "$(gs_f dirty)" = 0 ] && grep -q '^ctr.v0=2$' "$T/fix/_player/config/games.txt" && head -1 "$T/fix/_player/config/games.txt" | grep -qx pcgs1 && [ ! -e "$T/fix/_player/config/games.txt.tmp" ] || { EVID="$(gs_f writes) $(gs_f dirty) $(head -3 "$T/fix/_player/config/games.txt" | tr '\n' ' ')"; fail counter_saved_file; }
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; tools counter; @0.5; counter" 2>&1)
[ "$(ct_last)" = "APP counter n=8 sel=0 name=3 style=2 v=2,0,0,0,0,0,1,0" ] || { EVID="$(ct_last)"; fail counter_restart; }
# a corrupt file: the store starts empty, the file is kept as .bad, the next save is a good file
fresh
mkdir -p "$T/fix/_player/config"; printf 'pcgs1\nctr.v0=12\nend=1 deadbeef\n' > "$T/fix/_player/config/games.txt"
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; gamesave; tools counter; @0.5; counter tap 0; gamesave flush; gamesave" 2>&1)
[ "$(grep '^APP gamesave status=' <<<"$out" | sed -n 1p | sed -n 's/.* status=\([a-z_]*\).*/\1/p')" = corrupt ] && [ -f "$T/fix/_player/config/games.txt.bad" ] && grep -q '^ctr.v0=12$' "$T/fix/_player/config/games.txt.bad" \
  && grep -q '^ctr.v0=1$' "$T/fix/_player/config/games.txt" && [ "$(grep '^APP gamesave status=' <<<"$out" | sed -n 1p | sed -n 's/.* keys=\([0-9]*\).*/\1/p')" = 0 ] || { EVID="$(grep '^APP gamesave' <<<"$out" | head -3 | tr '\n' '|')"; fail gamesave_corrupt; }
# no card: values live in memory, nothing is written, the status says why
fresh
out=$(SIM_STATE="sd=0" $SIM --headless --root "$T/fix" --cmd "screen on; vol 0; tools counter; @0.5; counter tap 1; @12; gamesave" 2>&1)
[ "$(gs_f status)" = no_card ] && [ "$(gs_f card)" = 0 ] && [ "$(ct_last | sed 's/.* v=//')" = "0,1,0,0" ] && [ ! -e "$T/fix/_player/config/games.txt" ] || { EVID="$(gs_f status) $(ct_v)"; fail gamesave_no_card; }
# a write is debounced: ten counts in ten seconds are written as at most two files; the page closing writes what is left at once
fresh
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; tools counter; @0.5; counter tap 0; @1.5; counter tap 0; @1.5; counter tap 0; @1.5; counter tap 0; @1.5; counter tap 0; gamesave; back; @0.3; gamesave" 2>&1)
w1=$(grep '^APP gamesave status=' <<<"$out" | head -1 | sed -n 's/.* writes=\([0-9]*\).*/\1/p'); w2=$(grep '^APP gamesave status=' <<<"$out" | tail -1 | sed -n 's/.* writes=\([0-9]*\).*/\1/p')
[ "$w1" -le 2 ] && [ "$w2" -le 3 ] && [ "$w2" -gt "$w1" ] && grep -q '^ctr.v0=5$' "$T/fix/_player/config/games.txt" || { EVID="w1=$w1 w2=$w2 $(head -3 "$T/fix/_player/config/games.txt" | tr '\n' ' ')"; fail gamesave_debounce; }
# the bests of the games survive a restart: 井字棋 (two players) won in 5 moves in the first run; in the second run a win in 7 moves is not a best, the stored 5 is
fresh
out=$($SIM --headless --root "$T/fix" --cmd "screen on; games open ttt; @0.3; games mode 2; @0.2; board tap 0; board tap 3; board tap 1; board tap 4; board tap 2; @1.5; gamesave" 2>&1)
grep -qx 'APP game ttt end outcome=1 streak=0 best=5 new_best=1 sound=win' <<<"$out" && grep -q '^ttt.m[0-9]=5$' "$T/fix/_player/config/games.txt" || { EVID="$(grep '^APP game ttt end' <<<"$out")"; fail gamesave_best_written; }
out=$($SIM --headless --root "$T/fix" --cmd "screen on; games open ttt; @0.3; games mode 2; @0.2; board tap 0; board tap 1; board tap 2; board tap 3; board tap 4; board tap 5; board tap 8; @1.5; games result" 2>&1)
grep -qx 'APP game ttt end outcome=1 streak=0 best=5 new_best=0 sound=win' <<<"$out" || { EVID="$(grep '^APP game ttt end' <<<"$out")"; fail gamesave_best_restart; }
# the same without the file: the 7-move win is the first one, so it is the best
fresh
out=$($SIM --headless --root "$T/fix" --cmd "screen on; games open ttt; @0.3; games mode 2; @0.2; board tap 0; board tap 1; board tap 2; board tap 3; board tap 4; board tap 5; board tap 8; @1.5" 2>&1)
grep -qx 'APP game ttt end outcome=1 streak=0 best=7 new_best=1 sound=win' <<<"$out" || { EVID="$(grep '^APP game ttt end' <<<"$out")"; fail gamesave_best_fresh; }
# the picture pages open and the leak check has its actions
# fix round 1: the save is never the only copy lost. (a) games.txt gone but a complete .tmp left (a power cut between the remove and the rename): the next boot takes it
fresh
$SIM --headless --root "$T/fix" --cmd "screen on; vol 0; tools counter; @0.5; counter tap 0; counter tap 0; gamesave flush" >/dev/null 2>&1
mv "$T/fix/_player/config/games.txt" "$T/fix/_player/config/games.txt.tmp"
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; gamesave; tools counter; @0.5; counter" 2>&1)
grep -q '^APP gamesave recovered from .tmp keys=1 rename=0' <<<"$out" && [ "$(ct_v)" = "2,0,0,0" ] && [ -f "$T/fix/_player/config/games.txt" ] && [ ! -e "$T/fix/_player/config/games.txt.tmp" ] || { EVID="$(grep '^APP gamesave' <<<"$out" | head -3 | tr '\n' '|') $(ct_v)"; fail gamesave_recover_absent; }
# (b) a corrupt games.txt and a good .tmp: the .tmp wins, the corrupt one is kept as .bad
fresh
$SIM --headless --root "$T/fix" --cmd "screen on; vol 0; tools counter; @0.5; counter tap 0; gamesave flush" >/dev/null 2>&1
mv "$T/fix/_player/config/games.txt" "$T/fix/_player/config/games.txt.tmp"; printf 'garbage\n' > "$T/fix/_player/config/games.txt"
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; tools counter; @0.5; counter" 2>&1)
[ "$(ct_v)" = "1,0,0,0" ] && grep -qx garbage "$T/fix/_player/config/games.txt.bad" || { EVID="$(ct_v)"; fail gamesave_recover_corrupt; }
# (c) both renames fail (FatFs refuses, then the second; a third failure: the page closing at the end retries the pending .tmp first): the .tmp stays, nothing is deleted after the remove, a later save puts it in place, a restart without it recovers it
fresh
$SIM --headless --root "$T/fix" --cmd "screen on; vol 0; tools counter; @0.5; counter tap 0; gamesave flush" >/dev/null 2>&1
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; tools counter; @0.5; gamesave failrename 3; counter tap 0; gamesave flush; gamesave" 2>&1)
[ ! -e "$T/fix/_player/config/games.txt" ] && grep -q '^ctr.v0=2$' "$T/fix/_player/config/games.txt.tmp" && grep -q 'status=write_failed .* pending=1' <<<"$out" || { EVID="$(grep '^APP gamesave status' <<<"$out" | tail -1)"; fail gamesave_rename_fails; }
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; tools counter; @0.5; counter" 2>&1)
[ "$(ct_v)" = "2,0,0,0" ] || { EVID="$(ct_v)"; fail gamesave_rename_fails_recovered; }
# (d) games.txt cannot be read (here: it is a directory): status read_failed, a save never overwrites it
fresh
mkdir -p "$T/fix/_player/config/games.txt"
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; gamesave; tools counter; @0.5; counter tap 0; gamesave flush; @12; gamesave" 2>&1)
[ "$(grep '^APP gamesave status=' <<<"$out" | head -1 | sed -n 's/.* status=\([a-z_]*\).*/\1/p')" = read_failed ] && [ -d "$T/fix/_player/config/games.txt" ] && [ "$(gs_f writes)" = 0 ] && grep -q ' ro=1 ' <<<"$out" && [ "$(ct_v)" = "1,0,0,0" ] || { EVID="$(grep '^APP gamesave' <<<"$out" | tr '\n' '|')"; fail gamesave_read_failed; }
# (e) updating existing keys of a full store is not "full"; a new key of a full store is refused and says so
fresh
out=$($SIM --headless --root "$T/fix" --cmd "gamesave clear; gamesave set a 1; gamesave set a 2; gamesave" 2>&1)
[ "$(gs_f full)" = 0 ] || { EVID="$(gs_f full)"; fail gamesave_not_full; }
