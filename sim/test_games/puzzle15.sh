# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G4: 15 拼圖 on the shared board's tiles (app/main/ui/scr_game_puzzle15.c, ui_board.c tiles / slide / key cursor; rules in components/player_core/src/pc_game_puzzle15.c); sourced by sim/test.sh ---
# The game's console words (through `games`): `games state` prints "APP game puzzle15 state n= moves= blank= solved= movable= cursor= tiles=<a,b,...>" (cell order, 0 = the blank),
# `games count <n>` the shuffle's seed counter (a new game), `games set <t0> <t1> ...` puts tiles, `games almost [m]` the solved board but for one slide (the blank at n*n-2, `board tap` the last
# cell solves; m moves so far, default 40), `board slide` the tile in flight ("APP board slide on= to= from= cursor= keys="). The board dump's look letters: n tile, h home/solved, d the blank.
P15="screen on; games open puzzle15; @0.3; games mode 0; games style 0; games rule 0"
p15_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
p15_st() { grep '^APP game puzzle15 state ' <<<"$out" | sed -n "$1p"; }
# solvable by inversion parity: tiles of the shuffles of every rule (a few seeds), checked here independently of the C code
p15_parity() { awk -F, -v n="$1" '{ inv=0; br=0; for(i=1;i<=NF;i++){ if($i==0){br=int((i-1)/n); continue} for(j=i+1;j<=NF;j++) if($j!=0 && $j<$i) inv++ } if (n%2) ok=(inv%2==0); else ok=((inv+br)%2==1); print ok?"ok":"bad" }' <<<"$2"; }
nowrun p15_open "$P15; games state; board; uitop; uistack" && {
    s=$(p15_st 1)
    [ "$(p15_v "$s" n)" = 4 ] && [ "$(p15_v "$s" moves)" = 0 ] && [ "$(p15_v "$s" solved)" = 0 ] && [ "$(tp)" = "tools_game_puzzle15 " ] && grep -qx 'APP board w=4 h=4' <<<"$out" || { EVID="$s $(tp)"; fail p15_open; }
    t=$(p15_v "$s" tiles); [ "$(tr ',' '\n' <<<"$t" | sort -n | tr '\n' ',')" = "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15," ] || { EVID="$t"; fail p15_tiles_once; }
    grep -q '^APP game puzzle15 new rule=0 side=4 cell=[0-9]* style=0 mode=0 gold=0$' <<<"$out" || { EVID="$(grep '^APP game puzzle15 new' <<<"$out")"; fail p15_cell_size; }; }
# every rule's shuffles are solvable (parity) and differ with the seed
for r in 0 1 2; do
    nowrun p15_shuffles_$r "$P15; games rule $r; games count 1; games state; games count 2; games state; games count 3; games state; games count 4; games state" && {
        n=$((r == 1 ? 3 : r == 2 ? 5 : 4)); bad=0; seen=""
        for k in 1 2 3 4; do t=$(p15_v "$(p15_st $k)" tiles); [ "$(p15_parity $n "$t")" = ok ] || bad=1; seen="$seen$t;"; done
        [ $bad = 0 ] && [ "$(tr ';' '\n' <<<"$seen" | sort -u | grep -c .)" = 4 ] || { EVID="$seen"; fail p15_shuffles_$r; }; }
done
# a tap next to the blank slides it (a move, the tile now in the blank's cell, the blank where it was); a tile far from the blank and the blank itself are no moves; the slide is in flight, then done
nowrun p15_slide "$P15; games almost 5; board tap 0; board tap 14; games state; board tap 10; board slide; games state; @0.5; board slide" && {
    grep -qx 'APP game puzzle15 refused=0' <<<"$out" && [ "$(p15_v "$(p15_st 1)" moves)" = 5 ] || { EVID="$(p15_st 1)"; fail p15_refuse_far; }
    s=$(p15_st 2); [ "$(p15_v "$s" moves)" = 6 ] && [ "$(p15_v "$s" blank)" = 10 ] && [ "$(tr ',' '\n' <<<"$(p15_v "$s" tiles)" | sed -n 15p)" = 11 ] || { EVID="$s"; fail p15_slide_move; }
    sl=$(grep '^APP board slide on=' <<<"$out"); [ "$(sed -n 1p <<<"$sl")" = "APP board slide on=1 to=14 from=10 cursor=-1 keys=0" ] && [ "$(sed -n 2p <<<"$sl" | cut -d' ' -f4)" = on=0 ] || { EVID="$sl"; fail p15_slide_flight; }; }
