# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: need_bbs)
need_bbs
# --- AVRCP 1.6 step 1: speaker mode shows the phone's title, artist, album and cover (fix round 1: the fake phone of fake_bt_svc.c answers the SAME core
# pc_bma_* that bt_meta_avrc.c feeds on the board: TRACK_CHANGED, asks, answers, the cover-art link, GETs in pieces) ---
BMSTREAM="screen on; @0.5; vol 0; simbt stream; uipush now; @0.5"
# nothing sent: the page is as before AVRCP (the phone's name in the middle, no band, no 詳細 cell); clear brings that back pixel for pixel
SIM_STATE="bt=connected" nowrun bm_none "$BMSTREAM; btmeta; uiboxes; shot $T/bm0.png; nowmenu -2; @0.3; nowmenu -5; nowmenu -2; @4.5; simbtmeta title Paper Planes; @0.3; simbtmeta clear; @0.5; shot $T/bm1.png; btmeta; status" && {
    [ "$(bml 1)" = "APP btmeta title= artist= album= track=0/0 ms=0 art=- art_bytes=0 art_state=none" ] || { EVID="$(bml 1)"; fail bm_none_console; }
    grep -q '^APP uibox label .*|Bluetooth:  phone$' <<<"$out" && [ -z "$(bmband 1)" ] || { EVID="$(grep '^APP uibox label' <<<"$out" | head -3 | tr '\n' ' ')"; fail bm_none_name_in_middle; }
    grep -q '^APP now cells=2 items=host,stop$' <<<"$out" || { EVID="$(grep '^APP now cells' <<<"$out")"; fail bm_none_no_details; }
    cmp -s "$T/bm0.png" "$T/bm1.png" || { EVID="the page after clear differs from the one before any metadata"; fail bm_clear_same_page; }
    [ "$(bml 2)" = "$(bml 1)" ] && grep -q '^APP status Source: Bluetooth (phone)$' <<<"$out" || { EVID="$(bml 2) / $(grep '^APP status Source' <<<"$out")"; fail bm_clear_status; }; }
# title, artist, album: the band as a local track's ("title  -  artist"), the disc with the placeholder record, 詳細 with the album, the 現況 line with the title
SIM_STATE="bt=connected" nowrun bm_title "$BMSTREAM; simbtmeta title Paper Planes; simbtmeta artist Sim Band; simbtmeta album Night Train; @0.5; uiboxes; btmeta; status; nowmenu -2; @0.3; nowmenu -5; nowmenu 1; @0.5; uitop; uiboxes" && {
    [ "$(bmband 1)" = "Paper Planes  -  Sim Band" ] || { EVID="band '$(bmband 1)'"; fail bm_title_band; }
    [ "$(bml 1)" = "APP btmeta title=Paper Planes artist=Sim Band album=Night Train track=0/0 ms=0 art=- art_bytes=0 art_state=none" ] || { EVID="$(bml 1)"; fail bm_title_console; }
    grep -q '^APP status Source: Bluetooth (phone) · Paper Planes$' <<<"$out" || { EVID="$(grep '^APP status Source' <<<"$out")"; fail bm_title_status; }
    grep -q '^APP now cells=3 items=host,info,stop$' <<<"$out" || { EVID="$(grep '^APP now cells' <<<"$out")"; fail bm_title_details_cell; }
    [ "$(tp)" = "meta " ] && grep -q '^APP uibox label .*|Paper Planes$' <<<"$out" && grep -q '^APP uibox label .*|Sim Band$' <<<"$out" && grep -q '^APP uibox label .*|Album  Night Train$' <<<"$out" || { EVID="top $(tp) $(grep '^APP uibox label 50' <<<"$out" | tr '\n' ' ')"; fail bm_title_details_page; }
    [ "$(grep '^APP bt meta title=' <<<"$out" | tail -1)" = "APP bt meta title=Paper Planes artist=Sim Band album=Night Train art=-" ] || { EVID="$(grep '^APP bt meta' <<<"$out" | tail -2 | tr '\n' '|')"; fail bm_title_last_line; }; }
