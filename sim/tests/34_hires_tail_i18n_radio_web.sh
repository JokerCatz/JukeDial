# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: need_hv8)
need_hv8
# --- hires T12 ---
# 176.4/192 kHz native on the APLL. The simulator has no DAC: the plan, the knob row, the status text, the guard and one scan per firmware-only fix.
# the dacmax row has three values now: the first tap is one step (96k), then the knob turns it both ways (R21)
nowrun hr12_dacmax_axis "screen on; @0.5; seti dac_max_rate 48000; wired open; @0.3; wired key dacmax; wired key dacmax; cfg dac_max_rate; simknob 1; cfg dac_max_rate; simknob -1; cfg dac_max_rate; simknob -1; cfg dac_max_rate" && {
    c=$(grep '^APP cfg dac_max_rate=' <<<"$out" | tr '\n' '|')
    [ "$c" = "APP cfg dac_max_rate=48000|APP cfg dac_max_rate=96000|APP cfg dac_max_rate=192000|APP cfg dac_max_rate=96000|APP cfg dac_max_rate=48000|" ] || { EVID="$c"; fail hr12_dacmax_axis; }; }   # the first line is seti's own; }
nowrun hr12_192k_native "seti dac_max_rate 192000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 192000 1; mp3 1; @0.5; status" && {
    { ! grep -q 'APP play refused' <<<"$out" && grep -qx 'APP pipe src=192000 pipe=192000 halvings=0 why=native' <<<"$out" && grep -qx 'APP status 處理：192 kHz 原生\|APP status Processing: 192 kHz native' <<<"$out"; } || { EVID="$(grep '^APP pipe\|^APP status' <<<"$out" | tr '\n' '|')"; fail hr12_192k_native; }; }
nowrun hr12_176k_native "seti dac_max_rate 192000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 176400 1; mp3 1; @0.5" && {
    { grep -qx 'APP pipe src=176400 pipe=176400 halvings=0 why=native' <<<"$out"; } || { EVID="$(grep '^APP pipe' <<<"$out" | tr '\n' '|')"; fail hr12_176k_native; }; }
nowrun hr12_192k_fx_mode "seti dac_max_rate 192000; fx on $HV8 1; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 192000 1; mp3 1; @1.2; fx state" && {
    { grep -q '^APP fx mode=48' <<<"$out"; } || { EVID="$(grep '^APP fx' <<<"$out" | tr '\n' '|')"; fail hr12_192k_fx_mode; }; }
nowrun hr12_192k_burst_halved "seti dac_max_rate 192000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 192000 1 32768; mp3 1; @0.5; status" && {
    { grep -qx 'APP pipe src=192000 pipe=96000 halvings=1 why=burst' <<<"$out" && grep -qx 'APP status 處理：96 kHz（從 192 kHz 減半：區塊過大）\|APP status Processing: 96 kHz (halved from 192 kHz: block too large)' <<<"$out"; } || { EVID="$(grep '^APP pipe\|^APP status' <<<"$out" | tr '\n' '|')"; fail hr12_192k_burst_halved; }; }
nowrun hr12_192k_cap_halved "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 192000 1; mp3 1; @0.5" && {
    { grep -qx 'APP pipe src=192000 pipe=96000 halvings=1 why=cap' <<<"$out"; } || { EVID="$(grep '^APP pipe' <<<"$out" | tr '\n' '|')"; fail hr12_192k_cap_halved; }; }
# the guard: selftest (a planted hit fails, a clean tree passes), and the real tree has no APLL outside bsp_audio.c
[ "$(bash tools/check/apll_guard.sh --selftest)" = "apll_guard selftest: ok" ] && [ "$(bash tools/check/apll_guard.sh)" = "apll_guard: ok" ] || fail hr12_apll_guard
grep -q 'apll_guard.sh' tools/check/host_lint.sh && grep -q 'apll_guard.sh' tools/check/app_lint.sh || fail hr12_apll_guard_called
[ "$(grep -rlE 'I2S_CLK_SRC_APLL' app/main components --include='*.c' --include='*.cpp' --include='*.h')" = "components/jc3636f118_bsp/src/bsp_audio.c" ] || fail hr12_apll_single_owner
# the audio task polls the real read-back every block above 48k; a verdict goes through the same hires_off path as a refused switch
grep -q 'bsp_audio_clock_poll() == ESP_ERR_INVALID_RESPONSE' $AS && grep -q 'if (hires_off("readback", s_rate) == SW_OK) { audio_unmute(); s_ramp_in = true; }' $AS || fail hr12_poll_wired
# bsp_audio.c: the APLL row, the tx-done counter, the measured settle, the reshape on a source change, the same-rate fallback shape
{ grep -q 'row.src == PC_CLK_APLL ? I2S_CLK_SRC_APLL : I2S_CLK_SRC_XTAL' $BA && grep -q '.on_sent = on_sent' $BA && grep -q 'static esp_err_t settle_and_measure(uint32_t rate, bool measure)' $BA \
  && grep -q 'row.src != s_src)) return reshape' $BA && grep -q 'APP audio dma fallback rate=%lu dma=6x480' $BA && grep -q 'APP audio clk rate=%lu src=%s mclk=%u dma=%lux%lu bclk=%lu us=%lld' $BA; } || fail hr12_bsp_scan
