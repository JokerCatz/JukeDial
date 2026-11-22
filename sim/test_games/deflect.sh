# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G6: 旋轉擋板 on the shared field ui_arena (app/main/ui/scr_game_deflect.c; rules and geometry in components/player_core/src/pc_game_deflect.c); sourced by sim/test.sh ---
# `arena tap <x> <y>` a touch at that point from the field's centre (the first starts, then x < 0 turns the shield -15 degrees, x >= 0 +15), `arena step <n>` n fixed steps now, `arena hold 1` stops
# the field's own steps. `arena game shot <i> <x> <y> <vx> <vy> [gold|back]`, `game deg <d>`, `game kill`, `game spawn 0|1`, `game lives <n>`, `game combo <n>`, `game shape 0|1` set a case up.
# Every `arena` command but `at` ends with both dumps ("APP arena ..." then "APP game deflect state=<0 wait|1 play|2 over> level= lives= score= combo= ... deg= half= shields= shape= ...").
df_g() { grep '^APP game deflect state=' <<<"$out" | sed -n "${1}p"; }
df_a() { grep '^APP arena name=' <<<"$out" | sed -n "${1}p"; }
df_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
DF="screen on; games open deflect; @0.3; games mode 0; games style 0; games rule 0"
DFS="$DF; arena hold 1; arena game spawn 0; arena tap 0 0"      # started, held, no shots of its own
nowrun df_open "$DF; arena; uiboxes; uitop" && {
    a=$(df_a 1); g=$(df_g 1)
    [ "$(df_v "$a" objs)" = 2/21 ] && [ "$(df_v "$a" knob)" = 0 ] && [ "$(df_v "$g" state)" = 0 ] && [ "$(df_v "$g" deg)" = 270 ] && [ "$(df_v "$g" lives)" = 3 ] \
        && [ "$(df_v "$g" shape)" = 0 ] && [ "$(tp)" = "tools_game_deflect " ] || { EVID="$a | $g | $(tp)"; fail df_open; }
    ! grep -q '^APP uibox label .*|Tap to start$' <<<"$out" || { EVID="$(grep '^APP uibox label' <<<"$out" | head -1)"; fail df_open_status; }; }
# the first tap starts and does not turn; then the left half turns -15, the right half +15
nowrun df_taps "$DF; arena hold 1; arena game spawn 0; arena tap 0 0; arena; arena tap -50 0; arena; arena tap 50 0; arena tap 50 0; arena" && {
    [ "$(df_v "$(df_g 3)" state)" = 1 ] && [ "$(df_v "$(df_g 3)" deg)" = 270 ] && [ "$(df_v "$(df_g 5)" deg)" = 255 ] && [ "$(df_v "$(df_g 8)" deg)" = 285 ] || { EVID="$(df_g 3) | $(df_g 5) | $(df_g 8)"; fail df_taps; }; }
# the knob is the volume until a tap lends it; then a detent = 15 degrees both ways
nowrun df_knob "$DF; vol 0; @0.3; simknob 1; @0.3; simvol; frame hide; arena; simtap 180 193; @0.1; arena; simknob 2; @0.1; arena; simknob -3; @0.1; arena; simvol" && {
    [ "$(df_v "$(df_g 1)" deg)" = 270 ] && [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="$(df_g 1) vol $(vols)"; fail df_knob_volume_first; }
    v1=$(vols | cut -d' ' -f1)
    [ "$(df_v "$(df_g 2)" state)" = 1 ] && [ "$(df_v "$(df_a 2)" knob)" = 1 ] && [ "$(df_v "$(df_g 3)" deg)" = 300 ] && [ "$(df_v "$(df_g 4)" deg)" = 255 ] && [ "$(vols | cut -d' ' -f2)" = "$v1" ] \
        || { EVID="$(df_g 2) | $(df_g 3) | $(df_g 4) | vol $(vols)"; fail df_knob_shield; }; }
# a shot straight at the shield bounces back and scores; one at the far side hits the core
nowrun df_bounce_miss "$DFS; arena game shot 0 0 -75 0 1.5; arena step 14; arena; arena game kill; arena game deg 90; arena game shot 0 0 -90 0 1.5; arena step 60; arena" && {
    g=$(df_g 6); [ "$(df_v "$g" bounces)" = 1 ] && [ "$(df_v "$g" score)" = 10 ] && [ "$(df_v "$g" backs)" = 1 ] && [ "$(df_v "$g" combo)" = 1 ] || { EVID="$g"; fail df_bounce; }
    g=$(df_g 11); [ "$(df_v "$g" lives)" = 2 ] && [ "$(df_v "$g" combo)" = 0 ] && [ "$(df_v "$g" misses)" = 1 ] && [ "$(df_v "$g" half)" = 26 ] || { EVID="$g"; fail df_miss; }; }
