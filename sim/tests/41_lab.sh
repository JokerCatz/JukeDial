# SPDX-License-Identifier: LGPL-2.1-or-later
# --- LAB (2026-11-21): the test-day run-time knobs (app/main/lab.c, pc_lab.h). `lab` lists, `lab <key> <value>` sets (range-checked, stored in the config as lab_<key>), `lab reset`;
# a boot with a knob off its default logs "APP lab active: ..."; a boot after a crash drops them (the simulator fakes it with SIM_LAB_CRASH=1). The gesture knobs change behaviour.
# The FakePod's panel / touch / DAC knobs have no simulator side (the BSP setters are board-only); the key timings are checked in the host tests (test_keys / test_blind).
labrun() { fresh; [ -n "$1" ] && printf '%s\n' "$1" >> "$T/fix/_player/config/app.txt"; out=$(env $4 $SIM --headless --root "$T/fix" --cmd "screen on; vol 0; @0.5; $2" 2>&1); [ -z "$3" ] || [ "$3" = 0 ]; }

nowrun lab_list "lab; lab flick_edge; lab lab_flick_edge" && {
    [ "$(grep -c '^APP lab [a-z0-9_]*=[0-9]* default=' <<<"$out")" -ge 17 ] && grep -qx 'APP lab flick_edge=50 default=50 range=20..120 when=live' <<<"$out" \
      && [ "$(grep -c '^APP lab flick_edge=50 default=50' <<<"$out")" = 3 ] && grep -qx 'APP lab usb_suspend_ms=2000 default=2000 range=0,500..30000 when=live' <<<"$out" \
      && grep -qx 'APP lab dac_xtal_ibias=4 default=4 range=2..6 (even) when=next track' <<<"$out" && grep -qx 'APP lab co5300_c4=1 default=1 range=0..1 when=reboot' <<<"$out" \
      || { EVID="$(grep '^APP lab' <<<"$out" | head -4 | tr '\n' '|')"; fail lab_list; }; }
nowrun lab_set_get_refuse_reset "lab flick_edge 100; lab flick_edge; lab flick_edge 19; lab flick_edge 121; lab flick_edge x; lab flick_edge 100x; lab nope 1; lab dac_xtal_ibias 3; lab dac_xtal_ibias 6; lab co5300_c4 0; lab nowb_hide_ms 0; lab nowb_hide_ms 500; cfg lab_flick_edge; lab reset; lab flick_edge; cfg lab_flick_edge" && {
    want='APP lab set flick_edge=100 (live)|APP lab flick_edge=100 default=50 range=20..120 when=live|APP lab refused flick_edge=19 range=20..120|APP lab refused flick_edge=121 range=20..120|APP lab refused flick_edge=x range=20..120|APP lab refused flick_edge=100x range=20..120|APP lab unknown key=nope|APP lab refused dac_xtal_ibias=3 range=2..6 (even)|APP lab set dac_xtal_ibias=6 (takes effect at the next track start; stored)|APP lab set co5300_c4=0 (takes effect after a reboot; stored)|APP lab set nowb_hide_ms=0 (live)|APP lab refused nowb_hide_ms=500 range=0,1000..15000|APP cfg lab_flick_edge=100|APP lab reset n=4 (a knob that is read at boot or at the next track takes it then)|APP lab flick_edge=50 default=50 range=20..120 when=live|APP cfg lab_flick_edge=|'
    got=$(grep '^APP lab\|^APP cfg lab' <<<"$out" | tr '\n' '|')
    [ "$got" = "$want" ] || { EVID="got: $got"; fail lab_set_get_reset; }
    :; }
