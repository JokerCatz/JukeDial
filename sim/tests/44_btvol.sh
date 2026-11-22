# SPDX-License-Identifier: LGPL-2.1-or-later
# --- BTVOL: one volume per output. The wired output keeps cfg "volume"; every Bluetooth headset has its own value by address (cfg "bt_vols"), a headset never seen starts at 33.
# The fake: "simbt hp a|b" = that headset connected and streaming (the board: ui_hp_stream(1) -> vol_out_switch), "simbt hp off" = stream stopped / headset gone, "simbt hpkey n" = the headset's own
# volume key, "simbt hpforget a|b" = unpaired, "simbt cast 1|0" = the Auracast broadcast; "simblock 0|1": the fake audio task makes no block / makes blocks (an output change that must RAISE the volume waits for one).
# `vol` prints APP vol out=<wired|bt:<addr>|cast> value=<current output> wired=<wired>; simaudiovol = what the audio path was given.
bv() { grep '^APP vol out=' <<<"$out" | sed -n "$1p"; }
av() { grep '^SIM audiovol' <<<"$out" | sed -n "$1p" | sed 's/SIM audiovol //'; }
# the owner's sequence: wired 60 -> headset A (never seen) 33 -> set 40 -> disconnect: wired 60 -> A again: 40 -> headset B: 33 -> forget A: 33 again
nowrun btvol_per_output "screen on; @0.5; vol 60; simbt hp a; simaudiovol; @0.1; vol; vol 40; simaudiovol; simbt hp off; @0.1; vol; simaudiovol; simbt hp a; @0.1; vol; simaudiovol; simbt hp off; @0.1; simbt hp b; @0.1; vol; simaudiovol; simbt hp off; @0.1; simbt hpforget a; simbt hp a; @0.1; vol; cfg bt_vols; cfg volume" && {
    [ "$(bv 1)" = "APP vol out=wired value=60 wired=60" ] || { EVID="$(bv 1)"; fail btvol_wired_start; }
    [ "$(av 1)" = 33 ] || { EVID="audio $(av 1)"; fail btvol_first_connect_third_before_sink; }          # at once: the lower value is in force before the sink
    [ "$(bv 2)" = "APP vol out=bt:020000000001 value=33 wired=60" ] || { EVID="$(bv 2)"; fail btvol_first_connect_third; }
    [ "$(bv 3)" = "APP vol out=bt:020000000001 value=40 wired=60" ] && [ "$(av 2)" = 40 ] || { EVID="$(bv 3) audio $(av 2)"; fail btvol_headset_set; }
    [ "$(bv 4)" = "APP vol out=wired value=60 wired=60" ] && [ "$(av 3)" = 60 ] || { EVID="$(bv 4) audio $(av 3)"; fail btvol_wired_back; }
    [ "$(bv 5)" = "APP vol out=bt:020000000001 value=40 wired=60" ] && [ "$(av 4)" = 40 ] || { EVID="$(bv 5) audio $(av 4)"; fail btvol_headset_remembered; }
    [ "$(bv 6)" = "APP vol out=bt:020000000002 value=33 wired=60" ] && [ "$(av 5)" = 33 ] || { EVID="$(bv 6) audio $(av 5)"; fail btvol_other_headset_default; }
    [ "$(bv 7)" = "APP vol out=bt:020000000001 value=33 wired=60" ] || { EVID="$(bv 7)"; fail btvol_forget_default_again; }
    grep -qx 'APP cfg bt_vols=' <<<"$out" && grep -qx 'APP cfg volume=60' <<<"$out" || { EVID="$(grep '^APP cfg ' <<<"$out" | tr '\n' '|')"; fail btvol_storage; }; }
