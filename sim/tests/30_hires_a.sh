# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: need_btn)
need_btn
# --- hires T2 ---
nowrun hr2_tfread_na "tfread /x" && { grep -qx 'SIM n/a tfread /x' <<<"$out" || { EVID="$out"; fail hr2_tfread_na; }; }
# --- fx apps T9 ---
# the velvet widener's length (a REBUILD value) spun with the knob. The row is found by its label. After the tap a wait lets the borrowed knob come up (the screen may be off:
# the first knob event only wakes it); simknob 12 from 25 ms in x1.12 steps (one tap step already taken: 28) clamps at 50. Every turn is one swap; at the end one block is live
# and the mailbox and the return ring are empty again.
nowrun t9_vwide_rows "fx on velvet_widener 1; fxpage velvet_widener; @0.3; fxui rows" && {
    VW9=$(sed -n 's/^APP fxui row=\([0-9]*\) look=[a-z]* text=Length: .*/\1/p' <<<"$out"); { [ -n "$VW9" ] && [ "$(wc -l <<<"$VW9")" = 1 ]; } || { EVID="$(grep '^APP fxui row' <<<"$out" | tr '\n' '|')"; fail t9_vwide_rows; }; }
nowrun t9_vwide_length_spin "fx on velvet_widener 1; fxpage velvet_widener; fxui tap ${VW9:-2}; @0.5; simknob 12; @0.5; fx state; fx stat" && {
    { grep '^APP fx app=velvet_widener ' <<<"$out" | grep -q ' length_ms=50 ' && grep -q '^APP fx stat mail_free=32 ret_free=16 blocks=1 live=1 ' <<<"$out"; } || { EVID="$(grep '^APP fx app=velvet_widener ' <<<"$out" | cut -c1-200) $(grep '^APP fx stat ' <<<"$out")"; fail t9_vwide_length_spin; }; }

# --- hires T5 ---
# native 88.2/96 kHz: the fake takes the firmware's plan (pc_src_rate + pc_pipe_plan with out_max = pc_out_max_rate(WIRED, cap)) and prints the board's APP pipe line once per plan change
nowrun hr5_96k_plays "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.5" && {
    { ! grep -q 'APP play refused' <<<"$out" && [ "$(grep -c '^APP track idx=' <<<"$out")" = 1 ] && grep -qx 'APP pipe src=96000 pipe=96000 halvings=0 why=native' <<<"$out"; } || { EVID="$(grep '^APP pipe\|refused' <<<"$out" | tr '\n' '|')"; fail hr5_96k_plays; }; }
nowrun hr5_192k_halved "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 192000 1; mp3 1; @0.5" && {
    { ! grep -q 'APP play refused' <<<"$out" && grep -qx 'APP pipe src=192000 pipe=96000 halvings=1 why=cap' <<<"$out"; } || { EVID="$(grep '^APP pipe\|refused' <<<"$out" | tr '\n' '|')"; fail hr5_192k_halved; }; }
# LOWRATE: one rule for both boards (the round board's table: any rate up to 48k, then 88.2 .. 384k): a 22.05k source stays native, an odd 99.999k source is resampled to 96k (was refused: odd above the ceiling), 7000 Hz is native here
nowrun lowrate_round_22k_native "screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 22050 1; mp3 1; @0.5" && {
    { ! grep -q 'APP play refused\|APP dac rate:' <<<"$out" && grep -qx 'APP pipe src=22050 pipe=22050 halvings=0 why=native' <<<"$out"; } || { EVID="$(grep '^APP pipe\|refused\|dac rate' <<<"$out" | tr '\n' '|')"; fail lowrate_round_22k_native; }; }
nowrun lowrate_round_odd_resampled "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 99999 1; mp3 1; @0.5" && {
    { ! grep -q 'APP play refused\|APP dac rate:' <<<"$out" && grep -qx 'APP pipe src=99999 pipe=96000 halvings=0 why=native' <<<"$out"; } || { EVID="$(grep '^APP pipe\|refused\|dac rate' <<<"$out" | tr '\n' '|')"; fail lowrate_round_odd_resampled; }; }
