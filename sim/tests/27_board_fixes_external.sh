# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: mk_root)
mk_root
# --- board batch A fixes ---
# fixBoardA_console_order (F2): ten lines that reach the console before the UI task runs the first (simburst: no LVGL run in between), console
# commands and settings-protocol lines mixed, run in arrival order 1..10, each exactly once
burst=""; for i in 1 2 3 4 5 6 7 8 9 10; do if [ $((i % 2)) = 1 ]; then burst="$burst|burst$i"; else burst="$burst|{\"op\":\"nope\",\"id\":$i}"; fi; done
out=$($SIM --headless --root "$T/root" --cmd "simburst ${burst#|}" 2>&1); rc=$?
got=$(grep -oE '^APP cmd burst[0-9]+$|^CTL \{"id":[0-9]+' <<<"$out" | grep -oE '[0-9]+$' | tr '\n' ' ')
{ [ $rc = 0 ] && [ "$got" = "1 2 3 4 5 6 7 8 9 10 " ] && ! grep -q 'Sanitizer\|runtime error' <<<"$out"; } || { EVID="rc=$rc order=$got"; fail fixBoardA_console_order; }
# fixBoardA_console_full: a runaway burst of 70 lines: the first 64 run in order, each of the other 6 is refused with a console line (none vanishes)
burst=$(seq -f 'b%g' 1 70 | paste -sd'|')
out=$($SIM --headless --root "$T/root" --cmd "simburst $burst" 2>&1); rc=$?
got=$(grep -oE '^APP cmd b[0-9]+$' <<<"$out" | tr -d 'APP cmd b' | paste -sd' ')
{ [ $rc = 0 ] && [ "$got" = "$(seq -s' ' 1 64)" ] && [ "$(grep -c '^APP cmd refused: too many lines waiting$' <<<"$out")" = 6 ] && ! grep -q 'Sanitizer\|runtime error' <<<"$out"; } || { EVID="rc=$rc ran=$(wc -w <<<"$got") refused=$(grep -c '^APP cmd refused' <<<"$out")"; fail fixBoardA_console_full; }
# fixBoardA_boxes_text (F3 guard): ui_boxes_check fails a page where a label is drawn over Back; Back's own label and a label above it pass
lb() { set -- $1; printf 'APP uibox label %s %s %s %s %s %s %s %s |%s\n' "$1" "$2" "$3" "$4" "$1" "$2" "$3" "$4" "${5:-rule}"; }
bk() { printf 'APP uibox button 136 294 223 341 136 294 223 341 |返回\n'; }
{ bk; lb "40 250 319 307"; } >"$T/text_bad.txt"
{ bk; lb "150 305 210 330 返回"; lb "40 248 319 276"; } >"$T/text_ok.txt"
o=$(python3 tools/check/ui_boxes_check.py --from-file "$T/text_bad.txt" 2>&1); rc=$?
{ [ $rc = 1 ] && grep -q '^BOX fail text_bad: text ' <<<"$o"; } || { EVID="rc=$rc $o"; fail fixBoardA_boxes_text_catch; }
o=$(python3 tools/check/ui_boxes_check.py --from-file "$T/text_ok.txt" 2>&1); rc=$?
{ [ $rc = 0 ] && grep -q '^BOX ok text_ok' <<<"$o"; } || { EVID="rc=$rc $o"; fail fixBoardA_boxes_text_ok; }
# fixBoardA_slot_one_line (F3): the slot's text (the rule, one win, two and five lines paid) is one line between the reel frame and Back
fresh
for g in 100343531 227343531 222343000 777777777; do
    o=$($SIM --headless --root "$T/fix" --cmd "tools grid $g; @6; frame hold; uiboxes" 2>&1)
    read -r ly1 ly2 <<<"$(grep '^APP uibox label ' <<<"$o" | awk '{print $5, $7}')"
    by1=294   # POP: the old Back slot (the card moves 返回)
    { [ -n "$ly1" ] && [ -n "$by1" ] && [ "$ly1" -ge 244 ] && [ "$ly2" -lt "$by1" ] && [ $((ly2 - ly1)) -lt 40 ]; } || { EVID="grid $g label y=$ly1..$ly2 back y1=$by1"; fail fixBoardA_slot_one_line; }
done
# fixBoardA_cfg_guard (F6): a simulator tool run that leaves a setting changed fails (tools/simtools/sim_con.py cfg_guard), one that puts it back passes
cat >"$T/cfgguard.py" <<'PY'
import os, sys
sys.path[:0] = ["tools", "tools/simtools", "tools/gen", "tools/check", "tools/board"]
import ui_baseline, cfg_keep
which, out = sys.argv[1], sys.argv[2]
def pages(c, root, outdir, only=None):
    send = lambda x: c.cmd(x, 0)
    saved = cfg_keep.read(send, ["volume", "clockface"])
    c.cmd("seti volume 7", 0.1); c.cmd("clockface moon", 0.1); c.cmd("cfgdel clockface", 0.1)
    if which == "restored": cfg_keep.restore(send, saved)
