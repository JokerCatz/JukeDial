# SPDX-License-Identifier: LGPL-2.1-or-later
# --- GS: the gesture system (ui.c recogniser, ui_qs.c quick settings, ui_tour.c tutorial); sourced by sim/test.sh (name order); uses nowrun, tp, us, fail ---
# Four fast flicks through the middle (simflick: 60 % of the panel in 150 ms): down = quick settings, up = close them / popup / back (the home page: nothing), left / right = ring page. A slow
# drag of the same length is none. A game in flight takes the down flick too (the quick settings cover it; closing resumes it).
gs_flicks() { grep -c "^APP flick $1" <<<"$out"; }
nowrun gs_flick_down_up "screen on; vol 0; @0.5; simflick up; @0.2; uitop; simflick down; @0.2; uitop; simflick down; @0.2; uitop; simflick up; @0.2; uitop" && {
    [ "$(tp)" = "menu qs qs menu " ] || { EVID="$(tp)"; fail gs_flick_down_up; }; }
nowrun gs_up_is_back "screen on; vol 0; @0.5; tools; @0.3; simflick up; @0.3; uitop; setui; @0.3; simflick up; @0.3; uitop" && {
    [ "$(tp)" = "menu menu " ] || { EVID="$(tp)"; fail gs_up_is_back; }; }
nowrun gs_side_flicks_turn "screen on; vol 0; @0.5; tools; ring; simflick right; ring; simflick left; ring; simflick left; ring; simflick left; ring" && {
    [ "$(rg | sed 's/center=[^|]*//g')" = "page=0/2 sel=-1 |page=1/2 sel=-1 |page=0/2 sel=-1 |page=1/2 sel=-1 |page=0/2 sel=-1 |" ] || { EVID="$(rg)"; fail gs_side_flicks_turn; }; }
nowrun gs_slow_drags_none "screen on; vol 0; @0.5; tools; simdrag 180 60 180 300 1200; @0.2; simdrag 180 300 180 60 1200; @0.2; simdrag 300 180 60 180 1200; @0.2; uitop; ring" && {
    [ "$(gs_flicks '[a-z]*')" = 0 ] && [ "$(tp)" = "tools " ] && [ "$(rg | cut -c1-8)" = "page=0/2" ] || { EVID="flicks $(gs_flicks '[a-z]*') $(tp) $(rg)"; fail gs_slow_drags_none; }; }
nowrun gs_game_down_pauses "screen on; vol 0; games open tower; @0.5; simflick down; @0.3; uistack; simflick up; @0.3; uistack" && {
    [ "$(grep '^APP uistack' <<<"$out" | sed -n 1p | grep -c ' qs$')" = 1 ] && [ "$(grep '^APP uistack' <<<"$out" | sed -n 2p | grep -c ' qs$')" = 0 ] || { EVID="$(us)"; fail gs_game_down_pauses; }; }
# the quick settings: brightness and volume `- +` (10 and 1; the volume stops at volume_max), the switches, airplane both ways; closes by a tap on the empty page and after 4 s
NOWCFG=volume_max=5 nowrun gs_qs_controls "screen on; vol 0; @0.5; qs; qs tap bright-; qs tap bright-; qs tap bright+; qs tap vol+; qs tap vol+; qs tap vol+; qs tap vol+; qs tap vol+; qs tap vol+; qs tap vol+; qs tap wifi; qs tap wifi; qs tap air; qs tap air" && {
    d() { grep '^APP qs open=' <<<"$out" | sed -n "${1}p"; }
    [ "$(sed -n 's/.*bright=\([0-9]*\).*/\1/p' <<<"$(d 1)")" = 50 ] && [ "$(sed -n 's/.*bright=\([0-9]*\).*/\1/p' <<<"$(d 2)")" = 40 ] \
      && [ "$(sed -n 's/.* vol=\([0-9]*\).*/\1/p' <<<"$(d 10)")" = 5 ] \
      && grep -q 'wifi=0' <<<"$(d 11)" && grep -q 'wifi=1' <<<"$(d 12)" \
      && grep -q 'bt=0 wifi=0 air=1' <<<"$(d 13)" && grep -q 'bt=0 wifi=1 air=0' <<<"$(d 14)" || { EVID="$(d 1) | $(d 2) | $(d 10) | $(d 11) | $(d 13) | $(d 14)"; fail gs_qs_controls; }; }
nowrun gs_qs_wifi_denied "screen on; vol 0; @0.5; qs; qs tap wifi; cfg wifi_allow; qs tap wifi; cfg wifi_allow" && {   # the switch is allow / deny (wifi_svc_hold refuses while denied)
    [ "$(grep '^APP cfg wifi_allow=' <<<"$out" | tr '\n' ' ')" = "APP cfg wifi_allow=0 APP cfg wifi_allow=1 " ] || { EVID="$(grep '^APP cfg wifi_allow=' <<<"$out" | tr '\n' ' ')"; fail gs_qs_wifi_denied; }; }
nowrun gs_qs_close_tap_and_idle "screen on; vol 0; @0.5; simflick down; @0.3; uitop; simtap 180 20; @0.3; uitop; simflick down; @0.3; uitop; @4.8; uitop" && {
    [ "$(tp)" = "qs menu qs menu " ] || { EVID="$(tp)"; fail gs_qs_close_tap_and_idle; }; }