# FIX3 I2: the odd 99.999 kHz source lands on 96k at every ceiling from 96k up (the plan's 48k no-clock-row ceiling no longer limits the pick), 48k at a 48k ceiling
for cap in 192000 384000; do
nowrun lowrate_round_odd_cap$cap "seti dac_max_rate $cap; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 99999 1; mp3 1; @0.5" && {
    { ! grep -q 'APP play refused\|APP dac rate:' <<<"$out" && grep -qx 'APP pipe src=99999 pipe=96000 halvings=0 why=native' <<<"$out"; } || { EVID="$(grep '^APP pipe\|refused\|dac rate' <<<"$out" | tr '\n' '|')"; fail lowrate_round_odd_cap$cap; }; }
done
# FIX3 source scans (the real audio_svc.c / radio_svc.c are not built into the simulator)
RS=app/main/radio_svc.c
{ grep -q 'PC_UP_SBUF_FRAMES' $AS && grep -q 'pc_up_want(P.up_carry, frames, P.up_in, s_rate)' $AS && ! grep -q 'pc_up_need(frames, P.up_in, s_rate) - P.up_carry' $AS; } || fail fix3_sbuf_bound
{ grep -q 'const bool spx = S.o.speed && spd != 100 && !S.paused && s_rate' $AS && ! grep -q '!S.paused && !up' $AS && grep -q 'if (up && !spx)' $AS && grep -q 'else if (!spx && sb != buf)' $AS; } || fail fix3_speed_after_rate_stage
{ grep -q 'plan_builds_table' $AS && grep -q 'if (build) bsp_audio_mute(true);' $AS && grep -q 'PC_RSQ_TAPS / 2 < room' $AS && grep -q 's_low_rate = raw;' $AS; } || fail fix3_audio_minors
{ grep -q '#error "radio_svc.c needs CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC' $RS && grep -q 'return PC_TRY_STOP' $RS && grep -q 'I4: a station switch before the open' $RS && grep -q 's_got = true;                                 /\* I4' $RS && ! grep -q 's_twin_cache' $RS && grep -q 'pc_url_redact' $RS && grep -q 'APP_RADIO_TEST_HOOKS' $RS; } || fail fix3_radio
python3 tools/check_sdkconfig.py >/dev/null || fail fix3_sdkconfig_pins
grep -q '^CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=16' app/sdkconfig.defaults && grep -q '^CONFIG_ESP_WIFI_RX_BA_WIN=16' app/sdkconfig.defaults && grep -q '^CONFIG_LWIP_TCP_OOSEQ_MAX_PBUFS=0' app/sdkconfig.defaults || fail fix3_wifi_pins
nowrun hr5_384k_refused "screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 768000 1; mp3 1; @0.5" && {
    { grep -qx 'APP play refused rate=768000 max=384000' <<<"$out" && grep -A50 '^APP play refused rate=768000' <<<"$out" | grep -q '^APP skip reason=decode idx=7$'; } || { EVID="$(grep 'refused\|^APP skip' <<<"$out" | tr '\n' '|')"; fail hr5_384k_refused; }; }
# a sink (Bluetooth) while 96k plays: the pipeline is re-planned to 48k (one halving, why=sink) and back to 96k when it clears, one APP pipe line per change
nowrun hr5_sink_replan "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.5; simsink 1; @0.2; simsink 1; simsink 0; @0.2; simsink 2" && {
    p=$(grep '^APP pipe ' <<<"$out" | tr '\n' '|')
    { [ "$p" = "APP pipe src=96000 pipe=96000 halvings=0 why=native|APP pipe src=96000 pipe=48000 halvings=1 why=sink|APP pipe src=96000 pipe=96000 halvings=0 why=native|" ] && grep -qx 'SIM sink bad' <<<"$out"; } || { EVID="$p"; fail hr5_sink_replan; }; }

