# SPDX-License-Identifier: LGPL-2.1-or-later
# --- cardfmt (記憶卡格式化: only a card that failed to mount with gpt / nofs, behind a warning, the Konami code by taps and a final armed tap;
# the internal notes). The fake SD layer (sim/fake_sd.c): "simsd fail gpt|nofs|nocard|err", "simsd ok", "simsd size <bytes>",
# "simsd formatfail 0|1|write|mkfs|mount", "simsd swap|fixed" (fix round 1), "simsd" (state: formats=<n> counts the formats that reached the first write); a fake format prints
# "SIM sd format fs= cluster= bu= align= start= sectors= serial=" when it passes the checks the board makes before writing
# and ends 1.5 s later. "cardfmt" dumps the flow (APP cardfmt view= step= got= top= row=) and the service (APP sd format state= err= offered= reason= mounted= usb=);
# "cardfmt open|row|continue|arm" tap the card page's row, 繼續, the final row; "codepad" dumps the pad (APP codepad len= lit= keys=), "codepad tap u|d|l|r|b|a", "codepad at <k>".
cf() { grep '^APP cardfmt view=' <<<"$out" | sed "$1q;d" | sed 's/^APP cardfmt //'; }          # the $1-th flow dump
KON="codepad tap u; codepad tap u; codepad tap d; codepad tap d; codepad tap l; codepad tap r; codepad tap l; codepad tap r; codepad tap b; codepad tap a"
TO_CODE="simsd fail gpt; setui; @0.3; settings key card; @0.3; cardfmt row; @0.3; cardfmt continue; @0.3"
# no entry (cardsafe: the settings row now shows for any failed mount, with the guide; the FORMAT is what is not offered): a mounted card (the fixture), no card (sd=0), an "err" mount, the USB drive mode: no settings row, the card page without the row, offered=0
nowrun cardfmt_no_entry_mounted "setui; @0.3; settings rows; settings key card; cardfmt; cardfmt open; @0.3; uitop; cardfmt row; @0.3; uitop" && {
    ! grep -q '^APP settings card=' <<<"$out" && grep -qx 'APP settings card none' <<<"$out" && [ "$(sdf 1 | grep -o 'offered=[0-9] reason=[^ ]* mounted=[0-9]')" = "offered=0 reason=- mounted=1" ] \
      && [ "$(tp)" = "card card " ] && grep -qx 'APP cardfmt no row' <<<"$out" && [ "$(nfmt)" = 0 ] || { EVID="$(sdf 1) top $(tp)"; fail cardfmt_no_entry_mounted; }; }
SIM_STATE="sd=0" nowrun cardfmt_no_entry_nocard "setui; @0.3; settings rows; settings key card; cardfmt; cardfmt open; @0.3; cardfmt row; simsd fail nocard; cardfmt" && {
    grep -qx 'APP settings card=Card cannot be used (no card)' <<<"$out" && [ "$(sdf 1 | grep -o 'offered=[0-9] reason=[^ ]*')" = "offered=0 reason=nocard" ] && grep -qx 'APP cardfmt no row' <<<"$out" \
      && [ "$(sdf 2 | grep -o 'offered=[0-9]')" = "offered=0" ] && [ "$(nfmt)" = 0 ] || { EVID="$(sdf 1)|$(sdf 2)"; fail cardfmt_no_entry_nocard; }; }
nowrun cardfmt_no_entry_err "simsd fail err; setui; @0.3; settings rows; settings key card; cardfmt" && {
    grep -qx 'APP settings card=Card cannot be used (read error)' <<<"$out" && [ "$(sdf 1 | grep -o 'offered=[0-9] reason=[^ ]*')" = "offered=0 reason=err" ] || { EVID="$(sdf 1)"; fail cardfmt_no_entry_err; }; }
fresh; out=$($SIM --headless --mode ext --root "$T/fix" --cmd "simsd fail gpt; cardfmt" 2>&1)
[ "$(sdf 1 | grep -o 'offered=[0-9] reason=[^ ]* mounted=[0-9] usb=[0-9]')" = "offered=0 reason=gpt mounted=0 usb=1" ] || { EVID="$(sdf 1)"; fail cardfmt_no_entry_usb_mode; }
# the entry for gpt and nofs: the settings list's first row, the card page with the format row
nowrun cardfmt_entry_rows "simsd fail gpt; setui; @0.3; settings rows; back; simsd fail nofs; setui; @0.3; settings rows; settings key card; @0.3; uitop; cardfmt row; @0.3; uitop; cardfmt" && {
    [ "$(grep '^APP settings card=' <<<"$out" | tr '\n' '|')" = "APP settings card=Card cannot be used (GPT partitions)|APP settings card=Card cannot be used (no readable file system)|" ] \
      && [ "$(tp)" = "card cardfmt " ] && [ "$(cfv 1 step)" = warn ] && [ "$(nfmt)" = 0 ] || { EVID="$(grep '^APP settings card=' <<<"$out" | tr '\n' '|') top $(tp) $(cf 1)"; fail cardfmt_entry_rows; }; }
