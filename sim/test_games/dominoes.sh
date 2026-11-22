# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G4: 骨牌 on the shared piles ui_cards (app/main/ui/scr_game_dominoes.c, ui_cards.c; rules in components/player_core/src/pc_game_dominoes.c); sourced by sim/test.sh ---
# `games dom deal <n>` deals game n of the page's own seed (deal 1: the device holds the opening double 6|6 ... start tile 27, you hold 7 tiles 0,3,18,5,19,13,27 as faces a*7+b,
# the last two playable), `games dom hold` stops the device's timer and dots, `games dom dev` makes the device (or an automatic draw / pass) step now, `games dom auto <n>` plays both
# sides by the device's rules until the chain has n tiles and it is your turn, `games dom gold <tile|-1>` the golden double, `cards tap <pile> <idx>` a touch on a card
# (piles 0 the device's hand, 1 the chain, 2 your hand), `cards` the piles. "APP game dominoes state= turn= rule= chain= yard= hand=<you>,<device> left= right= score= winner= gold_tile= plays= passes= ends= think=".
dm_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
dm_g() { grep '^APP game dominoes state=' <<<"$out" | sed -n "$1p"; }       # a dump: $DM makes three (deal, gold, hold)
dm_c() { grep '^APP cards set=' <<<"$out" | sed -n "$1p"; }
dm_p() { grep "^APP cards pile $2 " <<<"$out" | sed -n "$1p"; }       # the n-th dump's pile
DM="screen on; games open dominoes; @0.3; games mode 0; games style 0; games rule 0; games dom deal 1; games dom gold -1; games dom hold"
# open: the three piles, your turn, the opening tile on the chain
nowrun dm_open "$DM; cards; uitop" && {
    [ "$(tp)" = "tools_game_dominoes " ] && [ "$(dm_v "$(dm_c 1)" sel)" = -1:-1 ] && [ "$(dm_v "$(dm_c 1)" en)" = 1 ] && [ "$(dm_v "$(dm_p 1 0)" n)" = 6 ] && [ "$(dm_v "$(dm_p 1 1)" n)" = 1 ] \
        && [ "$(dm_v "$(dm_p 1 2)" n)" = 7 ] && [ "$(dm_v "$(dm_p 1 1)" faces)" = 48 ] && [ "$(dm_v "$(dm_p 1 2)" flags)" = 0,0,0,0,0,4,4 ] || { EVID="$(tp) $(dm_c 1) | $(dm_p 1 0) | $(dm_p 1 1) | $(dm_p 1 2)"; fail dm_open; }; }
# a tile that fits is selected (lifted, the chain's ends ringed), the same tile again deselects; a tile that does not fit is selected without rings (each `cards tap` dumps too)
nowrun dm_select "$DM; cards tap 2 6; cards tap 2 6; cards tap 2 0" && {
    [ "$(dm_v "$(dm_c 1)" sel)" = 2:6 ] && [ "$(dm_v "$(dm_p 1 1)" ends)" = 3 ] && [ "$(dm_v "$(dm_p 1 2)" flags)" = 0,0,0,0,0,4,6 ] || { EVID="$(dm_c 1) | $(dm_p 1 1) | $(dm_p 1 2)"; fail dm_select; }
    [ "$(dm_v "$(dm_c 2)" sel)" = -1:-1 ] && [ "$(dm_v "$(dm_p 2 1)" ends)" = 0 ] || { EVID="$(dm_c 2) | $(dm_p 2 1)"; fail dm_deselect; }
    [ "$(dm_v "$(dm_c 3)" sel)" = 2:0 ] && [ "$(dm_v "$(dm_p 3 1)" ends)" = 0 ] || { EVID="$(dm_c 3) | $(dm_p 3 1)"; fail dm_select_no_fit; }; }
