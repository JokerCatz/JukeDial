# SPDX-License-Identifier: LGPL-2.1-or-later
# --- games G0: 井字棋 (app/main/ui/scr_game_ttt.c, rules and the computer in components/player_core/src/pc_game_ttt.c); sourced by sim/test.sh ---
# The page: the HEAD row has two cells, the mode button (a tap at 119,68 steps it: You: X -> You: O -> 2 players -> ...) and the rule's name (240,68), which opens the game's
# ring (G0b, `popring <i>`: 0 規則, 1 風格, 2 重來, 3 說明 last), the board (cells 0..8 row by row), the status line (title slot).
# Console: `board` dumps "APP board row <r> <cells>" and "APP board look <r> <looks>" (n normal, a X, b O, h highlight), `board tap <i>` is a touch on cell i (then the dump),
# the page prints "APP game ttt move=<cell> turn=<next> moves=<n> winner=<0|1|2>" after every stone.
TT2="frame hold; simtaprow 0; simtaprow 0; frame hide; @0.2"                       # two players: two steps of the mode button
tt_flat() { grep '^APP board row ' <<<"$out" | tail -3 | sed 's/^APP board row [0-9] //; s/ //g' | tr -d '\n'; }      # the last dump: nine characters, - empty
tt_looks() { grep '^APP board look ' <<<"$out" | tail -3 | sed 's/^APP board look [0-9] //; s/ //g' | tr -d '\n'; }
tt_flat_n() { grep '^APP board row ' <<<"$out" | sed -n "$(($1 * 3 - 2)),$(($1 * 3))p" | sed 's/^APP board row [0-9] //; s/ //g' | tr -d '\n'; }   # the n-th dump
tt_looks_n() { grep '^APP board look ' <<<"$out" | sed -n "$(($1 * 3 - 2)),$(($1 * 3))p" | sed 's/^APP board look [0-9] //; s/ //g' | tr -d '\n'; }
tt_n() { grep -c '^APP board w=' <<<"$out"; }
tt_label() { grep '^APP uibox label ' <<<"$out" | sed -n "$1p" | sed 's/.*|//'; }                  # the $1-th status line
tt_but() { grep '^APP uibox button ' <<<"$out" | grep -v '|Back$\||Leave$\||Close$\||$\||Help$\||Classic$\||Vanish$\||Blind$' | sed -n "$1p" | sed 's/.*|//'; }  # the $1-th mode button text (the rule cell left out)
nowrun ttt_two_player_win "screen on; games open ttt; @0.3; $TT2; board tap 0; board tap 3; board tap 1; board tap 4; board tap 2; uiboxes" && {
    [ "$(tt_flat)" = "XXXOO----" ] && [ "$(tt_looks)" = "hhhbbnnnn" ] && grep -qx 'APP game ttt move=2 turn=2 moves=5 winner=1' <<<"$out" || { EVID="$(tt_flat) $(tt_looks)"; fail ttt_two_player_win; }
    [ "$(tt_label 1)" = "X wins!" ] || { EVID="$(tt_label 1)"; fail ttt_win_status; }; }
# a taken cell is not a move; a draw is shown; a tap after the end starts again (cleared board, X to move)
nowrun ttt_two_player_draw "screen on; games open ttt; @0.3; $TT2; board tap 0; board tap 1; board tap 2; board tap 4; board tap 3; board tap 5; board tap 7; board tap 6; board tap 0; board tap 8; uiboxes; board tap 4; uiboxes" && {
    [ "$(tt_flat_n 10)" = "XOXXOOOXX" ] && grep -qx 'APP game ttt move=8 turn=2 moves=9 winner=0' <<<"$out" || { EVID="$(tt_flat_n 10) $(tt_flat)"; fail ttt_two_player_draw; }
    [ "$(tt_flat_n 9)" = "XOXXOOOX-" ] || { EVID="$(tt_flat_n 9)"; fail ttt_taken_cell_ignored; }   # the second tap on cell 0 changed nothing (dump 9 is that tap's)
    [ "$(tt_label 1)" = "Draw!" ] && [ "$(tt_label 2)" = "X to move" ] && [ "$(tt_flat)" = "---------" ] || { EVID="$(tt_label 1)|$(tt_label 2) $(tt_flat)"; fail ttt_draw_then_restart; }; }