# the cover: the art replaces the placeholder (another picture), a new file replaces it again, "art -" (no handle) gives the placeholder back pixel for pixel (rot 0: the disc stands still, the shots do not depend on the turn timer's phase)
SIM_STATE="bt=connected" nowrun bm_cover "$BMSTREAM; rot 0 50; simbtmeta title Paper Planes; @0.5; shot $T/bc0.png; simbtmeta art /sdcard/_player/config/radio.txt; @0.5; shot $T/bc1.png; btmeta; simbtmeta art /sdcard/_player/config/app.txt; @0.5; shot $T/bc2.png; simbtmeta art -; @0.5; shot $T/bc3.png; btmeta" && {
    ! cmp -s "$T/bc0.png" "$T/bc1.png" && ! cmp -s "$T/bc1.png" "$T/bc2.png" || { EVID="the cover did not change the page"; fail bm_cover_shown; }
    cmp -s "$T/bc0.png" "$T/bc3.png" || { EVID="no handle: not the placeholder of before"; fail bm_cover_placeholder_back; }
    sz=$(stat -c %s sim/fixture/_player/config/radio.txt)
    [ "$(bml 1)" = "APP btmeta title=Paper Planes artist= album= track=0/0 ms=0 art=0000001 art_bytes=$sz art_state=ok" ] && [ "$(bml 2)" = "APP btmeta title=Paper Planes artist= album= track=0/0 ms=0 art=- art_bytes=0 art_state=none" ] || { EVID="$(bml 1) / $(bml 2)"; fail bm_cover_console; }; }
# metadata that came before the stream: shown once the stream takes the player; the stream stopping and coming back keeps it; a cover without a title stays hidden
SIM_STATE="bt=connected" nowrun bm_before_stream "screen on; @0.5; vol 0; simbtmeta art /sdcard/_player/config/radio.txt; simbtmeta title Night Owl; simbt stream; uipush now; @0.5; uiboxes; simbt stop; @0.5; simbt stream; uipush now; @0.5; uiboxes; simbtmeta title ; @0.5; uiboxes" && {
    [ "$(bmband 1)" = "Night Owl" ] && [ "$(bmband 2)" = "Night Owl" ] || { EVID="'$(bmband 1)' '$(bmband 2)'"; fail bm_before_stream; }
    [ -z "$(bmband 3)" ] && [ "$(grep -c '|Bluetooth:  phone$' <<<"$out")" = 1 ] || { EVID="band '$(bmband 3)'"; fail bm_empty_title_is_unknown; }; }
# a long CJK title is cut whole (159 bytes at most, never half a character), control characters become spaces
LONG=$(printf '夜航星圖%.0s' $(seq 1 20))
nowrun bm_long "simbtmeta title $LONG; @0.5; btmeta" && {
    t=$(bml 1 | sed 's/^APP btmeta title=\(.*\) artist=.*/\1/')
    [ "${#t}" -gt 0 ] && [ "$(printf '%s' "$t" | wc -c)" -le 159 ] && printf '%s' "$t" | iconv -f UTF-8 -t UTF-8 >/dev/null 2>&1 && [[ "$LONG" == "$t"* ]] || { EVID="$(printf '%s' "$t" | wc -c) bytes"; fail bm_long_cut; }; }