ui_baseline.pages = pages
sys.exit(ui_baseline.sim_run(out))
PY
o=$(python3 "$T/cfgguard.py" left "$T/cg1" 2>&1); rc=$?
{ [ $rc != 0 ] && grep -q '^FAILED: settings changed by the run' <<<"$o" && grep -q 'volume: 40 -> 7' <<<"$o"; } || { EVID="rc=$rc $(tail -3 <<<"$o" | tr '\n' '|')"; fail fixBoardA_cfg_guard_catch; }
o=$(python3 "$T/cfgguard.py" restored "$T/cg2" 2>&1); rc=$?
[ $rc = 0 ] || { EVID="rc=$rc $(tail -3 <<<"$o" | tr '\n' '|')"; fail fixBoardA_cfg_guard_ok; }
# fixBoardA_baseline_restores (F6): the shot steps that move settings (clock faces, volume, the axis shots) leave app.txt's settings as they found them,
# from a root where they are set to non-default values (the board's case: clockface was deleted)
rm -rf "$T/cfgroot"; cp -r sim/fixture "$T/cfgroot"
printf 'volume=40\nlang=0\nclockface=moon\nbrightness=70\nlast_screen=mp3\nalarm_on=1\nalarm_min=400\nalarm_days=31\n' >"$T/cfgroot/_player/config/app.txt"
o=$(python3 tools/simtools/ui_baseline.py sim "$T/cfgshots" --sim-root "$T/cfgroot" --only clockface_moon,show_alarm,now_menu_p2,settings_bright_axis,alarm_hour_axis 2>&1); rc=$?
{ [ $rc = 0 ] && ! grep -q '^FAILED' <<<"$o"; } || { EVID="rc=$rc $(grep -A8 '^FAILED' <<<"$o" | tr '\n' '|')"; fail fixBoardA_baseline_restores; }
# fixBoardA_vizfps_restores (F6): viz_fps.py's measure steps (viz kinds, variants, order, gap, size, vol 0) leave the settings as they were (one row, in the simulator)
o=$(python3 tools/board/viz_fps.py A --sim --only bars_v0 --out "$T/vf" 2>&1); rc=$?
{ [ $rc = 0 ] && ! grep -q '^FAILED' <<<"$o"; } || { EVID="rc=$rc $(grep -A12 '^FAILED' <<<"$o" | tr '\n' '|')"; fail fixBoardA_vizfps_restores; }
# fixBoardA_cfgset_key (review M4): cfgset refuses a key the settings file cannot hold (over 31 bytes, or with '='); a plain key still works
out=$($SIM --headless --root "$T/root" --cmd "cfgset a234567890123456789012345678901x 1; cfgset a=b 1; cfgset t12k 7; cfg t12k" 2>&1)
{ [ "$(grep -c '^APP cfg refused key=' <<<"$out")" = 2 ] && [ "$(grep -c '^APP cfg t12k=7$' <<<"$out")" = 2 ] && ! grep -q '^APP cfg a' <<<"$out"; } || { EVID="$(grep '^APP cfg' <<<"$out" | tr '\n' '|')"; fail fixBoardA_cfgset_key; }
# fixBoardA_ring_p2_after_slow_grab (F5): the board's grab of now_menu takes longer than the ring's 4 s timeout; the now_menu_p2 shot still shows
# the ring open on its second page (the simulator stands in: 5 s pass after the now_menu picture)
cat >"$T/slowgrab.py" <<'PY'
import os, sys
sys.path.insert(0, sys.argv[2])
import ui_baseline
from sim_con import SimCon
class Slow(SimCon):
    def shot(self, path):
        super().shot(path)
        if path.endswith("now_menu.png"): self.drain(5)          # what a board grab costs
seen = []
os.makedirs(sys.argv[1], exist_ok=True)
c = Slow(strict=True)
c.cmd("mem", 1.0)
ui_baseline.pages(c, ui_baseline.find_root(c), sys.argv[1], {"now_menu", "now_menu_p2"},
                  lambda n: seen.append((n, c.cmd("nowmenu -5", 0).decode())))