# the move: tap a tile, then the chain: it lands on the end (3|6 left of 6|6), the turn goes to the device (its dots, no taps), the device then plays or draws
# (the game's own dumps: the three of $DM, then one per `games dom`)
nowrun dm_move "$DM; cards tap 2 6; cards tap 1 0; games dom; cards tap 2 0; games dom dev; cards" && {
    g=$(dm_g 4); [ "$(dm_v "$g" chain)" = 2 ] && [ "$(dm_v "$g" turn)" = 1 ] && [ "$(dm_v "$g" hand)" = 6,6 ] && [ "$(dm_v "$g" left)" = 3 ] && [ "$(dm_v "$g" plays)" = 1,1 ] || { EVID="$g"; fail dm_move; }
    [ "$(dm_v "$(dm_p 2 1)" faces)" = 27,48 ] && [ "$(dm_v "$(dm_c 2)" think)" = 1 ] && [ "$(dm_v "$(dm_c 2)" en)" = 0 ] || { EVID="$(dm_c 2) | $(dm_p 2 1)"; fail dm_move_view; }
    [ "$(dm_v "$(dm_c 3)" sel)" = -1:-1 ] || { EVID="$(dm_c 3)"; fail dm_disabled_taps; }          # the device's turn: a tap on a card selects nothing
    g=$(dm_g 5); [ "$(dm_v "$g" turn)" = 0 ] && { [ "$(dm_v "$g" chain)" = 3 ] || [ "$(dm_v "$g" yard)" -lt 14 ]; } || { EVID="$g"; fail dm_device_steps; }
    [ "$(dm_v "$(dm_c 4)" think)" = 0 ] || { EVID="$(dm_c 4)"; fail dm_think_off; }; }
# a tile that does not fit the ends cannot be placed (the selection stays), and the chain's tiles are the same size at every length (the snake keeps its tile size)
nowrun dm_refuse "$DM; cards tap 2 0; cards tap 1 0; games dom; games dom auto 14; cards" && {
    [ "$(dm_v "$(dm_g 4)" chain)" = 1 ] && [ "$(dm_v "$(dm_c 2)" sel)" = 2:0 ] || { EVID="$(dm_g 4) | $(dm_c 2)"; fail dm_refuse; }
    [ "$(dm_v "$(dm_p 2 1)" card)" = "$(dm_v "$(dm_p 3 1)" card)" ] && [ "$(dm_v "$(dm_p 3 1)" n)" -ge 14 ] || { EVID="$(dm_p 2 1) | $(dm_p 3 1)"; fail dm_snake_size; }; }