# fix round 1: the counting maths has a host test (test_clkmeas); the callback feeds it the ring position (dma_buf); the registration is checked; reinit
# takes the same road as reshape (fallback shape at the same rate, muted measurement of an APLL rate); the guard knows the other APLL names
{ grep -q 'pc_clkmeas_event(&e->m, ev->dma_buf, t);' $BA && grep -q 'e = i2s_channel_register_event_callback(s_tx, &cbs, s_ev);' $BA && grep -q 'event callbacks: %s' $BA \
  && grep -q 'esp_err_t e = init_shape(rate, fol ? row.desc : s_desc_cfg' $BA && z=$(sed -n '/^esp_err_t bsp_audio_reinit/,/^}/p' $BA) && grep -q 'settle_and_measure(rate, true)' <<<"$z" \
  && [ "$(grep -c 'init_shape(' $BA)" = 3 ] && grep -q 'CLKOUT_SIG_AUDIO_PLL' tools/check/apll_guard.sh && grep -q 'app/managed_components' tools/check/apll_guard.sh; } || fail hr12_fix1_scan
# T10b re-review (carried): the default hand-off macros of pc_rsq.c are acquire / release (the test hook overrides them, so no host test looks at the firmware's own bodies)
{ grep -q '^#define LD(x) PC_RSQ_LOAD(x, __ATOMIC_ACQUIRE)$' components/player_core/src/pc_rsq.c && grep -q '^#define ST(x, v) PC_RSQ_STORE(x, v, __ATOMIC_RELEASE)$' components/player_core/src/pc_rsq.c \
  && grep -q '__atomic_load_n(&(x), (mo))' components/player_core/src/pc_rsq.c && grep -q '__atomic_store_n(&(x), (v), (mo))' components/player_core/src/pc_rsq.c; } || fail hr12_rsq_acqrel_pin
# --- end hires T12 ---

# --- hires T13 ---
# 352.8/384 kHz: native when dac_max_rate is 384k and the decode burst fits (<= 4096), else halved through pc_halve (cap / burst); a Bluetooth sink three halvings
nowrun hr13_384k_native "seti dac_max_rate 384000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 384000 1 4096; mp3 1; @0.5; status" && {
    { ! grep -q 'APP play refused' <<<"$out" && grep -qx 'APP pipe src=384000 pipe=384000 halvings=0 why=native' <<<"$out" && grep -qx 'APP status 處理：384 kHz 原生\|APP status Processing: 384 kHz native' <<<"$out"; } || { EVID="$(grep '^APP pipe\|^APP status\|refused' <<<"$out" | tr '\n' '|')"; fail hr13_384k_native; }; }
nowrun hr13_352k_native "seti dac_max_rate 384000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 352800 1; mp3 1; @0.5" && {
    { grep -qx 'APP pipe src=352800 pipe=352800 halvings=0 why=native' <<<"$out"; } || { EVID="$(grep '^APP pipe' <<<"$out" | tr '\n' '|')"; fail hr13_352k_native; }; }
nowrun hr13_384k_burst "seti dac_max_rate 384000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 384000 1 65536; mp3 1; @0.5" && {
    { grep -qx 'APP pipe src=384000 pipe=96000 halvings=2 why=burst' <<<"$out"; } || { EVID="$(grep '^APP pipe' <<<"$out" | tr '\n' '|')"; fail hr13_384k_burst; }; }