# the order when the new output is LOUDER (wired 20 -> a headset stored at 50, and 50 -> wired 70): nothing is raised until the audio task has begun a block with the new sink; the knob cannot break the ceiling
NOWCFG=$'volume=20\nbt_vols=020000000001:50' nowrun btvol_raise_waits "screen on; @0.5; simblock 0; simbt hp a; @0.3; vol; simaudiovol; vol 60; simaudiovol; simblock 1; @0.1; simaudiovol; vol 70; simbt hp off; simaudiovol; @0.1; simaudiovol; vol; cfg bt_vols" && {
    grep -q '^APP vol switch out=bt:020000000001 across=20 value=50 full=50 hold=1$' <<<"$out" || { EVID="$(grep '^APP vol switch' <<<"$out" | head -2 | tr '\n' '|')"; fail btvol_raise_held; }
    [ "$(bv 1)" = "APP vol out=bt:020000000001 value=50 wired=20" ] && [ "$(av 1)" = 20 ] || { EVID="$(bv 1) audio $(av 1)"; fail btvol_raise_not_before_block; }
    [ "$(av 2)" = 20 ] || { EVID="audio $(av 2)"; fail btvol_hold_caps_other_callers; }                    # a volume command during the hold: kept, not applied
    [ "$(av 3)" = 60 ] || { EVID="audio $(av 3)"; fail btvol_release_applies_newest; }
    [ "$(av 4)" = 20 ] && [ "$(av 5)" = 20 ] || { EVID="audio $(av 4) $(av 5)"; fail btvol_wired_lower_first; }   # 70 on the headset -> wired 20: down before the sink, and it stays
    grep -qx 'APP cfg bt_vols=020000000001:70' <<<"$out" || { EVID="$(grep '^APP cfg bt_vols' <<<"$out")"; fail btvol_headset_stored; }; }
# volume_max clamps every output's value (stored 90 / 80, the limit 50), the headset's own keys are stored for the headset (never for wired), also above the limit
NOWCFG=$'volume=90\nvolume_max=50\nbt_vols=020000000001:80' nowrun btvol_max_and_keys "screen on; @0.5; vol; simaudiovol; simbt hp a; @0.1; vol; simbt hpkey 44; vol; simaudiovol; simbt hpkey 99; vol; simbt hp off; @0.1; vol; simbt hpkey 10; vol; simaudiovol; simbt hp a; @0.1; vol" && {
    [ "$(bv 1)" = "APP vol out=wired value=50 wired=50" ] || { EVID="$(bv 1)"; fail btvol_max_wired; }
    [ "$(bv 2)" = "APP vol out=bt:020000000001 value=50 wired=50" ] || { EVID="$(bv 2)"; fail btvol_max_headset; }
    [ "$(bv 3)" = "APP vol out=bt:020000000001 value=44 wired=50" ] && [ "$(av 2)" = 44 ] || { EVID="$(bv 3) audio $(av 2)"; fail btvol_headset_key; }
    [ "$(bv 4)" = "APP vol out=bt:020000000001 value=50 wired=50" ] || { EVID="$(bv 4)"; fail btvol_headset_key_clamped; }
    [ "$(bv 5)" = "APP vol out=wired value=50 wired=50" ] || { EVID="$(bv 5)"; fail btvol_wired_untouched_by_keys; }
    [ "$(bv 6)" = "APP vol out=wired value=50 wired=50" ] && [ "$(av 3)" = 50 ] || { EVID="$(bv 6) audio $(av 3)"; fail btvol_key_while_wired_not_applied; }   # the stream is suspended: stored for the headset, the wired sound is not touched
    [ "$(bv 7)" = "APP vol out=bt:020000000001 value=10 wired=50" ] || { EVID="$(bv 7)"; fail btvol_key_while_wired_stored; }; }
# the Auracast broadcast keeps the wired value (as before the split); the knob in a headset session changes the headset's value only
nowrun btvol_cast_and_knob "screen on; @0.5; vol 60; simbt cast 1; @0.1; vol; simaudiovol; simbt cast 0; @0.1; simbt hp a; @0.1; mp3 all; @1; simknob 1; @0.1; vol; simaudiovol; simbt hp off; @0.1; vol" && {
    [ "$(bv 2)" = "APP vol out=cast value=60 wired=60" ] && [ "$(av 1)" = 60 ] || { EVID="$(bv 2) audio $(av 1)"; fail btvol_cast_keeps_wired; }
    [ "$(bv 3)" = "APP vol out=bt:020000000001 value=35 wired=60" ] && [ "$(av 2)" = 35 ] || { EVID="$(bv 3) audio $(av 2)"; fail btvol_knob_is_headset; }
    [ "$(bv 4)" = "APP vol out=wired value=60 wired=60" ] || { EVID="$(bv 4)"; fail btvol_knob_left_wired; }; }
# an alarm's ramp (or any value held below the output's own) is not lifted by a change of output: wired 60 ducked to 12 -> headset 33: stays 12
nowrun btvol_duck_carries "screen on; @0.5; vol 60; simsetvol 12; simbt hp a; @0.1; simaudiovol; vol" && {
    [ "$(av 1)" = 12 ] && [ "$(bv 2)" = "APP vol out=bt:020000000001 value=33 wired=60" ] || { EVID="audio $(av 1) $(bv 2)"; fail btvol_duck_carries; }; }