# the hit test: a tap on a card takes that card, a tap in the chain's zone the nearest tile, outside every zone nothing
nowrun dm_hit "$DM; cards at 2 3; cards at 1 0; cards hit 5 5" && {
    h=$(grep '^APP cards at 2 3' <<<"$out"); x=${h#*x=}; x=${x%,*}; y=${h#*y=}
    h1=$(grep '^APP cards at 1 0' <<<"$out"); cx=${h1#*x=}; cx=${cx%,*}; cy=${h1#*y=}
    [ -n "$x" ] && [ -n "$cx" ] && [ "$(grep -c '^APP cards hit -1 -1' <<<"$out")" = 1 ] || { EVID="$h $h1 $(grep '^APP cards hit' <<<"$out")"; fail dm_hit_outside; }
    echo "$x $y $cx $cy" > "$T/dm_xy"; }
read -r dmx dmy dmcx dmcy < "$T/dm_xy"
nowrun dm_hit_cards "$DM; cards hit $dmx $dmy; cards hit $((dmcx + 60)) $dmcy; cards hit $dmcx $((dmcy + 4))" && {
    [ "$(grep '^APP cards hit' <<<"$out" | sed -n 1p)" = "APP cards hit 2 3" ] && [ "$(grep '^APP cards hit' <<<"$out" | sed -n 2p)" = "APP cards hit 1 0" ] \
        && [ "$(grep '^APP cards hit' <<<"$out" | sed -n 3p)" = "APP cards hit 1 0" ] || { EVID="$(grep '^APP cards hit' <<<"$out" | tr '\n' '|')"; fail dm_hit_cards; }; }
# taps only: a turn of the knob never selects or plays a tile (it is the volume); the page's key console says it took nothing on a board with a knob
nowrun dm_knob "$DM; vol 0; @0.3; simknob 1; @0.3; cards; simvol; games dom key 1; games dom key -1; cards" && {
    [ "$(dm_v "$(dm_c 1)" sel)" = -1:-1 ] && [ "$(vols | cut -d' ' -f1)" != 0 ] && [ "$(grep -c '^APP game dominoes key .* took=0' <<<"$out")" = 2 ] && [ "$(dm_v "$(dm_c 2)" sel)" = -1:-1 ] || { EVID="$(dm_c 1) vol $(vols) $(grep 'dominoes key' <<<"$out")"; fail dm_knob; }; }
# the rules: 阻塞賽 keeps the boneyard, 五點 scores (the status line shows the score); every rule can be dealt and played through to the end by the device's own rules
nowrun dm_rules "screen on; games open dominoes; @0.3; games rule 1; games dom auto 30; games dom; games rule 2; games dom auto 30; games dom; games rule 0; games dom auto 30; games dom" && {
    for k in 1 2 3; do g=$(dm_g $((k + 3))); [ -n "$g" ] || true; done
    gl=$(grep '^APP game dominoes state=' <<<"$out")
    [ "$(grep -c ' rule=1 ' <<<"$gl")" -ge 1 ] && [ "$(grep -c ' rule=2 ' <<<"$gl")" -ge 1 ] && [ "$(grep -c ' state=1 ' <<<"$gl")" -ge 3 ] || { EVID="$(sed 's/ gold_tile.*//' <<<"$gl" | tr '\n' '|')"; fail dm_rules; }; }
# the end: the card after its delay with the outcome line, a tap on the card = a new deal
nowrun dm_end "$DM; games dom auto 40; games dom; games result; @1; games result; uiboxes; games result tap; games dom" && {
    grep -q '^APP game dominoes end outcome=[012] streak=[0-9]* best=[0-9]* new_best=[01] sound=' <<<"$out" || { EVID="$(grep ' end ' <<<"$out")"; fail dm_end_line; }
    grep -q '^APP game_result up=1 shown=1 ' <<<"$out" || { EVID="$(grep '^APP game_result' <<<"$out" | tr '\n' '|')"; fail dm_card; }
    last=$(grep '^APP game dominoes state=' <<<"$out" | tail -1); [ "$(dm_v "$last" state)" = 0 ] && [ "$(dm_v "$last" chain)" = 1 ] || { EVID="$last"; fail dm_card_again; }; }
# the golden double is gold on the chain and in the hand
nowrun dm_gold_tile "$DM; games dom gold 27; cards; games dom gold 24; cards" && {
    [ "$(dm_v "$(dm_p 1 1)" flags)" = 10 ] && [ "$(dm_v "$(dm_p 2 2)" flags)" = 0,0,0,0,0,4,14 ] || { EVID="$(dm_p 1 1) | $(dm_p 2 2)"; fail dm_gold_tile; }; }
nowrun dm_back_mid "screen on; games open dominoes; @0.3; exit pause; @0.2; exit tap back; @0.3; uitop; cards" && {
    [ "$(tp)" = "tools_games " ] && grep -qx 'APP cards none' <<<"$out" || { EVID="$(tp) $(grep '^APP cards' <<<"$out")"; fail dm_back; }; }
nowrun dm_own_help "screen on; games open dominoes; @0.3; frame show; simtaprow 1; @0.3; popring 3; @0.3; help show; simflick up; @0.3; uitop" && {
    grep -qx 'APP explain id=game title=Dominoes paras=1 qr=0 web=0' <<<"$out" && [ "$(tp)" = "tools_game_dominoes " ] || { EVID="$(grep '^APP explain' <<<"$out") $(tp)"; fail dm_own_help; }; }
nowrun dm_gold_game "screen on; games open dominoes; @0.3; games gold 1; frame show; simtaprow 1; @0.3; popring 2; @0.3; games dom hold; games dom; games gold auto" && {
    grep -q '^APP game dominoes new rule=0 style=0 mode=0 gold=1 ' <<<"$out" || { EVID="$(grep 'dominoes new' <<<"$out")"; fail dm_gold_game; }; }
# --- end 骨牌 (dominoes) ---
