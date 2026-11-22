# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: need_btn)
need_btn
# --- fx apps T13 ---
# the settings row 重開機 (two taps within 4 s); the help paragraph
nowrun t13_reboot_armed "setui; @0.3; settings key reboot; settings rows; @0.5; settings key reboot; @4.5; settings rows" && {
    [ "$(nrb)" = 1 ] && grep -qx 'APP reboot msg=\(重新開機…\|Restarting...\)' <<<"$out" || { EVID="reboots $(nrb) $(grep '^APP reboot msg' <<<"$out")"; fail t13_reboot_armed; }; }   # the plain row's text, not the mode switch's (final review M3)
nowrun t13_reboot_one_tap "setui; @0.3; settings key reboot; settings rows; @4.5; settings rows" && {
    [ "$(nrb)" = 0 ] && [ "$(grep -c '^APP settings row=Tap again: reboot$' <<<"$out")" = 1 ] && [ "$(grep -c '^APP settings row=Reboot$' <<<"$out")" = 1 ] || { EVID="reboots $(nrb) $(grep '^APP settings row=.*[Rr]eboot' <<<"$out" | tr '\n' '|')"; fail t13_reboot_one_tap; }; }
nowrun t13_reboot_late "setui; @0.3; settings key reboot; @4.5; settings key reboot; settings rows" && {
    [ "$(nrb)" = 0 ] && [ "$(grep -c '^APP settings row=Tap again: reboot' <<<"$out")" = 1 ] || { EVID="reboots $(nrb)"; fail t13_reboot_late; }; }
nowrun t13_rows "$ALLROWS" && {
    [ "$(rows_total)" = 23 ] && [ "$(grep '^APP settings row=' <<<"$out" | sed 's/^APP settings row=//; s/[:].*//; s/ (.*//' | tr '\n' '|')" = "Brightness|Theme|Menu style|Rotation|Language|Wired output|Bluetooth audio|Crossfade|Amiga tone|Toy sounds|Haptics|Sleep|Rescan library|Cover cache|Card diagnostics|USB drive mode|Wi-Fi|Rate test|Experimental formats|Experimental CPU clock|Set time|Gesture tour|Reboot|" ] || { EVID="$(grep '^APP settings row' <<<"$out" | sed 's/^APP settings //' | tr '\n' '|')"; fail t13_rows; }; }
nowrun t13_help_rgu "uipush help; @0.3; help show" && {
    grep -Eq '^APP explain id=settings title=(說明|Help) paras=13 qr=1 web=0$' <<<"$out" || { EVID="$(grep '^APP explain' <<<"$out")"; fail t13_help_rgu; }; }
# --- QRL: setup_url is a list: one QR per address, primary first (cap 4); one address / none = one QR ---
NOWCFG='setup_url=https://a.example/x https://github.com/OWNER/REPO,https://gitee.com/OWNER/REPO;http://d.example/ http://e.example/' nowrun qrl_three_qrs "help open settings; @0.3; help qrs; help show" && {
    grep -qx 'APP explain qrs=4' <<<"$out" && grep -Eq '^APP explain id=settings .* qr=1 web=0$' <<<"$out" || { EVID="$(grep '^APP explain' <<<"$out")"; fail qrl_four_qrs_cap; }; }
nowrun qrl_default_two_qrs "help open settings; @0.3; help qrs" && {   # the default = GitHub + Gitee
    grep -qx 'APP explain qrs=2' <<<"$out" || { EVID="$(grep '^APP explain' <<<"$out")"; fail qrl_default_two_qrs; }; }
NOWCFG='setup_url=' nowrun qrl_empty_default_list "help open settings; @0.3; help qrs" && {
    grep -qx 'APP explain qrs=2' <<<"$out" || { EVID="$(grep '^APP explain' <<<"$out")"; fail qrl_empty_default_list; }; }

# --- fx apps T14 ---
# the edit-on-the-computer entries: the Wi-Fi settings row (never the name or the password: only 已連線 / 在電腦上設定), the radio list's and the tool-list chooser's end rows; each opens its help page
# (final review M7) the simulator's Wi-Fi fake never reports connected: only the 在電腦上設定 branch is tested here; 已連線 + the help page is the board run's step (Batch D brief)
nowrun t14_wifi_row "setui; @0.3; settings key wifi; @0.3; help show; uitop; back; @0.3; settings rows" && {
    { grep -Eqx 'APP explain id=wifi title=[^ ]+ paras=[0-9]+ qr=1 web=1' <<<"$out" && [ "$(grep -c '^APP settings row=Wi-Fi: set up on PC$' <<<"$out")" = 1 ]; } || { EVID="$(grep '^APP explain\|^APP settings row=Wi' <<<"$out" | tr '\n' '|')"; fail t14_wifi_row; }; }