# the slide animates only with the screen on, and never changes content: the same state with the screen off
nowrun p15_slide_screen_off "$P15; games almost 5; screen off; board tap 10; board slide; games state" && {
    [ "$(grep '^APP board slide on=' <<<"$out" | cut -d' ' -f4)" = on=0 ] && [ "$(p15_v "$(p15_st 1)" blank)" = 10 ] || { EVID="$(grep '^APP board slide' <<<"$out") $(p15_st 1)"; fail p15_slide_screen_off; }; }
# a slide in flight when the page closes: Back at once, then the hub (the timer goes with the board)
nowrun p15_back_mid_slide "$P15; games almost 5; board tap 10; exit pause; @0.2; exit tap back; @0.5; uitop" && { [ "$(tp)" = "tools_games " ] || { EVID="$(tp)"; fail p15_back_mid_slide; }; }
# solving: every tile is home (look h), the end line and the card; 41 moves is within the par (70): a golden solve; 101 moves is not
nowrun p15_solve_gold "$P15; games gold 0; games almost 40; board tap 15; board; games result; @1; games result" && {
    grep -qx 'APP game puzzle15 move=15 moves=41 blank=15 solved=1' <<<"$out" && grep -qx 'APP game puzzle15 end outcome=1 streak=0 best=41 new_best=1 sound=win' <<<"$out" || { EVID="$(grep 'puzzle15' <<<"$out" | tail -3)"; fail p15_solve_end; }
    [ "$(grep '^APP board look ' <<<"$out" | tail -4 | sed 's/^APP board look [0-9] //; s/ //g' | tr -d '\n')" = "hhhhhhhhhhhhhhhd" ] || { EVID="$(grep '^APP board look ' <<<"$out" | tr '\n' '|')"; fail p15_solved_looks; }
    grep -qx 'APP game_result up=1 shown=0 title=Solved! moves=41 ms=0 best=41/0 new_best=1 streak=0 gold=1' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out" | tr '\n' '|')"; fail p15_card_gold; }; }
nowrun p15_solve_plain "$P15; games gold 0; games almost 100; board tap 15; @1; games result" && {
    grep -qx 'APP game_result up=1 shown=1 title=Solved! moves=101 ms=0 best=101/0 new_best=1 streak=0 gold=0' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out")"; fail p15_card_plain; }; }
# a tap after the end starts again: a new shuffle, no moves, the card gone; the best stays (fewer moves a new best, more not)
nowrun p15_again "$P15; games gold 0; games almost 100; board tap 15; @1; board tap 5; games state; games result; games almost 40; board tap 15" && {
    s=$(p15_st 1); [ "$(p15_v "$s" moves)" = 0 ] && [ "$(p15_v "$s" solved)" = 0 ] && grep -qx 'APP game_result up=0' <<<"$out" || { EVID="$s"; fail p15_again; }
    grep -qx 'APP game puzzle15 end outcome=1 streak=0 best=41 new_best=1 sound=win' <<<"$out" || { EVID="$(grep ' end ' <<<"$out")"; fail p15_best_fewer; }; }
# the rules rebuild the board: 3x3 and 5x5 have their own tiles, the ring and Back keep working; each rule keeps its own best
nowrun p15_rules "$P15; games rule 1; board; games state; games rule 2; board; games state; games rule 0; board; games state; uitop" && {
    [ "$(grep -c '^APP board w=3 h=3$' <<<"$out")" = 1 ] && [ "$(grep -c '^APP board w=5 h=5$' <<<"$out")" = 1 ] && [ "$(p15_v "$(p15_st 2)" n)" = 5 ] && [ "$(p15_v "$(p15_st 3)" n)" = 4 ] && [ "$(tp)" = "tools_game_puzzle15 " ] \
        || { EVID="$(grep '^APP board w=' <<<"$out" | tr '\n' ' ') $(tp)"; fail p15_rules; }
    c3=$(grep '^APP game puzzle15 new rule=1 ' <<<"$out" | sed 's/.* cell=\([0-9]*\) .*/\1/'); c5=$(grep '^APP game puzzle15 new rule=2 ' <<<"$out" | sed 's/.* cell=\([0-9]*\) .*/\1/'); c4=$(grep '^APP game puzzle15 new rule=0 ' <<<"$out" | tail -1 | sed 's/.* cell=\([0-9]*\) .*/\1/')
    [ "$c3" -gt "$c4" ] && [ "$c4" -gt "$c5" ] && [ "$c5" -ge 28 ] || { EVID="cells 3x3=$c3 4x4=$c4 5x5=$c5"; fail p15_cell_sizes; }; }
nowrun p15_rule_best "$P15; games gold 0; games rule 1; games almost 50; board tap 8; @1; games rule 0; games almost 80; board tap 15; @1" && {
    grep -qx 'APP game puzzle15 end outcome=1 streak=0 best=51 new_best=1 sound=win' <<<"$out" && grep -qx 'APP game puzzle15 end outcome=1 streak=0 best=81 new_best=1 sound=win' <<<"$out" || { EVID="$(grep ' end ' <<<"$out")"; fail p15_rule_best; }; }