# a stored value: a boot with a config line takes it and logs it once; an out-of-range line (a hand edit) is the default; a value equal to the default is not kept
labrun "lab_flick_edge=100
lab_nowb_hide_ms=1500
lab_key_hold_ms=9999" "lab flick_edge; lab nowb_hide_ms; lab key_hold_ms" 1
{ grep -qx 'APP lab active: flick_edge=100 nowb_hide_ms=1500' <<<"$out" && grep -qx 'APP lab flick_edge=100 default=50 range=20..120 when=live' <<<"$out" && grep -q '^APP lab key_hold_ms=450 default=450' <<<"$out"; } || { EVID="$(grep '^APP lab' <<<"$out" | tr '\n' '|')"; fail lab_boot_active_line; }
labrun "" "lab" 1; ! grep -q '^APP lab active' <<<"$out" || fail lab_no_line_at_defaults
labrun "lab_flick_edge=100" "lab flick_edge 50; lab; cfg lab_flick_edge" 1; grep -q '^APP cfg lab_flick_edge=$\|^APP cfg lab_flick_edge=(none)\|^APP cfg lab_flick_edge$' <<<"$out" || grep -qi 'lab_flick_edge.*\(unset\|none\|absent\|missing\)' <<<"$out" || { EVID="$(grep '^APP cfg' <<<"$out")"; fail lab_default_value_dropped; }
# after a crash boot every lab_ key is dropped and logged; the value is the default for that boot and stays so
labrun "lab_flick_edge=100
lab_touch_x_num=31" "lab flick_edge; cfg lab_flick_edge" 1 SIM_LAB_CRASH=1
{ grep -q '^APP lab: the last reset was a crash, 2 knob(s) back to their defaults' <<<"$out" && ! grep -q '^APP lab active' <<<"$out" && grep -qx 'APP lab flick_edge=50 default=50 range=20..120 when=live' <<<"$out"; } || { EVID="$(grep '^APP lab' <<<"$out" | tr '\n' '|')"; fail lab_crash_boot_reset; }

# the gesture knobs: an up flick that begins 110 px from the centre (70 px from the bottom rim: outside the default 50 px edge) goes back at lab flick_edge 100, not at the default
G="tools; @0.3; uistack; simdrag 180 290 180 100 150; @0.3; uistack"
nowrun lab_flick_edge_default_refuses "$G" && { [ "$(grep '^APP uistack' <<<"$out" | tr '\n' '|')" = 'APP uistack menu tools|APP uistack menu tools|' ] || { EVID="$(grep '^APP uistack' <<<"$out" | tr '\n' '|')"; fail lab_flick_edge_default_refuses; }; }
nowrun lab_flick_edge_100_accepts "lab flick_edge 100; $G" && { [ "$(grep '^APP uistack' <<<"$out" | tr '\n' '|')" = 'APP uistack menu tools|APP uistack menu|' ] || { EVID="$(grep '^APP uistack' <<<"$out" | tr '\n' '|')"; fail lab_flick_edge_100_accepts; }; }
# the distance share: a 140 px (39 %) up flick from the rim is no flick at 40 %, one at lab flick_pct 30
G2="tools; @0.3; simdrag 180 340 180 200 150; @0.3; uistack"
nowrun lab_flick_pct_default "$G2" && { [ "$(grep '^APP uistack' <<<"$out")" = 'APP uistack menu tools' ] || { EVID="$(grep '^APP uistack' <<<"$out")"; fail lab_flick_pct_default; }; }
nowrun lab_flick_pct_30 "lab flick_pct 30; $G2" && { [ "$(grep '^APP uistack' <<<"$out")" = 'APP uistack menu' ] || { EVID="$(grep '^APP uistack' <<<"$out")"; fail lab_flick_pct_30; }; }
# the time: a 200 ms swipe is too slow for 150..., a flick at 300 ms (default) : drag of 400 ms is no flick at the default, one at lab flick_ms 600
G3="tools; @0.3; simdrag 180 340 180 100 450; @0.3; uistack"
nowrun lab_flick_ms_default "$G3" && { [ "$(grep '^APP uistack' <<<"$out")" = 'APP uistack menu tools' ] || { EVID="$(grep '^APP uistack' <<<"$out")"; fail lab_flick_ms_default; }; }
nowrun lab_flick_ms_600 "lab flick_ms 600; $G3" && { [ "$(grep '^APP uistack' <<<"$out")" = 'APP uistack menu' ] || { EVID="$(grep '^APP uistack' <<<"$out")"; fail lab_flick_ms_600; }; }