t14_radio "uipush radio; @0.3; uiboxes; help tap; @0.3; help show; uitop"
t14_prev=$(t14_last "$out")
[ "$t14_prev" = "Edit stations on PC" ] || { EVID="last row: $t14_prev"; fail t14_radio_entry_last_row; }
grep -Eqx 'APP explain id=radio title=[^ ]+ paras=[0-9]+ qr=0 web=1' <<<"$out" && ! grep -q '^APP ui leak' <<<"$out" || { EVID="$(grep '^APP explain' <<<"$out")"; fail t14_radio_entry; }
t14_radio "uipush radio; @0.3; uiboxes"
set -- $(list_rows "$(grep '^APP uibox' <<<"$out")" | head -1)
t14_radio "uipush radio; @0.3; simtap $(( ($1 + $3) / 2 )) $(( ($2 + $4) / 2 )); @0.3"
grep -qx 'APP pick radio 0' <<<"$out" || { EVID="$(grep '^APP pick' <<<"$out")"; fail t14_radio_entry_plays; }
t14_lists_fix
out=$($SIM --headless --root "$T/fix" --cmd "tools lunch; @0.5; uitop; uiboxes; help tap; @0.3; help show; uitop" 2>&1)
t14_prev=$(t14_last "$out")
[ "$t14_prev" = "Edit lists on PC" ] || { EVID="last row: $t14_prev"; fail t14_lists_entry_last_row; }
{ grep -qx 'APP uitop tools_lists' <<<"$out" && grep -Eqx 'APP explain id=lists title=[^ ]+ paras=[0-9]+ qr=0 web=1' <<<"$out" && [ "$(tp)" = "tools_lists explain " ]; } || { EVID="$(grep '^APP explain\|^APP uitop' <<<"$out" | tr '\n' '|')"; fail t14_lists_entry; }
nowrun t14_rows "$ALLROWS" && {
    [ "$(rows_total)" = 23 ] && [ "$(grep -c '^APP settings row=Wi-Fi: set up on PC$' <<<"$out")" = 1 ] || { EVID="$(grep '^APP settings row' <<<"$out" | sed 's/^APP settings //' | tr '\n' '|')"; fail t14_rows; }; }
# AMIGA: the row exists and reads Off by default
nowrun amiga_row "setui; @0.3; settings group sound; settings rows" && {
    grep -qi '^APP settings row=Amiga tone: off$' <<<"$out" || { EVID="$(grep '^APP settings row=Amiga' <<<"$out")"; fail amiga_row; }; }

# --- fx apps T15 ---
# 說明 for the slot pay list (its two rule sentences moved behind it), the Bluetooth ring (last cell), the alarm list (last row) and the pomodoro ring (the popring's help cell)
o=$($SIM --headless --root "$T/fix" --cmd "tools slot; @0.5; simtap 180 68; @0.3; uiboxes; help tap; @0.3; help show; uitop" 2>&1)
{ grep -Eqx 'APP explain id=slot title=.* paras=2 qr=0 web=0' <<<"$o" && [ "$(grep '^APP uibox button' <<<"$o" | grep -v '|\(Back\|返回\)$' | tail -1 | sed 's/.*|//')" = "Help" ] \
    && ! grep -q '^APP uibox label .*|\(五條線\|Five lines\|Two cherries\|左起兩顆\)' <<<"$o"; } || { EVID="$(grep '^APP explain\|^APP uibox' <<<"$o" | cut -c1-100 | tr '\n' '|')"; fail t15_slot_help; }
nowrun t15_bt_help_cell "btui; bt tap $((BTN - 1)); @0.3; help show; uitop" && {
    { grep -Eqx 'APP explain id=bt title=[^ ]+ paras=[0-9]+ qr=0 web=0' <<<"$out" && [ "$(tp)" = "explain " ] && [ "$(nrb)" = 0 ]; } || { EVID="$(grep '^APP explain' <<<"$out") top $(tp) reboots $(nrb)"; fail t15_bt_help_cell; }; }
nowrun t15_alarm_help "screen on; @0.5; show alarm; @0.3; help tap; @0.3; help show; uitop" && {
    { grep -Eqx 'APP explain id=alarm title=[^ ]+ paras=[0-9]+ qr=0 web=0' <<<"$out" && [ "$(tp)" = "explain " ]; } || { EVID="$(grep '^APP explain' <<<"$out") top $(tp)"; fail t15_alarm_help; }; }
nowrun t15_alarm_rows_kept "screen on; @0.5; alarm 7 0; show alarm; @0.3; alarm tap 0; alarm tap 2; alarm tap 3; alarm st" && {
    [ "$(grep -c '^APP alarm on=' <<<"$out")" = 1 ] && [ "$(tp)" != "explain " ] || { EVID="$(grep '^APP alarm' <<<"$out")"; fail t15_alarm_rows_kept; }; }
nowrun t15_pomo_help_idle "$PM; popring -2; popring 2; @0.3; help show; uitop" && {
    { grep -Eqx 'APP explain id=pomo title=[^ ]+ paras=[0-9]+ qr=0 web=0' <<<"$out" && [ "$(tp)" = "explain " ] && [ "$(ps)" = "" ]; } || { EVID="$(grep '^APP explain' <<<"$out") top $(tp) $(grep '^APP popring' <<<"$out" | tr '\n' '|')"; fail t15_pomo_help_idle; }; }