# the whole path: the warning, a wrong tap that clears the entry, the code, the armed tap (one tap arms, the second formats), the progress page, 完成;
# the fake recorded FAT32 with 32 KB clusters at the 4 MiB boundary for the "32 GB" card; the folders exist and the library scan ran
SECT=$((31914983424 / 512)); PART=$((SECT - 8192))
nowrun cardfmt_full_path "vol 0; track 1; @1; cardfmt; $TO_CODE; cardfmt; codepad; codepad tap u; codepad tap u; codepad tap d; codepad; codepad tap u; codepad; cardfmt; $KON; @0.3; cardfmt; simsd; cardfmt arm; cardfmt; simsd; cardfmt arm; @0.3; cardfmt; @0.5; cardfmt; @1.5; cardfmt; uitop; simsd; back; @0.3; uitop; cardfmt" && {
    [ "$(cfv 2 step)" = code ] && [ "$(cpd 1)" = "len=10 lit=0 keys=" ] && [ "$(cpd 2)" = "len=10 lit=3 keys=UUD" ] && [ "$(cpd 3)" = "len=10 lit=0 keys=" ] && [ "$(cfv 3 step)" = code ] && [ "$(cfv 3 got)" = 0 ] \
      || { EVID="$(cf 2)|$(cpd 1)|$(cpd 2)|$(cpd 3)|$(cf 3)"; fail cardfmt_wrong_tap_clears; }
    [ "$(cfv 4 step)" = arm ] && [ "$(cfrow 4)" = "Last check: format 31.9 GB" ] && [ "$(cfrow 5)" = "Tap again: format 31.9 GB" ] && [ "$(cfv 5 step)" = arm ] \
      || { EVID="$(cf 4)|$(cf 5)"; fail cardfmt_arm_row; }
    [ "$(grep '^SIM sd mounted=' <<<"$out" | sed -n 3p | grep -o 'formats=[0-9]*')" = "formats=0" ] || { EVID="$(grep '^SIM sd mounted=' <<<"$out" | tr '\n' '|')"; fail cardfmt_first_arm_tap_formats_nothing; }
    [ "$(nfmt)" = 1 ] && grep -qx "SIM sd format fs=FAT32 cluster=32768 bu=8192 align=8192 start=8192 sectors=$PART serial=5D01" <<<"$out" \
      && grep -qx "APP sd format start size=31914983424 fs=FAT32 cluster=32768 bu=8192 align=8192 start=8192 sectors=$PART" <<<"$out" || { EVID="$(grep '^SIM sd format\|^APP sd format start' <<<"$out" | tr '\n' '|')"; fail cardfmt_format_args; }
    [ "$(cfv 6 view)" = run ] && [ "$(cfv 7 view)" = run ] && [ "$(cfv 8 view)" = result ] && [ "$(sdf 8 | cut -d' ' -f1)" = "state=done" ] && grep -Eq '^APP sd format done ms=1[5-9][0-9]{2} stage=mount mounted=1$' <<<"$out" \
      || { EVID="$(cf 6)|$(cf 7)|$(cf 8)|$(grep '^APP sd format done' <<<"$out")"; fail cardfmt_progress_done; }
    [ "$(cfv 1 playing)" = 1 ] && [ "$(cfv 2 playing)" = 0 ] && grep -q '^APP scan n=' <<<"$out" && [ -d "$T/fix/_player/config" ] && [ -d "$T/fix/_player/index" ] || { EVID="$(cf 1)|$(cf 2)|$(grep '^APP scan' <<<"$out" | tr '\n' '|')"; fail cardfmt_audio_stopped_scan; }
    [ "$(grep '^SIM sd mounted=' <<<"$out" | sed -n 4p | cut -d' ' -f3-4)" = "mounted=1 reason=-" ] && [ "$(tp | cut -d' ' -f1-2)" = "cardfmt card" ] && [ "$(sdf 9 | grep -o 'offered=[0-9]')" = "offered=0" ] \
      || { EVID="$(grep '^SIM sd mounted=' <<<"$out" | tr '\n' '|') top $(tp)"; fail cardfmt_after_done; }; }
# exFAT above 32 GiB (a "64 GB" card: 128 KB clusters, 16 MiB boundary), from the nofs entry
SECT=$((63864569856 / 512)); PART=$((SECT - 32768))
nowrun cardfmt_exfat "simsd size 63864569856; simsd fail nofs; setui; @0.3; settings key card; @0.3; cardfmt row; @0.3; cardfmt continue; @0.3; $KON; @0.3; cardfmt arm; cardfmt arm; @2; cardfmt" && {
    [ "$(nfmt)" = 1 ] && grep -qx "SIM sd format fs=exFAT cluster=131072 bu=32768 align=32768 start=32768 sectors=$PART serial=5D01" <<<"$out" && [ "$(cfv 1 view)" = result ] || { EVID="$(grep '^SIM sd format' <<<"$out") $(cf 1)"; fail cardfmt_exfat; }; }
# a format that fails: the result page says so (the card stays unmounted), the log line
nowrun cardfmt_fails "simsd formatfail 1; $TO_CODE; $KON; @0.3; cardfmt arm; cardfmt arm; @2; cardfmt; uitop" && {
    [ "$(nfmt)" = 1 ] && grep -Eq '^APP sd format failed err=ESP_FAIL stage=mkfs out=broken ms=[0-9]+$' <<<"$out" && [ "$(cfv 1 view)" = result ] && [ "$(sdf 1 | cut -d' ' -f1-4)" = "state=fail err=ESP_FAIL stage=mkfs out=broken" ] \
      && [ "$(sdf 1 | grep -o 'mounted=[0-9]')" = "mounted=0" ] || { EVID="$(cf 1)|$(sdf 1)"; fail cardfmt_fails; }; }
# 30 s without a tap: back to the warning (29.8 s: still the pad; a tap restarts the 30 s); the code works again after 繼續. "screen on" before the tap: a console tap is
# not a touch, and the screen must not go dark (60 s on USB power) before the timeout is seen
nowrun cardfmt_timeout "$TO_CODE; @29.5; cardfmt; screen on; codepad tap u; @29.5; cardfmt; @1; cardfmt; codepad; @0.3; cardfmt continue; @0.3; $KON; @0.3; cardfmt" && {   # @0.3: the new page ignores taps for 250 ms (review m1)
    [ "$(cfv 1 step)" = code ] && [ "$(cfv 2 step)" = code ] && [ "$(cfv 2 got)" = 1 ] && [ "$(cfv 3 step)" = warn ] && [ "$(cfv 3 top)" = cardfmt ] && grep -qx 'APP codepad none' <<<"$out" \
      && [ "$(cfv 4 step)" = arm ] && [ "$(nfmt)" = 0 ] || { EVID="$(cf 1)|$(cf 2)|$(cf 3)|$(cf 4)"; fail cardfmt_timeout; }; }
# Back at every step formats nothing, and a new visit starts at the warning: WARN, CODE (mid-code), ARM (armed once), the card page; long-press home from the pad
nowrun cardfmt_back_every_step "$TO_CODE; back; @0.3; cardfmt; cardfmt continue; cardfmt row; @0.3; cardfmt; back; @0.3; cardfmt; cardfmt row; @0.3; cardfmt continue; @0.3; codepad tap u; codepad tap u; back; @0.3; cardfmt; cardfmt row; @0.3; cardfmt continue; @0.3; $KON; @0.3; cardfmt arm; back; @0.3; cardfmt; cardfmt row; @0.3; cardfmt; @5; simsd; uistack" && {
    [ "$(cfv 1 step)" = off ] && [ "$(cfv 1 top)" = card ] && grep -qx 'APP cardfmt no row' <<<"$out" && [ "$(cfv 2 step)" = warn ] && [ "$(cfv 3 step)" = off ] && [ "$(cfv 4 step)" = off ] \
      && [ "$(cfv 5 step)" = off ] && [ "$(cfv 6 step)" = warn ] && [ "$(cfv 6 got)" = 0 ] && [ "$(nfmt)" = 0 ] && grep -q '^SIM sd mounted=0 reason=gpt .* formats=0 ' <<<"$out" \
      && [ "$(us)" = "menu settings card cardfmt|" ] || { EVID="$(cf 1)|$(cf 2)|$(cf 3)|$(cf 4)|$(cf 5)|$(cf 6) n=$(nfmt) $(us)"; fail cardfmt_back_every_step; }; }