nowrun hr13_384k_bigframe "seti dac_max_rate 384000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 384000 1 32768; mp3 1; @0.5" && {
    { grep -qx 'APP pipe src=384000 pipe=384000 halvings=0 why=native' <<<"$out"; } || { EVID="$(grep '^APP pipe' <<<"$out" | tr '\n' '|')"; fail hr13_384k_bigframe; }; }
nowrun hr13_384k_cap "seti dac_max_rate 192000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 384000 1; mp3 1; @0.5" && {
    { grep -qx 'APP pipe src=384000 pipe=192000 halvings=1 why=cap' <<<"$out"; } || { EVID="$(grep '^APP pipe' <<<"$out" | tr '\n' '|')"; fail hr13_384k_cap; }; }
nowrun hr13_384k_sink "seti dac_max_rate 384000; screen on; vol 0; @0.5; mp3ui; @0.3; simsink 1; simaudiorate 384000 1; mp3 1; @0.5" && {
    { grep -qx 'APP pipe src=384000 pipe=48000 halvings=3 why=sink' <<<"$out"; } || { EVID="$(grep '^APP pipe' <<<"$out" | tr '\n' '|')"; fail hr13_384k_sink; }; }
nowrun hr13_384k_sink_replan "seti dac_max_rate 384000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 384000 1 4096; mp3 1; @0.5; simsink 1; @0.2; simsink 0; @0.2" && {
    p=$(grep '^APP pipe ' <<<"$out" | tr '\n' '|')
    [ "$p" = "APP pipe src=384000 pipe=384000 halvings=0 why=native|APP pipe src=384000 pipe=48000 halvings=3 why=sink|APP pipe src=384000 pipe=384000 halvings=0 why=native|" ] || { EVID="$p"; fail hr13_384k_sink_replan; }; }
nowrun hr13_dacmax_axis "screen on; @0.5; seti dac_max_rate 192000; wired open; @0.3; wired key dacmax; wired key dacmax; cfg dac_max_rate; simknob -1; cfg dac_max_rate" && {
    c=$(grep '^APP cfg dac_max_rate=' <<<"$out" | tr '\n' '|')
    [ "$c" = "APP cfg dac_max_rate=192000|APP cfg dac_max_rate=384000|APP cfg dac_max_rate=192000|" ] || { EVID="$c"; fail hr13_dacmax_axis; }; }
# the FLAC backend does not resample any more: no decimator left in it
{ ! grep -q 'fir_init\|coef\[\|hist\[' components/player_core/src/pc_backend_flac.c; } || fail hr13_flac_no_decimator
# --- end hires T13 ---

# --- i18n A: the language setting (繁中 default / 簡中 / English), at run time, on the console, the settings axis and the web page ---
nowrun i18n_lang_console 'screen on; lang; lang cn; cfg lang; lang en; cfg lang; lang xx; lang tw; cfg lang; lang' && {   # the fixture has no CJK font: every text is English (the pictures of each language are lang_* in the baseline)
    l=$(grep '^APP lang=\|^APP cfg lang=\|^APP cmd lang:' <<<"$out" | tr '\n' '|')
    [ "$l" = "APP lang=tw|APP lang=cn|APP cfg lang=1|APP lang=en|APP cfg lang=2|APP cmd lang: tw|cn|en|APP lang=tw|APP cfg lang=0|APP lang=tw|" ] || { EVID="$l"; fail i18n_lang_console; }; }
nowrun i18n_lang_web '{"op":"config_set","id":1,"values":{"lang":2}}; lang; {"op":"config_set","id":2,"values":{"lang":7}}; lang; {"op":"config_set","id":3,"values":{"lang":0}}; lang' && {
    l=$(grep '^APP lang=' <<<"$out" | tr '\n' '|')
    [ "$l" = "APP lang=en|APP lang=en|APP lang=tw|" ] || { EVID="$l"; fail i18n_lang_web; }; }
nowrun i18n_lang_axis 'screen on; @0.5; setui; @0.3; settings key lang; settings key lang; lang; simknob -1; lang; simknob 1; simknob 1; lang; lang tw' && {
    l=$(grep '^APP lang=' <<<"$out" | tr '\n' '|')
    [ "$l" = "APP lang=cn|APP lang=tw|APP lang=en|APP lang=tw|" ] || { EVID="$l"; fail i18n_lang_axis; }; }
