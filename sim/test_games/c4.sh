# SPDX-License-Identifier: LGPL-2.1-or-later
# --- games G0: 四子棋 (app/main/ui/scr_game_c4.c, rules and the computer in components/player_core/src/pc_game_c4.c); sourced by sim/test.sh ---
# The HEAD row has the mode button (119,68: You 1st -> You 2nd -> 2 players) and the rule's name (240,68), which opens the game's ring (G0b, `popring <i>`: 0 規則, 1 風格, 2 重來, 3 說明). The board is 7 columns x 6 rows, cell = row * 7 + col with row 0 on top; a tap on ANY cell drops a disc into its column (`board tap <i>`: i % 7 is the column).
# The dump's look characters say whose disc a cell holds (a first player, b second, h part of the winning four, n empty); `board` text is "-" for every cell (discs have no text).
# The page prints "APP game c4 move=<cell> col=<col> turn=<next> moves=<n> winner=<0|1|2>" after every disc and "APP game c4 ai slices= max_us= nodes= nodes_max=" when the computer has searched.
C2="frame hold; simtaprow 0; simtaprow 0; frame hide; @0.2"                          # two players: two steps of the mode button
c4_looks() { grep '^APP board look ' <<<"$out" | tail -6 | sed 's/^APP board look [0-9] //; s/ //g' | tr -d '\n'; }          # the last dump: 42 characters
c4_looks_n() { grep '^APP board look ' <<<"$out" | sed -n "$(($1 * 6 - 5)),$(($1 * 6))p" | sed 's/^APP board look [0-9] //; s/ //g' | tr -d '\n'; }
c4_discs() { tr -cd 'ab' <<<"$1" | wc -c; }
c4_label() { grep '^APP uibox label ' <<<"$out" | sed -n "$1p" | sed 's/.*|//'; }
c4_taps() { local s= c; for c in "$@"; do s="$s board tap $c;"; done; printf '%s' "$s"; }      # one tap per column (cell index = the column, row 0)
# gravity: a disc falls to the lowest free cell of its column; whichever cell of the column is tapped
nowrun c4_gravity "screen on; games open c4; @0.3; $C2; board tap 3; board tap 24; board tap 17; board tap 6" && {
    l=$(c4_looks); [ "${l:38:1}" = a ] && [ "${l:31:1}" = b ] && [ "${l:24:1}" = a ] && [ "${l:41:1}" = b ] && [ "$(c4_discs "$l")" = 4 ] || { EVID="$l"; fail c4_gravity; }
    grep -qx 'APP game c4 move=38 col=3 turn=2 moves=1 winner=0' <<<"$out" && grep -qx 'APP game c4 move=31 col=3 turn=1 moves=2 winner=0' <<<"$out" || { EVID="$(grep '^APP game c4 move' <<<"$out" | head -3 | tr '\n' '|')"; fail c4_gravity_moves; }; }
# a full column refuses: six discs go in, the seventh tap is no move (the player to move does not change)
nowrun c4_column_full "screen on; games open c4; @0.3; $C2; $(c4_taps 0 0 0 0 0 0 0)" && {
    [ "$(grep -c '^APP game c4 move=[0-9]* col=0 ' <<<"$out")" = 6 ] && [ "$(c4_discs "$(c4_looks)")" = 6 ] || { EVID="$(grep -c '^APP game c4 move=[0-9]* col=0 ' <<<"$out")"; fail c4_column_full; }; }   # (the first mode tap let the device move once, in column 3, before the second tap made it two players: not counted)
# a tap on a full column is not a move, but not a dead tap: a tick and a note (the status line), no extra disc, and the state is as before
SIM_TRACE=1 nowrun c4_column_full_feedback "screen on; games open c4; @0.3; $C2; $(c4_taps 0 0 0 0 0 0) board; uiboxes" && h6=$(grep -c '^SIM haptic [0-9]' <<<"$out")
SIM_TRACE=1 nowrun c4_column_full_tick "screen on; games open c4; @0.3; $C2; $(c4_taps 0 0 0 0 0 0 0) board; frame hold; uiboxes" && {
    h7=$(grep -c '^SIM haptic [0-9]' <<<"$out")
    [ "$h7" = "$((h6 + 1))" ] && [ "$(c4_label 1)" = "Column full" ] && [ "$(c4_discs "$(c4_looks)")" = 6 ] || { EVID="haptics $h6 -> $h7, label $(c4_label 1)"; fail c4_column_full_tick; }; }