# the steps only in order: 繼續 on the pad, the final row on the warning or the pad, the pad on the warning: refused / no such control; the armed text runs out after 4 s
nowrun cardfmt_steps_in_order "simsd fail gpt; setui; @0.3; settings key card; @0.3; cardfmt row; @0.3; cardfmt arm; codepad tap u; cardfmt; cardfmt continue; @0.3; cardfmt continue; cardfmt arm; cardfmt; $KON; @0.3; cardfmt arm; @4.5; cardfmt; cardfmt arm; cardfmt" && {
    [ "$(grep -c '^APP cardfmt no row$' <<<"$out")" = 3 ] && grep -qx 'APP codepad none' <<<"$out" && [ "$(cfv 1 step)" = warn ] && [ "$(cfv 2 step)" = code ] \
      && [ "$(cfrow 3)" = "Last check: format 31.9 GB" ] && [ "$(cfrow 4)" = "Tap again: format 31.9 GB" ] && [ "$(nfmt)" = 0 ] || { EVID="$(cf 1)|$(cf 2)|$(cf 3)|$(cf 4) n=$(nfmt)"; fail cardfmt_steps_in_order; }; }
# real touches on the pad keys (the piece's own hit boxes): the code by simtap, and a tap between the keys does nothing
nowrun cardfmt_pad_at "$TO_CODE; codepad at u; codepad at d; codepad at l; codepad at r; codepad at b; codepad at a" && {
    at() { grep "^APP codepad at $1 " <<<"$out" | sed 's/.*x=\([0-9]*\),y=\([0-9]*\)$/\1 \2/'; }
    U=$(at u); D=$(at d); L=$(at l); R=$(at r); B=$(at b); A=$(at a)
    [ -n "$U" ] && [ -n "$A" ] || { EVID="$(grep '^APP codepad at' <<<"$out" | tr '\n' '|')"; fail cardfmt_pad_at; }
    nowrun cardfmt_pad_touch "$TO_CODE; simtap 180 40; @0.1; codepad; simtap $U; simtap $U; simtap $D; simtap $D; simtap $L; simtap $R; simtap $L; simtap $R; codepad; simtap $B; simtap $A; @0.3; cardfmt" && {
        [ "$(cpd 1)" = "len=10 lit=0 keys=" ] && [ "$(cpd 2)" = "len=10 lit=8 keys=UUDDLRLR" ] && [ "$(cfv 1 step)" = arm ] && [ "$(nfmt)" = 0 ] || { EVID="$(cpd 1)|$(cpd 2)|$(cf 1)"; fail cardfmt_pad_touch; }; }; }
# the screen going dark on the pad, or a page over it: back to the warning (the code is not kept)
nowrun cardfmt_dark_restarts "$TO_CODE; codepad tap u; screen off; @0.6; screen on; @0.6; cardfmt; cardfmt continue; @0.3; $KON; @0.3; uipush message; @0.3; back; @0.6; cardfmt" && {
    [ "$(cfv 1 step)" = warn ] && [ "$(cfv 1 top)" = cardfmt ] && [ "$(cfv 2 step)" = warn ] && [ "$(nfmt)" = 0 ] || { EVID="$(cf 1)|$(cf 2)"; fail cardfmt_dark_restarts; }; }
# the page and the piece do not hand-build UI; the code pad is registered in the shared-area doc and the lint file; the code is written only in 說明
# fix round 1 (review I1): the result says whether the card was touched. "simsd formatfail write|mkfs|mount" fails the fake at that stage (1 = mkfs).
# write / mkfs: written, not finished -> out=broken, the card still fails to mount (gpt / nofs), so the flow can be started again; mount: formatted, the mount after it failed
nowrun cardfmt_fail_write "simsd formatfail write; $TO_CODE; $KON; @0.3; cardfmt arm; cardfmt arm; @2; cardfmt; back; @0.3; uitop; cardfmt row; @0.3; cardfmt" && {
    [ "$(nfmt)" = 1 ] && grep -Eq '^APP sd format failed err=ESP_FAIL stage=write out=broken ms=[0-9]+$' <<<"$out" && [ "$(cfv 1 view)" = result ] \
      && [ "$(sdf 1 | cut -d' ' -f1-4)" = "state=fail err=ESP_FAIL stage=write out=broken" ] && [ "$(sdf 1 | grep -o 'mounted=[0-9]')" = "mounted=0" ] \
      && [ "$(tp)" = "card " ] && [ "$(cfv 2 view)" = guard ] && [ "$(cfv 2 step)" = warn ] || { EVID="$(cf 1)|$(sdf 1)|$(cf 2) top $(tp)"; fail cardfmt_fail_write; }; }
nowrun cardfmt_fail_mount "simsd formatfail mount; $TO_CODE; $KON; @0.3; cardfmt arm; cardfmt arm; @2; cardfmt" && {
    [ "$(nfmt)" = 1 ] && grep -Eq '^APP sd format failed err=ESP_FAIL stage=mount out=unmounted ms=[0-9]+$' <<<"$out" && [ "$(cfv 1 view)" = result ] \
      && [ "$(sdf 1 | cut -d' ' -f1-4)" = "state=fail err=ESP_FAIL stage=mount out=unmounted" ] && [ "$(sdf 1 | grep -o 'mounted=[0-9]')" = "mounted=0" ] || { EVID="$(cf 1)|$(sdf 1)"; fail cardfmt_fail_mount; }; }