# the phone goes (AVRCP link down): its track goes at once, the page is the phone's name again; a new phone shows nothing of the old one
SIM_STATE="bt=connected" nowrun bm_link_drop "$BMSTREAM; simbtmeta title Paper Planes; simbtmeta art /sdcard/_player/config/radio.txt; @0.5; btmeta; simbtmeta link 0; @0.3; btmeta; uiboxes; simbtmeta link 1; @0.5; btmeta; status" && {
    grep -q '^APP btmeta title=Paper Planes .* art=0000001 ' <<<"$(bml 1)" || { EVID="$(bml 1)"; fail bm_link_drop_before; }
    [ "$(bml 2)" = "APP btmeta title= artist= album= track=0/0 ms=0 art=- art_bytes=0 art_state=none" ] && grep -q '|Bluetooth:  phone$' <<<"$out" || { EVID="$(bml 2)"; fail bm_link_drop_gone; }
    [ "$(bml 3)" = "APP btmeta title=Paper Planes artist= album= track=0/0 ms=0 art=0000001 art_bytes=$(stat -c %s sim/fixture/_player/config/radio.txt) art_state=ok" ] || { EVID="$(bml 3)"; fail bm_link_new_phone_asks_again; }; }
# one cover-art transfer at a time: the phone keeps the first GET (hold), the track changes to another picture, the old thumbnail then arrives late: dropped,
# never shown as the new track's cover; the new handle is asked only after the old transfer ended, and its picture is the one shown
sz1=$(stat -c %s sim/fixture/_player/config/radio.txt); sz2=$(stat -c %s sim/fixture/_player/config/app.txt)
SIM_STATE="bt=connected" nowrun bm_late_art "$BMSTREAM; simbtmeta hold 1; simbtmeta title Paper Planes; simbtmeta art /sdcard/_player/config/radio.txt; @0.5; btmeta; simbtmeta art /sdcard/_player/config/app.txt; @0.5; btmeta; simbtmeta hold 0; @0.5; btmeta" && {
    grep -q 'art=0000001 art_bytes=0 art_state=asking$' <<<"$(bml 1)" && grep -q 'art=0000002 art_bytes=0 art_state=asking$' <<<"$(bml 2)" || { EVID="$(bml 1) / $(bml 2)"; fail bm_late_art_one_get; }
    [ "$(grep -c '^APP bt art bytes=' <<<"$out")" = 1 ] && grep -q "^APP bt art bytes=$sz2 " <<<"$out" && ! grep -q "^APP bt art bytes=$sz1 " <<<"$out" || { EVID="$(grep '^APP bt art' <<<"$out" | tr '\\n' '|')"; fail bm_late_art_dropped; }
    grep -q "art=0000002 art_bytes=$sz2 art_state=ok$" <<<"$(bml 3)" || { EVID="$(bml 3)"; fail bm_late_art_new_shown; }; }
# the decoder refuses the cover for low internal memory: the page says so under the placeholder record, btmeta art_state=no_mem; one retry 5 s later shows it
SIM_STATE="bt=connected" nowrun bm_lowmem "$BMSTREAM; rot 0 50; simthumblow 1; simbtmeta title Paper Planes; simbtmeta art /sdcard/_player/config/radio.txt; @0.8; btmeta; uiboxes; shot $T/lm0.png; simbtmeta art -; @0.5; shot $T/lm1.png; simthumblow 1; simbtmeta art /sdcard/_player/config/radio.txt; @0.8; @6; btmeta; uiboxes" && {
    grep -q 'art_state=no_mem$' <<<"$(bml 1)" && grep -q '^APP bt art shown=0 reason=low_internal$' <<<"$out" || { EVID="$(bml 1)"; fail bm_lowmem_state; }
    [ "$(grep -c '^APP uibox label .*|Cover unavailable (low memory)$' <<<"$out")" = 1 ] || { EVID="$(grep -c 'Cover unavailable' <<<"$out") notes"; fail bm_lowmem_note; }
    ! cmp -s "$T/lm0.png" "$T/lm1.png" || { EVID="the note did not change the page"; fail bm_lowmem_visible; }
    grep -q '^APP bt art decode retry$' <<<"$out" && grep -q 'art_state=ok$' <<<"$(bml 2)" || { EVID="$(bml 2)"; fail bm_lowmem_retry; }; }