# the game's own 說明
nowrun c4_own_help "screen on; games open c4; @0.3; frame show; simtaprow 1; @0.3; popring 3; @0.3; help show; simflick up; @0.3; uitop" && {
    grep -qx 'APP explain id=game title=Connect four paras=1 qr=0 web=0' <<<"$out" && [ "$(tp)" = "tools_game_c4 " ] || { EVID="$(grep '^APP explain' <<<"$out") $(tp)"; fail c4_own_help; }; }
# a vertical win: the first player stacks column 0, the second column 1; the four are highlighted and the status says who won
nowrun c4_vertical_win "screen on; games open c4; @0.3; $C2; $(c4_taps 0 1 0 1 0 1 0) uiboxes" && {
    l=$(c4_looks); [ "${l:14:1}${l:21:1}${l:28:1}${l:35:1}" = hhhh ] && [ "${l:36:1}" = b ] && [ "$(tr -cd h <<<"$l" | wc -c)" = 4 ] || { EVID="$l"; fail c4_vertical_win; }
    grep -qx 'APP game c4 move=14 col=0 turn=2 moves=7 winner=1' <<<"$out" && [ "$(c4_label 1)" = "1st wins!" ] || { EVID="$(grep '^APP game c4 move=14' <<<"$out") $(c4_label 1)"; fail c4_vertical_win_status; }; }
# a rising diagonal win for the first player (bottom left to top right): columns 0 1 1 2 2 3 2 3 3 6 3
nowrun c4_diagonal_win "screen on; games open c4; @0.3; $C2; $(c4_taps 0 1 1 2 2 3 2 3 3 6 3)" && {
    l=$(c4_looks); [ "${l:35:1}${l:29:1}${l:23:1}${l:17:1}" = hhhh ] && grep -qx 'APP game c4 move=17 col=3 turn=2 moves=11 winner=1' <<<"$out" || { EVID="$l $(grep '^APP game c4 move=17' <<<"$out")"; fail c4_diagonal_win; }; }
# a horizontal win for the second player: columns 0 1 0 2 6 3 6 4 (the second: 1 2 3 4 on the bottom row)
nowrun c4_horizontal_win "screen on; games open c4; @0.3; $C2; $(c4_taps 0 1 0 2 6 3 6 4)" && {
    l=$(c4_looks); [ "${l:36:4}" = "hhhh" ] && grep -q ' winner=2$' <<<"$out" || { EVID="$l"; fail c4_horizontal_win; }; }
# after the end a tap starts again: an empty board, the first player to move
nowrun c4_restart "screen on; games open c4; @0.3; $C2; $(c4_taps 0 1 0 1 0 1 0) board tap 20; board; uiboxes" && {
    [ "$(c4_looks)" = "$(printf 'n%.0s' $(seq 1 42))" ] && [ "$(c4_label 1)" = "1st to move" ] || { EVID="$(c4_looks) $(c4_label 1)"; fail c4_restart; }; }
# against the device: it answers after the tap, not inside it (a timer, one slice a tick), in the same column or next to it (a few plies of minimax); each search stays small
nowrun c4_device_answers "screen on; games open c4; @0.3; board tap 3; @1.5; board; frame hold; uiboxes" && {
    [ "$(c4_discs "$(c4_looks_n 1)")" = 1 ] && [ "$(c4_discs "$(c4_looks_n 2)")" = 2 ] || { EVID="$(c4_discs "$(c4_looks_n 1)") $(c4_discs "$(c4_looks_n 2)")"; fail c4_device_answers; }
    ai=$(grep '^APP game c4 ai slices=' <<<"$out" | head -1); nm=$(sed 's/.*nodes_max=\([0-9]*\).*/\1/' <<<"$ai")
    [ -n "$ai" ] && [ "$nm" -gt 0 ] && [ "$nm" -le 2000 ] && [ "$(c4_label 1)" = "Your move" ] || { EVID="$ai $(c4_label 1)"; fail c4_ai_sliced; }; }
