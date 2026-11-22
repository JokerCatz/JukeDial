# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G5: 萬花尺 on the shared field's canvas (app/main/ui/scr_game_spiro.c, ui_arena.c canvas; curve in components/player_core/src/pc_game_spiro.c); sourced by sim/test.sh ---
# `arena game new <gear> <pen> <epi>` sets the wheels at once and starts the drawing, `arena step <n>` n fixed steps, `arena hold 1` stops the field's own steps, `arena game finish`
# draws to the end. Every `arena` command ends with both dumps ("APP arena ..." then "APP game spiro gear= pen= epi= rule= speed= colour= layer= seg=<n>/<total> done= gold= paused= lobes= lines= canvas=").
SPO=0
sp_g() { grep '^APP game spiro gear=' <<<"$out" | sed -n "$(($1 + SPO))p"; }
sp_a() { grep '^APP arena name=' <<<"$out" | sed -n "$(($1 + SPO))p"; }
sp_v() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }
SP="screen on; games open spiro; @0.3; games mode 0; games style 0; games rule 0; games colour 0; arena hold 1; arena game gold 0"
SPO=0
nowrun sp_open "screen on; games open spiro; @0.3; arena; uitop" && {
    a=$(sp_a 1); g=$(sp_g 1)
    [ "$(sp_v "$g" canvas)" = 1 ] && [ "$(sp_v "$g" done)" = 0 ] && [ "$(sp_v "$a" run)" = 1 ] && [ "$(tp)" = "tools_game_spiro " ] || { EVID="$a | $g | $(tp)"; fail sp_open; }; }
# the pen draws 5 segments a step at the normal speed; gear 0 (5:3) closes after 3 turns = 360 segments, then the field rests
SPO=3
nowrun sp_draw "$SP; arena game new 0 2 0; arena step 10; arena step 100; arena" && {
    [ "$(sp_v "$(sp_g 2)" seg)" = 50/360 ] && [ "$(sp_v "$(sp_g 3)" done)" = 1 ] && [ "$(sp_v "$(sp_g 3)" lines)" -ge 360 ] && [ "$(sp_v "$(sp_a 3)" run)" = 0 ] || { EVID="$(sp_g 2) | $(sp_g 3) | $(sp_a 3)"; fail sp_draw; }; }
# a step redraws little: the pen's segments only, far below the field
nowrun sp_step_redraws_little "$SP; arena game new 0 2 0; arena step 3; arena" && {
    a=$(sp_a 1); [ "$(sp_v "$a" inv_px)" -gt 0 ] && [ "$(sp_v "$a" inv_px)" -lt $(( $(sp_v "$a" field_px) / 3 )) ] || { EVID="$a"; fail sp_step_redraws_little; }; }
# the rules: layers draw three curves (3 x 360 segments), music creeps in silence; a rule restarts, the wheels stay
SPO=2
nowrun sp_rules "$SP; arena game new 0 2 0; games rule 1; arena game finish; games rule 2; arena step 30; arena" && {
    g=$(sp_g 3); [ "$(sp_v "$g" rule)" = 1 ] && [ "$(sp_v "$g" gear)" = 0 ] && [ "$(sp_v "$g" done)" = 1 ] && [ "$(sp_v "$g" lines)" -ge 1080 ] || { EVID="$g"; fail sp_layers; }
    g=$(sp_g 4); [ "$(sp_v "$g" rule)" = 2 ] && [ "$(sp_v "$g" done)" = 0 ] && [ "$(sp_v "${g}" gear)" = 0 ] || { EVID="$g"; fail sp_music; }; }
# the axes: gear / pen / colour as the ring sets them; gear and pen restart (seg 0), colour keeps the progress
SPO=3
nowrun sp_axes "$SP; arena game new 0 2 0; arena step 10; games gear 3; arena step 4; games colour 2; games pen 1" && {
    g=$(sp_g 3); [ "$(sp_v "$g" gear)" = 3 ] && [ "$(sp_v "$g" seg)" = 0/480 ] || { EVID="$g"; fail sp_gear; }
    g=$(sp_g 5); [ "$(sp_v "$g" colour)" = 2 ] && [ "$(sp_v "$g" seg)" = 20/480 ] || { EVID="$g"; fail sp_colour_keeps_progress; }
    g=$(sp_g 6); [ "$(sp_v "$g" pen)" = 1 ] && [ "$(sp_v "$g" seg)" = 0/480 ] || { EVID="$g"; fail sp_pen; }; }