# review I2: the card at the final tap must be the card the warning showed. "simsd swap" (another card, same size, another CID serial) and "simsd fixed"
# (the card in the slot mounts now: fixed on a computer; the player does not know yet) are caught by the lower half before any write: refused, nothing
# recorded (formats=0), out=untouched. "simsd ok" (the player knows the card mounts) is refused by the upper half: why=entry.
nowrun cardfmt_swap "$TO_CODE; $KON; @0.3; simsd swap; cardfmt arm; cardfmt arm; @2; cardfmt; simsd" && {
    [ "$(nfmt)" = 0 ] && grep -Eq '^APP sd format failed err=ESP_ERR_INVALID_SIZE stage=before out=untouched ms=[0-9]+$' <<<"$out" && [ "$(sdf 1 | cut -d' ' -f1-4)" = "state=fail err=ESP_ERR_INVALID_SIZE stage=before out=untouched" ] \
      && [ "$(sdf 1 | grep -o 'mounted=[0-9]')" = "mounted=0" ] && grep -q '^SIM sd mounted=0 reason=gpt .* formats=0 ' <<<"$out" || { EVID="$(cf 1)|$(sdf 1)|$(grep '^APP sd format\|^SIM sd' <<<"$out" | tr '\n' '|')"; fail cardfmt_swap; }; }
nowrun cardfmt_fixed "$TO_CODE; $KON; @0.3; simsd fixed; cardfmt arm; cardfmt arm; @2; cardfmt; simsd" && {
    [ "$(nfmt)" = 0 ] && grep -Eq '^APP sd format failed err=ESP_ERR_INVALID_STATE stage=before out=untouched ms=[0-9]+$' <<<"$out" && [ "$(sdf 1 | grep -o 'mounted=[0-9]')" = "mounted=1" ] \
      && grep -q '^SIM sd mounted=1 reason=- .* formats=0 ' <<<"$out" || { EVID="$(cf 1)|$(sdf 1)|$(grep '^APP sd format\|^SIM sd' <<<"$out" | tr '\n' '|')"; fail cardfmt_fixed; }; }
nowrun cardfmt_ok_midflow "$TO_CODE; $KON; @0.3; simsd ok; cardfmt arm; cardfmt arm; @0.3; cardfmt; @2; simsd" && {
    [ "$(nfmt)" = 0 ] && grep -qx 'APP sd format refused why=entry' <<<"$out" && ! grep -q '^APP sd format start' <<<"$out" && [ "$(cfv 1 step)" = off ] \
      && grep -q '^SIM sd mounted=1 reason=- .* formats=0 ' <<<"$out" || { EVID="$(cf 1)|$(grep '^APP sd format\|^SIM sd' <<<"$out" | tr '\n' '|')"; fail cardfmt_ok_midflow; }; }
# review m1: taps in the first 250 ms after a step change are ignored (an eager double tap on the last key must not reach the final row); m4: reboot and
# USB-mode reboot are refused while the card is being written
nowrun cardfmt_settle "$TO_CODE; $KON; cardfmt arm; cardfmt arm; cardfmt; @0.3; cardfmt; simsd" && {
    [ "$(cfrow 1)" = "Last check: format 31.9 GB" ] && [ "$(cfrow 2)" = "Last check: format 31.9 GB" ] && [ "$(nfmt)" = 0 ] && ! grep -q '^APP sd format start' <<<"$out" || { EVID="$(cf 1)|$(cf 2)"; fail cardfmt_settle; }; }
nowrun cardfmt_no_reboot_while_run "$TO_CODE; $KON; @0.3; cardfmt arm; cardfmt arm; @0.1; back; back; @0.3; uitop; settings key reboot; uitop; back; settings key usb; uitop; back; @2; cardfmt" && {
    [ "$(nrb)" = 0 ] && ! grep -q '^APP usb_mode request' <<<"$out" && [ "$(grep -c '^APP settings busy=format$' <<<"$out")" = 2 ] && [ "$(tp)" = "settings message message " ] \
      && [ "$(sdf 1 | cut -d' ' -f1)" = "state=done" ] || { EVID="$(tp) nrb=$(nrb) $(grep -c 'busy=format' <<<"$out") $(sdf 1)"; fail cardfmt_no_reboot_while_run; }; }
# --- end cardfmt ---
# --- hires T8c ---
# covers vs hi-res (Batch B M-B4): while a source above 48 kHz plays no cover is decoded; a request gets the placeholder at once (the fake prints px=0 why=hires) and is served when the
# pause ends (stop: px=1 why=after; a plain answer prints no line); 現況 says so on the 狀況 line; `thumbcache build` is refused with the reason. At 44.1 kHz the cover comes at once.
nowrun hr8c_cover_paused_96k "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @1; status; thumbcache stat; thumbcache build; stop; @1; status; thumbcache build" && {
    p=$(grep '^SIM thumb' <<<"$out" | sed 's/req=[0-9]* //' | tr '\n' '|')
    { [ "$p" = "SIM thumb px=0 why=hires|SIM thumb px=1 why=after|" ] && grep -qx 'APP status 狀況：underrun 0・封面建置暫停（高取樣率）\|APP status Status: underrun 0 · covers paused (hi-res)' <<<"$out" \
      && [ "$(grep -c '封面建置暫停\|covers paused' <<<"$out")" = 1 ] && grep -q '^APP thumbcache stat count=0 bytes=0 cap=0 aliases=0 bad=0 missing=[0-9]* none=[0-9]* unresolved=0 tracks=[0-9]* building=0 compacting=0 wait=hires ' <<<"$out" \
      && grep -qx 'APP thumbcache build refused reason=hires src=96000' <<<"$out" && grep -q '^APP thumbcache build tracks=[0-9]*$' <<<"$out"; } || { EVID="$p $(grep '^APP status 狀況\|^APP status Status\|^APP thumbcache' <<<"$out" | tr '\n' '|')"; fail hr8c_cover_paused_96k; }; }
nowrun hr8c_cover_44k "screen on; vol 0; @0.5; mp3ui; @0.3; mp3 1; @1; status" && {
    p=$(grep '^SIM thumb' <<<"$out" | sed 's/req=[0-9]* //' | tr '\n' '|')
    { [ -z "$p" ] && ! grep -q '封面建置暫停\|covers paused' <<<"$out"; } || { EVID="$p"; fail hr8c_cover_44k; }; }   # a plain answer prints nothing
# a newer request at 44.1 kHz (the next track) supersedes the waiting one: answered at once (no line), the waiting one never comes after it
nowrun hr8c_cover_next_supersedes "seti dac_max_rate 96000; screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @1; next; @1.5" && {
    p=$(grep '^SIM thumb' <<<"$out" | sed 's/req=[0-9]* //' | tr '\n' '|')
    [ "$p" = "SIM thumb px=0 why=hires|" ] || { EVID="$p"; fail hr8c_cover_next_supersedes; }; }