# styles: numbers, letters (A..), numbers again on the gradient; the dump keeps the tile's text
nowrun p15_styles "$P15; games almost 5; games style 1; board; games style 2; board" && {
    [ "$(grep '^APP board row 0 ' <<<"$out" | sed -n 1p)" = "APP board row 0 A B C D" ] && [ "$(grep '^APP board row 3 ' <<<"$out" | sed -n 1p)" = "APP board row 3 M N - O" ] && [ "$(grep '^APP board row 0 ' <<<"$out" | sed -n 2p)" = "APP board row 0 1 2 3 4" ] \
        || { EVID="$(grep '^APP board row [03] ' <<<"$out" | tr '\n' '|')"; fail p15_styles; }; }
# hints (mode 1): the tiles at home carry the highlight look; a slide off home loses it
nowrun p15_hints "$P15; games almost 5; games mode 1; board; board tap 10; board" && {
    l1=$(grep '^APP board look ' <<<"$out" | sed -n 1,4p | sed 's/^APP board look [0-9] //; s/ //g' | tr -d '\n'); l2=$(grep '^APP board look ' <<<"$out" | sed -n 5,8p | sed 's/^APP board look [0-9] //; s/ //g' | tr -d '\n')
    [ "${l1:14:2}" = dn ] && [ "${l1:0:14}" = "hhhhhhhhhhhhhh" ] && [ "${l2:10:1}" = d ] && [ "${l2:14:1}" = n ] || { EVID="$l1|$l2"; fail p15_hints; }; }
# set: a permutation is taken (a picture, a test), a repeated value is refused
nowrun p15_set "$P15; games set 1 2 3 4 5 6 7 8 9 10 11 12 13 14 0 15; games state; games set 1 1 2; games state" && {
    grep -qx 'APP game puzzle15 set refused' <<<"$out" && [ "$(p15_v "$(p15_st 1)" blank)" = 14 ] && [ "$(p15_v "$(p15_st 2)" blank)" = 14 ] || { EVID="$(p15_st 1)"; fail p15_set; }; }
# taps only: a knob turn is never a move (it is the volume here, a board with a knob); the board's key cursor is off (a build without a knob only)
nowrun p15_knob "screen on; games open puzzle15; @0.3; vol 0; @0.3; games almost 5; simknob 3; @0.2; games state; simvol" && {
    s=$(p15_st 1); [ "$(p15_v "$s" moves)" = 5 ] && [ "$(p15_v "$s" cursor)" = -1 ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$s vol $(vols)"; fail p15_knob_no_move; }; }
# a touch: the centre of a cell is that cell (a tile's slide), a touch beside the board does nothing
nowrun p15_touch "$P15; games almost 5; board at 10; board hit 2 2" && {
    a=$(grep '^APP board at 10 ' <<<"$out" | sed 's/.*x=\([0-9]*\),y=\([0-9]*\)$/\1 \2/'); [ -n "$a" ] && grep -qx 'APP board hit 2,2 = -1' <<<"$out" || { EVID="$a"; fail p15_hit; }
    nowrun p15_touch_slides "$P15; games almost 5; simtap $a; @0.4; games state; simtap 2 2; @0.2; games state" && {
        [ "$(p15_v "$(p15_st 1)" blank)" = 10 ] && [ "$(p15_v "$(p15_st 1)" moves)" = 6 ] && [ "$(p15_st 1)" = "$(p15_st 2)" ] || { EVID="$(p15_st 1)|$(p15_st 2)"; fail p15_touch_slides; }; }; }
# the game's own 說明
nowrun p15_own_help "screen on; games open puzzle15; @0.3; frame show; simtaprow 1; @0.3; popring 3; @0.3; help show; simflick up; @0.3; uitop" && {
    grep -qx 'APP explain id=game title=15 puzzle paras=1 qr=0 web=0' <<<"$out" && [ "$(tp)" = "tools_game_puzzle15 " ] || { EVID="$(grep '^APP explain' <<<"$out") $(tp)"; fail p15_own_help; }; }
# the ring while the rule changes: a new rule from the ring leaves the ring on top (the board is behind it) and the page alive
nowrun p15_ring_rule "$P15; frame show; simtaprow 1; @0.3; games rule 1; @0.3; uitop; popring -1; @0.3; uitop; games state" && {
    [ "$(p15_v "$(p15_st 1)" n)" = 3 ] && [ "$(tp | cut -d' ' -f1)" = "tools_game_puzzle15" ] || { EVID="$(tp)"; fail p15_ring_rule; }; }
# --- end 15 拼圖 (sliding puzzle) ---
