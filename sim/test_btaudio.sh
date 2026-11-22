# SPDX-License-Identifier: LGPL-2.1-or-later
# BTAUDIO: the Bluetooth audio and Wired output pages, the reasons of every stage and the details page's output block.
# Sourced by sim/test.sh (nowrun, fail, EVID). Texts are read with `outstat tw|cn|en` (a named language prints with its own characters whatever fonts the run has).
HP="simbt mode 2"; CONN="simbt connect"
# the headset's best and the actual, with the reason (no greyed cells: a preference above the headset caps and says so)
nowrun btaudio_caps_aac "screen on; vol 0; $HP; simbt caps aac,sbc; $CONN; @0.3; outstat tw; outstat en" && {
    grep -qx 'APP outstat best=耳機最高：AAC 48k / 16-bit' <<<"$out" && grep -qx 'APP outstat actual=實際：AAC 48k / 16-bit' <<<"$out" \
      && grep -q '^APP outstat bt=正在傳輸：AAC 48k / 16-bit / 立體聲 / 256 kbps（耳機：最高 AAC）$' <<<"$out" \
      && grep -qx 'APP outstat actual=Actual: AAC 48k / 16-bit' <<<"$out" && grep -q '^APP outstat bt=Streaming: AAC 48k / 16-bit / stereo / 256 kbps (headset: max AAC)$' <<<"$out" \
      || { EVID="$(grep '^APP outstat' <<<"$out" | tr '\n' '|')"; fail btaudio_caps_aac; }; }
nowrun btaudio_not_connected "screen on; vol 0; $HP; @0.3; outstat tw; outstat cn; outstat en" && {
    grep -qx 'APP outstat bt=未連線' <<<"$out" && grep -qx 'APP outstat bt=未连接' <<<"$out" && grep -qx 'APP outstat bt=Not connected' <<<"$out" \
      && grep -qx 'APP outstat best=耳機最高：未連線' <<<"$out" && grep -qx 'APP outstat actual=Actual: not connected' <<<"$out" || { EVID="$(grep '^APP outstat' <<<"$out" | tr '\n' '|')"; fail btaudio_not_connected; }; }
nowrun btaudio_ldac_sink "screen on; vol 0; $HP; simbt caps ldac,aptxhd,aptx,aac,sbc; $CONN; @0.3; outstat tw" && {
    grep -q '^APP outstat bt=正在傳輸：LDAC 48k / 16-bit / 立體聲 / 990 kbps（' <<<"$out" && grep -qx 'APP outstat best=耳機最高：LDAC 96k / 24-bit' <<<"$out" \
      || { EVID="$(grep '^APP outstat' <<<"$out" | tr '\n' '|')"; fail btaudio_ldac_sink; }; }
nowrun btaudio_ldac_abr "screen on; vol 0; $HP; simbt caps ldac,aac,sbc; $CONN; simbt ldac 660; @0.3; outstat tw" && {
    grep -q '^APP outstat bt=.*990→660 kbps（訊號：自動降速）' <<<"$out" || { EVID="$(grep '^APP outstat bt' <<<"$out")"; fail btaudio_ldac_abr; }; }
# a build without the vendor stack: the LDAC preference caps to AAC and the page says why
nowrun btaudio_no_ldac_build "screen on; vol 0; $HP; simbt caps ldac,aac,sbc; simbt build 0; $CONN; @0.3; btaudio open; @0.3; btaudio rows; outstat tw" && {
    grep -qx 'APP btaudio build=此版韌體不含 LDAC\|APP btaudio build=This firmware has no LDAC' <<<"$out" && grep -q '^APP outstat bt=正在傳輸：AAC 48k.*（韌體：此版不含 LDAC）' <<<"$out" \
      || { EVID="$(grep '^APP btaudio build\|^APP outstat bt' <<<"$out" | tr '\n' '|')"; fail btaudio_no_ldac_build; }; }