# against the device (X, you move first): the answer comes a little later (a timer, not inside the tap), and it is the perfect one: a corner against the centre
nowrun ttt_device_answers_corner "screen on; games open ttt; @0.3; board tap 4; @0.5; board" && {
    [ "$(tt_flat_n 1)" = "----X----" ] || { EVID="$(tt_flat_n 1)"; fail ttt_device_not_instant; }
    f=$(tt_flat_n 2); [ "${f:4:1}" = X ] && [ "$(tr -cd O <<<"$f" | wc -c)" = 1 ] && { [ "${f:0:1}" = O ] || [ "${f:2:1}" = O ] || [ "${f:6:1}" = O ] || [ "${f:8:1}" = O ]; } || { EVID="$f"; fail ttt_device_answers_corner; }; }
# while the device thinks, a tap on the board does nothing
nowrun ttt_tap_while_thinking "screen on; games open ttt; @0.3; board tap 4; board tap 0; board tap 1; @0.5; board; uiboxes" && {
    f=$(tt_flat); [ "$(tr -cd X <<<"$f" | wc -c)" = 1 ] && [ "$(tr -cd O <<<"$f" | wc -c)" = 1 ] && [ "${f:4:1}" = X ] || { EVID="$f"; fail ttt_tap_while_thinking; }
    [ "$(tt_label 1)" = "Your move (X)" ] || { EVID="$(tt_label 1)"; fail ttt_status_your_move; }; }
# the device first (you are O): one X appears without a tap, and your tap is an O
nowrun ttt_device_first "screen on; games open ttt; @0.3; frame show; simtaprow 0; @0.5; board; uiboxes" && {
    f=$(tt_flat); [ "$(tr -cd X <<<"$f" | wc -c)" = 1 ] && [ "$(tr -cd O <<<"$f" | wc -c)" = 0 ] && [ "$(tt_label 1)" = "Your move (O)" ] && [ "$(tt_but 1)" = "You: O" ] || { EVID="$f $(tt_label 1) $(tt_but 1)"; fail ttt_device_first; }; }
# the device never loses: nine cells tapped over and over in order, win or not, the game restarts after its end; no game ends with winner=1 (X, the human)
taps=$(for r in 1 2 3; do for c in 0 1 2 3 4 5 6 7 8; do printf 'board tap %d; @0.3; ' $c; done; done)
nowrun ttt_device_never_loses "screen on; games open ttt; @0.3; ${taps}board" && {
    ! grep -q ' winner=1$' <<<"$out" && grep -q '^APP game ttt move=' <<<"$out" || { EVID="$(grep ' winner=[12]$' <<<"$out" | head -3 | tr '\n' '|')"; fail ttt_device_never_loses; }; }
# the mode button: tap steps, the knob steps while it holds the knob, three modes wrap
nowrun ttt_mode_button "screen on; games open ttt; @0.3; frame hold; uiboxes; simtaprow 0; uiboxes; simtaprow 0; uiboxes; simtaprow 0; uiboxes; simknob 1; uiboxes; simknob -2; uiboxes" && {
    w="You: X|You: O|2 players|You: X|You: O|2 players|"; g=$(for i in 1 2 3 4 5 6; do printf '%s|' "$(tt_but $i)"; done)
    [ "$g" = "$w" ] || { EVID="$g"; fail ttt_mode_button; }; }