# hires 8d: a track whose embedded cover is the board's Batch C2 picture (ffmpeg yuvj444p: 1x2 sampling on all three components, refused by
# both of the S31's decoders) gets a real thumbnail from our decoder (pc_jpeg, the same route and code as thumb_svc.c), and a progressive
# cover gets the placeholder with its reason. The pictures are made here (ffmpeg, Pillow) into the scratch root, never committed.
if command -v ffmpeg >/dev/null; then
    NOWFRESH=c444fresh nowrun hr8d_cover_444_own "scan; @0.5; screen on; vol 0; mp3ui; @0.3; mp3 0; @1; next; @1" && {
        p=$(grep '^SIM thumb' <<<"$out" | sed 's/req=[0-9]* //' | sort | tr '\n' '|')   # the two tracks, either order
        [ "$p" = "SIM thumb px=0 why=progressive|SIM thumb px=1 why=own|" ] || { EVID="$p"; fail hr8d_cover_444_own; }; }
else echo "SKIP hr8d_cover_444_own (no ffmpeg)"; fi
# the board's routing in thumb_svc.c: probe first; the JPEG engine for NATIVE pictures, esp_new_jpeg (behind the low-internal gate) when the
# engine cannot, our decoder for OWN pictures and for NATIVE ones both refuse; the reason in the fail line; the engine made once at start
TS8=app/main/thumb_svc.c
{ grep -q 'int route = pc_jpeg_probe(jpg, len, &in);' $TS8 && grep -q 'uint16_t \*full = jpeg_route_hw(jpg, len, &in, &w, &h, &hw_why);' $TS8 \
  && grep -q 'if (low_gate(req)) { s_why = "low_internal"; return NULL; }' $TS8 && grep -q 'uint16_t \*full = jpeg_route_own(jpg, len, s_sz, &w, &h, &rc);' $TS8 \
  && grep -q 'printf("APP thumb fail reason=%s path=%s\\n", s_why, path);' $TS8 && grep -q 'jpeg_route_init();' $TS8 \
  && grep -q 'int route = pc_jpeg_probe(jpg, len, &in);' app/main/album_svc.c && grep -q 'jpeg_route_own(jpg, len, ALBUM_PX, ow, oh, &rc)' app/main/album_svc.c; } || fail hr8d_route_call_sites
# the board's thumb_svc.c (not built in the simulator): the pause is checked before every decode (request, builder, compaction); one pack file
# (pc_thumbpak, host-tested in test_thumbpak: pixels synced before the slot, a transient open error never replaces the pack, 1 GiB cap,
# shared entries); an unfinished compaction is dropped, a finished one not yet renamed is taken; a full card / write error / full table /
# free-space margin / cap stops writes once; the compaction works only while nothing plays (fix round 1, review I4; the pass since hires T8e: hr8e_*); the now page asks
# for the playing track's cover (made at once when missing); the library hands over its tracks only after a load or a scan that succeeded
TS=app/main/thumb_svc.c
{ grep -q 'else if (found && paused_now(&src)) {' $TS && grep -q 'paused_now(NULL) ? "hires" : opening_now() ? "opening" : !idle_now() ? "playing"' $TS \
  && grep -q 'r = pc_tpk_attach(&s_tc.pak, PAK, tpk_alloc, NULL, pc_tpk_cap_for(want_cap), S, create);' $TS && grep -q 'printf("APP thumbcache stop reason=space free=%llu\\n"' $TS \
  && grep -q 'stop_writes(errno == ENOSPC ? "full" : "write", errno);' $TS && grep -q 'if (r == -ENOSPC) stop_writes("table_full", 0);' $TS \
  && grep -q '} else if (found && (now || idle_now()) && !opening_now()) {' $TS && grep -q 'thumb_svc_request_now(path, off, len, thumb_ready, NULL)' app/main/ui/scr_now.c \
  && grep -q '#define PAK PC_THUMB_PAK' $TS && grep -q 'if (!rc) thumb_svc_library_changed();' app/main/library_svc.c \
  && grep -q 'return true;                                   /\* len 0: no embedded picture' app/main/mp3_ctl.c && grep -q 's_creq = thumb_svc_request(pc_str(&g_lib, t->path), t->apic_off, t->apic_len, card_thumb, NULL);' app/main/ui/scr_fortune.c \
  && grep -q '.covers_paused = thumb_svc_paused()' app/main/ui/ui_help_texts.c; } || fail hr8c_call_sites
# fix round 2 (re-review I-1/I-4): every write to the pack goes through pak_write, which asks pc_thumb_write_gate at the moment of the write
# (hi-res loaded or a source still opening, background while playing, free space not read yet) before the margin and the cap; the
# compaction asks it again between its read and its write, pass_end right before it makes the thumbs.new table (fix round 3, re-review 2 I-B);
# "opening" comes from the live generation (fix round 3, I-A); a failed compaction rename keeps the complete file (pc_tpk_recover first)
{ [ "$(grep -c 'pc_tpk_append(&s_tc.pak' $TS)" = 1 ] && [ "$(grep -c 'pc_tpk_alias(&s_tc.pak' $TS)" = 1 ] && [ "$(grep -c 'pc_tpk_add_shared(&s_tc.pak' $TS)" = 1 ] \
  && [ "$(grep -c 'pc_tpk_forget(&s_tc.pak' $TS)" = 1 ] && [ "$(grep -c 'pc_thumb_write_gate(' $TS)" = 3 ] \
  && [ "$(sed -n '/^static int pak_write(/,/^}/p' $TS | grep -n 'pc_thumb_write_gate(\|pc_tpk_append(&s_tc.pak' | cut -d: -f1 | tr '\n' ' ')" != "" ] \
  && sed -n '/^static int pak_write(/,/^}/p' $TS | awk '/pc_thumb_write_gate\(/{g=NR} /pc_tpk_append\(&s_tc.pak/{a=NR} END{exit !(g && a && g < a)}' \
  && sed -n '/^static bool compact_step(/,/^}/p' $TS | awk '/pc_tpk_read\(/{r=NR} /pc_thumb_write_gate\(/{g=NR} /pc_tpk_append\(&s_tc.neu/{a=NR} END{exit !(r < g && g < a)}' \
  && grep -q 'int r = create ? pc_tpk_recover(PAK, PAK_NEW) : 0;' $TS && grep -q 'return pc_thumb_opening(audio_svc_live_gen(), st.gen);' $TS \
  && sed -n '/^static void pass_end(/,/^}/p' $TS | awk '/pc_thumb_write_gate\(/{g=NR} /pc_tpk_create\(PAK_NEW/{c=NR} END{exit !(g && c && g < c)}' \
  && [ "$(grep -c 'pc_tpk_create(PAK_NEW' $TS)" = 1 ]; } || fail hr8c_write_gate