# the page: five rows, every option stays selectable (a codec tap steps through all four), a change shows 重新連線以套用
nowrun btaudio_page "screen on; vol 0; $HP; simbt caps aac,sbc; $CONN; @0.3; btaudio open; @0.3; btaudio rows; axishalf -1; btaudio key codec; btaudio key codec; btaudio key rate; btaudio key rate; axishalf 1; btaudio key bits; btaudio key chan; axishalf -1; btaudio key ldac; btaudio key ldac; axishalf 1; @0.1; btaudio rows; cfg bt_codec; cfg bt_rate; cfg bt_bits; cfg bt_chan; cfg ldac_kbps" && {
    [ "$(grep -c '^APP btaudio row=' <<<"$out")" = 10 ] && grep -qx 'APP btaudio rows=5 dirty=0' <<<"$out" && grep -qx 'APP btaudio rows=5 dirty=1' <<<"$out" \
      && grep -q '^APP cfg bt_codec=2$' <<<"$out" && grep -q '^APP cfg bt_rate=48000$' <<<"$out" && grep -q '^APP cfg bt_bits=16$' <<<"$out" && grep -q '^APP cfg bt_chan=1$' <<<"$out" && grep -q '^APP cfg ldac_kbps=660$' <<<"$out" \
      || { EVID="$(grep '^APP btaudio rows\|^APP cfg' <<<"$out" | tr '\n' '|')"; fail btaudio_page; }
    grep -q '^APP btaudio apply=\(重新連線以套用\|Reconnect to apply\)$' <<<"$out" || { EVID="$(grep '^APP btaudio apply' <<<"$out" | tr '\n' '|')"; fail btaudio_page_apply_hint; }; }
nowrun btaudio_pref_caps "screen on; vol 0; $HP; simbt caps ldac,aac,sbc; $CONN; seti bt_codec 1; seti bt_rate 44100; @0.3; outstat tw" && {
    grep -q '^APP outstat bt=正在傳輸：AAC 44.1k / 16-bit / 立體聲 / 256 kbps（設定：偏好 AAC）$' <<<"$out" || { EVID="$(grep '^APP outstat bt' <<<"$out")"; fail btaudio_pref_caps; }
    grep -qx 'APP outstat actual=實際：AAC 44.1k / 16-bit' <<<"$out" && grep -qx 'APP outstat best=耳機最高：LDAC 96k / 24-bit' <<<"$out" || { EVID="$(grep '^APP outstat' <<<"$out" | tr '\n' '|')"; fail btaudio_pref_best_stays; }; }
# apply = one reconnect request for all the changes of a visit, none without a change, never a reboot
nowrun btaudio_apply_once "screen on; vol 0; $HP; simbt caps aac,sbc; $CONN; @0.3; btaudio open; @0.3; btaudio key codec; btaudio key rate; btaudio key chan; back; @0.3" && {
    [ "$(grep -c '^APP bt apply prefs' <<<"$out")" = 1 ] || { EVID="$(grep -c '^APP bt apply' <<<"$out") applies"; fail btaudio_apply_once; }; }
nowrun btaudio_apply_none "screen on; vol 0; $HP; simbt caps aac,sbc; $CONN; @0.3; btaudio open; @0.3; back; @0.3" && {
    ! grep -q '^APP bt apply prefs' <<<"$out" || fail btaudio_apply_none; }
# FIX4 I3: a stored preference not applied yet is the NEXT connection; the actual line keeps the link's codec until the reconnect
nowrun btaudio_next_line "screen on; vol 0; $HP; simbt caps ldac,aac,sbc; $CONN; @0.3; btaudio open; @0.3; btaudio rows; btaudio key codec; axishalf -1; btaudio key codec; axishalf 1; @0.3; btaudio rows" && {
    n=$(grep -c '^APP btaudio next=\(下次連線：\|Next connection: \)' <<<"$out")
    { [ "$n" = 1 ] && grep -q '^APP btaudio actual=\(實際\|Actual\)[：:] \?LDAC' <<<"$out" && grep -qx 'APP btaudio next=' <<<"$out"; } || { EVID="$(grep '^APP btaudio \(next\|actual\)' <<<"$out" | tr '\n' '|')"; fail btaudio_next_line; }; }
# the Wired output page (round board): the wired ceiling is the one row; a change while a 192k file plays re-plans it at once (a short mute on the board), no reboot
nowrun wired_page_round "screen on; vol 0; seti dac_max_rate 48000; wired open; @0.3; wired rows; wired key dacmax; wired rows" && {
    [ "$(grep -c '^APP wired row=' <<<"$out")" = 2 ] && grep -qx 'APP wired rows=1' <<<"$out" && grep -q '^APP cfg dac_max_rate=' <<<"$out" || true
    grep -q '^APP wired live=\(沒有在播放\|Nothing playing\)$' <<<"$out" || { EVID="$(grep '^APP wired' <<<"$out" | tr '\n' '|')"; fail wired_page_round; }; }