# the device first (you are second): a disc appears by itself, in the middle column
nowrun c4_device_first "screen on; games open c4; @0.3; frame show; simtaprow 0; @1.5; board; uiboxes" && {
    l=$(c4_looks); [ "$(c4_discs "$l")" = 1 ] && [ "${l:38:1}" = a ] && [ "$(c4_label 1)" = "Your move" ] || { EVID="$l $(c4_label 1)"; fail c4_device_first; }; }
# leaving while the computer thinks: the timer goes with the page
nowrun c4_back_while_thinking "screen on; games open c4; @0.3; board tap 3; exit pause; @0.2; exit tap back; @1; uitop" && { [ "$(tp)" = "tools_games " ] || { EVID="$(tp)"; fail c4_back_while_thinking; }; }
# a touch: the centre of a cell drops into its column, a touch beside the board does nothing
nowrun c4_touch "screen on; games open c4; @0.3; $C2; board at 10; board hit 2 2; board hit 357 357" && {
    a=$(grep '^APP board at 10 ' <<<"$out" | sed 's/.*x=\([0-9]*\),y=\([0-9]*\)$/\1 \2/'); [ -n "$a" ] && grep -qx 'APP board hit 2,2 = -1' <<<"$out" || { EVID="$a"; fail c4_hit; }
    nowrun c4_touch_drops "screen on; games open c4; @0.3; $C2; simtap $a; @0.2; board; simtap 2 2; simtap 357 357; @0.2; board" && {
        l=$(c4_looks_n 1); [ "${l:38:1}" = a ] && [ "$(c4_discs "$l")" = 1 ] && [ "$l" = "$(c4_looks_n 2)" ] || { EVID="$l|$(c4_looks_n 2)"; fail c4_touch_drops; }; }; }
# ---- G0b: the rule variants, the effects, the streak ----
# 環形: the bottom row 5 6 0 1 is a line across the edge; the four are highlighted, the line is drawn in their order along the line (broken at the edge when drawn)
nowrun c4_wrap_win "screen on; games open c4; @0.3; games mode 2; games rule 1; games fx hold 1; $(c4_taps 5 3 6 3 0 3 1) games fx" && {
    l=$(c4_looks); [ "${l:40:2}${l:35:2}" = hhhh ] && grep -qx 'APP game c4 move=36 col=1 turn=2 moves=7 winner=1' <<<"$out" || { EVID="$l"; fail c4_wrap_win; }
    grep -q '^APP game_fx e[0-9] kind=line frame=0/9 cells=40,41,35,36$' <<<"$out" && grep -q '^APP game_fx e[0-9] kind=glow frame=0/12 cells=40,41,35,36$' <<<"$out" || { EVID="$(grep '^APP game_fx e' <<<"$out" | tail -4 | tr '\n' '|')"; fail c4_wrap_fx; }; }