python3 tools/gen/i18n_gen.py --check >"$T/i18n_gen.log" 2>&1 || { EVID="$(tail -3 "$T/i18n_gen.log" | tr '\n' '|')"; fail i18n_table_in_step; }
# --- end i18n A ---
# --- i18n B: the first-boot language picker (no "lang" key in the cfg = picker over the menu; any lang value: no picker; no card: no picker) ---
[ "$(pick_top '')" = "APP uitop langpick" ] || { EVID="$(pick_top '')"; fail i18n_pick_empty_cfg; }
[ "$(pick_top 'volume=30')" = "APP uitop langpick" ] || fail i18n_pick_no_lang_key
[ "$(pick_top '' 'sd=0')" = "APP uitop menu" ] || { EVID="$(pick_top '' 'sd=0')"; fail i18n_pick_never_without_card; }
for lv in 0 1 2; do [ "$(pick_top "lang=$lv")" = "APP uitop menu" ] || { EVID="lang=$lv"; fail i18n_pick_never_with_lang; }; done
rm -rf "$T/lp"; mkdir -p "$T/lp/_player/config"
out=$($SIM --headless --root "$T/lp" --cmd "@0.5; simtap 180 95; @0.5; uitop; cfg lang; lang" 2>&1)
{ grep -qx 'APP uitop tour' <<<"$out" &&   # GS: the gesture tutorial follows the pick
 grep -qx 'APP cfg lang=0' <<<"$out"; } || { EVID="$(grep -E '^APP (uitop|cfg|lang|langpick)' <<<"$out" | tr '\n' '|')"; fail i18n_pick_stores_default; }
# --- end i18n B ---

# --- radio2: several station lists on the card ---
fresh; printf 'Jazz One|http://j1.example/s|http://j1b.example/s\nJazz Two|http://j2.example/s\n' > "$T/fix/_player/config/radio_jazz.txt"
out=$($SIM --headless --root "$T/fix" --cmd "uipush radio; @0.3; uiboxes" 2>&1)
rl=$(rl_labels "$out")
{ [ "$(tail -3 <<<"$rl" | head -1)" = "Stations: Default" ] && [ "$(tail -2 <<<"$rl" | head -1)" = "Broadcast radio" ] && [ "$(tail -1 <<<"$rl")" = "Edit stations on PC" ]; } || { EVID="$(tail -3 <<<"$rl" | tr '\n' '|')"; fail radio2_chooser_row; }
set -- $(grep '^APP uibox button' <<<"$out" | grep '|Stations: Default$' | awk '{print $4, $5, $6, $7}')
out=$($SIM --headless --root "$T/fix" --cmd "uipush radio; @0.3; simtap $(( ($1 + $3) / 2 )) $(( ($2 + $4) / 2 )); @0.5; uiboxes; cfg radio_list; uistack" 2>&1)
rl=$(rl_labels "$out")
{ grep -qx 'APP radio list=jazz' <<<"$out" && [ "$(head -2 <<<"$rl" | tr '\n' '|')" = "Jazz One|Jazz Two|" ] && grep -qx 'APP cfg radio_list=jazz' <<<"$out" && ! grep -q '^APP ui leak' <<<"$out"; } || { EVID="$(grep '^APP radio\|^APP cfg' <<<"$out" | tr '\n' '|') $(tr '\n' '|' <<<"$rl" | cut -c1-120)"; fail radio2_chooser_switch; }
fresh                                                          # 簡中: its own default list (radio_cn.txt, empty: no mainland station is built in); the note names the file
out=$($SIM --headless --root "$T/fix" --cmd "lang cn; uipush radio; @0.3; uiboxes" 2>&1)
grep -q '^APP uibox label .*|\(沒有電台/\|没有电台/\|No stations/\)/sdcard/_player/config/radio_cn.txt$' <<<"$out" || { EVID="$(grep '^APP uibox' <<<"$out" | tr '\n' ' ')"; fail radio2_cn_default_list; }
# --- end radio2 ---

# --- webtool W5/W6: status reports the board; font_charset / font_miss for the web font builder ---
fresh
out=$($SIM --headless --root "$T/fix" --cmd 'scan; @0.2; {"op":"status","id":1}; {"op":"font_charset","id":2,"off":0}; {"op":"font_charset","id":3,"off":100000}; {"op":"font_miss","id":4}' 2>&1)
g=$(grep '^CTL' <<<"$out")
{ grep -q '"id":1.*"board":"jc3636f118"' <<<"$g" && grep -q '"id":2.*"ok":true.*"total":[1-9][0-9]*' <<<"$g" && grep -q '"id":2.*"chars":"[^"]\+"' <<<"$g" && grep -q '"id":3.*"chars":""' <<<"$g" &&
  grep -q '"id":4.*"ok":true.*"list":\[' <<<"$g"; } || { EVID="$(cut -c1-160 <<<"$g" | tr '\n' '|')"; fail webtool_board_charset; }