c.close()
p2 = [o for n, o in seen if n == "now_menu_p2"]
print("p2:", p2)
sys.exit(0 if p2 and "APP now menu=1 page=1" in p2[0] else 1)
PY
o=$(python3 "$T/slowgrab.py" "$T/slowshots" tools/simtools 2>&1); rc=$?
[ $rc = 0 ] || { EVID="rc=$rc $(grep '^p2:\|Error\|FAILED' <<<"$o" | tr '\n' ' ')"; fail fixBoardA_ring_p2_after_slow_grab; }
# --- 外接模式 (external mode) T2 ---
# the external mode (sim --mode ext; alias usb): one menu ring on the bottom page (keys of the layer, layer cells, 設定, 重讀設定, 輸入網址, 回播放器 armed, 說明), the HID send queue
# (pc_extsend, printed as "SIM ext hid ..." by the fake sender), the config read at the start and again after the computer ejected (pc_usb_where rules).
mk_t12root
# t12_ext_default: no keys.txt = the default media layer, no error; the first key (播放) is consumer 0xCD down and up; a key before the computer is there sends nothing
out=$(t12 "ext key 0; @0.3; ext state; simusb mount; @0.3; ext key 0; @0.3; ext key 1; @0.3")
{ grep -qx 'APP ext cfg layers=1 from_file=0 ok=1 errors=0' <<<"$out" && [ "$(hid)" = 'SIM ext hid cons 00CD|SIM ext hid cons 0000|SIM ext hid cons 00B6|SIM ext hid cons 0000|' ] && ! t12bad; } || { EVID="$(hid) $(grep '^APP ext' <<<"$out" | tr '\n' '|')"; fail t12_ext_default; }
# t12_ext_knob: the media layer's knob is the volume: two detents = two consumer volume pairs; scroll layer = Down arrows
out=$(t12 "simusb mount; @1; knob 2; @0.5; knob -1; @0.3")   # (the first second after the boot swallows a knob turn: the wake state)
{ [ "$(hid)" = 'SIM ext hid cons 00E9|SIM ext hid cons 0000|SIM ext hid cons 00E9|SIM ext hid cons 0000|SIM ext hid cons 00EA|SIM ext hid cons 0000|' ] && ! t12bad; } || { EVID="$(hid)"; fail t12_ext_knob; }
mkdir -p "$T/t12root/_player/config"
printf 'layer a\nknob scroll\nkey 複製 combo ctrl+c\nkey 簽名 text "Hi\\n"\nkey 巨集 macro ctrl+a, wait 50, ctrl+c\nlayer b\nknob none\nkey 播 consumer 0xCD\n' >"$T/t12root/_player/config/keys.txt"
# t12_ext_file_layers: keys.txt read at the start: two layers; combo, text and macro (with its wait) reach the sender; the scroll knob = Down; layer b has knob none
out=$(t12 "simusb mount; @1; ext key 0; @0.5; ext key 1; @1.5; ext key 2; @1.5; knob 1; @0.5; ext layer 1; @0.5; knob 1; @0.5; ext state")
{ grep -qx 'APP ext cfg layers=2 from_file=1 ok=1 errors=0' <<<"$out" \
  && [ "$(hid)" = 'SIM ext hid kbd mod=01 code=06|SIM ext hid kbd mod=00 code=00|SIM ext hid kbd mod=02 code=0B|SIM ext hid kbd mod=00 code=00|SIM ext hid kbd mod=00 code=0C|SIM ext hid kbd mod=00 code=00|SIM ext hid kbd mod=00 code=28|SIM ext hid kbd mod=00 code=00|SIM ext hid kbd mod=01 code=04|SIM ext hid kbd mod=00 code=00|SIM ext hid wait 50|SIM ext hid kbd mod=01 code=06|SIM ext hid kbd mod=00 code=00|SIM ext hid wheel -1|' ] \
  && grep -q '^SIM ext layer=1/2 ' <<<"$out" && ! t12bad; } || { EVID="$(hid)"; fail t12_ext_file_layers; }