nowrun t15_pomo_help_run "$PM; popring -2; popring -1; popring 3; @0.3; help show; uitop; pomo stat" && {
    { grep -Eqx 'APP explain id=pomo title=[^ ]+ paras=[0-9]+ qr=0 web=0' <<<"$out" && [ "$(ps)" = "run " ]; } || { EVID="$(grep '^APP explain' <<<"$out") state $(ps)"; fail t15_pomo_help_run; }; }
nowrun t15_pomo_help_run_stop_cell "$PM; popring -2; popring -1; popring 0; popring -5; popring 0; pomo stat" && {
    [ "$(ps)" = "idle " ] || { EVID="state $(ps) $(grep '^APP popring' <<<"$out" | tr '\n' '|')"; fail t15_pomo_help_run_stop_cell; }; }

# --- hires T8b ---
# Batch B board findings, in code the simulator does not build (bsp_audio.c, audio_svc.c): one scan per fix.
# settle: the 20 ms PLL settle after a clock change is spent writing silence (the ring is full when the first block comes, so the first FLAC frame's
# decode at 96k no longer underruns), the old idle delay is gone, and the stale ring is zeroed before the enable (never replayed at the new clock)
{ ! grep -q 'vTaskDelay(pdMS_TO_TICKS(20)); /\* PCM5102A PLL lock \*/' $BA && grep -q 'settle_and_measure(sample_rate, row.src == PC_CLK_APLL)' $BA && grep -q 'e = settle_and_measure(rate, row->src == PC_CLK_APLL);' $BA && grep -q '^    settle_silence(rate);' $BA && grep -q '^static void settle_silence(uint32_t rate)' $BA \
  && z=$(grep -n 'if (e == ESP_OK) ring_zero();' $BA | cut -d: -f1) && en=$(grep -n 'e2 = i2s_channel_enable(s_tx);' $BA | head -1 | cut -d: -f1) && [ -n "$z" ] && [ -n "$en" ] && [ "$z" -lt "$en" ]; } || fail hr8b_settle_writes
# reshape: a table-shaped channel takes the row's DMA shape at a rate change (8 x 480 at 88.2/96k, 6 x 480 at <= 48k) and at a reinit; a caller's own shape is kept
{ grep -q 'if (follows && (row.desc != s_desc_cfg || row.frames != s_frames_cfg || row.src != s_src)) return reshape(sample_rate, &row, s_bits);' $BA \
  && grep -q 'esp_err_t e = init_shape(rate, fol ? row.desc : s_desc_cfg, fol ? row.frames : s_frames_cfg, want_bits(rate));' $BA; } || fail hr8b_reshape
# a failed reshape leaves no channel: the audio task treats it as released, so the reinit brings it back at the rate the switch settles on (never silent for good)
grep -q 'if (!bsp_audio_is_init() && !s_released) { s_released = true; P.t_reinit = 0; }' $AS || fail hr8b_reshape_lost
# idle audio info: no source, no plan fields (as the fake)
grep -q '\.pipe_why = S\.h || S\.mix ? P\.plan\.why : PC_PIPE_NATIVE, \.halvings = S\.h || S\.mix ? P\.plan\.halvings : 0,' $AS || fail hr8b_info_idle_plan
# H27: the chain and the list ask the per-app rule (the limiter stays on under HIRES2), never the class rule alone
{ grep -q 'pc_fx_app_allowed(ap, mode)' components/player_core/src/pc_fxchain.c && grep -q 'pc_fx_app_allowed(ap, mode)' app/main/fx_state.c && ! grep -q 'pc_fx_allowed((pc_fxclass_t)ap->cls' components/player_core/src/pc_fxchain.c app/main/fx_state.c; } || fail hr8b_fx_limiter

# --- hires T11 ---
# the rate test (pc_ratetest host-tested in test_ratetest; rate_test.c on the fake audio service with the fake DAC `simdac`): a mixer per tone, the listener taps 200 ms after tone 2 of a
# tier it can play; this build's tiers are 48k, 96k, 192k and 384k (T12, T13) (hw_cap = PC_AUDIO_MAX_RATE). The result is the last confirmed tier, stored on TOP / TIMEOUT / FAULT only.
nowrun hr11_ratetest_flow "simdac 384000; setui; @0.3; settings key ratetest; @0.3; uitop; @16; cfg dac_max_rate; ratetest show" && {
    p=$(rt)
    { grep -qx 'APP uitop ratetest' <<<"$out" && [ "$p" = "APP ratetest tier=48000 tone=880|APP ratetest tier=48000 tone=1320|APP ratetest confirm tier=48000|APP ratetest tier=96000 tone=880|APP ratetest tier=96000 tone=1320|APP ratetest confirm tier=96000|APP ratetest tier=192000 tone=880|APP ratetest tier=192000 tone=1320|APP ratetest confirm tier=192000|APP ratetest tier=384000 tone=880|APP ratetest tier=384000 tone=1320|APP ratetest confirm tier=384000|APP ratetest end result=384000 why=top|APP ratetest state=done tier=384000 confirmed=384000 tiers=4 end=top refused=0|" ] && grep -qx 'APP cfg dac_max_rate=384000' <<<"$out" && grep -qx 'APP audio cap=384000' <<<"$out"; } || { EVID="$p"; fail hr11_ratetest_flow; }; }