# fix round 2: the metadata clock (a 50 ms LVGL timer) runs only while a request is open: before any phone it is paused, while a cover downloads (the
# fake phone holds the GET) it runs, once the answer and the picture are in it is paused again, and stays so while the phone is idle-linked
nowrun bm_timer_idle "simtimers; simbtmeta hold 1; simbtmeta title X; simbtmeta art /sdcard/_player/config/radio.txt; @0.5; simtimers; btmeta; simbtmeta hold 0; @0.5; simtimers; btmeta; @5; simtimers" && {
    base=$(t50 1)
    [ "$(t50p 1)" -ge 1 ] && [ "$(t50 2)" = $((base + 1)) ] && grep -q 'art_state=asking$' <<<"$(bml 1)" || { EVID="$(grep '^SIM timers' <<<"$out" | tr '\n' '|')"; fail bm_timer_runs_while_open; }
    [ "$(t50 3)" = "$base" ] && [ "$(t50 4)" = "$base" ] && [ "$(t50p 4)" = "$(t50p 1)" ] && grep -q 'art_state=ok$' <<<"$(bml 2)" || { EVID="$(grep '^SIM timers' <<<"$out" | tr '\n' '|')"; fail bm_timer_idle_after; }; }
# --- end AVRCP 1.6 step 1 ---

# --- toy sound effects (sfx: a chiptune synth mixed before the device gain, the internal notes) ---
# "sfx" prints "APP sfx on=<0/1> posted=<n> off=<n> full=<n> stale=<n> started=<n> stolen=<n> mixed=<blocks> last=<newest ids, the newest last>"; the fake audio service
# runs the firmware's sfx_mix every 20 ms tick into a scratch block (mixed = blocks the effects changed). "sfx play <name>" posts like a toy.
nowrun sfx_idle "@0.5; sfx" && { [ "$(sx)" = "on=1 posted=0 off=0 full=0 stale=0 started=0 stolen=0 mixed=0 last=-" ] || { EVID="$(sx)"; fail sfx_idle; }; }
BP=$([ "${BB_S:-0}" = 0 ] && echo 1 || echo 0)    # a plain bubble (BB_S: the special one, from bubbles_open)
nowrun sfx_bubbles "screen on; tools bubbles; @0.3; bubbles pop $BP; @0.1; bubbles pop ${BB_S:-0}; @0.5; sfx" && {
    [ "$(sxv last)" = "pop,pop_big" ] && [ "$(sxv started)" = 2 ] && [ "$(sxv posted)" = 2 ] && [ "$(sxv mixed)" -ge 5 ] || { EVID="$(sx)"; fail sfx_bubbles; }; }
# while the music plays: the effect is mixed over it, the music is not touched (one track, no stop)
nowrun sfx_over_music "screen on; vol 0; track 3; @2; tools bubbles; @0.3; bubbles pop $BP; @0.5; sfx; audio info" && {
    [ "$(sxv last)" = "pop" ] && [ "$(sxv mixed)" -ge 2 ] && [ "$(grep -c '^APP track idx=' <<<"$out")" = 1 ] && ! grep -q '^APP mp3 stop\|^APP play stop' <<<"$out" || { EVID="$(sx)"; fail sfx_over_music; }; }
# the setting off (cfg toysfx=0): nothing is posted or mixed
NOWCFG=toysfx=0 nowrun sfx_off "screen on; tools bubbles; @0.3; bubbles pop $BP; bubbles pop ${BB_S:-0}; @0.5; sfx" && {
    [ "$(sx)" = "on=0 posted=0 off=2 full=0 stale=0 started=0 stolen=0 mixed=0 last=-" ] || { EVID="$(sx)"; fail sfx_off; }; }
