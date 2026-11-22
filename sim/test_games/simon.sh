# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G1: 四色記憶 / Simon (app/main/ui/scr_game_simon.c on the shared piece ui_sectors; rules in components/player_core/src/pc_game_simon.c); sourced by sim/test.sh ---
# The page: the HEAD row (who plays at 119,68: 單人 / 雙人 / 練習; the rule at 240,68 opens the game's ring: popring 0 規則, 1 風格, 2 重來, 3 說明), four sectors
# (0 top right, 1 bottom right, 2 bottom left, 3 top left) round the hub, the milestone ring outside them, the status line, Back.
# Console: `sectors` dumps the piece ("APP sectors n= ... en= lit=<mask> looks=<n|l|d,..> paint= hub= progress=<n>/<max>"), `sectors tap <i>` / `sectors hub` are touches;
# the page prints "APP game simon show len=<n> seq=<sectors>", "... input len=<n>", "... tap=<i> result=<0 ok|1 round|2 wrong> pos= len= done=", "... milestone=<n> sound=";
# `games right` / `games wrong` tap the sector the game wants now / another one (the game's own console words, game_ring_t.cmd).
# Timing (virtual time): the first colour 0.7 s after the start, each colour lit 0.42 s + 0.17 s pause (rounds 1..5), the next round 0.8 s after the last tap.
sm_w() { awk -v l="$1" 'BEGIN{printf "%.1f", 1.5 + 0.6 * l}'; }                                  # a wait that covers the show of a round of length $1
sm_play() { local s="" l; for l in $(seq "$1"); do s+="@$(sm_w "$l"); "; for _ in $(seq "$l"); do s+="games right; "; done; done; printf '%s' "$s"; }   # $1 rounds, every tap right
sm_dump() { grep '^APP sectors n=' <<<"$out" | sed -n "$(($1 + ${SM_SKIP:-1}))p"; }   # the $1-th dump after the one `sectors hub` prints
sm_last() { grep '^APP sectors n=' <<<"$out" | tail -1; }
sm_field() { sed 's/.* '"$1"'=\([^ ]*\).*/\1/' <<<"$2"; }

# open: idle (ready), the hub says Start, four normal sectors in the classic colours (paint 0..3), the ring empty, the HEAD row and the status line
nowrun simon_open "screen on; games open simon; @0.3; uitop; sectors; frame hold; uiboxes" && {
    d=$(sm_last)
    [ "$(tp)" = "tools_game_simon " ] && [ "$(sm_field n "$d")" = 4 ] && [ "$(sm_field hub "$d")" = Start ] && [ "$(sm_field looks "$d")" = n,n,n,n ] \
      && [ "$(sm_field paint "$d")" = 0,1,2,3 ] && [ "$(sm_field progress "$d")" = 0/12 ] && [ "$(sm_field en "$d")" = 1 ] && [ "$(sm_field keys "$d")" = 0 ] || { EVID="$(tp) $d"; fail simon_open; }
    grep -q '^APP uibox label .*|Tap to start$' <<<"$out" && grep -q '^APP uibox button .*|Solo$' <<<"$out" && grep -q '^APP uibox button .*|Classic$' <<<"$out" || { EVID="$(grep '^APP uibox' <<<"$out" | tr '\n' '|')"; fail simon_open_head; }; }
# the device plays: the first colour lit (with its tone) while the sectors take no touch; then the player's turn
nowrun simon_show "screen on; games open simon; @0.3; sectors hub; @0.9; sectors; sfx; sectors tap 0; @0.6; sectors" && {
    s=$(grep '^APP game simon show len=1 seq=' <<<"$out" | sed 's/.*seq=//'); d1=$(sm_dump 1); d2=$(sm_last)
    [ -n "$s" ] && [ "$(sm_field lit "$d1")" = $((1 << s)) ] && [ "$(sm_field en "$d1")" = 0 ] && [ "$(sm_field hub "$d1")" = 1 ] || { EVID="seq=$s $d1"; fail simon_show_lit; }
    grep -q "^APP sfx .*last=.*sector_$((s + 1))" <<<"$out" || { EVID="$(grep '^APP sfx ' <<<"$out")"; fail simon_show_tone; }
    ! grep -q '^APP game simon tap=' <<<"$out" || { EVID="$(grep 'simon tap=' <<<"$out")"; fail simon_show_ignores_taps; }
    [ "$(sm_field lit "$d2")" = 0 ] && [ "$(sm_field en "$d2")" = 1 ] && grep -qx 'APP game simon input len=1' <<<"$out" || { EVID="$d2"; fail simon_show_then_input; }; }