# a golden shot bounced: +1 life; golden missed: free
nowrun df_gold_shot "$DFS; arena game lives 2; arena game shot 0 0 -80 0 1.5 gold; arena step 14; arena; arena game kill; arena game deg 90; arena game shot 0 0 -90 0 1.5 gold; arena step 60; arena" && {
    [ "$(df_v "$(df_g 6)" lives)" = 3 ] && [ "$(df_v "$(df_g 6)" score)" = 60 ] && [ "$(df_v "$(df_g 11)" lives)" = 3 ] || { EVID="$(df_g 6) | $(df_g 11)"; fail df_gold_shot; }; }
# the rules: twin = two shields (a shot from below is bounced by the second), rebound = a bounced shot breaks the one it meets
nowrun df_rules "$DF; games rule 1; arena hold 1; arena game spawn 0; arena tap 0 0; arena; arena game shot 0 0 80 0 -1.5; arena step 14; arena; games rule 2; arena hold 1; arena game spawn 0; arena tap 0 0; arena game shot 0 0 -75 0 1.2; arena game shot 1 0 -92 0 0; arena step 30; arena" && {
    [ "$(df_v "$(df_g 4)" shields)" = 2 ] && [ "$(df_v "$(df_g 7)" bounces)" = 1 ] && [ "$(df_v "$(df_g 7)" half)" = 19 ] || { EVID="$(df_g 4) | $(df_g 7)"; fail df_twin; }
    g=$(df_g 14); [ "$(df_v "$g" rule)" = 2 ] && [ "$(df_v "$g" shots)" = 0 ] && [ "$(df_v "$g" score)" = 40 ] || { EVID="$g"; fail df_rebound; }; }
# the square layout: the same game on four edges (a shot at the top edge bounces there)
nowrun df_square "$DFS; arena game shape 1; arena tap 0 0; arena game spawn 0; arena game shot 0 0 -85 0 1.4; arena step 20; arena" && {
    g=$(df_g 9); [ "$(df_v "$g" shape)" = 1 ] && [ "$(df_v "$g" bounces)" = 1 ] && [ "$(df_v "$g" backs)" = 1 ] || { EVID="$g"; fail df_square; }; }
# the end: the third miss ends it, the result card with the points, a tap = again
nowrun df_over "$DFS; arena game lives 1; arena game deg 90; arena game shot 0 0 -90 0 1.5; arena step 60; arena; games result; @1; games result; games result tap; arena" && {
    [ "$(df_v "$(df_g 8)" state)" = 2 ] && grep -q '^APP game_result up=1' <<<"$out" || { EVID="$(df_g 8) | $(grep '^APP game_result' <<<"$out" | tr '\n' '|')"; fail df_over; }
    [ "$(df_v "$(df_g 9)" state)" = 0 ] || { EVID="$(df_g 9)"; fail df_again; }; }
nowrun df_own_help "$DF; frame show; simtaprow 1; @0.3; popring 3; @0.3; help show; simflick up; @0.3; uitop" && {
    grep -qx 'APP explain id=game title=Deflector paras=1 qr=0 web=0' <<<"$out" && [ "$(tp)" = "tools_game_deflect " ] || { EVID="$(grep '^APP explain' <<<"$out") $(tp)"; fail df_own_help; }; }
nowrun df_screen_off "$DF; arena hold 0; simtap 180 193; @0.5; screen off; @0.5; arena; @1; arena; screen on; @0.3; arena" && {
    [ "$(df_v "$(df_a 2)" paused)" = 1 ] && [ "$(df_v "$(df_a 2)" ticks)" = "$(df_v "$(df_a 3)" ticks)" ] || { EVID="$(df_a 2) | $(df_a 3)"; fail df_screen_off; }; }
nowrun df_back_mid_play "$DF; simtap 180 193; @0.5; exit pause; @0.2; exit tap back; @0.3; uitop; arena" && {
    [ "$(tp)" = "tools_games " ] && grep -qx 'APP arena none' <<<"$out" || { EVID="$(tp)"; fail df_back_mid_play; }; }