nowrun hr11_ratetest_timeout "simdac 48000; setui; @0.3; settings key ratetest; @12; cfg dac_max_rate" && {
    p=$(rt)
    { grep -qx 'APP ratetest end result=48000 why=timeout' <<<"$out" && grep -qx 'APP cfg dac_max_rate=48000' <<<"$out" && ! grep -q 'confirm tier=96000' <<<"$out" && grep -qx 'APP audio cap=48000' <<<"$out"; } || { EVID="$p"; fail hr11_ratetest_timeout; }; }
# nobody taps: nothing confirmed, 8 s after the start, and the setting is not written
nowrun hr11_ratetest_timeout_none "setui; @0.3; settings key ratetest; @9; cfg dac_max_rate" && {
    p=$(rt)
    { grep -qx 'APP ratetest end result=0 why=timeout' <<<"$out" && ! grep -q '^APP audio cap=' <<<"$out" && ! grep -q '^APP ratetest confirm' <<<"$out"; } || { EVID="$p $(grep '^APP audio cap' <<<"$out")"; fail hr11_ratetest_timeout_none; }; }
# the output underruns in tier 2's tone 2 (the fake DAC cannot keep up at 96k): FAULT, the result is the tier before
nowrun hr11_ratetest_fault "simdac 384000; simdac fault 96000; setui; @0.3; settings key ratetest; @12; cfg dac_max_rate" && {
    p=$(rt)
    { grep -qx 'APP ratetest end result=48000 why=fault' <<<"$out" && grep -qx 'APP cfg dac_max_rate=48000' <<<"$out" && ! grep -q 'why=top' <<<"$out"; } || { EVID="$p"; fail hr11_ratetest_fault; }; }
# a fault in the first tier confirms nothing: nothing is stored
nowrun hr11_ratetest_fault_first "simdac 384000; simdac fault 48000; setui; @0.3; settings key ratetest; @6; ratetest show" && {
    p=$(rt)
    { grep -qx 'APP ratetest end result=0 why=fault' <<<"$out" && ! grep -q '^APP audio cap=' <<<"$out"; } || { EVID="$p"; fail hr11_ratetest_fault_first; }; }
# Review Focus 5: left half-way with a library track playing: the tone stops, nothing is stored, the track resumes once at its position
nowrun hr11_ratetest_leave_mid_tier "screen on; vol 0; @0.5; mp3ui; @0.3; mp3 1; @1.5; ratetest start; @1; back; @1; uistack" && {
    p=$(rt)
    n=$(grep -c '^APP track idx=' <<<"$out"); i=$(sed -n 's/^APP track idx=\([0-9]*\) .*/\1/p' <<<"$out" | sort -u | tr '\n' ' ' | xargs)
    { grep -qx 'APP ratetest end result=0 why=left' <<<"$out" && [ "$(grep -c '^APP ratetest end' <<<"$out")" = 1 ] && ! grep -q '^APP audio cap=' <<<"$out" && [ "$n" = 2 ] && [ "$(wc -w <<<"$i")" = 1 ] \
      && [ "$(grep -c '^APP mp3 resume at byte [1-9]' <<<"$out")" = 1 ] && [ "$(grep -A9 '^APP ratetest end' <<<"$out" | grep -c "^APP track idx=$i ")" = 1 ]; } || { EVID="$p tracks=$n idx=$i $(grep '^APP mp3 resume' <<<"$out" | tr '\n' '|')"; fail hr11_ratetest_leave_mid_tier; }; }
# a play command of another owner (the console / the web) ends it as left and is not played over: its track plays, the old one does not come back
nowrun hr11_ratetest_play_cmd_ends "screen on; vol 0; @0.5; mp3ui; @0.3; mp3 1; @1; ratetest start; @1; mp3 2; @1; ratetest show" && {
    p=$(rt); i=$(sed -n 's/^APP track idx=\([0-9]*\) .*/\1/p' <<<"$out" | tr '\n' ' ')
    { grep -qx 'APP ratetest end result=0 why=left' <<<"$out" && [ "$(grep -c '^APP ratetest end' <<<"$out")" = 1 ] && ! grep -q '^APP audio cap=' <<<"$out" && [ "$(wc -w <<<"$i")" = 2 ] && [ "$(tr ' ' '\n' <<<"$i" | sort -u | grep -c .)" = 2 ] && grep -q 'APP ratetest state=done .* end=left' <<<"$out"; } || { EVID="$p tracks=$i"; fail hr11_ratetest_play_cmd_ends; }; }
# a Bluetooth sink: refused with the reason, nothing plays; one that comes during the run ends it as left
nowrun hr11_ratetest_bt_refused "simsink 1; ratetest start; @0.5; uitop; ratetest show; ratetest tap" && {
    p=$(rt)
    { [ "$(grep -c '^APP ratetest refused: bluetooth$' <<<"$out")" = 1 ] && ! grep -q '^APP ratetest tier=' <<<"$out" && grep -qx 'APP uitop ratetest' <<<"$out" && grep -qx 'APP ratetest state none refused=1' <<<"$out"; } || { EVID="$p"; fail hr11_ratetest_bt_refused; }; }