# a tap on the field = a new random curve, the field running again; golden games say so
SPO=0
nowrun sp_tap "$SP; arena game finish; arena tap 0 0; games gold 1; arena tap 0 0; arena; games gold auto" && {
    grep -c '^APP game spiro new ' <<<"$out" | grep -qx 3 && grep -q '^APP game spiro new .* gold=1$' <<<"$out" || { EVID="$(grep 'spiro new' <<<"$out")"; fail sp_tap; }; }
# taps only: a knob turn before a tap is the volume
nowrun sp_knob_volume "screen on; games open spiro; @0.3; vol 0; arena hold 1; @0.3; simknob 1; @0.3; simvol" && {
    [ "$(vols | cut -d' ' -f1)" != 0 ] || { EVID="vol $(vols)"; fail sp_knob_volume; }; }
# the screen off stops the pen (a toy: its timer stops, nothing is "paused"; the screen waking restarts it)
SPO=0
nowrun sp_screen_off "screen on; games open spiro; @0.3; screen off; @0.5; arena; @1; arena; screen on; @0.5; arena" && {
    [ "$(sp_v "$(sp_a 1)" run)" = 0 ] && [ "$(sp_v "$(sp_a 1)" ticks)" = "$(sp_v "$(sp_a 2)" ticks)" ] || { EVID="$(sp_a 1) | $(sp_a 2)"; fail sp_screen_off; }
    [ "$(sp_v "$(sp_a 3)" run)" = 1 ] && [ "$(sp_v "$(sp_a 3)" ticks)" -gt "$(sp_v "$(sp_a 2)" ticks)" ] || { EVID="$(sp_a 3)"; fail sp_screen_on_restarts; }; }
SPO=0
nowrun sp_back_mid_draw "screen on; games open spiro; @0.5; back; @0.3; uitop; arena" && {
    [ "$(tp)" = "tools_games " ] && grep -qx 'APP arena none' <<<"$out" || { EVID="$(tp)"; fail sp_back_mid_draw; }; }
nowrun sp_own_help "screen on; games open spiro; @0.3; frame show; simtaprow 1; @0.3; popring 6; @0.3; help show; uitop" && {
    grep -q '^APP explain id=game title=Spirograph' <<<"$out" || { EVID="$(grep '^APP explain' <<<"$out")"; fail sp_own_help; }; }
nowrun sp_music_plays "screen on; vol 0; track 3; @2; audio info; games open spiro; @0.3; games rule 2; @2; audio info" && {
    a=$(grep '^APP audio info ' <<<"$out" | sed 's/ underruns_src.*//' | sort -u | wc -l)
    [ "$(grep -c '^APP audio info ' <<<"$out")" = 2 ] && [ "$a" = 1 ] && ! grep -q '^APP mp3 stop\|^APP play stop' <<<"$out" || { EVID="$(grep '^APP audio info' <<<"$out" | cut -c1-80 | tr '\n' '|')"; fail sp_music_plays; }; }
# --- end 萬花尺 (spirograph) ---
# a gear step after a golden curve gives the plain palette back (restart rebuilds it); a colour step redraws once (one pass at the next tick, however many steps)
SPO=3
nowrun sp_gold_gear "$SP; games gold 1; arena tap 0 0; games gear 2; arena" && {
    [ "$(sp_v "$(sp_g 1)" gold)" = 1 ] && [ "$(sp_v "$(sp_g 2)" gold)" = 0 ] || { EVID="$(sp_g 1) | $(sp_g 2)"; fail sp_gold_gear; }; }
SPO=3
nowrun sp_colour_coalesces "$SP; arena game new 0 2 0; arena step 20; games colour 1; games colour 2; games colour 0; arena step 1; arena" && {
    [ "$(sp_v "$(sp_g 6)" seg)" = 105/360 ] || { EVID="$(sp_g 6)"; fail sp_colour_coalesces; }; }
