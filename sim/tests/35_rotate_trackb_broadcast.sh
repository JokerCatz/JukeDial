# SPDX-License-Identifier: LGPL-2.1-or-later
# --- rotate: screen rotation 0 / 180 (--rotate N or the card's "rotate"; anything else reads as 0): the picture is the unrotated one turned 180, a tap on the SCREEN reaches the same cell ---
fresh
$SIM --headless --root "$T/fix" --cmd "uipush menu; @0.5; shot $T/rot_ref.png" >/dev/null 2>&1
for k in 0 1; do
    p=$(rot_pt $k 360 86 272)                                   # the 'Songs' cell's label on the unrotated menu, where the screen shows it now
    out=$($SIM --headless --rotate $k --root "$T/fix" --cmd "uipush menu; @0.5; shot $T/rot_$k.png; simtap $p; @0.5; simtap $p; @0.5; uitop" 2>&1)
    grep -qx 'APP uitop mp3' <<<"$out" || { EVID="k=$k tap=$p $(grep '^APP uitop' <<<"$out" | tr '\n' ' ')"; fail "rotate_tap_$k"; }
    grep -q 'Sanitizer\|runtime error' <<<"$out" && fail "rotate_sanitizer_$k"
    python3 - "$T/rot_ref.png" "$T/rot_$k.png" $k <<'PY' || fail "rotate_shot_$k"
import sys
from PIL import Image, ImageChops
a, b, k = Image.open(sys.argv[1]).convert('RGB'), Image.open(sys.argv[2]).convert('RGB'), int(sys.argv[3])
sys.exit(0 if ImageChops.difference(b, a.rotate(180 * k)).getbbox() is None else 1)
PY
done
cmp -s "$T/rot_0.png" "$T/rot_ref.png" || fail rotate_zero_identical
# the card's value without --rotate; anything but 1 reads as 0
for v in 1 2 7; do
    printf 'lang=0\nrotate=%s\n' $v > "$T/fix/_player/config/app.txt"
    $SIM --headless --root "$T/fix" --cmd "uipush menu; @0.5; shot $T/rot_c.png" >/dev/null 2>&1
    want=$T/rot_ref.png; [ $v = 1 ] && want=$T/rot_1.png
    cmp -s "$T/rot_c.png" "$want" || fail "rotate_cfg_$v"
done
# the settings row: a plain tap toggles the stored value, the row says it applies after a reboot; the web path (config_set) takes only 0 / 1
fresh
out=$($SIM --headless --root "$T/fix" --cmd "uipush settings; @0.5; settings key rotate; cfg rotate; settings rows; settings key rotate; cfg rotate" 2>&1)
[ "$(grep '^APP cfg rotate=' <<<"$out" | tr '\n' ' ')" = 'APP cfg rotate=1 APP cfg rotate=0 ' ] || { EVID="$(grep '^APP cfg rotate' <<<"$out" | tr '\n' ' ')"; fail rotate_settings_row; }
grep -q '^APP settings row=Rotation: 180°(after' <<<"$out" || { EVID="$(grep 'Rotation' <<<"$out")"; fail rotate_settings_after_reboot; }
out=$($SIM --headless --root "$T/fix" --cmd '{"op":"config_set","id":1,"values":{"rotate":1}}; {"op":"config_set","id":2,"values":{"rotate":2}}; cfg rotate' 2>&1)
{ grep -q '"id":1.*"ok":true' <<<"$out" && grep -q '"id":2.*"rejected":1' <<<"$out" && grep -qx 'APP cfg rotate=1' <<<"$out"; } || { EVID="$(grep '^CTL\|rotate' <<<"$out" | cut -c1-120 | tr '\n' '|')"; fail rotate_web_range; }
# --- end rotate ---

# --- Track B: speed (the playing ring's last cell) and the crossfade row; both off with a reason when the mode is too heavy ---
nowrun tb_speed_cell "seti mp3_order 1; screen on; vol 0; track 3; @3; nowmenu -2; nowmenu -5; nowmenu 8; nowmenu 8; nowmenu -5; nowmenu 8; nowmenu 8; nowmenu 8; nowmenu 8; fx mode 4; @0.3; nowmenu -5" && {
    { grep -qx 'APP audio speed request=125%' <<<"$out" && grep -qx 'APP audio speed request=150%' <<<"$out" && grep -qx 'APP audio speed request=75%' <<<"$out" && grep -qx 'APP audio speed request=100%' <<<"$out" \
      && grep -q 'items=.*speed' <<<"$out"; } || { EVID="$(grep '^APP audio speed\|^APP now cells' <<<"$out" | tr '\n' '|')"; fail tb_speed_cell; }; }
