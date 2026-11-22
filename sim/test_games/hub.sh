# SPDX-License-Identifier: LGPL-2.1-or-later
# --- games G0: the 遊戲 hub and the registry (app/main/ui/games.def); sourced by sim/test.sh ---
# The expectations are derived from games.def, so adding a game changes no number here.
GAME_IDS=$(grep -o '^GAME([A-Za-z0-9_]*' app/main/ui/games.def | sed 's/^GAME(//' | paste -sd, -)
GAME_N=$(grep -c '^GAME(' app/main/ui/games.def)
python3 tools/check/check_games.py >"$T/cg.out" 2>&1 || { EVID="$(tail -3 "$T/cg.out" | tr '\n' '|')"; fail games_check_registry; }
python3 tools/check/check_games.py --selftest >"$T/cg2.out" 2>&1 || { EVID="$(tail -3 "$T/cg2.out" | tr '\n' '|')"; fail games_check_selftest; }
nowrun games_hub_open "screen on; tools games; @0.3; ring; uitop; uistack; games list" && {
    [ "$(tp)" = "tools_games " ] && stack_is games_hub_stack 1 "menu tools tools_games" || { EVID="$(tp) $(us)"; fail games_hub_open; }
    N_HUB=$((GAME_N + 1)); HUB_PAGES=$(( (N_HUB + 7) / 8 ))   # UI_RING_PER_PAGE 7: one page up to 7 cells, else 6 cells a page and the next-page cell (G5: 13 cells = 3 pages)
    [ "$(rg)" = "page=0/$HUB_PAGES sel=-1 center=Pick one,/tap the centre|" ] && grep -q "^APP ring page=0/$HUB_PAGES .* n=$((GAME_N + 1))$" <<<"$out" || { EVID="$(grep '^APP ring' <<<"$out")"; fail games_hub_ring_cells; }   # the games and 說明 last
    [ "$(grep '^APP games n=' <<<"$out")" = "APP games n=$GAME_N ids=$GAME_IDS" ] || { EVID="$(grep '^APP games n=' <<<"$out")"; fail games_hub_list; }; }
# a touch path: the 小工具 ring's last cell (page 2) opens the hub (bubble style: the cell positions are the bub() ones; a first tap selects, the second runs)
nowrun games_tools_cell "screen on; vol 0; style next; @0.5; tools; ring; simknob 1; ring; simtap $(bub 1); ring; simtap $(bub 1); @0.3; uitop; uistack" && {
    [ "$(tp)" = "tools_games " ] && stack_is games_tools_cell_stack 1 "menu tools tools_games" || { EVID="$(rg) $(tp)"; fail games_tools_cell; }; }
# the hub's last cell is 說明 (the hub only: how a game page works; each game has its own 說明 button), Back leaves it for the hub
nowrun games_hub_help "screen on; games help; @0.3; help show; uistack; simflick up; @0.3; uitop" && {
    grep -qx "APP explain id=games title=Games paras=2 qr=0 web=0" <<<"$out" && stack_is games_hub_help_stack 1 "menu tools tools_games explain" && [ "$(tp)" = "tools_games " ] || { EVID="$(grep '^APP explain' <<<"$out") $(us)"; fail games_hub_help; }; }
# every listed game opens (console and the ring's cell), is named tools_game_<id>, and Back returns to the hub; its play piece is up (a ui_board, or the ui_arena of G1)
for gid in ${GAME_IDS//,/ }; do
    nowrun "games_open_$gid" "screen on; games open $gid; @0.3; uitop; uistack; board; sectors; arena; dial; cards; exit pause; @0.2; exit tap back; @0.3; uitop" && {   # G1: a game's field is a board, sectors or an arena
        [ "$(tp)" = "tools_game_$gid tools_games " ] && stack_is "games_open_${gid}_stack" 1 "menu tools tools_games tools_game_$gid" && grep -q '^APP board w=[0-9]* h=[0-9]*$\|^APP sectors n=[0-9]* \|^APP arena name=[a-z0-9_]* \|^APP dial name=[a-z0-9_]* \|^APP cards set=[a-z]* ' <<<"$out" || { EVID="$(tp) $(us)"; fail "games_open_$gid"; }; }
done
nowrun games_unknown_id "screen on; games open nope; games; @0.3; uitop" && { grep -qx 'APP games unknown nope' <<<"$out" && [ "$(tp)" = "tools_games " ] || { EVID="$(grep '^APP games' <<<"$out" | tr '\n' '|')"; fail games_unknown_id; }; }
# a game keeps the music playing: the audio plan does not change and nothing stops
nowrun games_music_plays "screen on; vol 0; track 3; @2; audio info; games open ttt; @0.3; board tap 4; @1; board tap 8; @1; audio info; back; @0.3; audio info" && {
    a=$(grep '^APP audio info ' <<<"$out" | sed 's/ underruns_src.*//' | sort -u | wc -l)
    [ "$(grep -c '^APP audio info ' <<<"$out")" = 3 ] && [ "$a" = 1 ] && ! grep -q '^APP mp3 stop\|^APP play stop' <<<"$out" && [ "$(grep -c '^APP track idx=' <<<"$out")" = 1 ] || { EVID="$(grep '^APP audio info' <<<"$out" | cut -c1-80 | tr '\n' '|')"; fail games_music_plays; }; }
# the shared piece and the pages: registered in the shared-area doc and the lint table; the games are pages with allowance 0
# --- end games hub ---