# the settings row: a plain tap toggles it (no axis), its label says the state
nowrun sfx_setting_row "setui; @0.3; settings group sound; settings rows; settings key toysfx; settings rows; sfx; settings key toysfx; settings rows; sfx" && {
    r=$(grep '^APP settings row=' <<<"$out" | grep '小工具音效\|Toy sounds' | sed 's/^APP settings row=//' | tr '\n' '|')
    [ "$r" = "Toy sounds: on|Toy sounds: off|Toy sounds: on|" ] || [ "$r" = "小工具音效: 開|小工具音效: 關|小工具音效: 開|" ] || { EVID="$r"; fail sfx_setting_row; }
    [ "$(grep '^APP sfx on=' <<<"$out" | cut -d' ' -f3 | tr '\n' ' ')" = "on=0 on=1 " ] || { EVID="$(grep '^APP sfx' <<<"$out" | tr '\n' '|')"; fail sfx_setting_cfg; }; }
# 拉霸: a tick per reel step, a stop per reel; a winning spin ends with win, a big one (>= 60, the rim chase) with the jackpot jingle (sfx task 2)
nowrun sfx_slot "screen on; tools slot; @0.5; tools spin big; @6; sfx" && {
    l=$(sxv last); st=$(sxv started)
    [ "${st:-0}" -ge 10 ] && grep -q 'tick' <<<"$l" && grep -q 'stop' <<<"$l" && [ "${l##*,}" = jackpot ] && grep -q '^APP tools slot .* win=[1-9]' <<<"$out" || { EVID="$(sx)"; fail sfx_slot; }; }
nowrun sfx_slot_small "screen on; tools slot; @0.5; tools grid 001234561; @6; sfx" && {   # two cherries: win=2
    l=$(sxv last); [ "${l##*,}" = win ] || { EVID="$(sx)"; fail sfx_slot_small; }; }
# 擲筊: one clack per block landing, then the result's own sound (sfx task 2: 聖筊 / 笑筊 / 陰筊, the one the page shows)
nowrun sfx_jiao "screen on; fortune jiao; @0.3; fortune draw; @2; sfx" && {
    r=$(grep -m1 '^APP fortune jiao result=' <<<"$out" | sed 's/.*result=\([0-2]\).*/\1/'); want=(jiao_sheng jiao_xiao jiao_yin)
    { [ -n "$r" ] && [ "$(sxv last)" = "clack,clack,${want[$r]}" ] && [ "$(sxv started)" = 3 ]; } || { EVID="r=$r $(sx)"; fail sfx_jiao; }; }
# 骰子: ticks while tumbling, one dice per die landing
NOWCFG=dice_n=2 nowrun sfx_dice "screen on; tools dice; @0.5; tools roll; @2; sfx" && {
    l=$(sxv last); [ "$(tr ',' '\n' <<<"$l" | grep -c '^dice$')" = 2 ] && grep -q 'tick' <<<"$l" || { EVID="$(sx)"; fail sfx_dice; }; }
# the console names: an unknown one says so and posts nothing, a known one is started at the next block
nowrun sfx_console "sfx play nope; sfx play win; @0.5; sfx" && {
    grep -q '^APP sfx unknown nope$' <<<"$out" && [ "$(sxv last)" = win ] && [ "$(sxv posted)" = 1 ] || { EVID="$(grep '^APP sfx' <<<"$out" | tr '\n' '|')"; fail sfx_console; }; }