nowrun hr11_ratetest_bt_arrives "simdac 192000; ratetest start; @1; simsink 1; @0.3; ratetest show; simsink 0" && {
    p=$(rt)
    { grep -qx 'APP ratetest end result=0 why=left' <<<"$out" && ! grep -q '^APP audio cap=' <<<"$out"; } || { EVID="$p"; fail hr11_ratetest_bt_arrives; }; }
# taps before tone 2 are ignored; a second start while running changes nothing; stop twice is one end line; the 說明 page over it ends it (the page is not seen)
nowrun hr11_ratetest_tap_rules "simdac 0; ratetest start; @0.5; ratetest tap; @0.3; ratetest start; @1.4; ratetest tap; @0.5; ratetest stop; ratetest stop; ratetest tap" && {
    p=$(rt)
    { [ "$p" = "APP ratetest tier=48000 tone=880|APP ratetest tier=48000 tone=1320|APP ratetest confirm tier=48000|APP ratetest tier=96000 tone=880|APP ratetest end result=0 why=left|" ]; } || { EVID="$p"; fail hr11_ratetest_tap_rules; }; }
nowrun hr11_ratetest_help_ends "simdac 192000; ratetest start; @1; help tap; @0.3; uitop; help show" && {
    p=$(rt)
    { grep -qx 'APP ratetest end result=0 why=left' <<<"$out" && grep -qx 'APP uitop explain' <<<"$out" && grep -q '^APP explain id=ratetest ' <<<"$out"; } || { EVID="$p $(grep '^APP uitop\|^APP explain' <<<"$out" | tr '\n' '|')"; fail hr11_ratetest_help_ends; }; }
# the page: note, row, help row (the settings row opens it; Back leaves it)
nowrun hr11_ratetest_settings_row "setui; @0.3; settings group advanced; settings rows; settings key ratetest; @0.3; uitop; back; @0.3; uitop" && {
    { grep -q '^APP settings row=取樣率測試$\|^APP settings row=Rate test$' <<<"$out" && grep -qx 'APP uitop ratetest' <<<"$out" && [ "$(grep -c '^APP uitop setgroup' <<<"$out")" = 1 ] && grep -qx 'APP ratetest end result=0 why=left' <<<"$out"; } || { EVID="$(grep '^APP uitop\|^APP settings row=' <<<"$out" | tr '\n' '|')"; fail hr11_ratetest_settings_row; }; }
# the test never changes the ceiling of the playing track (it applies at the next track, T6), and `show` before any start says none
nowrun hr11_ratetest_show_none "ratetest show" && { grep -qx 'APP ratetest state none refused=0' <<<"$out" || fail hr11_ratetest_show_none; }
# call sites the sim cannot run (the real audio_svc.c) or that the sim does not discriminate: the result is stored only for a run that confirmed a tier and did not end as left, the tone state is a PSRAM block
# (a block under 1 KB would otherwise be internal), the underrun baseline is taken after the tone's own rate switch, the sink and the clock ceiling are watched, only a library track is resumed (once, by the owner path)
RT=app/main/rate_test.c
{ grep -q 'bool store = s_rt.end != PC_RT_END_LEFT && s_rt.confirmed > 0;' $RT && grep -q 'heap_caps_malloc(sizeof \*t, MALLOC_CAP_SPIRAM)' $RT && grep -q 'pc_ratetest_baseline_at(&s_rt, now)' $RT \
  && grep -q 'a.hw_cap < s_hw0' $RT && grep -q 'bool resume = s_lib && ours;' $RT && grep -q 'mp3_ctl_play_folder_track_at(s_lib_folder, s_lib_idx, s_lib_byte)' $RT && grep -q 'if (a.sink) {' $RT; } || fail hr11_call_sites
grep -q 'rate_test.c' app/main/CMakeLists.txt && grep -q 'rate_test.c' sim/CMakeLists.txt || fail hr11_build_lists
# fix round 1: the tier's baseline is taken 300 ms in, after the reconfigure settled: an underrun at the tier's start (the reconfigure's own) never faults, one after the baseline does;
# the DAC clock ceiling dropping faults at once (simhw); a paused library track comes back paused; a rebuild of every page (theme) does not restart a run under another page
nowrun hr11_ratetest_glitch_start_ok "simdac 384000; simdac glitch; setui; @0.3; settings key ratetest; @16" && {
    p=$(rt)
    { grep -qx 'APP ratetest end result=384000 why=top' <<<"$out"; } || { EVID="$p"; fail hr11_ratetest_glitch_start_ok; }; }
nowrun hr11_ratetest_glitch_mid_faults "simdac 192000; simdac glitch; ratetest start; @0.6; simdac glitch; @0.5; ratetest show" && {
    p=$(rt)
    { grep -qx 'APP ratetest end result=0 why=fault' <<<"$out" && ! grep -q '^APP audio cap=' <<<"$out"; } || { EVID="$p"; fail hr11_ratetest_glitch_mid_faults; }; }