# five rounds right: the sequence only grows at its end; the fifth is a milestone (fanfare, one segment of the ring); the sixth round starts
nowrun simon_rounds "screen on; sfx; games open simon; @0.3; sectors hub; $(sm_play 5)@0.3; sectors; sfx" && {
    prev=; ok=1
    for l in 1 2 3 4 5 6; do s=$(grep "^APP game simon show len=$l seq=" <<<"$out" | sed 's/.*seq=//'); [ -n "$s" ] || ok=0; [ -z "$prev" ] || [ "${s%,*}" = "$prev" ] || ok=0; prev=$s; done
    [ $ok = 1 ] && [ "$(grep -c '^APP game simon tap=.* result=1 ' <<<"$out")" = 5 ] && ! grep -q 'result=2' <<<"$out" || { EVID="$(grep '^APP game simon show' <<<"$out" | tr '\n' '|')"; fail simon_rounds_grow; }
    grep -qx 'APP game simon milestone=5 sound=level_up' <<<"$out" && [ "$(sm_field progress "$(sm_last)")" = 1/12 ] && grep -q '^APP sfx .*level_up' <<<"$out" || { EVID="$(grep 'milestone' <<<"$out") $(sm_last)"; fail simon_milestone; }; }
# a wrong tap: the end, the right sector lit and the others dim, the result card (the length reached, a best of this session) after a moment, a tap on it = again
nowrun simon_wrong_ends "screen on; games open simon; @0.3; sectors hub; $(sm_play 2)@$(sm_w 3); games right; games wrong; sectors; @0.3; games result; @0.5; games result; games result tap; @0.2; sectors" && {
    w=$(grep '^APP game simon wrong=' <<<"$out" | sed 's/.*=//'); d=$(sm_dump 1)
    grep -qx 'APP game simon end outcome=0 streak=0 best=2 new_best=1 sound=win' <<<"$out" && [ "$(sm_field en "$d")" = 0 ] || { EVID="$(grep 'simon end' <<<"$out") $d"; fail simon_wrong_end; }
    lk=$(sm_field looks "$d"); [ "$(tr -cd n <<<"$lk")" = n ] && [ "$(tr -cd d <<<"$lk")" = ddd ] && [ "${lk:$((2 * w)):1}" = d ] || { EVID="wrong=$w $d"; fail simon_wrong_shows_answer; }
    grep -q '^APP game_result up=1 shown=0 ' <<<"$out" && grep -q '^APP game_result up=1 shown=1 title=Reached 2 moves=2 .* best=2/[0-9]* new_best=1 streak=0 gold=0$' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out" | tr '\n' '|')"; fail simon_card; }
    [ "$(grep -c '^APP game simon start ' <<<"$out")" = 2 ] && [ "$(sm_field looks "$(sm_last)")" = n,n,n,n ] || { EVID="$(sm_last)"; fail simon_card_again; }; }
# the best of the session: a shorter second game is not a best (the card shows the best, the sound is the loss's)
nowrun simon_best "screen on; games open simon; @0.3; sectors hub; $(sm_play 3)@$(sm_w 4); games wrong; @1.5; games result tap; $(sm_play 1)@$(sm_w 2); games wrong; @1.5; games result" && {
    grep -qx 'APP game simon end outcome=0 streak=0 best=3 new_best=1 sound=win' <<<"$out" && grep -qx 'APP game simon end outcome=0 streak=0 best=3 new_best=0 sound=lose' <<<"$out" \
      && grep -q '^APP game_result up=1 shown=1 title=Reached 1 moves=1 .* best=3/[0-9]* new_best=0 ' <<<"$out" || { EVID="$(grep 'simon end\|game_result up' <<<"$out" | tr '\n' '|')"; fail simon_best_session; }; }
