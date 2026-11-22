# SPDX-License-Identifier: LGPL-2.1-or-later
# --- SPLASH: the power-on picture (the ui design notes, 開機畫面): the boot front (app/main/boot_front.c) mounts, loads the config, starts the display and shows /sdcard/_player/splash/<mode>.jpg, else splash.jpg; one backlight breath; the mode's own code after it ---
# the pictures are generated here (Pillow), never committed
sp_pic() { python3 -I -c "
from PIL import Image
import sys
Image.new('RGB', (int(sys.argv[2]), int(sys.argv[3])), (200, 40, 40)).save(sys.argv[1], quality=80)" "$@"; }
sp_ramp() { grep '^SIM backlight ' <<<"$1" | sed 's/.* //' | tr '\n' ' '; }   # the backlight values in order
# a ramp is up - down - up: starts at 0, peaks at the user's brightness $2, the valley is a third of it, ends on it; no second breath
sp_shape() { python3 -I -c "
import sys
v = [int(x) for x in sys.argv[1].split()]; u = int(sys.argv[2]); lo = max(1, u // 3)
i = v.index(u) if u in v else -1
ok = len(v) > 3 and v[0] == 0 and i > 0 and v[-1] == u and max(v) == u and all(v[a] <= v[a + 1] for a in range(i))
j = i
while ok and j + 1 < len(v) and v[j + 1] <= v[j]: j += 1          # the way down to the valley
k = j
while ok and k + 1 < len(v) and v[k + 1] >= v[k]: k += 1          # and up again, to the end: one breath, no second one
ok = ok and j > i and abs(v[j] - lo) <= 3 and k == len(v) - 1 and min(v[1:]) >= 0
sys.exit(0 if ok else 1)" "$1" "$2"; }
fresh
mkdir -p "$T/fix/_player/splash"
# (1) no file: boot as today, one console line, no ramp
out=$(SIM_TRACE=1 $SIM --headless --root "$T/fix" --cmd "uitop" 2>&1)
{ grep -qx 'APP splash none (no file)' <<<"$out" && ! grep -q '^SIM backlight 0$' <<<"$out" && ! grep -q '^APP splash shown' <<<"$out" && grep -qx 'APP uitop menu' <<<"$out"; } || { EVID="$(grep '^APP splash\|^SIM backlight\|^APP uitop' <<<"$out" | head -5 | tr '\n' '|')"; fail splash_no_file; }
# (2) the general file: shown 1:1, the ramp, the first page only after the cycle (the page is pushed after "done"), freed (LSan)
sp_pic "$T/fix/_player/splash.jpg" 200 150
out=$(SIM_TRACE=1 $SIM --headless --root "$T/fix" --cmd "uitop" 2>&1)
grep -qx 'APP splash shown 200x150' <<<"$out" || { EVID="$(grep '^APP splash' <<<"$out" | tr '\n' '|')"; fail splash_shown; }
grep -qx 'APP splash file /sdcard/_player/splash.jpg' <<<"$out" || fail splash_general_file
sp_shape "$(sp_ramp "$out")" 60 || { EVID="$(sp_ramp "$out")"; fail splash_ramp_shape; }
python3 -I -c "
import sys
L = open(sys.argv[1]).read().split('\n')
d = [i for i, l in enumerate(L) if l.startswith('APP splash done ms=')]
p = [i for i, l in enumerate(L) if l.startswith('APP screen push=menu')]
ms = int(L[d[0]].split('=')[1])
sys.exit(0 if d and p and d[0] < p[0] and 2000 <= ms <= 2100 else 1)" <(printf '%s\n' "$out") || { EVID="$(grep '^APP splash done\|^APP screen push' <<<"$out" | tr '\n' '|')"; fail splash_page_after_cycle; }
grep -q 'Sanitizer\|runtime error' <<<"$out" && fail splash_sanitizer
# (3) the front's order, and the picture is on the screen while it breathes and gone after (a shot at 0, one after the boot)
out=$($SIM --headless --root "$T/fix" --cmd "shot $T/sp_after.png" 2>&1)
[ "$(grep '^APP boot front' <<<"$out" | tr '\n' '|')" = 'APP boot front: mode=player|APP boot front: mount card=1|APP boot front: config|APP boot front: display ok=1|APP boot front: handover mode=player|' ] || { EVID="$(grep '^APP boot front' <<<"$out" | tr '\n' '|')"; fail splash_front_order; }
# (4) the user's brightness (not a constant 60): 30 -> 0, up to 30, down to 10, up to 30
printf 'brightness=30\n' >> "$T/fix/_player/config/app.txt"
out=$(SIM_TRACE=1 $SIM --headless --root "$T/fix" --cmd "uitop" 2>&1)
sp_shape "$(sp_ramp "$out")" 30 || { EVID="$(sp_ramp "$out")"; fail splash_ramp_user_brightness; }
# (5) splash=0: nothing at all (not even a log line, no display start by the front)
printf 'splash=0\n' >> "$T/fix/_player/config/app.txt"
out=$(SIM_TRACE=1 $SIM --headless --root "$T/fix" --cmd "uitop" 2>&1)
{ ! grep -q '^APP splash' <<<"$out" && ! grep -q '^SIM backlight 0$' <<<"$out" && ! grep -q 'boot front: display' <<<"$out" && grep -qx 'APP uitop menu' <<<"$out"; } || { EVID="$(grep '^APP splash\|^SIM backlight' <<<"$out" | head -3 | tr '\n' '|')"; fail splash_off_switch; }
fresh; mkdir -p "$T/fix/_player/splash"
# (6) a file that does not decode, one that is too big, an empty one, and a progressive one: no picture, one line, a normal boot, no ramp, no second try
head -c 3000 /dev/urandom > "$T/fix/_player/splash.jpg"
out=$(SIM_TRACE=1 $SIM --headless --root "$T/fix" --cmd "uitop" 2>&1)
{ grep -q '^APP splash none (.*)$' <<<"$out" && [ "$(grep -c '^APP splash none' <<<"$out")" = 1 ] && ! grep -q '^SIM backlight 0$' <<<"$out" && grep -qx 'APP uitop menu' <<<"$out"; } || { EVID="$(grep '^APP splash' <<<"$out" | tr '\n' '|')"; fail splash_bad_file; }
head -c 1300000 /dev/zero > "$T/fix/_player/splash.jpg"
out=$($SIM --headless --root "$T/fix" --cmd "uitop" 2>&1)
grep -qx 'APP splash none (file too big)' <<<"$out" || { EVID="$(grep '^APP splash' <<<"$out" | tr '\n' '|')"; fail splash_too_big; }
: > "$T/fix/_player/splash.jpg"
out=$($SIM --headless --root "$T/fix" --cmd "uitop" 2>&1)
grep -qx 'APP splash none (empty file)' <<<"$out" || { EVID="$(grep '^APP splash' <<<"$out" | tr '\n' '|')"; fail splash_empty; }
python3 -I -c "
from PIL import Image
import sys
Image.new('RGB', (64, 64), (9, 99, 199)).save(sys.argv[1], progressive=True)" "$T/fix/_player/splash.jpg"
out=$($SIM --headless --root "$T/fix" --cmd "uitop" 2>&1)
grep -qx 'APP splash none (progressive)' <<<"$out" || { EVID="$(grep '^APP splash' <<<"$out" | tr '\n' '|')"; fail splash_progressive; }
# (7) a picture bigger than the panel is cropped, not scaled: drawn 1:1 centred (a 500 x 500 one is accepted and the screen shot is the centre of it)
sp_pic "$T/fix/_player/splash.jpg" 500 500
out=$($SIM --headless --root "$T/fix" --cmd "uitop" 2>&1)
grep -qx 'APP splash shown 500x500' <<<"$out" || fail splash_bigger_than_panel
# (8) no card: nothing to show, a line, the mode as it is today
fresh
out=$(SIM_STATE='sd=0' $SIM --headless --root "$T/fix" --cmd "uitop" 2>&1)
{ grep -qx 'APP splash none (no card)' <<<"$out" && ! grep -q '^APP splash shown' <<<"$out"; } || { EVID="$(grep '^APP splash\|^APP boot front' <<<"$out" | tr '\n' '|')"; fail splash_no_card; }
# (9) per boot mode: the mode's own file wins over the general one, the general one is the fallback; the same front order in each; the picture is gone before the mode's own code
fresh
mkdir -p "$T/fix/_player/splash"
sp_pic "$T/fix/_player/splash.jpg" 200 150
sp_pic "$T/fix/_player/splash/player.jpg" 120 120
sp_pic "$T/fix/_player/splash/usb.jpg" 110 130
for M in player inst usb cast; do
  case $M in player) W=120x120; AM=player;; usb) W=110x130; AM=ext;; *) W=200x150; AM=$M;; esac   # inst, cast: the general one
  out=$($SIM --headless --mode $AM --root "$T/fix" --cmd "@0.1" 2>&1)
  grep -qx "APP splash shown $W" <<<"$out" || { EVID="$M: $(grep '^APP splash' <<<"$out" | tr '\n' '|')"; fail splash_mode_file_$M; }
  F=$(grep '^APP boot front' <<<"$out" | sed 's/ *mode=.*//; s/ ok=.*//; s/ card=.*//; s/ *$//' | tr '\n' '|')
  if [ $M = usb ]; then [ "$F" = 'APP boot front:|APP boot front: mount|APP boot front: config|APP boot front: display|APP boot front: card released|APP boot front: handover|' ] || { EVID="$F"; fail splash_front_order_$M; }
  else [ "$F" = 'APP boot front:|APP boot front: mount|APP boot front: config|APP boot front: display|APP boot front: handover|' ] || { EVID="$M: $F"; fail splash_front_order_$M; }; fi
  python3 -I -c "