nowrun hr11_ratetest_hw_drop "simdac 192000; ratetest start; @0.5; simhw 48000; @0.3; simhw 96000" && {
    p=$(rt)
    { grep -qx 'APP ratetest end result=0 why=fault' <<<"$out" && ! grep -q '^APP audio cap=' <<<"$out"; } || { EVID="$p"; fail hr11_ratetest_hw_drop; }; }
nowrun hr11_ratetest_paused_resumes_paused "screen on; vol 0; track 3; @3; $NOWMENU; $PAUSE; nowmenu -3; ratetest start; @1; back; @1; nowmenu -3" && {
    pz=$(grep '^APP now paused' <<<"$out" | tr '\n' '|'); p=$(rt)
    { [ "$pz" = "APP now paused=1 audio=paused|APP now paused=1 audio=paused|" ] && grep -qx 'APP ratetest end result=0 why=left' <<<"$out"; } || { EVID="$pz $p"; fail hr11_ratetest_paused_resumes_paused; }; }
nowrun hr11_ratetest_rebuild_keeps_run "simdac 0; setui; @0.3; settings key ratetest; @1; help tap; @0.3; ratetest show; theme next; @0.3; back; @0.3; uitop; ratetest show" && {
    p=$(rt); sh=$(grep '^APP ratetest state' <<<"$out" | sort -u | wc -l)
    { [ "$(grep -c '^APP ratetest tier=' <<<"$out")" = 1 ] && [ "$(grep -c '^APP ratetest end' <<<"$out")" = 1 ] && [ "$sh" = 1 ] && grep -q '^APP ui rebuilt ' <<<"$out" && grep -qx 'APP uitop ratetest' <<<"$out"; } || { EVID="$p"; fail hr11_ratetest_rebuild_keeps_run; }; }
# the pictures of the page (tools/simtools/ui_baseline.py) and their boxes
nowrun hr11_ratetest_boxes "simdac 192000; ratetest start; @1; uiboxes" && { ! grep '^APP uibox label ' <<<"$out" | grep -qi 'knob\|旋鈕\|text too long' || { EVID="$(grep '^APP uibox label' <<<"$out" | head -3 | tr '\n' '|')"; fail hr11_ratetest_boxes; }; }
python3 tools/check/check_font_glyphs.py >"$T/gl11.out" 2>&1 || { EVID="$(tail -3 "$T/gl11.out" | tr '\n' '|')"; fail hr11_font_glyphs; }

# --- bubbles (捏泡泡紙: the shared bubble field + its page, the internal notes) ---
# dump: "APP bubbles n=<total> left=<left> special=<i>", then one "APP bubbles row <r> <1 raised / 0 popped ...>" per row; "bubbles tap <i>" pops like a touch (and dumps), "bubbles pop <i>" the same without the dump, "bubbles at <i>" prints the bubble's screen centre.
# The special bubble comes from the shared generator (ui_rng): seeded in the simulator (--seed, default 1), and a re-lay never repeats the previous special, so the change across sheets does not depend on the seed.
nowrun bubbles_open "screen on; tools bubbles; @0.3; bubbles; uitop; uistack" && {
    n=$(bbv 1 n); l=$(bbv 1 left); s=$(bbv 1 special)
    [ "${n:-0}" -ge 40 ] && [ "$l" = "$n" ] && [ "${s:--1}" -ge 0 ] && [ "$s" -lt "$n" ] && [ "$(tp)" = "tools_bubbles " ] && stack_is bubbles_stack 1 "menu tools tools_bubbles" || { EVID="$(bb 1) top $(tp)"; fail bubbles_open; }
    rows=$(grep -c '^APP bubbles row ' <<<"$out"); ones=$(grep '^APP bubbles row ' <<<"$out" | cut -d' ' -f5 | tr -cd 1 | wc -c)
    [ "$rows" -ge 5 ] && [ "$ones" = "$n" ] || { EVID="rows $rows ones $ones n $n"; fail bubbles_rows_dump; }
    BB_N=$n; BB_S=$s; }
nowrun bubbles_tap_pops "screen on; tools bubbles; @0.3; bubbles tap 0; bubbles tap 0; bubbles tap $((BB_N - 1)); bubbles tap 999; bubbles tap -1" && {
    [ "$(bbv 1 left)" = "$((BB_N - 1))" ] && [ "$(bbv 2 left)" = "$((BB_N - 1))" ] && [ "$(bbv 3 left)" = "$((BB_N - 2))" ] && [ "$(bbv 4 left)" = "$((BB_N - 2))" ] && [ "$(bbv 5 left)" = "$((BB_N - 2))" ] || { EVID="$(bb 1)|$(bb 2)|$(bb 3)|$(bb 4)|$(bb 5)"; fail bubbles_tap_pops; }
    :; }