# a real touch: a touch on a cell's centre moves, a touch on a taken cell and one far from the board do nothing; the piece's own hit test (board hit) agrees
nowrun ttt_touch "screen on; games open ttt; @0.3; board at 4; board at 0; board hit 2 2; board hit 357 357; board hit 180 180" && {
    a4=$(grep '^APP board at 4 ' <<<"$out" | sed 's/.*x=\([0-9]*\),y=\([0-9]*\)$/\1 \2/'); a0=$(grep '^APP board at 0 ' <<<"$out" | sed 's/.*x=\([0-9]*\),y=\([0-9]*\)$/\1 \2/')
    [ -n "$a4" ] && [ -n "$a0" ] && grep -qx 'APP board hit 2,2 = -1' <<<"$out" && grep -qx 'APP board hit 357,357 = -1' <<<"$out" && grep -qx 'APP board hit 180,180 = 4' <<<"$out" || { EVID="$a4|$a0 $(grep '^APP board hit' <<<"$out" | tr '\n' '|')"; fail ttt_hit_test; }
    nowrun ttt_touch_moves "screen on; games open ttt; @0.3; simtap $a4; @0.5; board; simtap $a4; @0.5; board; simtap 2 2; simtap 357 357; @0.5; board" && {
        [ "$(tt_flat_n 1)" = "$(tt_flat_n 2)" ] && [ "$(tt_flat_n 1)" = "$(tt_flat_n 3)" ] && [ "$(tt_flat_n 1 | cut -c5)" = X ] && [ "$(tr -cd 'XO' <<<"$(tt_flat_n 1)" | wc -c)" = 2 ] || { EVID="$(tt_flat_n 1)|$(tt_flat_n 2)|$(tt_flat_n 3)"; fail ttt_touch_moves; }; }; }
# haptic on every move (a click), a strong one for the win: two mode taps, the device's first move (the first tap made it start, X) and the five moves
SIM_TRACE=1 nowrun ttt_haptic "screen on; games open ttt; @0.3; $TT2; board tap 0; board tap 3; board tap 1; board tap 4; board tap 2" && {
    h=$(grep -c '^SIM haptic [0-9]' <<<"$out"); l=$(grep '^SIM haptic [0-9]' <<<"$out" | tail -1)
    [ "$h" = 8 ] && [ "$l" = "SIM haptic 1" ] || { EVID="clicks=$h last=$l"; fail ttt_haptic; }; }
# the game's own 說明 (the last cell of the game's ring, opened by the rule cell next to the mode button): this game's rules only, Back returns to the game with its state
nowrun ttt_own_help "screen on; games open ttt; @0.3; board tap 4; @0.5; frame show; simtaprow 1; @0.3; popring 3; @0.3; help show; uistack; simflick up; @0.3; uitop; board" && {
    grep -qx 'APP explain id=game title=Tic-tac-toe paras=1 qr=0 web=0' <<<"$out" && stack_is ttt_own_help_stack 1 "menu tools tools_games tools_game_ttt explain" && [ "$(tp)" = "tools_game_ttt " ] || { EVID="$(grep '^APP explain' <<<"$out") $(tp)"; fail ttt_own_help; }
    f=$(tt_flat); [ "$(tr -cd 'XO' <<<"$f" | wc -c)" = 2 ] || { EVID="$f"; fail ttt_help_keeps_game; }; }
# a game's choices come from its OWN random stream (seeded by its id and its own game count), not from the shared one: what another game does first (it draws too) changes nothing here.
# By construction: scr_game_*.c seed with games_seed("<id>", n) and never call ui_rng; the test: tic-tac-toe after a connect-four game == tic-tac-toe alone
nowrun ttt_alone "screen on; games open ttt; @0.3; board tap 4; @0.5; board" && A=$(tt_flat)
nowrun ttt_after_c4 "screen on; games open c4; @0.3; board tap 3; @1.5; board tap 2; @1.5; simflick up; @0.3; games open ttt; @0.3; board tap 4; @0.5; board" && {
    [ -n "$A" ] && [ "$(tt_flat)" = "$A" ] || { EVID="alone $A after $(tt_flat)"; fail ttt_independent_of_other_games; }; }
! grep -l 'ui_rng' app/main/ui/scr_game_*.c >/dev/null 2>&1 || fail games_pages_use_ui_rng
# leaving while the device thinks (the timer ends with the page): no leak, no crash (nowrun checks), back on the hub
nowrun ttt_back_while_thinking "screen on; games open ttt; @0.3; board tap 4; exit pause; @0.2; exit tap back; @0.6; uitop" && { [ "$(tp)" = "tools_games " ] || { EVID="$(tp)"; fail ttt_back_while_thinking; }; }

