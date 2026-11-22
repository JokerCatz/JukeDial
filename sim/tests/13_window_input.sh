# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: mk_root)
mk_root; fresh   # (the first check used the fixture a former piece left in $T/fix)
# ---- window input: the real window loop on SDL's "dummy" video driver, driven by synthetic SDL events (simsdl, test-only) ----
# Same code path as a desktop window (lv_sdl_window, event filter, mouse handler, sim_input_poll); only the video driver differs. "offscreen" is not used:
# its EGL set-up leaks 2.9 KB inside libSDL2/libdrm at window creation (LeakSanitizer fails the run), "dummy" leaks nothing. No suppression file anywhere.
ABS_SIM=$PWD/sim/build/sim
WP=

# click: window pixels -> the same screen as headless simtap on LVGL pixels (zoom 2: Clock; zoom 1: MP3). Ring: first tap selects a segment, the centre confirms.
for z in 2 1; do
    if [ $z = 2 ]; then hx=270; hy=87; want=clock; pick=4; else hx=80; hy=270; want=mp3; pick=0; fi
    hl=$($SIM --headless --root "$T/fix" --cmd "@0.5; simtap $hx $hy; simtap 180 180; uitop" 2>&1)
    grep -qx "APP menu pick=$pick" <<<"$hl" && grep -qx "APP uitop $want" <<<"$hl" || { fail "win_click_headless_ref z=$z"; echo "$hl" | tail -4; }
    fresh; wstart "$T/fix" $z
    wsend "simsdl click $((hx * z + z / 2)) $((hy * z + z / 2))"; wsend "simsdl click $((180 * z)) $((180 * z))"
    wexpect uitop "APP uitop" "APP uitop $want" || fail "win_click_zoom$z"
    grep -qx "APP menu pick=$pick" "$T/win.out" || fail "win_click_pick_zoom$z"
    wsend "simsdl key esc"; wend; wclean || fail "win_click_exit z=$z"
done
# the command exists only for a window; headless says so
out=$($SIM --headless --root "$T/root" --cmd "simsdl click 1 1; simsdl close" 2>&1)
[ "$(grep -c '^SIM n/a simsdl ' <<<"$out")" = 2 ] || { fail simsdl_headless_na; echo "$out"; }

# wheel (volume: on the kit2 page, which has no ring and no knob handler of its own; the main menu has two pages and its knob turns them, one detent = 2 volume points, wheel down = clockwise = louder), key T, key S
fresh; wstart "$T/fix" 2
wsend "uipush kit2"
wsend "vol 40"
wexpect simvol "SIM vol" "SIM vol 40" || fail win_vol_start
wsend "simsdl wheel -1"; wexpect simvol "SIM vol" "SIM vol 42" || fail win_wheel_down
wsend "simsdl wheel 1";  wexpect simvol "SIM vol" "SIM vol 40" || fail win_wheel_up
wsend "simsdl wheel 1";  wexpect simvol "SIM vol" "SIM vol 38" || fail win_wheel_up2
wsend "back"
# T: the theme changes (the UI rebuilds), and the picture differs
wlast "shot $T/th0.png" "SIM shot" >/dev/null
n=$(grep -c '^APP ui rebuilt' "$T/win.out"); wsend "simsdl key t"
wline "APP ui rebuilt" "$n" || fail win_key_t_rebuild
[ "$(grep -c '^APP ui rebuilt' "$T/win.out")" = $((n + 1)) ] || fail win_key_t_rebuild_once
wlast "shot $T/th1.png" "SIM shot" >/dev/null
[ -s "$T/th0.png" ] && [ -s "$T/th1.png" ] && ! cmp -s "$T/th0.png" "$T/th1.png" || fail win_key_t_pixels
# S: manual_1.png, a valid 360x360 PNG, pixel-identical to the headless shot of the same page (so no round-mask overlay in it)
n=$(grep -c '^SIM shot .local/sim/shots/manual_' "$T/win.out"); wsend "simsdl key s"
wline "SIM shot .local/sim/shots/manual_" "$n" || fail win_key_s_done   # the sim's own line, printed after the PNG is written
wsend "simsdl key esc"; wend
wclean || fail win_keys_exit
M="$T/win/.local/sim/shots/manual_1.png"
python3 -c "
import struct, sys
d = open(sys.argv[1], 'rb').read()
assert d[:8] == b'\x89PNG\r\n\x1a\n' and d[12:16] == b'IHDR', 'not a PNG'
w, h = struct.unpack('>II', d[16:24]); assert (w, h) == (360, 360), (w, h)" "$M" || fail win_key_s_png
fresh   # the headless twin: same data, same theme steps (vol 40, one theme next), same page
$SIM --headless --root "$T/fix" --cmd "vol 40; vol 38; @0.5; theme next; @0.5; shot $T/hl_s.png" >/dev/null 2>&1
cmp -s "$M" "$T/hl_s.png" || fail win_key_s_equals_headless
[ ! -e "$T/win/.local/sim/shots/manual_2.png" ] || fail win_key_s_once

# Esc, the window's close button (SDL_WINDOWEVENT_CLOSE) and SDL_QUIT with a screen open: exit 0, no sanitizer output
for how in "key esc" "close" "quit"; do
    fresh; wstart "$T/fix" 2
    wsend "uipush settings"; wexpect uitop "APP uitop" "APP uitop settings" || fail "win_open_settings ($how)"
    wsend "simsdl $how"
    for _ in $(seq 50); do [ -s "$T/win.rc" ] && break; sleep 0.1; done
    wend
    wclean || fail "win_exit ($how)"
done