# the same discs in classic: no line
nowrun c4_wrap_classic "screen on; games open c4; @0.3; games mode 2; $(c4_taps 5 3 6 3 0 3 1)" && { ! grep -q ' winner=1$' <<<"$out" || fail c4_wrap_classic; }
# 彈出: a tap on the bottom cell of a column whose bottom disc is the mover's pops it (the column falls one cell); with the other side's disc there, the tap drops;
# every disc of the popped column falls one cell (an effect each) and sparks fly where the popped one was
nowrun c4_pop "screen on; games open c4; @0.3; games mode 2; games rule 2; board tap 3; board tap 3; games fx hold 1; board tap 38; games fx; board tap 38; board tap 38; board tap 38" && {
    grep -qx 'APP game c4 move=38 col=3 turn=2 moves=3 winner=0 pop=1' <<<"$out" && [ "$(c4_looks_n 3 | cut -c32,39)" = "nb" ] || { EVID="$(grep 'move=' <<<"$out" | tr '\n' '|') $(c4_looks_n 3)"; fail c4_pop_own_bottom; }
    grep -q '^APP game_fx e[0-9] kind=burst frame=0/12 cells=38$' <<<"$out" && grep -q '^APP game_fx e[0-9] kind=drop frame=0/7 cells=38$' <<<"$out" || { EVID="$(grep '^APP game_fx e' <<<"$out" | tr '\n' '|')"; fail c4_pop_fx; }
    grep -qx 'APP game c4 move=38 col=3 turn=1 moves=4 winner=0 pop=1' <<<"$out" && grep -qx 'APP game c4 move=38 col=3 turn=2 moves=5 winner=0' <<<"$out" && grep -qx 'APP game c4 move=31 col=3 turn=1 moves=6 winner=0' <<<"$out" || { EVID="$(grep 'move=' <<<"$out" | tr '\n' '|')"; fail c4_pop_then_drop; }; }
# the variants against the device: it answers each first move with a legal disc
for r in 1 2; do
    nowrun "c4_device_rule$r" "screen on; games open c4; @0.3; games rule $r; board tap 3; @1.5; board" && {
        [ "$(c4_discs "$(c4_looks)")" = 2 ] && grep -q '^APP game c4 ai slices=' <<<"$out" || { EVID="$(c4_looks)"; fail "c4_device_rule$r"; }; }
done
# the streak: three wins in a row against the device (a line of play that beats it, found offline against its seeds; the same for all three games):
# the third win plays the streak sound and the card shows the badge
c4w=$(for c in 3 4 4 3 3 3 5 1 0 2 0 1 1 6 0 2 2; do printf 'board tap %d; @0.6; ' $c; done)
nowrun c4_streak "screen on; games open c4; @0.3; $c4w @1; board tap 0; @0.3; $c4w @1; board tap 0; @0.3; $c4w @1; games result" && {
    [ "$(grep -c '^APP game c4 end outcome=1 ' <<<"$out")" = 3 ] && grep -q '^APP game c4 end outcome=1 streak=2 .* sound=win$' <<<"$out" && grep -q '^APP game c4 end outcome=1 streak=3 .* sound=streak$' <<<"$out" || { EVID="$(grep ' end ' <<<"$out" | tr '\n' '|')"; fail c4_streak; }
    grep -q '^APP game_result up=1 shown=1 title=You win! moves=33 .* streak=3 gold=0$' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out")"; fail c4_streak_card; }; }

# fix round 1 M1: a pop while the column's drops are still in flight: one effect per cell (the old drops end, none is left on the emptied cell)
nowrun c4_pop_mid_drop "screen on; games open c4; @0.3; games mode 2; games rule 2; games fx hold 1; board tap 3; board tap 3; board tap 38; games fx; games fx step 20; board" && {
    [ "$(grep -c '^APP game_fx e[0-9] kind=drop .*cells=38$' <<<"$out")" = 1 ] && ! grep -q '^APP game_fx e[0-9] kind=drop .*cells=31$' <<<"$out" || { EVID="$(grep '^APP game_fx e' <<<"$out" | tr '\n' '|')"; fail c4_pop_mid_drop; }
    [ "$(c4_looks | cut -c32,39)" = "nb" ] || { EVID="$(c4_looks)"; fail c4_pop_mid_drop_board; }; }
# fix round 1 M8: a full column the mover could pop says so
nowrun c4_full_pop_note "screen on; games open c4; @0.3; games mode 2; games rule 2; $(c4_taps 0 0 0 0 0 0) board tap 7; frame hold; uiboxes" && {
    [ "$(c4_label 1)" = "Full: tap its bottom to pop" ] || { EVID="$(c4_label 1)"; fail c4_full_pop_note; }; }

# --- end 四子棋 (connect four) ---