# a boot mode that owns the radios: the three switches are grey with one line of reason; a tap on one changes nothing
out=$(t10 "@0.5; qs; qs tap bt; qs tap air")
{ [ "$(grep -c '^APP qs open=1 .*why=[^-]' <<<"$out")" = 2 ] && grep -q 'bt=0 wifi=1 air=0' <<<"$out" && ! t10bad; } || { EVID="$(grep '^APP qs' <<<"$out" | tr '\n' '|')"; fail gs_qs_degrade_inst; }
# I4: the Bluetooth switch is a reboot: the first tap only arms and says so, the second stores bt_mode and reboots; the running mode never changes live. Airplane with Bluetooth on: the same two taps
SIM_STATE=bt=on nowrun gs_qs_bt_reboot "screen on; vol 0; @0.5; qs; qs tap bt; cfg bt_mode; qs tap bt; cfg bt_mode; qs" && {
    [ "$(grep -c '^APP sysmode reboot' <<<"$out")" = 1 ] && grep -q '^APP qs open=1 .*bt=1 .*why=[^-]' <<<"$out" && [ "$(grep '^APP cfg bt_mode=' <<<"$out" | tr '\n' ' ')" = "APP cfg bt_mode= APP cfg bt_mode=0 " ] \
      && ! grep -q '^APP qs open=1 .*bt=0' <<<"$out" || { EVID="$(grep '^APP qs\|^APP cfg\|^APP sysmode' <<<"$out" | tr '\n' '|')"; fail gs_qs_bt_reboot; }; }
SIM_STATE=bt=on nowrun gs_qs_air_reboot "screen on; vol 0; @0.5; qs; qs tap air; cfg wifi_allow; qs tap air; cfg wifi_allow; cfg air_bt; cfg air_wifi" && {
    [ "$(grep -c '^APP sysmode reboot' <<<"$out")" = 1 ] && [ "$(grep '^APP cfg \(wifi_allow\|air_bt\|air_wifi\)=' <<<"$out" | tr '\n' ' ')" = "APP cfg wifi_allow= APP cfg wifi_allow=0 APP cfg air_bt=1 APP cfg air_wifi=1 " ] \
      || { EVID="$(grep '^APP cfg\|^APP sysmode' <<<"$out" | tr '\n' '|')"; fail gs_qs_air_reboot; }; }
# M8: two separate switch-offs are not airplane; airplane off restores the Wi-Fi state it saved (denied before: stays denied)
nowrun gs_qs_air_restores "screen on; vol 0; @0.5; qs; qs tap wifi; qs tap air; qs tap air; cfg wifi_allow" && {
    grep -q '^APP qs open=1 .*wifi=0 air=0' <<<"$out" && [ "$(grep '^APP qs open=' <<<"$out" | sed -n 2p | grep -c 'air=1')" = 1 ] && [ "$(grep '^APP cfg wifi_allow=' <<<"$out")" = "APP cfg wifi_allow=0" ] || { EVID="$(grep '^APP qs\|^APP cfg' <<<"$out" | tr '\n' '|')"; fail gs_qs_air_restores; }; }
# M6: a hold does not leave the language pick (the up flick does not either); a hold over the quick settings that cover the tour still finishes the tour
rm -rf "$T/lp"; mkdir -p "$T/lp/_player/config"
out=$($SIM --headless --root "$T/lp" --cmd "@0.5; simpress 180 8 3300; @0.4; uitop" 2>&1)
[ "$(grep '^APP uitop' <<<"$out")" = "APP uitop langpick" ] || { EVID="$(grep '^APP uitop' <<<"$out")"; fail gs_langpick_no_hold; }
nowrun gs_tour_hold_over_qs "screen on; vol 0; @0.5; tour; simflick down; @0.3; simpress 180 180 3100; @0.4; uitop; cfg tour_done" && {
    [ "$(grep '^APP uitop' <<<"$out")" = "APP uitop menu" ] && grep -qx 'APP cfg tour_done=1' <<<"$out" || { EVID="$(grep '^APP uitop\|^APP cfg' <<<"$out" | tr '\n' '|')"; fail gs_tour_hold_over_qs; }; }
# the tutorial: four steps, each one needs its gesture (the others are ignored); a hold at the end finishes and stores tour_done
nowrun gs_tour_steps "screen on; vol 0; @0.5; tour; tour dump; simflick down; tour dump; simflick up; tour dump; simflick down; @0.2; uitop; simflick up; @0.3; tour dump; simflick left; tour dump; simflick up; tour dump; simpress 180 180 3100; @0.4; uitop; cfg tour_done; tour dump" && {
    t() { grep '^APP tour step=' <<<"$out" | sed -n "${1}p" | sed 's/ needed.*//'; }
    [ "$(t 1)" = "APP tour step=0 done=0" ] && [ "$(t 2)" = "APP tour step=0 done=0" ] && [ "$(t 3)" = "APP tour step=1 done=0" ] && [ "$(t 4)" = "APP tour step=2 done=0" ] && [ "$(t 5)" = "APP tour step=3 done=0" ] \
      && [ "$(t 6)" = "APP tour step=3 done=0" ] && grep -qx 'APP cfg tour_done=1' <<<"$out" && [ "$(tp)" = "qs menu " ] || { EVID="$(t 1)|$(t 2)|$(t 3)|$(t 4)|$(t 5)|$(t 6) tp=$(tp)"; fail gs_tour_steps; }; }
nowrun gs_tour_skip "screen on; vol 0; @0.5; tour; @0.3; simtap 180 328; @0.3; uitop; cfg tour_done" && {
    [ "$(tp)" = "menu " ] && grep -qx 'APP cfg tour_done=1' <<<"$out" || { EVID="$(tp)"; fail gs_tour_skip; }; }