# a real touch on a bubble's centre pops exactly that one (hit test in the piece; the state characters of the row dump say which), a touch in the gap pops the NEAREST bubble,
# a touch farther than half a pitch from every centre (a screen corner) pops nothing, a touch on a popped bubble pops nothing
nowrun bubbles_at "screen on; tools bubbles; @0.3; bubbles at 3; bubbles at 0; bubbles at 1" && {
    a3=$(grep '^APP bubbles at 3 ' <<<"$out" | sed 's/.*x=\([0-9]*\),y=\([0-9]*\)$/\1 \2/'); [ -n "$a3" ] || { EVID="$(grep '^APP bubbles at' <<<"$out")"; fail bubbles_at_format; }
    a0=$(grep '^APP bubbles at 0 ' <<<"$out" | sed 's/.*x=\([0-9]*\),y=\([0-9]*\)$/\1 \2/'); set -- $a0; gx=$(($1 + 18)); gy=$2   # 18 px right of bubble 0 toward bubble 1 (26 px from it): nearer to 0
    nowrun bubbles_touch "screen on; tools bubbles; @0.3; simtap $a3; @0.1; bubbles; simtap $a3; @0.1; bubbles; bubbles tap 3; bubbles" && {
        want3=$(printf '1%.0s' $(seq 1 $BB_N)); want3="${want3:0:3}0${want3:4}"
        [ "$(bbv 1 left)" = "$((BB_N - 1))" ] && [ "$(bbv 2 left)" = "$((BB_N - 1))" ] && [ "$(bbv 3 left)" = "$((BB_N - 1))" ] || { EVID="$(bb 1)|$(bb 2)|$(bb 3)"; fail bubbles_touch; }
        [ "$(bbs 1)" = "$want3" ] && [ "$(bbs 2)" = "$want3" ] && [ "$(bbs 3)" = "$want3" ] || { EVID="want $want3 got $(bbs 1)|$(bbs 2)"; fail bubbles_touch_row_state; }; }
    nowrun bubbles_touch_gap "screen on; tools bubbles; @0.3; simtap $gx $gy; @0.1; bubbles; simtap 2 2; @0.1; bubbles; simtap 357 357; @0.1; bubbles" && {
        want0=$(printf '1%.0s' $(seq 1 $BB_N)); want0="0${want0:1}"
        [ "$(bbv 1 left)" = "$((BB_N - 1))" ] && [ "$(bbs 1)" = "$want0" ] && [ "$(bbs 2)" = "$want0" ] && [ "$(bbs 3)" = "$want0" ] && [ "$(bbv 3 left)" = "$((BB_N - 1))" ] || { EVID="gap $gx $gy: $(bb 1) $(bbs 1)|$(bbs 2)|$(bbs 3)"; fail bubbles_touch_gap_corner; }; }; }
# every bubble popped: the sheet is laid again after a short pause (not at once), full, the special another one; endless
taps=$(for ((i = 0; i < BB_N; i++)); do printf 'bubbles pop %d; ' $i; done)
nowrun bubbles_relay "screen on; tools bubbles; @0.3; bubbles; ${taps}bubbles; @0.1; bubbles; @0.5; bubbles; ${taps}@0.5; bubbles; ${taps}@0.5; bubbles; uitop" && {
    [ "$(bbv 2 left)" = 0 ] && [ "$(bbv 3 left)" = 0 ] && [ "$(bbv 4 left)" = "$BB_N" ] || { EVID="$(bb 2)|$(bb 3)|$(bb 4)"; fail bubbles_relay_pause; }
    s0=$(bbv 1 special); s1=$(bbv 4 special); s2=$(bbv 5 special); s3=$(bbv 6 special)
    [ "$(bbv 5 left)" = "$BB_N" ] && [ "$(bbv 6 left)" = "$BB_N" ] && [ "$s0" != "$s1" ] && [ "$s1" != "$s2" ] && [ "$s2" != "$s3" ] && [ "$(tp)" = "tools_bubbles " ] || { EVID="$(bb 4)|$(bb 5)|$(bb 6) s0=$s0"; fail bubbles_relay_special_changes; }; }
# Back (G4b: the shared 返回 pill after a knob turn) returns to the tools ring; 10 cells now (遊戲, 音效庫) = 2 pages
nowrun bubbles_back "screen on; vol 0; tools bubbles; @0.6; uistack; simknob 1; @0.3; frame tap back; @0.3; uistack; tools ring" && {
    [ "$(us)" = "menu tools tools_bubbles|menu tools|" ] || { EVID="$(us)"; fail bubbles_back; }
    [ "$(grep '^APP tools ring ' <<<"$out")" = "APP tools ring items=14 page=1 pages=2" ] || { EVID="$(grep '^APP tools ring' <<<"$out")"; fail bubbles_ring_eight; }; }
# the music keeps playing while bubbles are popped (no change of the audio plan, no stop, no new track)
nowrun bubbles_music_plays "screen on; vol 0; track 3; @2; audio info; tools bubbles; @0.3; bubbles tap 0; bubbles tap 1; @1; bubbles tap 2; audio info; back; @0.3; audio info" && {
    a=$(grep '^APP audio info ' <<<"$out" | sed 's/ underruns_src.*//' | sort -u | wc -l)
    [ "$(grep -c '^APP audio info ' <<<"$out")" = 3 ] && [ "$a" = 1 ] && ! grep -q '^APP mp3 stop\|^APP play stop' <<<"$out" && [ "$(grep -c '^APP track idx=' <<<"$out")" = 1 ] || { EVID="$(grep '^APP audio info' <<<"$out" | cut -c1-80 | tr '\n' '|')"; fail bubbles_music_plays; }; }