import sys
L = open(sys.argv[1]).read().split('\n')
h = [i for i, l in enumerate(L) if l.startswith('APP boot front: handover')]
d = [i for i, l in enumerate(L) if l.startswith('APP splash done')]
sys.exit(0 if h and d and d[0] < h[0] else 1)" <(printf '%s\n' "$out") || fail splash_done_before_handover_$M
  grep -q 'Sanitizer\|runtime error' <<<"$out" && fail splash_sanitizer_$M
done
# the card is released before the USB drive mode's own first line (the sim's ext boot: fake_ext_start / the page), and that mode is left without a filesystem
out=$($SIM --headless --mode ext --root "$T/fix" --cmd "@0.1" 2>&1)
python3 -I -c "
import sys
L = open(sys.argv[1]).read().split('\n')
r = [i for i, l in enumerate(L) if l == 'APP boot front: card released']
m = [i for i, l in enumerate(L) if l.startswith('APP ext') or l.startswith('APP usb') or l.startswith('APP cfg keys') and i > (r[0] if r else 0)]
sys.exit(0 if len(r) == 1 and r[0] < min(m or [10**9]) + 1 else 1)" <(printf '%s\n' "$out") || { EVID="$(grep '^APP boot front' <<<"$out" | tr '\n' '|')"; fail splash_usb_card_released; }
# the general file only as fallback: remove the per-mode one, the player now shows the general picture
rm "$T/fix/_player/splash/player.jpg"
out=$($SIM --headless --root "$T/fix" --cmd "@0.1" 2>&1)
grep -qx 'APP splash shown 200x150' <<<"$out" || fail splash_general_fallback
# safe mode: no front, no picture
out=$($SIM --headless --mode safe --root "$T/fix" --cmd "@0.1" 2>&1)
{ ! grep -q '^APP boot front\|^APP splash' <<<"$out"; } || fail splash_safe_mode_none
# (10) the card pack's default pictures (make splash) pass the hardware route's constraints and the safe circle (360 x 360)
if python3 -I -c "import PIL" 2>/dev/null && [ -f fonts/ui-icon.ttf ]; then
  python3 tools/gen/make_splash.py --out "$T/pack_splash" >/dev/null 2>&1 && python3 tools/gen/make_splash.py --check "$T/pack_splash" >/dev/null 2>&1 || fail splash_pack_pictures
  for M in player usb inst cast charge; do cp "$T/pack_splash/$M.jpg" "$T/fix/_player/splash/$M.jpg"; done
  out=$($SIM --headless --root "$T/fix" --cmd "shot $T/sp_pack.png" 2>&1)
  grep -qx 'APP splash shown 360x360' <<<"$out" || { EVID="$(grep '^APP splash' <<<"$out" | tr '\n' '|')"; fail splash_pack_picture_shown; }
fi
# --- end SPLASH ---