# t12_ext_bad_file: a bad line rejects the whole file: the default layer, the error count, the 設定 page lists it (line number first)
printf 'layer a\nkey x consumer 0xCD\nkey y text "你好"\n' >"$T/t12root/_player/config/keys.txt"
out=$(t12 "ext errors; @0.3; uitop; uiboxes")
{ grep -qx 'APP ext cfg layers=1 from_file=0 ok=0 errors=1' <<<"$out" && grep -qx 'APP uitop ext_errors' <<<"$out" && grep -q '3：' <<<"$out" && ! t12bad; } || { EVID="$(grep '^APP ext\|^APP uitop\|^APP uibox' <<<"$out" | cut -d'|' -f2 | tr '\n' '|')"; fail t12_ext_bad_file; }
rm -f "$T/t12root/_player/config/keys.txt"
# t12_ext_reload_after_eject: a file written while the computer has the card is read after the eject (once), not before; reload says "in use" while it uses the card
cp -r "$T/t12root" "$T/t12e"
out=$(rm -rf "$T/t12r"; cp -r "$T/t12root" "$T/t12r"; printf 'layer q\nkey 一 consumer 0xCD\nkey 二 consumer 0xB5\nlayer w\nkey 三 consumer 0xB6\n' >"$T/t12r/config_new.txt"; $SIM --headless --mode ext --root "$T/t12r" --cmd "simusb mount; ext cfg config_new.txt; @0.5; ext reload; ext state; simusb eject; @0.6; ext state; ext reload; ext state; simusb unplug; simusb mount; @0.6; ext state" 2>&1)
{ [ "$(grep -c '^APP ext cfg layers=2' <<<"$out")" -ge 1 ] && grep -qx 'SIM ext reload=1' <<<"$out" && grep -qx 'SIM ext reload=0' <<<"$out" && grep -q '^SIM ext layer=0/2 where=0 ' <<<"$out" && ! t12bad; } || { EVID="$(grep '^APP ext\|^SIM ext' <<<"$out" | tr '\n' '|')"; fail t12_ext_reload_after_eject; }
# t12_ext_exit_armed: the exit cell arms (the in-use warning in the centre); the second tap leaves anyway (in_use=1, the restart); an armed cell that is let run out does not
out=$(t12 "simusb mount; @0.3; ext arm; @4.5; ring")
{ ! grep -q '^APP usb mode exit' <<<"$out" && ! t12bad; } || { EVID="$(grep '^APP usb' <<<"$out")"; fail t12_ext_exit_timeout; }
out=$(t12 "simusb mount; @0.3; ext arm; ring; ext key 99; ext arm; @0.2")
{ grep -q '^APP ring .*center=.*' <<<"$out" && ! t12bad; } || { fail t12_ext_exit_armed; }
# t12_ext_url: 輸入網址 types the settings address (a queued text) only while the computer is there
out=$(t12 "ext url; @0.3; simusb mount; @0.3; ext url; @4")
{ [ "$(grep -c '^SIM usb typed' <<<"$out")" = 1 ] && [ "$(grep -c '^SIM ext hid kbd' <<<"$out")" -gt 40 ] && ! t12bad; } || { EVID="typed=$(grep -c '^SIM usb typed' <<<"$out")"; fail t12_ext_url; }
# t12_no_cfg_write: the card is the computer's: a changed setting is never saved in this mode (cfg_writes false)
out=$(SIM_STATE="usb_sd=1" t12 "@1; seti brightness 50; @5")
{ grep -qx 'APP cfg brightness=50' <<<"$out" && ! grep -q '^APP cfg saved' <<<"$out" && ! t12bad; } || { EVID="$(grep '^APP cfg' <<<"$out" | tr '\n' '|')"; fail t12_ext_no_cfg_write; }
# --- 外接模式 T3: Bluetooth in the same boot (pc_xport asks; ext_ble is the fake) ---
# t3_ext_unplug_yes: the cable goes -> the question page (nobody sends, no advertising); yes -> advertising, a computer connects, the key goes over BLE and never over USB
out=$(t12 "simusb mount; @1; ext xport; simusb unplug; @0.6; uitop; ext xport; ext key 0; @0.3; ext ask yes; @0.6; ext xport; ext ble conn; @0.3; ext key 0; @0.5; ext xport")
{ [ "$(xp)" = 'SIM ext xport sender=1 ask=0 ble_ok=1 link=1|SIM ext xport sender=0 ask=1 ble_ok=1 link=0|SIM ext xport sender=2 ask=0 ble_ok=1 link=0|SIM ext xport sender=2 ask=0 ble_ok=1 link=1|' ] \
  && grep -qx 'APP uitop ext_ask' <<<"$out" && grep -qx 'SIM ext ble adv=1' <<<"$out" && [ "$(grep -c '^SIM ext ble hid cons' <<<"$out")" = 2 ] && ! grep -q '^SIM ext hid' <<<"$out" && ! t12bad; } || { EVID="$(xp) $(grep '^SIM ext ble\|^SIM ext hid\|^APP uitop' <<<"$out" | tr '\n' '|')"; fail t3_ext_unplug_yes; }
# t3_ext_cancel_then_again: no = stays on the USB side (no advertising, keys wait for the cable); the next unplug asks again; 30 s without an answer is a no
out=$(t12 "simusb mount; @1; simusb unplug; @0.6; ext ask no; @0.3; ext xport; ext ble adv; simusb mount; @0.5; ext xport; simusb unplug; @0.6; ext xport; @31; ext xport; ext ble adv")
{ [ "$(xp)" = 'SIM ext xport sender=1 ask=0 ble_ok=1 link=0|SIM ext xport sender=1 ask=0 ble_ok=1 link=1|SIM ext xport sender=0 ask=1 ble_ok=1 link=0|SIM ext xport sender=1 ask=0 ble_ok=1 link=0|' ] \
  && [ "$(grep -c '^SIM ext ble adv=1 ' <<<"$out")" = 0 ] && ! grep -qx 'SIM ext ble adv=1' <<<"$out" && ! t12bad; } || { EVID="$(xp) $(grep '^SIM ext ble' <<<"$out" | tr '\n' '|')"; fail t3_ext_cancel_then_again; }