# 倒序: the round is tapped backwards: the first tap the game wants is the last colour shown
nowrun simon_reverse "screen on; games open simon; @0.3; games rule 1; sectors hub; $(sm_play 3)@0.2; frame hold; uiboxes" && {
    s=$(grep '^APP game simon show len=3 seq=' <<<"$out" | sed 's/.*seq=//'); r=$(grep '^APP game simon right=' <<<"$out" | sed -n '4p' | sed 's/.*=//')
    [ -n "$s" ] && [ "$r" = "${s##*,}" ] && [ "$(grep -c 'result=1 ' <<<"$out")" = 3 ] || { EVID="seq=$s first=$r"; fail simon_reverse_order; }
    grep -q '^APP uibox button .*|Reverse$' <<<"$out" || fail simon_reverse_head; }
# 洗牌: after five rounds the colours move (every colour to another sector), the sixth round is shown where they are now
nowrun simon_shuffle "screen on; games open simon; @0.3; games rule 2; sectors hub; $(sm_play 5)@0.5; sectors; uiboxes" && {
    p=$(grep '^APP game simon moved place=' <<<"$out" | sed 's/.*place=//'); paint=$(sm_field paint "$(sm_last)")
    [ -n "$p" ] && [ "$p" != 0,1,2,3 ] && [ "$paint" != 0,1,2,3 ] && [ "$(tr ',' '\n' <<<"$paint" | sort | tr -d '\n')" = 0123 ] || { EVID="place=$p paint=$paint"; fail simon_shuffle_moved; }
    ok=1; IFS=, read -r -a pl <<<"$p"; for c in 0 1 2 3; do [ "${pl[$c]}" != "$c" ] || ok=0; done; [ $ok = 1 ] || { EVID="place=$p"; fail simon_shuffle_every_colour; }
    grep -q '^APP uibox label .*|The colours moved!$' <<<"$out" || { EVID="$(grep '^APP uibox label' <<<"$out")"; fail simon_shuffle_status; }; }
nowrun simon_shuffle_play "screen on; games open simon; @0.3; games rule 2; sectors hub; $(sm_play 7)@0.3; sectors" && {
    [ "$(grep -c 'result=1 ' <<<"$out")" = 7 ] && ! grep -q 'result=2' <<<"$out" || { EVID="$(grep 'result=' <<<"$out" | tail -2 | tr '\n' '|')"; fail simon_shuffle_plays_on; }; }
# 加速: each round faster: round 4's first colour is lit 0.354 s (classic 0.42 s): 0.38 s after it came on it is already off in 加速, still on in classic
nowrun simon_fast "screen on; games open simon; @0.3; games rule 3; sectors hub; $(sm_play 3)@1.18; sectors" && {
    grep -q '^APP game simon show len=4 ' <<<"$out" && [ "$(sm_field lit "$(sm_last)")" = 0 ] && [ "$(sm_field hub "$(sm_last)")" = 4 ] || { EVID="$(sm_last)"; fail simon_fast_shorter; }; }
nowrun simon_classic_long "screen on; games open simon; @0.3; sectors hub; $(sm_play 3)@1.18; sectors" && {
    [ "$(sm_field lit "$(sm_last)")" != 0 ] && [ "$(sm_field hub "$(sm_last)")" = 4 ] || { EVID="$(sm_last)"; fail simon_classic_longer; }; }