nowrun wired_change_reopens "screen on; vol 0; seti dac_max_rate 48000; mp3ui; @0.3; simaudiorate 192000 1; mp3 1; @0.6; outstat tw; wired open; @0.3; wired key dacmax; wired key dacmax; @0.3; wired key dacmax; @0.3; outstat tw; @4.5; outstat tw" && {
    # FIX4 I5: the two steps are a draft (the playing chain is untouched while the knob is borrowed); one re-open when it is released
    [ "$(grep -c '^APP audio wired reopen' <<<"$out")" = 1 ] && [ "$(grep -c '^APP outstat wired=輸入 192k → 輸出 48k・折半（設定：有線上限 48k）$' <<<"$out")" = 2 ] && grep -q '^APP outstat wired=輸入 192k → 輸出 48k・折半（設定：有線上限 48k）$' <<<"$out" && grep -q '^APP outstat wired=輸入 192k → 輸出 192k・直通（原始）$' <<<"$out" \
      || { EVID="$(grep '^APP outstat wired\|^APP audio wired\|^APP pipe' <<<"$out" | tr '\n' '|')"; fail wired_change_reopens; }; }
# the reasons of the wired stages (the owner's examples): 96k file at the round board's 48k ceiling, an odd rate, and what plays says it in three languages
nowrun outstat_round_96k "screen on; vol 0; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.6; outstat tw; outstat cn; outstat en" && {
    grep -qx 'APP outstat wired=輸入 96k → 輸出 48k・折半（設定：有線上限 48k）' <<<"$out" && grep -qx 'APP outstat wired=输入 96k → 输出 48k・折半（设置：有线上限 48k）' <<<"$out" \
      && grep -qx 'APP outstat wired=In 96k → out 48k・halved (setting: wired limit 48k)' <<<"$out" || { EVID="$(grep '^APP outstat wired' <<<"$out" | tr '\n' '|')"; fail outstat_round_96k; }; }
nowrun outstat_source_equal "screen on; vol 0; seti dac_max_rate 384000; mp3ui; @0.3; simaudiorate 48000 1; mp3 1; @0.6; outstat tw" && {
    grep -q '^APP outstat wired=輸入 48k → 輸出 48k・直通（原始）' <<<"$out" || { EVID="$(grep '^APP outstat wired' <<<"$out")"; fail outstat_source_equal; }; }
# the details page: the 輸出 block at the bottom, wired and Bluetooth
nowrun meta_block_wired "screen on; vol 0; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.6; outstat tw" && {
    [ "$(grep -c '^APP outstat block ' <<<"$out")" -ge 4 ] && grep -qx 'APP outstat block 輸出：有線' <<<"$out" && grep -q '^APP outstat block 處理：折半 96k→48k（設定：有線上限 48k）$' <<<"$out" || { EVID="$(grep '^APP outstat block' <<<"$out" | tr '\n' '|')"; fail meta_block_wired; }; }
nowrun meta_block_bt "screen on; vol 0; $HP; simbt caps aac,sbc; $CONN; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.6; simsink 1; @0.3; outstat tw" && {
    grep -qx 'APP outstat block 輸出：藍牙耳機' <<<"$out" && grep -q '^APP outstat block 編碼：AAC（耳機：最高 AAC' <<<"$out" && grep -q '^APP outstat block 取樣率：48k（耳機：上限 48k' <<<"$out" \
      || { EVID="$(grep '^APP outstat block' <<<"$out" | tr '\n' '|')"; fail meta_block_bt; }; }
nowrun meta_block_page "screen on; vol 0; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.6; uipush meta; @0.3; uitop; uiboxes" && {
    grep -qx 'APP uitop meta' <<<"$out" || fail meta_block_page; }
nowrun meta_block_none "screen on; vol 0; outstat tw" && {
    grep -qx 'APP outstat block=none' <<<"$out" || fail meta_block_none; }
# speed off by the mode and effects paused say why (mode bits: fx mode 4 = aptX-class)
nowrun outstat_speed_mode "screen on; vol 0; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @0.6; speedstep 1; fx mode 4; @0.3; outstat tw" && {
    grep -q '^APP outstat block 倍速：關（模式：' <<<"$out" || { EVID="$(grep '^APP outstat' <<<"$out" | tr '\n' '|')"; fail outstat_speed_mode; }; }