# --- Speaker mode is an exclusive source. The main menu's Bluetooth cell is the entry: the phone streams -> its playing page (as every other source's cell); nothing streams -> the
# Bluetooth page with the waiting status. A local source picked in speaker mode asks ONE question first (the mode ends by a reboot, the text says so): cancel changes nothing, confirm starts the pick,
# stores bt_mode 0 and asks for the mode reboot (whose resume record carries what was started).
SIM_STATE="bt=on" nowrun spk_menu_waiting "screen on; @0.8; simknob 1; @0.3; simtap 81 81; simtap 81 81; @0.5; uistack; ring" && {
    [ "$(us)" = "menu bt|" ] || { EVID="$(us)"; fail spk_menu_waiting_page; }
    grep -q '^APP ring .*center=.*\(等待手機連線\|Waiting for a phone\)' <<<"$out" || { EVID="$(grep '^APP ring' <<<"$out")"; fail spk_menu_waiting_status; }; }
SIM_STATE="bt=connected" nowrun spk_menu_stream "screen on; @0.8; simbt stream; @0.2; simknob 1; @0.3; simtap 81 81; simtap 81 81; @0.5; uistack" && {
    [ "$(us)" = "menu bt now|" ] || { EVID="$(us)"; fail spk_menu_opens_phone_page; }; }
SIM_STATE="bt=connected" nowrun spk_ask_cancel "screen on; @0.5; vol 0; simbt stream; @0.2; mp3 all; @1; uistack; ring; simtap 40 180; @0.2; simtap 40 180; @0.3; uistack; mp3cur; status; cfg bt_mode" && {
    [ "$(us)" = "menu now spk_ask|menu now|" ] || { EVID="$(us)"; fail spk_ask_shown_then_gone; }
    grep -q '^APP ring .*center=\(離開藍牙喇叭模式並播放？/需要重新開機\|Leave speaker mode and play?/Needs a restart\) n=2$' <<<"$out" || { EVID="$(grep '^APP ring' <<<"$out")"; fail spk_ask_text_says_reboot; }
    grep -q '^APP mp3 cur=-1 ' <<<"$out" && grep -q '^APP status \(來源\|Source\).*\(藍牙\|Bluetooth\)' <<<"$out" || { EVID="$(grep '^APP mp3 cur\|^APP status' <<<"$out" | head -2 | tr '\n' '|')"; fail spk_cancel_keeps_stream; }
    ! grep -q '^APP sysmode reboot' <<<"$out" && grep -qx 'APP cfg bt_mode=' <<<"$out" || { EVID="$(grep '^APP sysmode\|^APP cfg bt' <<<"$out" | tr '\n' '|')"; fail spk_cancel_keeps_mode; }; }
SIM_STATE="bt=connected" nowrun spk_ask_confirm "screen on; @0.5; vol 0; simbt stream; @0.2; mp3 all; @1; simtap 81 279; @0.2; ring; simtap 81 279; @0.3; mp3cur; cfg bt_mode" && {
    grep -q '^APP ring .*sel=0 center=\(重新開機離開藍牙喇叭模式/然後播放\|Restart out of speaker mode/then play\)' <<<"$out" || { EVID="$(grep '^APP ring' <<<"$out")"; fail spk_confirm_hint_says_reboot; }
    grep -q '^APP spk ask leave$' <<<"$out" && grep -q '^APP sysmode reboot$' <<<"$out" && grep -qx 'APP cfg bt_mode=0' <<<"$out" || { EVID="$(grep '^APP spk\|^APP sysmode\|^APP cfg bt' <<<"$out" | tr '\n' '|')"; fail spk_confirm_leaves; }
    grep -q '^APP mp3 cur=[0-9]' <<<"$out" || { EVID="$(grep '^APP mp3 cur' <<<"$out")"; fail spk_confirm_plays_pick; }; }
# every local source asks (radio, colour noise, a noise scene, a chiptune), and no speaker mode: nobody is asked
SIM_STATE="bt=on" nowrun spk_ask_all_sources "screen on; @0.5; vol 0; radio 0; @0.5; back; @0.3; color 1; @0.5; back; @0.3; noise 0; @0.5; uistack" && {
    [ "$(grep -c '^APP spk ask$' <<<"$out")" = 3 ] || { EVID="asks: $(grep -c '^APP spk ask$' <<<"$out")"; fail spk_ask_all_sources; }; }