# 練習: a wrong tap shows the right one and plays the same round again; the game goes on
nowrun simon_practice "screen on; games open simon; @0.3; games mode 2; sectors hub; $(sm_play 1)@$(sm_w 2); games right; games wrong; @0.2; frame hold; uiboxes; frame hide; @$(sm_w 3); games right; games right; @0.3; games result" && {
    [ "$(grep -c '^APP game simon show len=2 ' <<<"$out")" = 2 ] && ! grep -q '^APP game simon end' <<<"$out" && grep -q '^APP game_result up=0$' <<<"$out" \
      && [ "$(grep -c 'result=1 ' <<<"$out")" = 2 ] && grep -q '^APP uibox label .*|Once more$' <<<"$out" || { EVID="$(grep '^APP game simon\|uibox label' <<<"$out" | tail -5 | tr '\n' '|')"; fail simon_practice; }; }
# 雙人: the players take the rounds in turn; the one who taps wrong loses
nowrun simon_two "screen on; games open simon; @0.3; frame hold; simtaprow 0; @0.1; sectors hub; $(sm_play 1)@$(sm_w 2); uiboxes; games wrong; @0.2; uiboxes" && {
    grep -q '^APP uibox label .*|Player 2: your turn$' <<<"$out" && grep -q '^APP uibox label .*|Player 1 wins!$' <<<"$out" && grep -q '^APP uibox button .*|2 players$' <<<"$out" \
      && grep -q '^APP game simon end outcome=1 ' <<<"$out" || { EVID="$(grep '^APP uibox label\|simon end' <<<"$out" | tr '\n' '|')"; fail simon_two_players; }; }
# the ring: 規則 has four values (a tap steps once, then the knob), the HEAD cell shows it, a rule makes a new (ready) game; 風格 keeps the game; 重來 starts at once
nowrun simon_ring "screen on; games open simon; @0.3; sectors hub; @1.4; frame show; simtaprow 1; @0.3; popring -5; popring 0; games sel; simknob 1; games sel; frame show; uiboxes; sectors; games rule 4; games sel" && {
    grep -q '^APP popring open=1 page=0 cells=4 ' <<<"$out" && grep -qx 'APP game_ring id=simon mode=0 rule=1 style=0 open=1' <<<"$out" && grep -qx 'APP game_ring id=simon mode=0 rule=2 style=0 open=1' <<<"$out" \
      && grep -qx 'APP game_ring id=simon mode=0 rule=0 style=0 open=1' <<<"$out" || { EVID="$(grep '^APP popring\|^APP game_ring' <<<"$out" | tr '\n' '|')"; fail simon_ring_rule; }
    [ "$(sm_field hub "$(sm_last)")" = Start ] && grep -q '^APP uibox button .*|Shuffle$' <<<"$out" || { EVID="$(sm_last)"; fail simon_ring_rule_new_game; }; }
nowrun simon_style_restart "screen on; games open simon; @0.3; sectors hub; $(sm_play 1)@0.3; games style 1; sectors; frame show; simtaprow 1; @0.3; popring 2; @0.2; sectors; games sel" && {
    [ "$(sm_field hub "$(sm_dump 1)")" = 2 ] && grep -qx 'APP game_ring id=simon mode=0 rule=0 style=1 open=0' <<<"$out" || { EVID="$(sm_dump 1)"; fail simon_style_keeps_game; }
    [ "$(grep -c '^APP game simon start ' <<<"$out")" = 2 ] && [ "$(sm_field hub "$(sm_last)")" = 1 ] || { EVID="$(sm_last)"; fail simon_restart; }; }
# the golden game: rolled by 重來 (forced here), gold on the card; the next game is plain again
nowrun simon_gold "screen on; games gold 1; games open simon; @0.3; frame show; simtaprow 1; @0.2; popring 2; @$(sm_w 1); games wrong; @1.5; games result; games gold auto; games rule 1" && {
    grep -q '^APP game simon new rule=0 style=0 mode=0 gold=1$' <<<"$out" && grep -q '^APP game_result up=1 shown=1 .* gold=1$' <<<"$out" && grep -qx 'APP game simon new rule=1 style=0 mode=0 gold=0' <<<"$out" \
      || { EVID="$(grep 'simon new\|game_result up' <<<"$out" | tr '\n' '|')"; fail simon_gold; }; }