# t3_ext_replug: on the Bluetooth side the cable asks "back to USB?"; no = stays (the link and the advertising go on); yes = the link drops, advertising stops, keys over USB
out=$(t12 "simusb mount; @1; simusb unplug; @0.6; ext ask yes; @0.4; ext ble conn; @0.3; simusb mount; @0.6; ext xport; ext ask no; @0.3; ext key 0; @0.5; simusb unplug; @0.4; simusb mount; @0.6; ext xport; ext ask yes; @0.4; ext xport; ext key 0; @0.5")
{ [ "$(xp)" = 'SIM ext xport sender=2 ask=2 ble_ok=1 link=1|SIM ext xport sender=2 ask=2 ble_ok=1 link=1|SIM ext xport sender=1 ask=0 ble_ok=1 link=1|' ] && grep -qx 'SIM ext ble disconnected' <<<"$out" \
  && [ "$(grep -c '^SIM ext ble hid cons 00CD' <<<"$out")" = 1 ] && [ "$(grep -c '^SIM ext hid cons 00CD' <<<"$out")" = 1 ] && ! t12bad; } || { EVID="$(xp) $(grep '^SIM ext ble\|^SIM ext hid' <<<"$out" | tr '\n' '|')"; fail t3_ext_replug; }
# t3_ext_no_ble: the stack did not start: no question at the unplug, the keys stay on USB, the 藍牙 cell says so
out=$(SIM_STATE="extble=0" t12 "simusb mount; @1; simusb unplug; @0.6; ext xport; uitop; ext bt; @0.2")
{ [ "$(xp)" = 'SIM ext xport sender=1 ask=0 ble_ok=0 link=0|' ] && grep -qx 'APP uitop ext' <<<"$out" && ! t12bad; } || { EVID="$(xp) $(grep '^APP uitop' <<<"$out")"; fail t3_ext_no_ble; }
# t3_ext_bt_cell: the 藍牙 cell asks for the other side by hand: no cable and USB side -> the question; the cable in -> nothing (a note)
out=$(t12 "simusb mount; @1; ext bt; @0.3; ext xport; simusb unplug; @0.6; ext ask no; @0.3; ext bt; @0.5; ext xport")
{ [ "$(xp)" = 'SIM ext xport sender=1 ask=0 ble_ok=1 link=1|SIM ext xport sender=0 ask=1 ble_ok=1 link=0|' ] && ! t12bad; } || { EVID="$(xp)"; fail t3_ext_bt_cell; }
# t3_ext_unpair: 清除配對 forgets every bond and drops the link
out=$(t12 "ext ble bond 2; simusb mount; @1; ext unpair; @0.3; ext ble adv")
{ grep -qx 'SIM ext ble unpaired' <<<"$out" && grep -q 'bonded=0' <<<"$out" && ! t12bad; } || { EVID="$(grep '^SIM ext ble' <<<"$out" | tr '\n' '|')"; fail t3_ext_unpair; }
# --- 外接模式 T4: the descriptor, the function mixes, the wheel ---
# t4_ext_dump_default: the three-in-one at the default 48 k cap: 306 bytes, interfaces MSC HID AC AS (4), the FIFO words of spec section 2 (453 of 992), rates 44.1 / 48 k, three alternate settings
out=$(t12 "ext usb dump")
{ [ "$(dump)" = 'combo=0 pid=4004 len=306 itf=4 desc_itf=7 eps=9 msc=1 hid=1 ac=1 as=1 alts=3 rates=44100,48000 fifo rx=304 total=453 of 992|' ] && ! t12bad; } || { EVID="$(dump)"; fail t4_ext_dump_default; }
# t4_ext_cap: the 有線最高取樣率 setting caps what the descriptor advertises (read at the mode start: here set before the dump the way the console does)
out=$(t12 "seti dac_max_rate 384000; ext usb dump; seti dac_max_rate 96000; ext usb dump")
{ [ "$(dump)" = 'combo=0 pid=4004 len=306 itf=4 desc_itf=7 eps=9 msc=1 hid=1 ac=1 as=1 alts=3 rates=44100,48000,88200,96000,176400,192000,352800,384000 fifo rx=304 total=453 of 992|combo=0 pid=4004 len=306 itf=4 desc_itf=7 eps=9 msc=1 hid=1 ac=1 as=1 alts=3 rates=44100,48000,88200,96000 fifo rx=304 total=453 of 992|' ] && ! t12bad; } || { EVID="$(dump)"; fail t4_ext_cap; }
# t4_ext_enum_alt: the computer walks the descriptor (every length, no repeated endpoint) and mounts; alternate settings 0..3 are accepted (16 / 24 / 32 bit, the packet size of the table), 4 is refused
out=$(t12 "ext usb plug; @0.5; ext usb alt 1; ext usb alt 2; ext usb alt 3; ext usb alt 0; ext usb alt 4; ext usb unplug; ext usb alt 1")
{ grep -qx 'SIM ext usb enum ok' <<<"$out" && [ "$(grep '^SIM ext usb alt' <<<"$out" | tr '\n' '|')" = 'SIM ext usb alt=1 pkt=28 bits=16|SIM ext usb alt=2 pkt=42 bits=24|SIM ext usb alt=3 pkt=56 bits=32|SIM ext usb alt=0 pkt=0 bits=0|SIM ext usb alt refused 4|SIM ext usb alt refused 1|' ] && ! t12bad; } || { EVID="$(grep '^SIM ext usb' <<<"$out" | tr '\n' '|')"; fail t4_ext_enum_alt; }
# t4_ext_combos: the exclusive fallbacks: HID + drive (57 bytes, no audio) and HID + sound card (283 bytes, no drive); the mix is kept and the mode "restarts"; the sound-only mix never has the card on the computer (reload allowed while mounted)
out=$(t12 "ext usb combo 1; ext usb dump; ext usb combo 2; ext usb dump; simusb mount; @0.5; ext reload")
{ [ "$(dump)" = 'combo=1 pid=4002 len=57 itf=2 desc_itf=2 eps=3 msc=1 hid=1 ac=0 as=0 alts=0 rates= fifo rx=304 total=452 of 992|combo=2 pid=4005 len=283 itf=3 desc_itf=6 eps=7 msc=0 hid=1 ac=1 as=1 alts=3 rates=44100,48000 fifo rx=76 total=97 of 992|' ] \
  && [ "$(grep -c '^APP usb combo=' <<<"$out")" = 2 ] && grep -qx 'SIM ext reload=0' <<<"$out" && ! t12bad; } || { EVID="$(dump) $(grep '^SIM ext reload' <<<"$out")"; fail t4_ext_combos; }