# --- end webtool ---

# --- webtool W7: font_put stages a font on the card (chunks with CRC, resend acknowledged, SHA-256 at the end, TrueType check, names fixed) ---
fresh
out=$($SIM --headless --root "$T/fix" --cmd '{"op":"font_put_begin","id":1,"name":"ui-tc.ttf","size":1024}; {"op":"font_put","id":2,"off":0,"crc":1076682067,"data":"AAEAAAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+foBAg=="}; {"op":"font_put","id":20,"off":256,"crc":1301047634,"data":"AwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+foBAgMEBQYHCA=="}; {"op":"font_put","id":3,"off":256,"crc":1301047635,"data":"AwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+foBAgMEBQYHCA=="}; {"op":"font_put","id":4,"off":512,"crc":801956100,"data":"CQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+foBAgMEBQYHCAkKCwwNDg=="}; {"op":"font_put","id":21,"off":0,"crc":1076682067,"data":"AAEAAAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+foBAg=="}; {"op":"font_put","id":5,"off":768,"crc":4062545236,"data":"DxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+foAAAAAAAAAAAAAAAAAAAAAAAAAAA=="}; {"op":"font_put_end","id":9,"sha256":"34234668f433795cbed1e8eb06025a850c5d8e86883fdd49920b098e553c57bd"}; {"op":"font_put_begin","id":10,"name":"evil.ttf","size":1024}; {"op":"font_put_begin","id":11,"name":"ui-sc.ttf","size":1024}; {"op":"font_put","id":30,"off":0,"crc":684618944,"data":"QUJDRAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+foBAg=="}; {"op":"font_put","id":31,"off":256,"crc":1301047635,"data":"AwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+foBAgMEBQYHCA=="}; {"op":"font_put","id":32,"off":512,"crc":801956100,"data":"CQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+foBAgMEBQYHCAkKCwwNDg=="}; {"op":"font_put","id":33,"off":768,"crc":4062545236,"data":"DxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3BxcnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmqq6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj5OXm5+jp6uvs7e7v8PHy8/T19vf4+foAAAAAAAAAAAAAAAAAAAAAAAAAAA=="}; {"op":"font_put_end","id":12,"sha256":"735133c6d8a2a296a47a2a98d22d48cc2cbbac72801f2e42a3febee35dbafff2"}; {"op":"font_put_commit","id":14,"names":["ui-sc.ttf"]}; {"op":"font_put_commit","id":13,"names":["ui-tc.ttf"]}' 2>&1)
g=$(grep '^CTL' <<<"$out")
{ grep -q '"id":1,"ok":true' <<<"$g" && grep -q '"id":20,"ok":false,"got":256.*"code":"chunk_crc"' <<<"$g" && grep -q '"id":3,"ok":true,"got":512' <<<"$g" && grep -q '"id":21,"ok":true,"got":768' <<<"$g" &&
  grep -q '"id":9,"ok":true,"staged":true,"size":1024,"sha256":"34234668f433795cbed1e8eb06025a850c5d8e86883fdd49920b098e553c57bd"' <<<"$g" && grep -q '"id":10,"ok":false,"code":"name"' <<<"$g" && grep -q '"id":12,"ok":false,"code":"not_ttf"' <<<"$g" && grep -q '"id":14,"ok":false,"code":"nothing_staged"' <<<"$g" && grep -q '"id":13,"ok":true,"committed":1' <<<"$g" &&
  [ "$(stat -c %s "$T/fix/_player/fonts/ui-tc.ttf.new")" = 1024 ] && [ ! -e "$T/fix/_player/fonts/ui-tc.ttf.stg" ] && [ ! -e "$T/fix/_player/fonts/ui-tc.ttf.part" ] && [ ! -e "$T/fix/_player/fonts/ui-sc.ttf.part" ] && [ ! -e "$T/fix/_player/fonts/ui-sc.ttf.new" ]; } ||
    { EVID="$(cut -c1-120 <<<"$g" | tr '\n' '|')"; fail webtool_font_put; }
# --- end webtool W7 ---