# touch: the piece's own hit test (nearest sector by angle, the hub, nothing far away); a real touch on a sector's middle is a tap of that sector
nowrun simon_touch "screen on; games open simon; @0.3; sectors at 0; sectors at 1; sectors at 2; sectors at 3; sectors hit 180 193; sectors hit 2 2; sectors hit 230 140; sectors hit 170 120; sectors hit 355 193" && {
    for i in 0 1 2 3; do grep -q "^APP sectors at $i x=[0-9]*,y=[0-9]*$" <<<"$out" || fail "simon_at_$i"; done
    grep -qx 'APP sectors hit 180,193 = -2' <<<"$out" && grep -qx 'APP sectors hit 2,2 = -1' <<<"$out" && grep -qx 'APP sectors hit 230,140 = 0' <<<"$out" && grep -qx 'APP sectors hit 170,120 = 3' <<<"$out" && grep -qx 'APP sectors hit 355,193 = -1' <<<"$out" \
      || { EVID="$(grep '^APP sectors hit' <<<"$out" | tr '\n' '|')"; fail simon_hit_test; }
    a=$(grep '^APP sectors at 0 ' <<<"$out" | sed 's/.*x=\([0-9]*\),y=\([0-9]*\)$/\1 \2/')
    nowrun simon_touch_taps "screen on; games open simon; @0.3; simtap 180 193; @$(sm_w 1); simtap $a; @0.1; sectors" && {
        s=$(grep '^APP game simon show len=1 seq=' <<<"$out" | sed 's/.*seq=//')
        grep -q '^APP game simon start ' <<<"$out" && grep -q "^APP game simon tap=0 result=$([ "$s" = 0 ] && echo 1 || echo 2) " <<<"$out" || { EVID="seq=$s $(grep 'simon tap=' <<<"$out")"; fail simon_touch_taps; }; }; }
# haptics: a tap each (the hub, every sector touch), a strong one at the end
SIM_TRACE=1 nowrun simon_haptic "screen on; games open simon; @0.3; sectors hub; $(sm_play 2)@$(sm_w 3); games wrong" && {
    h=$(grep -c '^SIM haptic [0-9]' <<<"$out"); l=$(grep '^SIM haptic [0-9]' <<<"$out" | tail -1)
    [ "$h" = 6 ] && [ "$(grep -c '^SIM haptic 4$' <<<"$out")" = 5 ] && [ "$l" = "SIM haptic 1" ] || { EVID="n=$h last=$l $(grep '^SIM haptic' <<<"$out" | tr '\n' ' ')"; fail simon_haptic; }; }
# keys (the FakePod's two keys): the cursor moves with +1, -1 presses its sector (here: starts the game); with key mode off the knob is not taken
nowrun simon_keys "screen on; games open simon; @0.3; sectors key 1; sectors keys 1; sectors key 1; sectors key 1; sectors key -1; @0.1; sectors" && {
    grep -qx 'APP sectors key 1 took=0' <<<"$out" && [ "$(sm_field cursor "$(SM_SKIP=0 sm_dump 3)")" = 1 ] && [ "$(sm_field cursor "$(SM_SKIP=0 sm_dump 4)")" = 2 ] && grep -q '^APP game simon start ' <<<"$out" \
      || { EVID="$(grep '^APP sectors' <<<"$out" | tr '\n' '|')"; fail simon_keys; }; }
# the game's own 說明 (the last cell of its ring), Back to the game; leaving while the device plays: no leak, back on the hub
nowrun simon_own_help "screen on; games open simon; @0.3; frame show; simtaprow 1; @0.3; popring 3; @0.3; help show; uistack; simflick up; @0.3; uitop" && {
    grep -qx 'APP explain id=game title=Simon paras=1 qr=0 web=0' <<<"$out" && stack_is simon_own_help_stack 1 "menu tools tools_games tools_game_simon explain" && [ "$(tp)" = "tools_game_simon " ] \
      || { EVID="$(grep '^APP explain' <<<"$out") $(tp)"; fail simon_own_help; }; }