# t4_ext_func_cell: the USB mix cell is two taps (armed first); the second cycles the mix (three-in-one -> drive + keys)
out=$(t12 "simusb mount; @1; ext page; @0.3; ext funcarm; @0.3; ext funcarm; @0.5; ext usb dump")
{ [ "$(dump)" = 'combo=1 pid=4002 len=57 itf=2 desc_itf=2 eps=3 msc=1 hid=1 ac=0 as=0 alts=0 rates= fifo rx=304 total=452 of 992|' ] && ! t12bad; } || { EVID="$(dump) $(grep '^APP usb' <<<"$out")"; fail t4_ext_func_cell; }
out=$(t12 "simusb mount; @1; ext page; @0.3; ext funcarm; @4.5; ext funcarm; @0.3; ext usb dump")
{ [ "$(dump)" = 'combo=0 pid=4004 len=306 itf=4 desc_itf=7 eps=9 msc=1 hid=1 ac=1 as=1 alts=3 rates=44100,48000 fifo rx=304 total=453 of 992|' ] && ! t12bad; } || { EVID="$(dump)"; fail t4_ext_func_cell_timeout; }
# t4_ext_scroll_wheel: knob scroll = mouse wheel notches (forward = down), no release; volume and arrows unchanged
printf 'layer a\nknob scroll\nkey x consumer 0xCD\nlayer b\nknob arrows\nkey y consumer 0xCD\n' >"$T/t12root/_player/config/keys.txt"
out=$(t12 "simusb mount; @1; knob 2; @0.5; knob -1; @0.5; ext layer 1; @0.3; knob 1; @0.5")
{ [ "$(hid)" = 'SIM ext hid wheel -1|SIM ext hid wheel -1|SIM ext hid wheel 1|SIM ext hid kbd mod=00 code=4F|SIM ext hid kbd mod=00 code=00|' ] && ! t12bad; } || { EVID="$(hid)"; fail t4_ext_scroll_wheel; }
rm -f "$T/t12root/_player/config/keys.txt"
# --- 外接模式 T5: the sound card streams (a fake host through the shared ext_audio / pc_extpath code and the fake audio_svc) ---
# t5_ext_play_rates: every rate x bit depth plays with no underrun, no overrun and no refusal; the full-scale sine (peak_in 1.0) leaves the fixed limiter at its ceiling (<= 0.8914) and the wire (x device volume 20) far below it
out=$(t12 "seti dac_max_rate 384000; ext usb plug; @0.5; ext uac play 44100 16 4; ext uac play 48000 24 4; ext uac play 88200 32 4; ext uac play 96000 16 4; ext uac play 176400 24 4; ext uac play 192000 32 4; ext uac play 352800 16 4; ext uac play 384000 24 4")
ok=1; for i in 1 2 3 4 5 6 7 8; do
  [ "$(uacf $i underruns)" = 0 ] && [ "$(uacf $i overruns)" = 0 ] && [ "$(uacf $i refused)" = 0 ] && [ "$(uacf $i wire_ok)" = 1 ] && [ "$(uacf $i limited)" = 0 ] \
    && awk -v p="$(uacf $i peak_hook)" 'BEGIN{exit !(p > 0.8 && p <= 0.8914)}' || ok=0; done
{ [ "$(uacd | wc -l)" = 8 ] && [ $ok = 1 ] && ! grep -q 'OVER FULL SCALE' <<<"$out" && ! t12bad; } || { EVID="$(uacd | cut -c1-200 | tr '\n' '|')"; fail t5_ext_play_rates; }
# t5_ext_drift: the device clock off by +-500 ppm (and a quiet signal, amp 0.25, passes the limiter untouched): converged, no underrun / overrun in 12 s, the feedback stays inside nominal +- 1 sample (65536)
out=$(t12 "ext usb plug; @0.5; ext uac play 48000 24 12 500 0.25; ext uac play 44100 24 12 -500 0.25")
ok=1; for i in 1 2; do
  [ "$(uacf $i underruns)" = 0 ] && [ "$(uacf $i overruns)" = 0 ] && awk -v lo="$(uacf $i fb_min)" -v hi="$(uacf $i fb_max)" -v n="$(uacf $i fb_nominal)" 'BEGIN{exit !(lo >= n - 65536 && hi <= n + 65536)}' \
    && awk -v p="$(uacf $i peak_hook)" 'BEGIN{exit !(p > 0.24 && p < 0.26)}' || ok=0; done
{ [ $ok = 1 ] && ! t12bad; } || { EVID="$(uacd | cut -c1-260 | tr '\n' '|')"; fail t5_ext_drift; }
# t5_ext_start_volume: the mode starts at min(saved, 20) BEFORE any block: the saved 40 / 100 -> 20, a saved 7 stays 7; the order of the log: service, volume, the fixed limiter, only then the mixer
out=$(t12 "ext uac status; ext usb plug; @0.5; ext uac play 48000 16 1")
a=$(grep -n '^APP ext audio start vol=20 (saved 40)' <<<"$out" | cut -d: -f1); b=$(grep -n '^APP ext audio mixer' <<<"$out" | cut -d: -f1)
{ grep -q '^SIM ext uac status alt=0 .* vol=20$' <<<"$out" && [ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ] && ! t12bad; } || { EVID="$(grep '^APP ext audio\|^SIM ext uac status' <<<"$out" | tr '\n' '|')"; fail t5_ext_start_volume; }
out=$(t12 "seti volume 100; ext uac status"); grep -q '^SIM ext uac status alt=0 .* vol=20$' <<<"$out" || { EVID="$(grep 'ext uac status' <<<"$out")"; fail t5_ext_start_volume_100; }
out=$(t12 "seti volume 7; ext uac status"); grep -q '^SIM ext uac status alt=0 .* vol=7$' <<<"$out" || { EVID="$(grep 'ext uac status' <<<"$out")"; fail t5_ext_start_volume_7; }
# t5_ext_pregain: the feature unit never raises: +127 dB asked = 0 dB (gain 1.0), -6 dB = 0.5012, channel 1 (left) adds another -6 dB (0.2512), mute = 0; a rate the board is not advertising is refused
out=$(t12 "ext uac fu 32512; ext uac fu -1536; ext uac fu -1536 1; ext uac mute 1; ext uac mute 0; ext uac play 31337 16 1")
{ [ "$(grep '^SIM ext uac fu' <<<"$out" | tr '\n' '|')" = 'SIM ext uac fu ok=1 gain_l=1.0000 gain_r=1.0000|SIM ext uac fu ok=1 gain_l=0.5012 gain_r=0.5012|SIM ext uac fu ok=1 gain_l=0.2512 gain_r=0.5012|' ] \
  && grep -qx 'SIM ext uac mute ok=1 gain_l=0.0000' <<<"$out" && grep -qx 'SIM ext uac mute ok=1 gain_l=0.2512' <<<"$out" && grep -qx 'SIM ext uac refused rate=31337 (not advertised)' <<<"$out" && ! t12bad; } \
  || { EVID="$(grep '^SIM ext uac' <<<"$out" | tr '\n' '|')"; fail t5_ext_pregain; }