# ---- G0b: the game's ring, the rule variants, the styles, the effects, the result card ----
# the ring: 規則 / 風格 / 重來 / 說明 last; 規則 takes the knob (three rules: a tap steps once, then the knob), the HEAD cell shows the rule; a rule starts a new game
nowrun ttt_ring "screen on; games open ttt; @0.3; board tap 4; @0.5; frame show; simtaprow 1; @0.3; popring -5; popring 0; games sel; board; simknob 1; games sel; frame show; uiboxes" && {
    grep -q '^APP popring open=1 page=0 cells=4 ' <<<"$out" && grep -qx 'APP game_ring id=ttt mode=0 rule=1 style=1 open=1' <<<"$out" && grep -qx 'APP game_ring id=ttt mode=0 rule=2 style=1 open=1' <<<"$out" || { EVID="$(grep '^APP popring\|^APP game_ring' <<<"$out" | tr '\n' '|')"; fail ttt_ring_rule_knob; }
    [ "$(tt_flat)" = "---------" ] && grep -q '^APP uibox button .*|Blind$' <<<"$out" && grep -q '^APP uibox label .*|Rules/Blind$' <<<"$out" || { EVID="$(tt_flat) $(grep 'Blind' <<<"$out" | tr '\n' '|')"; fail ttt_ring_rule_new_game; }; }
# 風格 changes the look, not the game: the marks stay; 重來 empties the board and closes the ring
nowrun ttt_ring_style_restart "screen on; games open ttt; @0.3; board tap 4; @0.5; frame show; simtaprow 1; @0.3; popring 1; games sel; board; popring 2; @0.2; popring -5; board" && {
    grep -qx 'APP game_ring id=ttt mode=0 rule=0 style=1 open=1' <<<"$out" && [ "$(tr -cd XO <<<"$(tt_flat_n 2)" | wc -c)" = 2 ] || { EVID="$(grep '^APP game_ring' <<<"$out") $(tt_flat_n 2)"; fail ttt_ring_style; }
    [ "$(tt_flat)" = "---------" ] && grep -q '^APP popring open=0 ' <<<"$out" || { EVID="$(tt_flat) $(grep '^APP popring' <<<"$out")"; fail ttt_ring_restart; }; }
# 消失版: each side keeps three; the mark that goes next is dim (d); the fourth placement removes the oldest first (gone=), it fades with sparks; no line through the removed mark
nowrun ttt_vanish "screen on; games open ttt; @0.3; games mode 2; games rule 1; board tap 0; board tap 8; board tap 1; board tap 7; board tap 5; board tap 3; board; games fx hold 1; board tap 2; games fx; board" && {
    [ "$(tt_flat_n 7)" = "XX-O-X-OO" ] && [ "$(tt_looks_n 7)" = "danbnanbd" ] || { EVID="$(tt_flat_n 7) $(tt_looks_n 7)"; fail ttt_vanish_dim; }
    grep -qx 'APP game ttt move=2 turn=2 moves=7 winner=0 gone=0' <<<"$out" && [ "$(tt_flat)" = "-XXO-X-OO" ] && [ "$(tt_looks)" = "ndabnanbd" ] || { EVID="$(tt_flat) $(tt_looks) $(grep 'move=2 ' <<<"$out")"; fail ttt_vanish_gone; }
    grep -q '^APP game_fx e[0-9] kind=fade frame=0/9 cells=0$' <<<"$out" && grep -q '^APP game_fx e[0-9] kind=burst frame=0/12 cells=0$' <<<"$out" || { EVID="$(grep '^APP game_fx' <<<"$out" | tr '\n' '|')"; fail ttt_vanish_fx; }; }
# 消失版 against the device: it plays legal moves under the rule (past nine moves, marks removed), its search is sliced
taps=$(for r in 1 2 3; do for c in 0 1 2 3 4 5 6 7 8; do printf 'board tap %d; @0.4; ' $c; done; done)
nowrun ttt_vanish_device "screen on; games open ttt; @0.3; games rule 1; ${taps}board" && {
    n=$(grep -c '^APP game ttt move=' <<<"$out"); [ "$n" -ge 10 ] && grep -q ' gone=[0-9]$' <<<"$out" && grep -q '^APP game ttt ai slices=' <<<"$out" || { EVID="moves=$n"; fail ttt_vanish_device; }; }