# fix round 3 (re-review 2 I-A): the write gate's "opening" comes from the live generation; after a stop the service publishes gen 0 (the fake as
# the board) and the gate lets a cover be written - no "opening" (the old rule, queued gen ahead of the published one, held every write after
# any stop). `simthumbgate` prints the gate with the fake's live/published generations, through the same pure rules as thumb_svc.c.
nowrun hr8c_gate_after_stop "screen on; vol 0; @0.5; mp3ui; @0.3; mp3 1; @1; simthumbgate; stop; @1; simthumbgate" && {
    p=$(grep '^SIM thumbgate' <<<"$out" | sed 's/ live=[0-9]* pub=[0-9]*//' | tr '\n' '|')
    { [ "$p" = "SIM thumbgate opening=0 write=ok|SIM thumbgate opening=0 write=ok|" ] && grep -q '^SIM thumbgate opening=0 write=ok live=0 pub=0$' <<<"$out"; } || { EVID="$(grep '^SIM thumbgate' <<<"$out" | tr '\n' '|')"; fail hr8c_gate_after_stop; }; }
# --- hires T8e: covers build in the background (also while <= 48 kHz plays) with a progress page, 自動/手動; a rescan keeps playing ---
# the fixture's pack is complete (nothing changes for the other tests); an empty pack builds while stopped (250 ms a cover) and while 44.1 kHz plays (1 s a cover)
nowrun hr8e_builds_stopped_and_playing "screen on; vol 0; @0.5; covers; simthumbpack clear; @0.6; covers; mp3ui; @0.3; mp3 1; @0.1; covers; @2.05; covers; status; thumbcache stat" && {
    [ "$(cvl 1)" = "APP covers state=done why=run done=11 total=11 pct=100 mode=auto user=0" ] || { EVID="$(cvl 1)"; fail hr8e_fixture_complete; }
    [ "$(cvl 2)" = "APP covers state=building why=run done=2 total=11 pct=18 mode=auto user=0" ] || { EVID="$(cvl 2)"; fail hr8e_builds_stopped; }
    d3=$(cvl 3 | sed 's/.* done=\([0-9]*\) .*/\1/'); d4=$(cvl 4 | sed 's/.* done=\([0-9]*\) .*/\1/')
    [ "$(cvl 4 | cut -d' ' -f3)" = "state=building" ] && [ $((d4 - d3)) = 2 ] || { EVID="$(cvl 3) / $(cvl 4)"; fail hr8e_builds_playing_slower; }   # 2 s at 44.1 kHz: two covers (1 s breather)
    grep -q "^APP status Status: underrun 0 · covers building $d4/11 ([0-9]*%)$" <<<"$out" || { EVID="$(grep '^APP status Status' <<<"$out")"; fail hr8e_status_fragment; }
    grep -q " pass=building why=run done=$d4 total=11 mode=auto user=0 underrun_holds=0 unchecked=0 tidy=0$" <<<"$out" || { EVID="$(grep '^APP thumbcache stat' <<<"$out")"; fail hr8e_stat_line; }; }
# held at hi-res (the same rule as the requests), with the reason on the page and on 現況; it resumes after the stop
nowrun hr8e_held_hires "seti dac_max_rate 96000; screen on; vol 0; @0.5; simthumbpack clear; @0.3; mp3ui; @0.3; simaudiorate 96000 1; mp3 1; @1; covers; status; @2; covers; stop; @1.1; covers" && {
    [ "$(cvl 1 | cut -d' ' -f3-4)" = "state=held why=hires" ] && [ "$(cvl 1 | cut -d' ' -f5)" = "$(cvl 2 | cut -d' ' -f5)" ] || { EVID="$(cvl 1) / $(cvl 2)"; fail hr8e_held_hires; }
    grep -q '^APP status Status: underrun 0 · covers paused (hi-res) [0-9]*/11$' <<<"$out" && [ "$(grep -c 'covers paused' <<<"$out")" = 1 ] || { EVID="$(grep '^APP status Status' <<<"$out")"; fail hr8e_held_hires_status; }
    [ "$(cvl 3 | cut -d' ' -f3)" = "state=building" ] || { EVID="$(cvl 3)"; fail hr8e_hires_resumes_after_stop; }
    grep -qx 'APP thumbcache wait reason=hires' <<<"$out" || fail hr8e_hires_logged; }
# an underrun while playing: held 30 s (the reason said), then on again; after the third hold only while stopped
nowrun hr8e_underrun "screen on; vol 0; @0.5; simthumbpack clear; @0.3; mp3ui; @0.3; mp3 1; @1.5; simunderrun; @0.3; covers; status; @20; covers; @11; covers; simunderrun; @0.3; @31; simunderrun; @0.3; @31; covers; stop; @1; covers; thumbcache stat" && {
    [ "$(cvl 1 | cut -d' ' -f3-4)" = "state=held why=underrun" ] && [ "$(cvl 2 | cut -d' ' -f3-4)" = "state=held why=underrun" ] && [ "$(cvl 1 | cut -d' ' -f5)" = "$(cvl 2 | cut -d' ' -f5)" ] \
      || { EVID="$(cvl 1) / $(cvl 2)"; fail hr8e_underrun_holds; }
    grep -q '^APP status Status: underrun 1 · covers paused (playback stutter) [0-9]*/11$' <<<"$out" || { EVID="$(grep '^APP status Status' <<<"$out")"; fail hr8e_underrun_status; }
    [ "$(cvl 3 | cut -d' ' -f3)" = "state=building" ] || { EVID="$(cvl 3)"; fail hr8e_underrun_resumes; }
    [ "$(cvl 4 | cut -d' ' -f3-4)" = "state=held why=underrun" ] || { EVID="$(cvl 4)"; fail hr8e_underrun_giveup; }        # 3 holds: 31 s quiet is not enough while playing
    [ "$(cvl 5 | cut -d' ' -f3)" != "state=held" ] && grep -q ' underrun_holds=3 ' <<<"$out" || { EVID="$(cvl 5) $(grep '^APP thumbcache stat' <<<"$out")"; fail hr8e_underrun_giveup_idle_runs; }; }