# the page and the piece do not hand-build UI, the field is registered in the shared-area doc
# --- end bubbles ---

# --- fx apps T17 ---
# velvet_room (compiled in, Kconfig default on) runs only with no mode bit: battery excluded (power tick), aptX / LDAC / broadcast (fx mode 2 / 4 / 8) and a Bluetooth sink (fx mode 64, or simsink) suspend it;
# the reason is the same text on the list row and the page. The app rule is the one function (pc_fx_app_allowed / pc_fx_app_why), the other HEAVY apps keep their class rule.
nowrun t17_vroom_in_build "fx state" && { grep -q '^APP fx app=velvet_room cls=heavy ' <<<"$out" && [ "$(grep -c '^APP fx app=' <<<"$out")" = 14 ] || { EVID="$(grep -c '^APP fx app=' <<<"$out")"; fail t17_vroom_in_build; }; }
nowrun t17_vroom_modes "fx on velvet_room 1; fx on freeverb 1; @0.3; fx state; fx mode 64; @0.3; fx state; fx mode 2; @0.3; fx state; fx mode 4; @0.3; fx state; fx mode 8; @0.3; fx state; fx mode 0; @0.3; fx state" && {
    { [ "$(vrs | wc -l)" = 6 ] && [ "$(vrs | sed -n 1p | grep -c ' run=1 look=run ')" = 1 ] && [ "$(vrs | sed -n 6p | grep -c ' run=1 look=run ')" = 1 ] \
      && [ "$(vrs | sed -n '2,5p' | grep -c ' run=0 look=dim ')" = 4 ] \
      && [ "$(grep '^APP fx app=freeverb ' <<<"$out" | sed -n 2p | grep -c ' run=1 look=run ')" = 1 ] && [ "$(grep '^APP fx app=freeverb ' <<<"$out" | sed -n 3p | grep -c ' run=0 look=dim ')" = 1 ]; } || { EVID="$(vrs | cut -c1-70 | tr '\n' '|')"; fail t17_vroom_modes; }; }
nowrun t17_vroom_list_page "fx on velvet_room 1; uipush fx; @0.3; fxui rows; fx mode 64; @0.3; fxui rows; fx on velvet_room 0; @0.3; fxui rows; fxpage velvet_room; @0.3; fxui rows; fx on velvet_room 1; @0.3; fxui rows" && {
    d1=$(sed -n '1,/^APP fxui end/p' <<<"$out"); rest=$(sed '1,/^APP fxui end/d' <<<"$out"); d2=$(sed -n '1,/^APP fxui end/p' <<<"$rest"); rest=$(sed '1,/^APP fxui end/d' <<<"$rest")
    d3=$(sed -n '1,/^APP fxui end/p' <<<"$rest"); rest=$(sed '1,/^APP fxui end/d' <<<"$rest"); d4=$(sed -n '1,/^APP fxui end/p' <<<"$rest"); d5=$(sed '1,/^APP fxui end/d' <<<"$rest")
    { grep -q ' look=on text=Velvet room  on$' <<<"$d1" && grep -q ' look=dim text=Velvet room  on, paused: wired playback only$' <<<"$d2" \
      && grep -q ' look=normal text=Velvet room  off: wired playback only$' <<<"$d3" \
      && grep -q '^APP fxui row=0 look=normal text=Off: wired playback only$' <<<"$d4" \
      && grep -q '^APP fxui row=0 look=dim text=On, paused: wired playback only$' <<<"$d5"; } || { EVID="$(grep -h 'Velvet\|row=0 ' <<<"$out" | cut -c1-90 | tr '\n' '|')"; fail t17_vroom_list_page; }; }
# the audio service's own view of the sink sets the BT bit (the tick, as for the rate bits) and drops it again
nowrun t17_vroom_sink_bit "fx on velvet_room 1; @0.3; simsink 1; @0.3; fx state; simsink 0; @0.3; fx state" && {
    { [ "$(grep '^APP fx state n=' <<<"$out" | sed -n 1p | grep -c ' mode=64 ')" = 1 ] && [ "$(grep '^APP fx state n=' <<<"$out" | sed -n 2p | grep -c ' mode=0 ')" = 1 ] \
      && [ "$(vrs | sed -n 1p | grep -c ' run=0 look=dim ')" = 1 ] && [ "$(vrs | sed -n 2p | grep -c ' run=1 look=run ')" = 1 ]; } || { EVID="$(grep '^APP fx state' <<<"$out" | cut -c1-40 | tr '\n' '|') $(vrs | cut -c1-70 | tr '\n' '|')"; fail t17_vroom_sink_bit; }; }
# the reason comes from the one per-app function, in the chain, the list and the page alike
{ grep -q 'pc_fx_app_why(ap, mode, zh)' app/main/fx_state.c && ! grep -q 'pc_fx_mode_why(mode' app/main/fx_state.c && grep -q 'PC_FXA_WIRED_ONLY' app/main/ui/ui_fxparam.c; } || fail t17_vroom_one_rule
grep -q '^CONFIG_FX_VELVET_ROOM=y$' app/sdkconfig.defaults || fail t17_vroom_kconfig_default
# --- end fx apps T17 ---