# --- hires T5 fix 1 ---
# audio info: the console prints aud_info_t (the fake: the real plan, a DAC at exactly 64 fs); with a sink the plan is 48k and the I2S output reads 0
nowrun hr5f_audio_info "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 192000 1; mp3 1; @0.5; audio info; simsink 1; audio info; simsink 0" && {
    a=$(grep '^APP audio info ' <<<"$out" | tr '\n' '|')
    [ "$a" = "APP audio info src=192000 pipe=96000 out=96000 bclk=6144000 hw_cap=384000 cap=96000 why=cap halvings=1 bits=32 sink=0 underruns_src=0 start_underruns_src=0 wr_timeouts=0|APP audio info src=192000 pipe=48000 out=0 bclk=0 hw_cap=384000 cap=96000 why=sink halvings=2 bits=0 sink=1 underruns_src=0 start_underruns_src=0 wr_timeouts=0|" ] || { EVID="$a"; fail hr5f_audio_info; }; }
nowrun hr5f_audio_info_idle "seti dac_max_rate 96000; audio info; audio infox" && {
    { grep -qx 'APP audio info src=0 pipe=44100 out=44100 bclk=2822400 hw_cap=384000 cap=96000 why=native halvings=0 bits=32 sink=0 underruns_src=0 start_underruns_src=0 wr_timeouts=0' <<<"$out" && grep -qx 'APP cmd unknown' <<<"$out"; } || { EVID="$(grep '^APP audio info\|unknown' <<<"$out" | tr '\n' '|')"; fail hr5f_audio_info_idle; }; }
# source scans, where only the board can show the behaviour (the real audio_svc.c / bsp_audio.c are not built into the simulator). M3 (the seamless join) and M4 (only
# clock errors lower the ceiling) are host-tested pure rules since T5b (test_pipe_rules: pc_join_kind, pc_hires_failure_lowers_cap); their call sites are pinned below (hr5b_*).
# M6 a source released by a re-plan is not reported as playing; M5 a mixer needs a clock row; M7 the BSP forgets its rate after any failure past the disable
[ "$(grep -c 'sw_t sw = switch_rate(rate);' $AS)" = 2 ] && [ "$(grep -c 'if (!sw) break;' $AS)" = 2 ] && grep -q 'if (switch_rate(pipe)) {' $AS && grep -q 'SW_GONE = 0' $AS && grep -q '^static sw_t hires_off(' $AS && grep -q '^static sw_t switch_rate(uint32_t rate)$' $AS || fail hr5f_released_not_playing
grep -q 'if (!pc_mixer_rate_ok(rate)) {' $AS && ! grep -q 'if (!pc_rate_ok(rate)) {' $AS && grep -q 'pc_mixer_rate_ok(rate)' sim/fake_audio_svc.c || fail hr5f_mixer_clock_row
grep -q 'if (e != ESP_OK || e2 != ESP_OK) s_rate = 0;' components/jc3636f118_bsp/src/bsp_audio.c || fail hr5f_bsp_forgets_rate