# the page: from 設定 (its row 封面快取：自動) and from 現況 (its first row); state, numbers, 暫停/繼續, 立即建置, 模式 (two values: a tap; stored), 說明 last
nowrun hr8e_page "screen on; vol 0; @0.5; simthumbpack clear; @0.3; setui; @0.3; settings group card; settings rows; settings key covers; @0.3; uitop; simthumbpack hold 1; covers; simthumbpack hold 0; covers tap pause; covers; @1; covers; covers tap pause; @0.3; covers; {\"op\":\"config_set\",\"values\":{\"cover_auto\":0}}; @0.8; covers; help tap; @0.3; help show; back; @0.3; back; @0.3; settings rows; back; @0.3; help open status; @0.3; help show; simtap 180 93; @0.3; uitop" && {
    grep -qx 'APP settings row=Cover cache: auto' <<<"$out" && grep -qx 'APP uitop covers' <<<"$out" || { EVID="$(grep '^APP settings row=Cover\|^APP uitop' <<<"$out" | tr '\n' '|')"; fail hr8e_page_from_settings; }
    [[ "$(cvp 1)" =~ ^"APP covers page state=Building count="[0-9]+" / 11 · "[0-9]+"% · "[0-9]+" left | Pause | Build now"$ ]] || { EVID="$(cvp 1)"; fail hr8e_page_building; }
    [[ "$(cvp 2)" =~ ^"APP covers page state=Paused: by you count=".*" | Resume | Build now"$ ]] || { EVID="$(cvp 2)"; fail hr8e_page_paused; }
    [ "$(cvl 2 | grep -o 'done=[0-9]*')" = "$(cvl 3 | grep -o 'done=[0-9]*')" ] && [ "$(cvl 3 | cut -d' ' -f3-4)" = "state=held why=user" ] || { EVID="$(cvl 2) / $(cvl 3)"; fail hr8e_pause_stops_building; }   # nothing built in the paused second
    [ "$(cvl 4 | cut -d' ' -f3)" = "state=building" ] || { EVID="$(cvl 4)"; fail hr8e_resume; }
    [[ "$(cvp 5)" =~ ^"APP covers page state=Manual count=".*" | Pause | Build now"$ ]] && grep -qx 'APP settings row=Cover cache: manual' <<<"$out" || { EVID="$(cvp 5)"; fail hr8e_page_manual; }
    grep -q '^APP explain id=covers title=Cover cache paras=5 qr=0 web=0$' <<<"$out" || { EVID="$(grep '^APP explain' <<<"$out" | tr '\n' '|')"; fail hr8e_page_help_last; }
    grep -q '^APP explain id=status ' <<<"$out" && [ "$(grep '^APP uitop' <<<"$out" | tail -1)" = "APP uitop covers" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail hr8e_page_from_status; }; }
# 手動: nothing builds until 立即建置; 立即建置 builds everything, then 手動 again; the setting survives a reboot (read at boot)
NOWCFG=cover_auto=0 nowrun hr8e_manual "screen on; vol 0; @0.5; simthumbpack clear; @1; covers; status; covers open; @0.3; covers tap build; @0.6; covers; @3; covers; thumbcache stat" && {
    [ "$(cvl 1)" = "APP covers state=manual why=run done=0 total=11 pct=0 mode=manual user=0" ] || { EVID="$(cvl 1)"; fail hr8e_manual_holds; }
    ! grep -q '^APP status .*covers' <<<"$out" || { EVID="$(grep '^APP status Status' <<<"$out")"; fail hr8e_manual_no_fragment; }
    [ "$(cvl 2 | cut -d' ' -f3)" = "state=building" ] && [ "$(cvl 2 | cut -d' ' -f8)" = "mode=manual" ] || { EVID="$(cvl 2)"; fail hr8e_build_now_in_manual; }
    [ "$(cvl 3)" = "APP covers state=done why=run done=11 total=11 pct=100 mode=manual user=0" ] || { EVID="$(cvl 3)"; fail hr8e_build_now_done; }; }
# the setting from the web page (config_set) applies at once
nowrun hr8e_web_setting "screen on; @0.5; simthumbpack clear; {\"op\":\"config_set\",\"values\":{\"cover_auto\":0}}; @0.6; covers; {\"op\":\"config_set\",\"values\":{\"cover_auto\":1}}; @0.6; covers" && {
    [ "$(cvl 1 | cut -d' ' -f3)" = "state=manual" ] && [ "$(cvl 2 | cut -d' ' -f3)" = "state=building" ] || { EVID="$(cvl 1) / $(cvl 2)"; fail hr8e_web_setting; }; }
# fix round 0: 現況's 封面快取 row only while there is something to see (C6): none when done, there while building, after it gone again; a compaction
# waiting for a stop is said on the page and on 現況 (C4); a Bluetooth sink's dry block holds the pass like a wired underrun (C1)
nowrun hr8e_status_row "screen on; @0.5; help open status; @0.3; uiboxes; simthumbpack clear; @1.2; uiboxes; simthumbpack tidy 1; @3; covers; status; uiboxes; simthumbpack tidy 0; @1.2; uiboxes" && {
    rows=$(grep -c '^APP uibox button .*|Cover cache$' <<<"$out"); bl=$(grep -n '^APP uiboxes end' <<<"$out" | cut -d: -f1 | tr '\n' ' ')
    blk() { awk -v k="$1" '/^APP cmd uiboxes/{n++} n==k && /^APP uibox button .*\|Cover cache$/{print "row"}' <<<"$out"; }
    [ -z "$(blk 1)" ] && [ "$(blk 2)" = row ] && [ "$(blk 3)" = row ] && [ -z "$(blk 4)" ] || { EVID="rows $(blk 1)|$(blk 2)|$(blk 3)|$(blk 4)"; fail hr8e_status_row_only_when_needed; }
    grep -q '^APP covers state=done .* tidy=1 row=1 | Done · tidy-up waits for a stop |' <<<"$out" && grep -qx 'APP status Status: covers done · tidy-up waits for a stop' <<<"$out" \
      || { EVID="$(grep '^APP covers\|^APP status Status' <<<"$out" | tr '\n' '|')"; fail hr8e_tidy_said; }; }
nowrun hr8e_sink_underrun "screen on; vol 0; @0.5; simthumbpack clear; @0.3; mp3 1; @1.5; simunderrun sink; @0.3; covers; stop; @1; simunderrun sink; @0.3; covers" && {
    [ "$(cvl 1 | cut -d' ' -f3-4)" = "state=held why=underrun" ] || { EVID="$(cvl 1)"; fail hr8e_sink_underrun_holds; }
    [ "$(cvl 2 | cut -d' ' -f3)" = "state=building" ] || { EVID="$(cvl 2)"; fail hr8e_sink_dry_while_stopped_ignored; }; }