nowrun tb_xfade_row "setui; @0.3; settings key xfade; settings key xfade; settings key xfade; settings rows; fx mode 4; @0.3; settings key xfade; uitop; back; @0.3; settings rows; fx mode 0; @0.3; settings key xfade; settings rows" && {
    { grep -qx 'APP audio xfade request=2000ms' <<<"$out" && grep -qx 'APP audio xfade request=4000ms' <<<"$out" && grep -qx 'APP audio xfade request=0ms' <<<"$out" \
      && grep -qx 'APP audio xfade request=6000ms' <<<"$out" && grep -q '^APP uitop message' <<<"$out" && [ "$(grep -c '^APP settings row=Crossfade: 4 s$' <<<"$out")" = 1 ] \
      && [ "$(grep -c '^APP settings row=Crossfade: off (LDAC' <<<"$out")" = 1 ] && [ "$(grep -c '^APP settings row=Crossfade: 6 s$' <<<"$out")" = 1 ]; } || { EVID="$(grep '^APP audio xfade\|Crossfade\|^APP uitop' <<<"$out" | tr '\n' '|')"; fail tb_xfade_row; }; }
# --- end Track B ---
# --- BR: the broadcast radio boot (sim --mode cast): play-only page (說明, 回播放器 armed), the station of the player (cfg radio_last), hold = exit, flicks and the knob change nothing but the volume,
# (the web tool's CTL refusal is the board console's: console.c); the radio page's 廣播電台 row asks first, then requests the reboot ---
fresh
out=$($SIM --headless --mode cast --root "$T/fix" --cmd "screen on; vol 0; @0.6; cast st; ring; cast flick left; cast flick right; cast flick up; cast flick down; @0.3; cast st; uitop; cast hold; cast tap 1; cast tap 1" 2>&1)
grep -q '^APP cast phase=3 name=JukeDial wifi=1 rssi=-52 station=0/' <<<"$out" || { EVID="$(grep '^APP cast' <<<"$out" | head -2)"; fail br_status; }
[ "$(grep -c '^APP cast phase=3.* station=0/' <<<"$out")" = 2 ] || fail br_station_fixed
grep -q '^APP ring .*n=2' <<<"$out" || { EVID="$(grep '^APP ring' <<<"$out")"; fail br_two_cells; }
[ "$(grep -c '^APP cast mode exit' <<<"$out")" = 1 ] || { EVID="$(grep -c '^APP cast mode exit' <<<"$out")"; fail br_hold_exit; }   # FIX2: the hold does not reboot; 回播放器 armed + second tap does (once)
out=$($SIM --headless --mode cast --root "$T/fix" --cmd "@0.6; cast hold; @0.3; cast hold; uitop; cast tap 1; cast tap 1" 2>&1)
{ [ "$(grep -c '^APP cast mode exit' <<<"$out")" = 1 ] && grep -q '^APP uitop cast' <<<"$out"; } || fail br_hold_no_reboot
grep -q '^APP uitop cast' <<<"$out" || fail br_page
fresh; printf 'radio_last=2\n' >> "$T/fix/_player/config/app.txt"
out=$($SIM --headless --mode cast --root "$T/fix" --cmd "@0.6; cast st" 2>&1)
grep -q '^APP cast phase=3 .* station=2/' <<<"$out" || { EVID="$(grep '^APP cast' <<<"$out")"; fail br_player_station; }
fresh
out=$(SIM_STATE="castphase=1" $SIM --headless --mode cast --root "$T/fix" --cmd "@0.6; cast st; simcast phase 2; @0.6; cast st; simcast phase 4; @0.6; cast st; simcast phase 5; @0.6; cast st" 2>&1)
{ grep -q 'phase=5 ' <<<"$out" && grep -q 'phase=1 .* station=-1' <<<"$out" && grep -q 'phase=2 ' <<<"$out" && grep -q 'phase=4 ' <<<"$out"; } || { EVID="$(grep '^APP cast' <<<"$out")"; fail br_phases; }
out=$($SIM --headless --mode cast --root "$T/fix" --cmd "@0.6; cast tap 0; @0.3; uitop; cast flick up; @0.3; uitop" 2>&1)   # FIX2 I3: the 說明 page above the cast page leaves by the up flick (the hook acts only on the cast page)
[ "$(grep '^APP uitop' <<<"$out" | sed -n 2p)" = "APP uitop cast" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail br_help_leave; }
fresh
out=$($SIM --headless --root "$T/fix" --cmd "uipush radio; @0.5; uistack; ring" 2>&1)
grep -q 'radio' <<<"$out" || fail br_radio_page
# --- end BR ---

# crossfade (the new cover showed late): the page switches to the next track when it starts fading in (AUD_XFADE), before the old one has ended (the join, "APP track ... gapless")
nowrun xfade_title_early "setui; @0.3; settings key xfade; settings key xfade; back; @0.3; vol 0; track 1; @3; @250; uitop" && {
    l=$(grep -n '^APP mp3 xfade show idx=\|^APP track idx=.* gapless$' <<<"$out" | sed 's/^[0-9]*://' | head -2 | tr '\n' '|')
    case "$l" in "APP mp3 xfade show idx="*"|APP track idx="*" gapless|") ;; *) EVID="$l"; fail xfade_title_early;; esac; }
nowrun xfade_off_no_early "seti xfade_s 0; vol 0; track 1; @3; @250; uitop" && { ! grep -q '^APP mp3 xfade show' <<<"$out" || { EVID="early notice with the crossfade off"; fail xfade_off_no_early; }; }