# --- hires T5b ---
# the audio task's hi-res decisions are pure functions (pc_pipe_rules.h), host-tested in test_pipe_rules; one scan per rule pins its call site in audio_svc.c
# (and the fake's plan): the plan arguments (no copy left in either), the ceiling step, the failure class (both failure sites), the join kind (gapless test, the
# rate-change join, the queue line), the write-or-drop rule; m1: a failed switch that keeps the ceiling releases the channel at every rate (no 48k gate); m3: s_info
# is filled once before the task starts; m5: APP switch only after a switch that worked (the retry prints its own line)
[ "$(grep -c 'pc_plan_args(raw, burst, P.sink, cap_req(), P.hw_cap)' $AS)" = 1 ] && [ "$(grep -c 'pc_plan_args(raw, burst, P.sink, P.cap, P.hw_cap)' $AS)" = 1 ] && ! grep -q 'plan_args(uint32_t' $AS && grep -q 'pc_plan_args(rate, burst, s_sink, cap, PC_AUDIO_MAX_RATE)' sim/fake_audio_svc.c && ! grep -q 'pc_out_max_rate(' sim/fake_audio_svc.c || fail hr5b_plan_args_shared
grep -q 'uint32_t t = pc_hires_tier_down(rate);' $AS && ! grep -q 't \* 2 < rate' $AS || fail hr5b_tier_down
[ "$(grep -c '> 48000 && pc_hires_failure_lowers_cap(e, e == ESP_ERR_INVALID_RESPONSE)' $AS)" = 2 ] && ! grep -q 'clock_err' $AS && grep -q 'PC_ERR_NOT_SUPPORTED == ESP_ERR_NOT_SUPPORTED' $AS || fail hr5b_failure_class
grep -q 'return pc_join_kind(P.src, P.plan, n->rate, plan_of(n->rate, n->burst)) == PC_JOIN_GAPLESS;' $AS && grep -q 'pc_join_name(pc_join_kind(P.src, P.plan, n->rate, plan_of(n->rate, n->burst)))' $AS && ! grep -q 'pc_join_kind(P.src, clock_plan()' $AS && grep -q '} else if (pc_join_kind(was_raw, was, r, P.plan) == PC_JOIN_RATE_CHANGE || s_rate_stale || build) {' $AS && grep -q 'pc_join_name(pc_join_kind(' $AS && ! grep -q 'short mute' $AS || fail hr5b_join_kind
grep -q 'uint8_t bits = pc_block_route(sink != NULL, !s_released, brate, s_rate) == PC_BLOCK_WRITE ? bsp_audio_bits() : 0;' $AS && ! grep -q 'brate != s_rate ? 0' $AS || fail hr5b_block_route
! grep -q 'if (rate > 48000) {' $AS && [ "$(grep -c 'release_retry(rate);' $AS)" = 2 ] || fail hr5b_retry_every_rate
sed -n '/^void audio_svc_start/,/^}/p' $AS | grep -B1 'xTaskCreatePinnedToCoreWithCaps(task' | head -1 | grep -q 'info_update();' || fail hr5b_info_at_start
[ "$(grep -c 'APP switch ms=' $AS)" = 2 ] && [ "$(grep -c 'if (sw == SW_OK) printf("APP switch ms=' $AS)" = 2 ] || fail hr5b_switch_line_after_retry
# T5b fix 1: the task-level calls the review's mutations are about (rules alone cannot see them deleted): the sink came / went re-plans (2 sites) and the halving stage;
# a mixer whose reinit fails with a clock error keeps the 1 s retry pace (hires_off's SW_RETRY does not reset the timer)
[ "$(grep -c 'switch_rate(replan());' $AS)" = 2 ] || fail hr5b_pin_replan_sites
grep -q 'n = pc_halve_run(P.halve, P.sbuf, ns);' $AS || fail hr5b_pin_halve_run
grep -q '!= SW_RETRY) P.t_reinit = 0;' $AS && grep -q '    if (!s_released) P.t_reinit = 0; ' $AS && ! grep -q 's_rate); P.t_reinit = 0; }' $AS || fail hr5b_mixer_retry_pace

# --- hires T6 ---
# dac_max_rate: tiers {48k, 96k, 192k} in this build (hires T12; two values were a plain wrapping tap, three borrow the knob: hr12_dacmax_axis); a changed value applies from the next source
# (the playing one keeps its pipe); garbage normalises to the tier below (minimum 48k). Every hi-res test above sets the cap itself: the board default (48k) decides none.
nowrun hr6_dacmax_row "seti dac_max_rate 48000; wired open; @0.3; wired key dacmax; wired key dacmax; cfg dac_max_rate; wired rows; wired key dacmax; cfg dac_max_rate; @4.5; ring" && {
    c=$(grep '^APP cfg dac_max_rate=' <<<"$out" | tr '\n' '|')
    { [ "$c" = "APP cfg dac_max_rate=48000|APP cfg dac_max_rate=96000|APP cfg dac_max_rate=192000|" ] && grep -q '^APP wired row=有線最高取樣率：96k$\|^APP wired row=Wired max rate: 96k$' <<<"$out" && [ "$(grep -c '^APP audio cap=' <<<"$out")" = 2 ] && grep -q '^APP audio cap=192000' <<<"$out"; } || { EVID="$c $(grep 'APP wired row=有線\|^APP audio cap' <<<"$out" | tr '\n' '|')"; fail hr6_dacmax_row; }; }
nowrun hr6_dacmax_row_en "seti dac_max_rate 96000; wired open; @0.3; wired rows" && {
    { grep -q '^APP wired row=有線最高取樣率：96k$\|^APP wired row=Wired max rate: 96k$' <<<"$out"; } || { EVID="$(grep '^APP wired row=' <<<"$out" | tr '\n' '|')"; fail hr6_dacmax_row_en; }; }
# (hr6_dacmax_no_borrow, "two tiers = no knob borrow", is gone: with three tiers the row borrows the knob, hr12_dacmax_axis)
nowrun hr6_dacmax_next_track "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 96000 2; mp3 1; @0.5; seti dac_max_rate 48000; @0.3; next; @0.5" && {
    p=$(grep '^APP pipe \|^APP audio cap=' <<<"$out" | tr '\n' '|')
    [ "$p" = "APP audio cap=96000|APP pipe src=96000 pipe=96000 halvings=0 why=native|APP audio cap=48000 (next track)|APP pipe src=96000 pipe=48000 halvings=1 why=cap|" ] || { EVID="$p"; fail hr6_dacmax_next_track; }; }
nowrun hr6_dacmax_garbage "seti dac_max_rate 7; seti dac_max_rate 100000; seti dac_max_rate -1; seti dac_max_rate 384000; seti dac_max_rate 0" && {
    a=$(grep '^APP audio cap=' <<<"$out" | tr '\n' ' ')
    [ "$a" = "APP audio cap=48000 APP audio cap=96000 APP audio cap=48000 APP audio cap=384000 APP audio cap=48000 " ] || { EVID="$a"; fail hr6_dacmax_garbage; }; }
# the board default (48k, BSP_DAC_RATE_DEFAULT) halves a 96k source until the setting or the default is raised; a stored value is read at start
nowrun hr6_default_cap48 "screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.5; audio info" && {
    { grep -qx 'APP pipe src=96000 pipe=48000 halvings=1 why=cap' <<<"$out" && grep -q '^APP audio info src=96000 pipe=48000 .* cap=48000 ' <<<"$out"; } || { EVID="$(grep '^APP pipe\|^APP audio info' <<<"$out" | tr '\n' '|')"; fail hr6_default_cap48; }; }
NOWCFG="dac_max_rate=96000" nowrun hr6_stored_cap96 "screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.5" && {
    { grep -qx 'APP pipe src=96000 pipe=96000 halvings=0 why=native' <<<"$out"; } || { EVID="$(grep '^APP pipe' <<<"$out" | tr '\n' '|')"; fail hr6_stored_cap96; }; }
# a sink coming during a 96k track re-plans with the cap the track opened with, not the one asked for since
nowrun hr6_dacmax_replan_keeps_cap "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.5; seti dac_max_rate 48000; simsink 1; @0.2; simsink 0; @0.2" && {
    p=$(grep '^APP pipe ' <<<"$out" | tr '\n' '|')
    [ "$p" = "APP pipe src=96000 pipe=96000 halvings=0 why=native|APP pipe src=96000 pipe=48000 halvings=1 why=sink|APP pipe src=96000 pipe=96000 halvings=0 why=native|" ] || { EVID="$p"; fail hr6_dacmax_replan_keeps_cap; }; }
# idle audio info follows the setting at once (the board: cap_req() when no source is loaded), also after a source played with the old cap
nowrun hr6_idle_info_follows_cap "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.5; stop; @0.5; seti dac_max_rate 48000; audio info" && {
    { grep '^APP audio info ' <<<"$out" | tail -1 | grep -q ' src=0 .* cap=48000 '; } || { EVID="$(grep '^APP audio info' <<<"$out" | tr '\n' '|')"; fail hr6_idle_info_follows_cap; }; }
# call sites the sim cannot run (the real audio_svc.c): the start reads the setting through pc_dacmax_norm with the board default, the console hook and the row call set_cap, the
# cap is applied only when a source opens (plan_set fresh), a re-plan reads P.cap
grep -q 'P.cap = P.cap_req = pc_dacmax_norm(cfg_int("dac_max_rate", 0), BSP_DAC_RATE_DEFAULT);' $AS && grep -q 'uint32_t nc = cap_req();' $AS && grep -q 'P.hw_cap = pc_hw_cap_after_cap_change(P.cap, nc, P.hw_cap);' $AS && grep -q 'P.cap = nc;' $AS && ! grep -q 's_cap_req' $AS && grep -q 'if (!out->src_rate) out->cap = cap_req();' $AS || fail hr6_cap_at_open
grep -q 'audio_svc_set_cap(pc_dacmax_norm(n, BSP_DAC_RATE_DEFAULT))' app/main/console.c && grep -q 'audio_svc_set_cap(dacmax());' app/main/ui/scr_wiredout.c && grep -q '#define BSP_DAC_RATE_DEFAULT 48000' components/jc3636f118_bsp/include/bsp/bsp_audio.h || fail hr6_cap_callers

# --- hires T7 ---
# 現況 (pc_nowinfo, host-tested in test_nowinfo): the console `status` prints the page's lines; the page is a help entry with refresh_ms (the explain page's timer); the player ring and the
# Bluetooth ring carry the cell. The fixture has no CJK font: both languages are accepted, as in the T6 tests.
nowrun hr7_status_idle "status" && {
    { grep -qx 'APP status 輸出：耳機孔 44.1 kHz・24 bit\|APP status Output: headphone jack 44.1 kHz · 24 bit' <<<"$out" && grep -qx 'APP status end' <<<"$out" && ! grep -q '^APP status .*\(來源\|Source\|處理\|Processing\)' <<<"$out"; } || { EVID="$(grep '^APP status' <<<"$out" | tr '\n' '|')"; fail hr7_status_idle; }; }
nowrun hr7_status_96k "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.5; status" && {
    { grep -qx 'APP status 處理：96 kHz 原生\|APP status Processing: 96 kHz native' <<<"$out" && grep -qx 'APP status 來源：MP3 96 kHz\|APP status Source: MP3 96 kHz' <<<"$out" && grep -qx 'APP status 輸出：耳機孔 96 kHz・24 bit\|APP status Output: headphone jack 96 kHz · 24 bit' <<<"$out"; } || { EVID="$(grep '^APP status' <<<"$out" | tr '\n' '|')"; fail hr7_status_96k; }; }
nowrun hr7_status_halved_cap "seti dac_max_rate 48000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.5; status; simsink 1; @0.2; status" && {
    { grep -qx 'APP status 處理：48 kHz（從 96 kHz 減半：上限設定）\|APP status Processing: 48 kHz (halved from 96 kHz: limit setting)' <<<"$out" && grep -q '^APP status Output: headphone jack\|^APP status 輸出：耳機孔' <<<"$out"; } || { EVID="$(grep '^APP status' <<<"$out" | tr '\n' '|')"; fail hr7_status_halved_cap; }; }
# the player ring: 現況 is the last cell (eight own cells for an MP3 + this one); a pick opens the explain page "status"
# a def at the ring's capacity with both extra cells: the code keeps 12 own cells (the status and help cells take 2 of 14), prints the cut, shows 14 cells; cell 12 = 現況, cell 13 = 說明
nowrun hr7_full_def_status "uipush poptest; poptest5; popring -5; popring 14; @0.3; uitop; help show" && {
    { grep -q '^APP popring open=1 page=0 cells=16 ' <<<"$out" && grep -qx 'APP uitop explain' <<<"$out" && grep -q '^APP explain id=status ' <<<"$out"; } || { EVID="$(grep '^APP popring\|^APP uitop\|^APP explain' <<<"$out" | tr '\n' '|')"; fail hr7_full_def_status; }; }
nowrun hr7_full_def_help "uipush poptest; poptest5; popring -5; popring 15; @0.3; uitop; help show" && {
    { grep -qx 'APP uitop explain' <<<"$out" && grep -q '^APP explain id=settings ' <<<"$out"; } || { EVID="$(grep '^APP popring\|^APP uitop\|^APP explain' <<<"$out" | tr '\n' '|')"; fail hr7_full_def_help; }; }
nowrun hr7_status_page "seti dac_max_rate 96000; screen on; vol 0; track 3; @3; nowmenu -2; nowmenu -5; nowmenu 9; @0.3; uitop; help show" && {
    { grep -qx 'APP now cells=9 items=fav,prev,list,host,order,next,info,stop|speed' <<<"$out" && grep -qx 'APP uitop explain' <<<"$out" && grep -q '^APP explain id=status title=現況 paras=[0-9]* qr=0 web=0$\|^APP explain id=status title=Status paras=[0-9]* qr=0 web=0$' <<<"$out"; } || { EVID="$(grep '^APP now cells\|^APP uitop\|^APP explain' <<<"$out" | tr '\n' '|')"; fail hr7_status_page; }; }
nowrun hr7_status_console_open "help open status; @0.3; uitop; help show" && {
    { grep -qx 'APP uitop explain' <<<"$out" && grep -q '^APP explain id=status title=.* paras=2 qr=0 web=0$' <<<"$out"; } || { EVID="$(grep '^APP uitop\|^APP explain' <<<"$out" | tr '\n' '|')"; fail hr7_status_console_open; }; }
# the page refreshes by itself: a stop changes the line count (rebuild), a rate change sets the changed notes in place, and nothing changing sets nothing
nowrun hr7_status_refresh "seti dac_max_rate 96000; screen on; vol 0; track 3; @3; nowmenu -2; nowmenu 9; @0.3; help show; help stats; @2.5; help stats; stop; @1.2; help show; help stats" && {
    p=$(grep -E '^APP explain (id|sets)' <<<"$out" | sed 's/title=[^ ]* //; s/sets=[0-9]* rebuilds=1$/sets=N rebuilds=1/' | tr '\n' '|')
    [ "$p" = "APP explain id=status paras=5 qr=0 web=0|APP explain sets=0 rebuilds=0|APP explain sets=0 rebuilds=0|APP explain id=status paras=2 qr=0 web=0|APP explain sets=N rebuilds=1|" ] || { EVID="$p"; fail hr7_status_refresh; }; }
nowrun hr7_status_refresh_text "seti dac_max_rate 96000; screen on; vol 0; track 3; @3; nowmenu -2; nowmenu 9; @0.3; help stats; simaudiorate 96000 1; next; @1.5; help show; help stats" && {
    p=$(grep -E '^APP explain (sets|id)' <<<"$out" | sed 's/title=[^ ]* //' | tr '\n' '|')
    { grep -q '^APP explain sets=0 rebuilds=0' <<<"$out" && tail -1 <<<"$(grep '^APP explain sets' <<<"$out")" | grep -q 'sets=[1-9][0-9]* rebuilds=0$'; } || { EVID="$p"; fail hr7_status_refresh_text; }; }
# the Bluetooth ring (9 cells: two pages; page 2 = 現況, 清除配對, 說明): 現況 is cell BTN - 3 = page 2 slot 0; one tap opens it (ui_ring_set_item_now: nothing to confirm)
BTST=${BTSLOT[$(( (BTN - 3) % 8 ))]}
nowrun hr7_bt_ring_status "screen on; vol 0; btui; @0.3; ring; simtap $BTST; @0.3; uitop; help show" && {
    { [ "$(rg | head -c 8)" = "page=0/1" ] && grep -qx 'APP uitop explain' <<<"$out" && grep -q '^APP explain id=status ' <<<"$out" && [ "$(nrb)" = 0 ]; } || { EVID="$(grep '^APP ring\|^APP uitop\|^APP explain' <<<"$out" | tr '\n' '|')"; fail hr7_bt_ring_status; }; }
nowrun hr7_bt_tap_status "btui; @0.3; bt tap $((BTN - 3)); @0.3; uitop; help show" && {
    { grep -qx 'APP uitop explain' <<<"$out" && grep -q '^APP explain id=status ' <<<"$out" && ! grep -qx 'APP cmd unknown' <<<"$out"; } || { EVID="$(grep '^APP uitop\|^APP explain\|unknown' <<<"$out" | tr '\n' '|')"; fail hr7_bt_tap_status; }; }
# call sites the sim cannot run (the real audio_svc.c fills the format fields once per source; the console `status` and the ring cell)
grep -q 'static void info_fmt(void)' $AS && grep -q '.fmt = P.fmt, .src_bits = P.src_bits, .kbps = P.kbps, .released = s_released' $AS && grep -q 'ui_status_body(b, 2048, i18n_zh(), 0);' app/main/console.c && grep -q '.status = &HELP_STATUS' app/main/ui/now_menu.c && grep -q 'ui_timer_bind(p, refresh_tick, h->refresh_ms, NULL);' app/main/ui/scr_explain.c || fail hr7_call_sites

# --- hires T8 ---
# the fx mode bits follow the pipeline rate: at 96 kHz a HEAVY app is paused with the reason, a LIGHT one runs; back at 44.1 kHz both run (fx_first_app_of: the first HEAVY / the first LIGHT app of the table)
nowrun hr8_fx_state_ids "fx state"; HV8=$(sed -n 's/^APP fx app=\([a-z_]*\) cls=heavy.*/\1/p' <<<"$out" | head -1); LT8=$(sed -n 's/^APP fx app=\([a-z_]*\) cls=light.*/\1/p' <<<"$out" | head -1)
nowrun hr8_fx_hires_dim "fx on $HV8 1; fx on $LT8 1; seti dac_max_rate 96000; simaudiorate 96000 1; mp3 1; @1.2; uipush fx; @0.3; fxui rows; fx state" && {
    h=$(grep "^APP fx app=$HV8 " <<<"$out" | tail -1); l=$(grep "^APP fx app=$LT8 " <<<"$out" | tail -1)
    { [ -n "$HV8" ] && [ -n "$LT8" ] && grep -q ' run=0 look=dim ' <<<"$h" && grep -q ' run=1 look=run ' <<<"$l" && grep -qx 'APP fx mode=16 heavy=off' <<<"$out" && grep -q '^APP fxui row=[0-9]* look=dim text=.*  on, paused$' <<<"$out"; } || { EVID="H=$h L=$l $(grep '^APP fx mode' <<<"$out" | tr '\n' '|')"; fail hr8_fx_hires_dim; }; }
# tick-path pin only: the fake already idles at 44100, so this does not discriminate the board path (pinned by hr8_info_no_source below)
nowrun hr8_fx_rate_bits_drop_on_stop "seti dac_max_rate 96000; simaudiorate 96000 1; mp3 1; @1.2; stop; @1.2; fx state" && {
    { grep -qx 'APP fx mode=16 heavy=off' <<<"$out" && [ "$(grep '^APP fx mode=' <<<"$out" | tail -1)" = 'APP fx mode=0 heavy=run' ] && grep -q '^APP fx state n=[0-9]* mode=0 ' <<<"$out"; } || { EVID="$(grep '^APP fx mode\|^APP fx state' <<<"$out" | tr '\n' '|')"; fail hr8_fx_rate_bits_drop_on_stop; }; }
grep -q '\.src_rate = S\.h || S\.mix ? P\.src : 0, \.pipe_rate = s_rate,' $AS || fail hr8_info_no_source