# 盲棋: a mark shows for 2 s, then the board shows the cell empty (it fades); a tap on it is refused (a tick, a red pulse, "Taken"), not a move; the end shows every mark again
SIM_TRACE=1 nowrun ttt_blind "screen on; games open ttt; @0.3; games mode 2; games rule 2; board tap 4; board; @2.5; board; games fx hold 1; board tap 4; games fx; uiboxes; board tap 0; board tap 1; board tap 3; board tap 7; board" && {
    [ "$(tt_flat_n 2)" = "----X----" ] && [ "$(tt_flat_n 3)" = "---------" ] || { EVID="$(tt_flat_n 2)|$(tt_flat_n 3)"; fail ttt_blind_hides; }
    grep -qx 'APP game ttt refused=4' <<<"$out" && grep -q '^APP game_fx e[0-9] kind=warn ' <<<"$out" && [ "$(tt_label 1)" = "Taken" ] && [ "$(tt_flat_n 4)" = "---------" ] || { EVID="$(grep 'refused\|kind=warn' <<<"$out" | tr '\n' '|') $(tt_label 1) $(tt_flat_n 4)"; fail ttt_blind_refused; }
    [ "$(tt_flat)" = "OX-OX--X-" ] && [ "$(tt_looks)" = "bhnbhnnhn" ] && grep -q 'move=7 turn=2 moves=5 winner=1$' <<<"$out" || { EVID="$(tt_flat) $(tt_looks)"; fail ttt_blind_end_shows_all; }; }
# effects: deterministic steps (games fx hold / step): the win line grows over 9 frames and stays, the glow goes after 12; the result card comes after its delay; tap = again
nowrun ttt_fx_and_card "screen on; games open ttt; @0.3; games mode 2; games fx hold 1; board tap 0; board tap 3; board tap 1; board tap 4; board tap 2; games fx; games fx step 3; games fx step 20; games result; @0.7; games result; games result tap; board; games fx" && {
    grep -q '^APP game_fx e[0-9] kind=line frame=0/9 cells=0,1,2$' <<<"$out" && grep -q '^APP game_fx e[0-9] kind=glow frame=3/12 cells=0,1,2$' <<<"$out" || { EVID="$(grep '^APP game_fx e' <<<"$out" | tr '\n' '|')"; fail ttt_fx_steps; }
    [ "$(grep '^APP game_fx n=' <<<"$out" | sed -n 4p | cut -d' ' -f3)" = "n=1" ] && grep -q '^APP game_fx e0 kind=line frame=9/9 cells=0,1,2$' <<<"$out" || { EVID="$(grep '^APP game_fx' <<<"$out" | tr '\n' '|')"; fail ttt_fx_line_stays; }
    grep -q '^APP game_result up=1 shown=0 title=X wins! moves=5 ' <<<"$out" && grep -q '^APP game_result up=1 shown=1 title=X wins! moves=5 .* new_best=1 streak=0 gold=0$' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out" | tr '\n' '|')"; fail ttt_result_card; }
    [ "$(tt_flat)" = "---------" ] && [ "$(grep '^APP game_fx n=' <<<"$out" | tail -1 | cut -d' ' -f3)" = "n=0" ] && grep -qx 'APP game_result up=0' <<<"$out" || { EVID="$(tt_flat) $(grep '^APP game_fx n=' <<<"$out" | tail -1)"; fail ttt_result_tap_again; }
    grep -qx 'APP game ttt end outcome=1 streak=0 best=5 new_best=1 sound=win' <<<"$out" || { EVID="$(grep ' end ' <<<"$out")"; fail ttt_end_sound; }; }
# Back while an effect is mid-flight: the effect, its timer and the card go with the page (no leak, no crash: nowrun checks)
nowrun ttt_back_mid_effect "screen on; games open ttt; @0.3; games mode 2; board tap 0; board tap 3; board tap 1; board tap 4; board tap 2; @0.1; exit pause; @0.2; exit tap back; @0.3; uitop; games fx; games result" && {
    [ "$(tp)" = "tools_games " ] && grep -qx 'APP game_fx none' <<<"$out" && grep -qx 'APP game_result up=0' <<<"$out" || { EVID="$(tp) $(grep '^APP game_' <<<"$out" | tr '\n' '|')"; fail ttt_back_mid_effect; }; }