nowrun simon_back_mid_show "screen on; games open simon; @0.3; sectors hub; @0.9; exit pause; @0.2; exit tap back; @1; uitop; sectors" && {
    [ "$(tp)" = "tools_games " ] && grep -qx 'APP sectors none' <<<"$out" || { EVID="$(tp)"; fail simon_back_mid_show; }; }
# the colours come from the game's own stream: the same first sequence whatever another game did before
nowrun simon_alone "screen on; games open simon; @0.3; sectors hub; $(sm_play 3)" && A=$(grep '^APP game simon show len=3 ' <<<"$out")
nowrun simon_after_ttt "screen on; games open ttt; @0.3; board tap 4; @1; simflick up; @0.3; games open simon; @0.3; sectors hub; $(sm_play 3)" && {
    [ -n "$A" ] && [ "$(grep '^APP game simon show len=3 ' <<<"$out")" = "$A" ] || { EVID="alone $A"; fail simon_independent_of_other_games; }; }
# ---- fix round 1 ----
# the hub gives a haptic tap only when it does something (start; again after the end), never during the device's or the player's turn
SIM_TRACE=1 nowrun simon_hub_haptic "screen on; games open simon; @0.3; sectors hub; @0.9; sectors hub; @$(sm_w 1); sectors hub" && {
    [ "$(grep -c '^SIM haptic [0-9]' <<<"$out")" = 1 ] && [ "$(grep -c '^APP game simon start ' <<<"$out")" = 1 ] || { EVID="$(grep '^SIM haptic\|simon start' <<<"$out" | tr '\n' ' ')"; fail simon_hub_no_false_haptic; }; }
# after the end: a fast tap right after the wrong one does nothing (the end is seen); once the card is up a tap on the field or the hub = again, like the card
nowrun simon_field_again "screen on; games open simon; @0.3; sectors hub; @$(sm_w 1); games wrong; sectors tap 0; @0.2; sectors tap 1; @0.5; sectors; sectors tap 2; @0.2; sectors; @$(sm_w 1); games wrong; @0.7; sectors hub" && {
    [ "$(grep -c '^APP game simon start ' <<<"$out")" = 3 ] && [ "$(sm_field en "$(sm_dump 3)")" = 1 ] && [ "$(sm_field looks "$(sm_dump 4)")" = n,n,n,n ] \
      || { EVID="$(grep '^APP game simon start\|^APP sectors n=' <<<"$out" | tr '\n' '|')"; fail simon_field_tap_again; }; }
# 雙人 keeps no best (a two-player length is not 單人's record) and its end is a win for somebody: the sound is win
nowrun simon_two_score "screen on; games open simon; @0.3; games mode 1; sectors hub; $(sm_play 2)@$(sm_w 3); games wrong; @1; games result; games mode 0; sectors hub; @$(sm_w 1); games wrong; @1; games result" && {
    grep -qx 'APP game simon end outcome=1 streak=0 best=0 new_best=0 sound=win' <<<"$out" && grep -q '^APP game_result up=1 shown=1 title=Player 2 wins! moves=2 .* best=0/0 new_best=0 ' <<<"$out" \
      && grep -qx 'APP game simon end outcome=0 streak=0 best=0 new_best=0 sound=lose' <<<"$out" || { EVID="$(grep 'simon end\|game_result up' <<<"$out" | tr '\n' '|')"; fail simon_two_no_best; }; }
# the seeds: the simulator pins the boot value to 0; `games count <n>` makes the game's next seed n (a picture independent of what ran before)
nowrun simon_count "screen on; games boot; games open simon; @0.3; games count 5; sectors hub; @0.3; simflick up; @0.3; games open simon; @0.3; games rule 1; games rule 0; games count 5; sectors hub" && {
    a=$(grep '^APP game simon show len=1 ' <<<"$out" | sed -n 1p); b=$(grep '^APP game simon show len=1 ' <<<"$out" | sed -n 2p)
    grep -qx 'APP games boot=0' <<<"$out" && [ -n "$a" ] && [ "$a" = "$b" ] || { EVID="$a | $b"; fail simon_count_seed; }; }
# the piece is registered (shared-area doc, lint table)
# --- end 四色記憶 (Simon) ---