# t5_ext_dac_refuse: the DAC clock refuses 192 k (simhw 96000): the screen-side status says refused, the stream is silence (nothing counted as played), the next entry's descriptor no longer advertises it (RTC note)
out=$(t12 "seti dac_max_rate 384000; simhw 96000; ext usb plug; @0.5; ext uac play 192000 24 2; ext uac status; ext uac play 96000 24 3; ext usb dump")
{ grep -q 'APP ext audio refused rate=192000 (the DAC clock: cap 96000)' <<<"$out" && [ "$(uacf 1 refused)" = 1 ] && [ "$(uacf 2 refused)" = 0 ] && [ "$(uacf 2 underruns)" = 0 ] \
  && grep -q '^SIM ext uac status alt=2 bits=24 rate=192000 mixer=1 refused=1' <<<"$out" && grep -q 'rates=44100,48000,88200,96000,176400,352800,384000 ' <<<"$out" && ! t12bad; } \
  || { EVID="$(grep '^APP ext audio\|^SIM ext uac status\|^SIM ext usb dump' <<<"$out" | cut -c1-200 | tr '\n' '|')"; fail t5_ext_dac_refuse; }
# t5_ext_stop: alt 0 = no stream: the status says so and the ring is empty; the mixer stays (the render gives silence); the same rate again plays at once
out=$(t12 "ext usb plug; @0.5; ext uac play 48000 16 2; ext uac stop; ext uac play 48000 16 2")
{ grep -q '^SIM ext uac status alt=0 bits=0 rate=48000 mixer=1 refused=0 fill=0 ' <<<"$out" && [ "$(grep -c '^APP ext audio mixer' <<<"$out")" = 1 ] && [ "$(uacf 2 underruns)" = 0 ] && ! t12bad; } || { EVID="$(grep '^APP ext audio\|^SIM ext uac status' <<<"$out" | tr '\n' '|')"; fail t5_ext_stop; }
# t5_ext_no_sound_mix: the drive + keys mix has no sound card: nothing to play
out=$(SIM_STATE="extusb=1" t12 "ext uac play 48000 16 1")
{ grep -qx 'SIM ext uac refused: no sound card in this mix' <<<"$out" && ! grep -q 'APP ext audio start' <<<"$out" && ! t12bad; } || { EVID="$(grep '^SIM ext uac' <<<"$out")"; fail t5_ext_no_sound_mix; }
# t5_ext_knob_volume: while the computer streams the volume role of the knob is the device's own volume (2 steps per detent from 20, volume_max applies); without a stream it is still the computer's (t12_ext_knob)
out=$(t12 "simusb mount; @1; ext uac play 48000 16 1; knob 2; @0.5; knob -1; @0.3; seti volume_max 22; knob 5; @0.3; ext uac status; ext uac stop; knob 1; @0.5")
{ [ "$(grep '^APP ext audio volume' <<<"$out" | tr '\n' '|')" = 'APP ext audio volume=24|APP ext audio volume=22|APP ext audio volume=22|' ] && grep -q '^SIM ext uac status alt=1 .* vol=22$' <<<"$out" && [ "$(hid)" = 'SIM ext hid cons 00E9|SIM ext hid cons 0000|' ] && ! t12bad; } || { EVID="$(grep '^APP ext audio volume\|^SIM ext uac status' <<<"$out" | tr '\n' '|') $(hid)"; fail t5_ext_knob_volume; }
# t5_ext_source_pins: the audio-safety source pins (tools/check/ext_audio_guard.sh), also run by make lint-app
bash tools/check/ext_audio_guard.sh >/dev/null 2>&1 || { EVID="$(bash tools/check/ext_audio_guard.sh 2>&1 | tail -3 | tr '\n' '|')"; fail t5_ext_source_pins; }
bash tools/check/ext_audio_guard.sh --selftest >/dev/null 2>&1 || fail t5_ext_source_pins_selftest
# flash_write_guard: direct flash/NVS writes only in the allowlist (tools/check/check_flash_writes.py), also run by make lint-app
python3 tools/check/check_flash_writes.py >/dev/null 2>&1 || { EVID="$(python3 tools/check/check_flash_writes.py 2>&1 | tail -3 | tr '\n' '|')"; fail flash_write_guard; }
python3 tools/check/check_flash_writes.py --selftest >/dev/null 2>&1 || fail flash_write_guard_selftest
# t12_ext_reload_card_lock (fix 4): the computer taking the card back (esp_tinyusb MOUNT_START, then it frees the FATFS) waits for a reload in progress and only then marks the card
# not ours: with the lock held (`ext lock` stands for the reload) the card stays ours and the start is refused; after the unlock it goes, and a reload then says "in use"
out=$(t12 "simusb mount; simusb eject; @0.3; ext lock; simusb mountstart; ext state; ext unlock; simusb mountstart; ext reload")
{ grep -qx 'SIM usb mountstart blocked' <<<"$out" && grep -q '^SIM ext layer=0/1 where=0 ' <<<"$out" && grep -qx 'SIM usb where=1 mounted=1' <<<"$out" && grep -qx 'SIM ext reload=1' <<<"$out" && ! t12bad; } || { EVID="$(grep '^SIM usb\|^SIM ext' <<<"$out" | tr '\n' '|')"; fail t12_ext_reload_card_lock; }
# t12_boxes_ext: the pictures pass the tap-target rules
out=$(python3 tools/check/ui_boxes_check.py sim --mode ext 2>&1); rc=$?
{ [ $rc = 0 ] && grep -q '^BOXES ok' <<<"$(tail -1 <<<"$out")"; } || { EVID="rc=$rc $(grep -v '^BOX ok' <<<"$out" | head -4 | tr '\n' '|')"; fail t12_boxes_ext; }