# the screen off: no effect animates, they are at their end at once
nowrun ttt_fx_screen_off "screen on; games open ttt; @0.3; games mode 2; board tap 0; board tap 3; board tap 1; board tap 4; screen off; board tap 2; @0.1; games fx; screen on" && {
    grep -q '^APP game_fx n=1 .* run=0 ' <<<"$out" && grep -q '^APP game_fx e0 kind=line frame=9/9 ' <<<"$out" || { EVID="$(grep '^APP game_fx' <<<"$out" | tr '\n' '|')"; fail ttt_fx_screen_off; }; }
# the golden game (forced for the test; one in 30 by the game's own seed otherwise): rolled only by 重來 or a new game after an end (fix round 1 M4: opening the
# page, a rule, a style or who plays keep it), the card says so; the style table's gold copy is on the board
nowrun ttt_gold_not_on_open "screen on; games gold 1; games open ttt; @0.3; games rule 1; games style 1; games mode 2" && {
    ! grep -q '^APP game ttt new .* gold=1$' <<<"$out" || { EVID="$(grep '^APP game ttt new' <<<"$out" | tr '\n' '|')"; fail ttt_gold_not_on_open; }; }
nowrun ttt_gold "screen on; games gold 1; games open ttt; @0.3; games mode 2; frame show; simtaprow 1; @0.2; popring 2; @0.2; board tap 0; board tap 3; board tap 1; board tap 4; board tap 2; @1; games result" && {
    grep -qx 'APP game ttt new rule=0 style=1 mode=2 gold=0' <<<"$out" && grep -qx 'APP game ttt new rule=0 style=1 mode=2 gold=1' <<<"$out" && grep -q ' gold=1$' <<<"$(grep '^APP game_result up=1' <<<"$out")" || { EVID="$(grep 'gold' <<<"$out" | tr '\n' '|')"; fail ttt_gold; }; }

# fix round 2 N1: a golden game that ended does not carry over: a rule change after it starts a plain game (no roll)
nowrun ttt_gold_cleared "screen on; games gold 1; games open ttt; @0.3; games mode 2; frame show; simtaprow 1; @0.2; popring 2; @0.2; $(for c in 0 3 1 4 2; do printf 'board tap %d; ' $c; done)@1; games result; games gold auto; games rule 1" && {
    grep -q '^APP game_result up=1 shown=1 .* gold=1$' <<<"$out" && [ "$(grep '^APP game ttt new ' <<<"$out" | tail -1)" = "APP game ttt new rule=1 style=1 mode=2 gold=0" ] || { EVID="$(grep '^APP game ttt new\|^APP game_result' <<<"$out" | tr '\n' '|')"; fail ttt_gold_cleared; }; }
# fix round 1 I1: a frame of the think dots redraws only their strip (one area, under a tenth of the board), never the whole board
nowrun ttt_fx_think_strip "screen on; games open ttt; @0.3; games fx hold 1; board tap 4; games fx step 1" && {
    l=$(grep '^APP game_fx n=0 think=1 ' <<<"$out" | tail -1); n=$(sed 's/.* inv_n=\([0-9]*\).*/\1/' <<<"$l"); px=$(sed 's/.* inv_px=\([0-9]*\).*/\1/' <<<"$l"); b=$(sed 's/.* board_px=\([0-9]*\).*/\1/' <<<"$l")
    [ -n "$l" ] && [ "$n" = 1 ] && [ $((px * 10)) -lt "$b" ] || { EVID="$l"; fail ttt_fx_think_strip; }; }
# fix round 1 M5: 盲棋's 100 ms check exists only in 盲棋
nowrun ttt_blind_timer_only_blind "screen on; games open ttt; @0.3; simtimers; games rule 2; simtimers; games rule 0; simtimers" && {
    a=$(grep '^SIM timers ' <<<"$out" | sed -n 1p | sed 's/.*periods=//' | tr ',' '\n' | grep -c '^100$')
    b=$(grep '^SIM timers ' <<<"$out" | sed -n 2p | sed 's/.*periods=//' | tr ',' '\n' | grep -c '^100$'); c=$(grep '^SIM timers ' <<<"$out" | sed -n 3p | sed 's/.*periods=//' | tr ',' '\n' | grep -c '^100$')
    [ $((b - a)) = 1 ] && [ "$c" = "$a" ] || { EVID="100 ms timers $a $b $c"; fail ttt_blind_timer_only_blind; }; }

# --- end 井字棋 (tic-tac-toe) ---