nowrun spk_no_ask_other_modes "screen on; @0.5; vol 0; mp3 all; @1; uistack; mp3cur" && {
    ! grep -q '^APP spk ask' <<<"$out" && grep -q '^APP mp3 cur=[0-9]' <<<"$out" || { EVID="$(grep '^APP spk\|^APP mp3 cur' <<<"$out" | tr '\n' '|')"; fail spk_no_ask_other_modes; }; }

# --- PMVOL: the jack has its own value in Bluetooth speaker mode (bt_mode 1) and Auracast listening (4): cfg volume_spk, default 60; the MODE decides, not a phone stream. The ordinary wired value is not touched.
# `vol` prints out=speaker there. The switch is vol_out_switch (lower first, a raise waits for a block), the same as for a headset.
sv() { grep '^APP vol spk=' <<<"$out" | sed -n "$1p"; }
NOWCFG=$'volume=90' nowrun pmvol_speaker_own_value "screen on; @0.5; vol; simaudiovol; simbt mode 1; @0.2; vol; simaudiovol; vol 70; simaudiovol; vol; simbt mode 0; @0.2; vol; simaudiovol; cfg volume; cfg volume_spk" && {
    [ "$(bv 1)" = "APP vol out=wired value=90 wired=90" ] || { EVID="$(bv 1)"; fail pmvol_wired_before; }
    [ "$(bv 2)" = "APP vol out=speaker value=60 wired=90" ] && [ "$(av 2)" = 60 ] || { EVID="$(bv 2) audio $(av 2)"; fail pmvol_speaker_default_60_lower_first; }   # 90 -> 60: down first
    [ "$(bv 3)" = "APP vol out=speaker value=70 wired=90" ] && [ "$(av 3)" = 70 ] || { EVID="$(bv 3) audio $(av 3)"; fail pmvol_speaker_knob; }
    [ "$(bv 5)" = "APP vol out=wired value=90 wired=90" ] && [ "$(av 4)" = 90 ] || { EVID="$(bv 5) audio $(av 4)"; fail pmvol_wired_back; }   # (the `vol 70` line is bv 3, the plain `vol` after it bv 4)
    grep -qx 'APP cfg volume=90' <<<"$out" && grep -qx 'APP cfg volume_spk=70' <<<"$out" || { EVID="$(grep '^APP cfg vol' <<<"$out" | tr '\n' '|')"; fail pmvol_storage_separate; }; }
# the other way (wired 40 < speaker 60): the raise waits for a block; Auracast listening (4) is the same value; headphones mode (2) and broadcast (3) stay on the wired value
NOWCFG=$'volume=40' nowrun pmvol_raise_waits_and_listen "screen on; @0.5; simblock 0; simbt mode 1; @0.2; simaudiovol; vol; simblock 1; @0.1; simaudiovol; simbt mode 4; @0.2; vol; simbt mode 2; @0.2; vol; simbt mode 3; @0.2; vol" && {
    [ "$(av 1)" = 40 ] || { EVID="audio $(av 1)"; fail pmvol_raise_not_before_block; }
    [ "$(bv 1)" = "APP vol out=speaker value=60 wired=40" ] || { EVID="$(bv 1)"; fail pmvol_speaker_value; }
    [ "$(av 2)" = 60 ] || { EVID="audio $(av 2)"; fail pmvol_raise_after_block; }
    [ "$(bv 2)" = "APP vol out=speaker value=60 wired=40" ] || { EVID="$(bv 2)"; fail pmvol_listen_is_speaker; }
    [ "$(bv 3)" = "APP vol out=wired value=40 wired=40" ] && [ "$(bv 4)" = "APP vol out=wired value=40 wired=40" ] || { EVID="$(bv 3) $(bv 4)"; fail pmvol_hp_broadcast_wired; }; }
# volume_max clamps the speaker value at read; stored 90 with a limit of 50 plays at 50
NOWCFG=$'volume_spk=90\nvolume_max=50' nowrun pmvol_max_clamps "screen on; @0.5; simbt mode 1; @0.2; vol" && { [ "$(bv 1)" = "APP vol out=speaker value=50 wired=40" ] || { EVID="$(bv 1)"; fail pmvol_max_clamps; }; }