# the audio path (the board's file): one route per block from S.paused (pc_sfx_route, host-tested): not paused = added after the post hook and BEFORE
# the gain; paused = added AFTER the gain loop at the device-volume gain times the sleep fade (fix round 1; task 2 m1); the proven unity rule's lines are untouched
A=app/main/audio_svc.c
{ [ "$(grep -c 'sfx_mix(' $A)" = 2 ] && [ "$(grep -c 'pc_sfx_route(' $A)" = 1 ] \
  && grep -qF '        const pc_sfx_route_t sfx_rt = pc_sfx_route(S.paused, s_target_gain, S.fading ? S.fade_gain : 1.0f);' $A \
  && grep -qF '        if (!sfx_rt.post) sfx_mix(buf, frames, brate, 1.0f);' $A \
  && grep -qF '        if (sfx_rt.post) sfx_mix(buf, frames, brate, sfx_rt.gain);' $A \
  && sed -n '/^static void task(void \*arg)/,/^}/p' $A | awk '/post\(buf, frames, brate/{p=NR} /pc_sfx_route\(/{q=NR} /if \(!sfx_rt.post\) sfx_mix\(/{m=NR} /pc_rv_block\(sink != NULL/{r=NR} /buf\[2 \* i\] \*= gain; buf\[2 \* i \+ 1\] \*= gain;/{g=NR} /if \(sfx_rt.post\) sfx_mix\(/{a=NR} /sink\(buf \+ 2 \* k/{k=NR} /bsp_audio_write32\(/{w=NR} END{exit !(p && q && m && r && g && a && k && w && p < q && q < m && m < r && r < g && g < a && a < k && k < w)}' \
  && grep -qF '        pc_rv_block_t bg = pc_rv_block(sink != NULL, __atomic_load_n(&s_remote, __ATOMIC_SEQ_CST) != NULL, S.paused, unity_prev, gain, s_target_gain);' $A \
  && [ "$(grep -c 'aud_sink_fn sink = s_sink;' $A)" = 1 ]; } || fail sfx_before_gain
# --- the sound bank and its audition page (sfx task 2, the internal notes) ---
# the bank text (app/main/sfx_bank.def) is parsed at boot: nothing refused, every name a toy calls is there
nowrun sfx_bank_loaded "sfx bank" && {
    n=$(grep '^APP sfx bank n=' <<<"$out" | tail -1 | sed 's/.*n=\([0-9]*\).*/\1/')
    have=1; for e in pop pop_big tick stop win clack dice jackpot jiao_sheng jiao_xiao jiao_yin extra_life; do grep -q "^APP sfx entry [0-9]* $e " <<<"$out" || have=0; done
    grep -q '^APP sfx bank n=[0-9]* dropped=0 ' <<<"$out" && [ "${n:-0}" -ge 30 ] && ! grep -q '^APP sfx bank refused' <<<"$out" && [ "$have" = 1 ] \
      && [ "$(grep -c '^APP sfx entry ' <<<"$out")" = "$n" ] || { EVID="$(grep '^APP sfx bank' <<<"$out" | tr '\n' '|')"; fail sfx_bank_loaded; }; }
# 音效庫 in 小工具: the ninth cell; one row an entry under its group's header, a tap plays it, 說明 is the last row, Back returns to the ring
nowrun sfxbank_page "sfx bank; screen on; tools sfxbank; @0.5; uistack; sfxbank rows; sfxbank tap extra_life; @1; sfx; help tap; @0.3; uitop; back; @0.3; back; @0.3; uistack" && {
    stack_is sfxbank_open 1 "menu tools tools_sfxbank"; stack_is sfxbank_back 2 "menu tools"
    [ "$(grep -c '^APP sfxbank row ' <<<"$out")" = "$(grep -c '^APP sfx entry ' <<<"$out")" ] \
      && grep -qx 'APP sfxbank play extra_life ok=1' <<<"$out" && [ "$(sxv last)" = extra_life ] && grep -qx 'APP uitop explain' <<<"$out" || { EVID="$(sx) $(grep '^APP uitop\|^APP sfxbank play' <<<"$out" | tr '\n' ' ')"; fail sfxbank_page; }; }
# the audition plays even with 小工具音效 off (the tap asks for it); the toys stay silent
NOWCFG=toysfx=0 nowrun sfxbank_audition_off "screen on; tools sfxbank; @0.5; sfxbank tap coin; @0.5; sfx play pop; @0.3; sfx" && {
    [ "$(sxv last)" = coin ] && [ "$(sxv posted)" = 1 ] && [ "$(sxv off)" = 1 ] || { EVID="$(sx)"; fail sfxbank_audition_off; }; }
# the shared-area doc registers the bank and the page
# --- end sfx ---