# fix round 1 (review I1): an underrun while no pass step ran (a rescan's card load, a pause) is not the pass's: a pass that starts afterwards builds, no hold
nowrun hr8e_underrun_not_charged "screen on; vol 0; @0.5; mp3 1; @1.5; simunderrun; simunderrun sink; @0.3; simthumbpack clear; @1.5; covers; thumbcache stat" && {
    [ "$(cvl 1 | cut -d' ' -f3)" = "state=building" ] && grep -q ' underrun_holds=0 ' <<<"$out" && ! grep -q '^APP thumbcache underrun' <<<"$out" \
      || { EVID="$(cvl 1) $(grep '^APP thumbcache underrun\|^APP thumbcache stat' <<<"$out" | tr '\n' '|')"; fail hr8e_underrun_not_charged; }; }
# a rescan keeps playing: the file still there -> the same song plays on, its index follows (a song before it was removed: 3 -> 2), the history too;
# the playing file gone -> stops with the reason (a message). Files are removed while the simulator runs (stdin).
fresh
out=$({ printf '%s\n' "screen on" "vol 0" "seti mp3_order 1" "track 2" "@1" "next" "@1" "mp3cur"; sleep 0.5; rm -f "$T/fix/export/夜航星圖/01_harbor_light.mp3"
        printf '%s\n' "scan" "@0.5" "mp3cur" "nowmenu -5" "uistack" "status" "prev" "mp3cur" quit; } | $SIM --headless --root "$T/fix" 2>&1)
{ grep -qx 'APP mp3 cur=3 folder=0 qidx=-1 hist=2 back=0 recent=2 path=/sdcard/export/夜航星圖/04_long_summer.mp3' <<<"$out" \
  && grep -qx 'APP mp3 library kept cur=3>2 queued=-1 folder=0 hist=2 recent=2' <<<"$out" \
  && grep -qx 'APP mp3 cur=2 folder=0 qidx=-1 hist=2 back=0 recent=2 path=/sdcard/export/夜航星圖/04_long_summer.mp3' <<<"$out" \
  && grep -q '^APP status Source: MP3' <<<"$out" && grep -q '^APP scan n=11 ' <<<"$out" && ! grep -q '^APP mp3 stop' <<<"$out" \
  && grep -qx 'APP mp3 cur=1 folder=0 qidx=-1 hist=2 back=1 recent=3 path=/sdcard/export/夜航星圖/03_late_tide.mp3' <<<"$out" \
  && ! grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out"; } || { EVID="$(grep '^APP mp3\|^APP scan\|^APP status Source\|Sanitizer' <<<"$out" | tr '\n' '|')"; fail hr8e_scan_keeps_playing; }
fresh
out=$({ printf '%s\n' "screen on" "vol 0" "track 2" "@1" "mp3cur"; sleep 0.5; rm -f "$T/fix/export/夜航星圖/03_late_tide.mp3"
        printf '%s\n' "scan" "@0.5" "mp3cur" "uistack" "status" quit; } | $SIM --headless --root "$T/fix" 2>&1)
{ grep -qx 'APP mp3 stop reason=gone path=/sdcard/export/夜航星圖/03_late_tide.mp3' <<<"$out" && grep -q '^APP mp3 cur=-1 ' <<<"$out" && grep -q '^APP uistack .* message$' <<<"$out" \
  && ! grep -q '^APP status Source' <<<"$out" && ! grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out"; } || { EVID="$(grep '^APP mp3\|^APP uistack\|^APP status Source\|Sanitizer' <<<"$out" | tr '\n' '|')"; fail hr8e_scan_gone_stops; }
# the board's code: the scan page no longer stops playback, the swap remaps before g_lib is replaced; the pass's rule is pc_tbg_rule and the
# write gate is the same function with the pass's rule as its idle; the compaction keeps the idle-only gate; reads in pieces while loaded
TS=app/main/thumb_svc.c
{ ! grep -q 'mp3_ctl_stop' app/main/ui/scr_scan.c && grep -q 'mp3_ctl_library_changed(&s_new);' app/main/library_svc.c \
  && [ "$(sed -n '/^static void swap_cb(/,/^}/p' app/main/library_svc.c | grep -n 'mp3_ctl_library_changed\|g_lib = s_new' | cut -d: -f1 | tr '\n' ' ')" = "5 7 " ] \
  && grep -q 'return pc_tbg_rule(&in);' $TS && grep -q '!background || idle_now() || bg_rule(false) == PC_TBG_RUN' $TS \
  && sed -n '/^static bool compact_step(/,/^}/p' $TS | grep -q 'const char \*why = gate(false);' \
  && grep -q 'if (at && s_tc.loaded) vTaskDelay(1);' $TS && grep -q 'thumb_svc_set_auto(v)' app/main/ctl.c \
  && grep -q '{ "cover_auto", 0, 1, 1, false }' components/player_core/src/pc_settings.c && grep -q "cover_auto:{g:" tools/web/page.js \
  && grep -q '\*count = a.underruns_src + bt_svc_sink_underflows();' $TS \
  && grep -q '.link_shown = ui_status_covers_row,' app/main/ui/ui_help_texts.c \
  && [ "$(sed -n '/^static void progress_update(/,/^}/p' $TS | grep -c 'pak_ready(')" = 1 ] && sed -n '/^static void progress_update(/,/^}/p' $TS | grep -q 'pc_tbg_may_read_table(paused_now(NULL), opening_now(), s_tc.want, active))' \
  && grep -q 'else if (s_tc.run) { worked = build_step(); under_check(); pc_tbg_under_step_end(&s_tc.under, worked); }' $TS && sed -n '/^static bool build_step(/,/^}/p' $TS | awk '/under_step\(!why\)/{a=NR} /if \(why\) return false;/{if(!b)b=NR} END{exit !(a && b && a < b)}' && [ "$(grep -c 'pc_tbg_under_check(' $TS)" = 1 ] \
  && [ "$(cat app/main/bt_svc_a2dp.c app/main/bt_svc_cast_tx.c | grep -c '^ *sink_block(d0);$\|^ *sink_block(dry);$')" = 3 ]; } || fail hr8e_call_sites   # fix 0: the three sink paths' dry blocks (SBC/AAC, vendor encoders, Auracast)
