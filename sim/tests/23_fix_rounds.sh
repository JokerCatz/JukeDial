# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: need_pick)
need_pick
# --- Task 13 fix round 1 ---
# R40: a cover / spectrum page that has seen playback and comes back on top with nothing playing leaves (ui_screen_t.on_resume). Each case: the stack
# before the failure, the message page, the stack after it went (the page underneath, not now), and the LVGL timer census (taken before anything played).
# (a) a radio stream gives up (the board: three failed connects; the fake runs the same stop + message): the message, then the station list again
r40 t13_r40_radio "screen on; vol 0; @0.5; uipush radio; @0.3; simtimers; simtap 180 82; @0.5; uistack; simradio giveup; @0.3; uistack; @3; uistack; simtimers" \
    "menu radio now" "menu radio now message" "menu radio"
# (a) MP3: five files in a row that cannot be decoded (simaudiofail): the message comes before the stop; dismissed by a tap, the cover page goes too
r40 t13_r40_mp3 "screen on; vol 0; @0.5; mp3ui; @0.3; simtimers; simaudiofail 5; mp3 1; @0.1; uistack; @1; uistack; simtap 180 180; @0.5; uistack; simtimers" \
    "menu mp3 now" "menu mp3 now message" "menu mp3"
grep -q '^APP mp3 stop reason=failures' <<<"$out" || fail t13_r40_mp3_failures
# (b) hires T8e: a rescan while Settings covers the cover page keeps playing, so the cover page stays (and the spectrum page over it)
nowrun t13_r40_covered "screen on; vol 0; @0.5; track 3; @2; setui; @0.3; uistack; settings key rescan; @0.3; settings key rescan; @1; uistack; back; back; @0.5; uistack; nowmenu -3" && {
    stack_is t13_r40_covered_before 1 "menu now settings"; stack_is t13_r40_covered_scanned 2 "menu now settings setgroup"; stack_is t13_r40_covered_after 3 "menu now"
    grep -q '^APP scan n=' <<<"$out" || fail t13_r40_covered_scan
    [ "$(pa)" = "playing " ] || { EVID="audio $(pa)"; fail t13_r40_covered_playing; }; }
nowrun t13_r40_covered_viz "screen on; vol 0; @0.5; track 3; @2; viz bars; @0.5; setui; @0.3; settings key rescan; @0.3; settings key rescan; @1; uistack; back; back; @0.5; uistack" && {
    stack_is t13_r40_covered_viz_before 1 "menu now viz settings setgroup"; stack_is t13_r40_covered_viz_after 2 "menu now viz"; }
# ... and playback that stops while Settings covers the page (the playing file is gone at the rescan, hires T8e): Back -> the cover page leaves, its timers go too.
# The file is removed while the simulator runs (stdin, virtual time stands still meanwhile).
fresh
out=$({ printf '%s\n' "screen on" "vol 0" "@0.5" "simtimers" "track 3" "@2" "setui" "@0.3" "uistack"; sleep 0.5; rm -f "$T/fix/export/夜航星圖/04_long_summer.mp3"
        printf '%s\n' "settings key rescan" "@0.3" "settings key rescan" "@1" "uistack" "back" "@0.5" "back" "@0.5" "back" "@0.5" "uistack" "simtimers" quit; } | $SIM --headless --root "$T/fix" 2>&1)
{ grep -q '^APP mp3 stop reason=gone path=/sdcard/export/夜航星圖/04_long_summer.mp3$' <<<"$out" && ! grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out"; } || { EVID="$(grep '^APP mp3\|Sanitizer' <<<"$out" | tr '\n' '|')"; fail t13_r40_covered_gone; }
stack_is t13_r40_covered_gone_before 1 "menu now settings"; stack_is t13_r40_covered_gone_after 3 "menu"
[ "$(tm 1)" = "$(tm 2)" ] || { EVID="$(tm 1) / $(tm 2)"; fail t13_r40_covered_gone_timers; }
# negatives: a cover page opened with nothing playing, covered (the spectrum page, Settings) and uncovered, stays; a paused one too
nowrun t13_r40_idle_stays "screen on; vol 0; viz bars; @0.5; back; @0.5; uistack; setui; @0.3; back; @0.5; uistack" && {
    stack_is t13_r40_idle_stays_viz 1 "menu now"; stack_is t13_r40_idle_stays_settings 2 "menu now"; }
nowrun t13_r40_paused_stays "screen on; vol 0; track 3; @2; nowmenu -2; nowmenu -1; @0.3; setui; @0.3; back; @0.5; uistack; nowmenu -3" && {
    stack_is t13_r40_paused_stays 1 "menu now"; grep -qx 'APP now paused=1 audio=paused' <<<"$out" || fail t13_r40_paused_still_paused; }
# 4(i): MP3 page, between the two rescan taps a knob turn or a tap on another row (a folder: its song list, then Back) cancels the first tap
fresh; out=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; vol 0; track 3; @2; uihome; mp3ui; @0.3; simknob 20; @0.1; uiboxes" 2>&1)   # the fixture, as nowrun uses
set -- $(block_rows "$out" 1 | tail -1 | cut -d'|' -f1) 0 0 0 0; RX=$((($1 + $3) / 2)); RY=$((($2 + $4) / 2))
set -- $(block_rows "$out" 1 | tail -2 | head -1 | cut -d'|' -f1) 0 0 0 0; OX=$((($1 + $3) / 2)); OY=$((($2 + $4) / 2))
nowrun t13_rescan_mp3_knob_cancels "screen on; @0.5; vol 0; track 3; @2; uihome; mp3ui; @0.3; simknob 20; @0.1; simtap $RX $RY; @0.2; simknob -1; @0.1; simknob 1; @0.1; simtap $RX $RY; @0.5; uistack; nowmenu -3; uiboxes" && {
    ! grep -q '^APP scan n=' <<<"$out" && [ "$(pa)" = "playing " ] && block_rows "$out" 1 | tail -1 | grep -q '|Tap again: rescan$' || { EVID="audio $(pa) $(grep -c '^APP scan n=' <<<"$out") scans"; fail t13_rescan_mp3_knob_cancels; }; }
nowrun t13_rescan_mp3_row_cancels "screen on; @0.5; vol 0; track 3; @2; uihome; mp3ui; @0.3; simknob 20; @0.1; simtap $RX $RY; @0.2; simtap $OX $OY; @0.3; uitop; flick up; @0.3; simtap $RX $RY; @0.5; uistack; nowmenu -3" && {   # no knob after Back: the knob alone would cancel
    grep -qx 'APP uitop mp3' <<<"$out" && ! grep -q '^APP scan n=' <<<"$out" && [ "$(pa)" = "playing " ] || { EVID="$(grep '^APP uitop' <<<"$out") audio $(pa)"; fail t13_rescan_mp3_row_cancels; }
    stack_is t13_rescan_mp3_row_cancels_stack 1 "menu mp3"; }
# 4(ii): scene editor: a picked layer keeps the knob for its volume (the column does not move); with no layer picked the knob scrolls the column
nowrun t13_scene_knob_layer "screen on; @0.5; vol 0; simnoise 30 7; noise 0; noiseui; sceneui 0; @0.3; uiboxes" && {
    set -- $(box_of '● Rain 80%' "$out" | awk '{print int(($1 + $3) / 2), int(($2 + $4) / 2)}')
    nowrun t13_scene_knob_layer "screen on; @0.5; vol 0; simnoise 30 7; noise 0; noiseui; sceneui 0; @0.3; simtap $1 $2; @0.2; popring 1; uiboxes; simknob 2; @0.1; uiboxes; @4.5; simknob 2; @0.1; uiboxes; simvol" && {
        [ "$(lay 1)|$(lay 2)" = "$(cut -d' ' -f1 <<<"$(lay 1)") 85%|$(cut -d' ' -f1 <<<"$(lay 1)") 95%" ] || { EVID="layer row (top, text) after the volume tap: $(lay 1) / after knob 2: $(lay 2)"; fail t13_scene_knob_layer_volume; }
        set -- $(lay 2) $(lay 3); [ "$3" != "$1" ] && [ "$4" = 95% ] && [ "$(vols)" = "0 " ] || { EVID="unborrowed: $(lay 2) -> $(lay 3), vol $(vols)"; fail t13_scene_knob_scrolls; }; }; }
# R41: every leak action that turns the knob really steps something: its own cycle (tools/simtools/leak_cycles.py, through the same driver as make sim-leak), with a
# probe before and after each "knob" command (uiboxes on lists, the face / the album setting on the rings); a knob that changes nothing fails
many_packs 7 7; rm -rf "$T/choosers"; cp -r "$T/fix" "$T/choosers"
for a in "choosers|uiboxes|$T/choosers" "clockfaceaxis|clockface ?|" "albumaxis|cfg now_view|" "pickers|uiboxes|$T/pick" "setaxis|cfg brightness|"; do
    IFS='|' read -r act probe root <<<"$a"
    r=$(leak_knob_proof "$act" "$probe" "$root" 2>&1 | tail -1)
    [[ "$r" =~ ^$act\ knobs=([1-9][0-9]*)\ unchanged=0$ ]] || { EVID="$r"; fail "t13_leak_knob_$act"; }
    echo "$r" >> "$T/leak_knob_proof.txt"
done
# --- end Task 13 ---

# --- final-review fix round (the internal notes) ---
# M1: ui_pop_to (console "uipopto <name>": the highest layer of that name): pages above it go, a name not on the stack pops nothing (rc=0);
# the page that ends on top is resumed (the cover page with playback gone leaves, R40)
nowrun ff_popto "screen on; setui; @0.3; uipush help; uipush favs; uistack; uipopto settings; uistack; uipopto bogus; uistack" && {
    [ "$(uis)" = "menu settings explain favs|menu settings|menu settings|" ] && grep -qx 'APP uipopto settings rc=1' <<<"$out" && grep -qx 'APP uipopto bogus rc=0' <<<"$out" ||
        { EVID="$(uis) $(grep '^APP uipopto\|APP cmd unknown' <<<"$out" | tr '\n' ' ')"; fail ff_popto; }; }
nowrun ff_popto_resume "screen on; vol 0; track 3; @2; setui; @0.3; uipush help; stop; @0.3; uistack; uipopto now; @0.5; uistack" && {
    [ "$(uis)" = "menu now settings explain|menu|" ] || { EVID="$(uis)"; fail ff_popto_resume; }; }
# M2 (F1): the playing ring's list cell with that list already under the cover page: the stack holds it once, then Back + knob act on the visible list.
# The brief's repro on the 20 stations: after the list cell the station list is on top (the cover page went with the pop); a station, Back, the knob scrolls.
NOWFRESH=pfresh nowrun ff_radio_twice "screen on; @0.5; vol 0; uipush radio; @0.3; radio 0; @0.3; nowmenu -2; nowmenu 0; @0.3; uistack; simtap 180 134; @0.5; uistack; back; @0.3; uistack; uiboxes; simknob 1; @0.1; uiboxes" && {
    [ "$(uis)" = "menu radio|menu radio now|menu radio|" ] || { EVID="$(uis)"; fail ff_radio_twice_stack; }
    [ "$(block_rows "$out" 1 | head -1 | cut -d' ' -f2)" != "$(block_rows "$out" 2 | head -1 | cut -d' ' -f2)" ] || { EVID="first row top $(block_rows "$out" 1 | head -1)"; fail ff_radio_twice_knob; }; }
# the song list is never under the cover page (a pick pops it before now_open): the ring's list cell pushes one; a song pops it again (a guard, not a repro)
nowrun ff_tracks_once "screen on; @0.5; vol 0; uipush tracks0; @0.3; simtap 180 82; @0.5; uistack; nowmenu -2; nowmenu 2; @0.3; uistack; simtap 180 134; @0.5; uistack; back; @0.3; uistack; simknob 1; @0.1; uitop" && {
    [ "$(uis)" = "menu now|menu now tracks|menu now|menu|" ] || { EVID="$(uis)"; fail ff_tracks_once; }; }
# the noise colours ring under the cover page: its list cell pops back to it
nowrun ff_colors_twice "screen on; @0.5; vol 0; uipush colors; @0.3; color 1; @0.3; uistack; nowmenu -2; nowmenu -5; nowmenu 0; @0.3; uistack" && {
    [ "$(uis)" = "menu colors now|menu colors|" ] || { EVID="$(uis) $(grep '^APP now cells' <<<"$out")"; fail ff_colors_twice; }; }
# M3 (F2, F3): the scene page's save, delete and add-a-sound, entered from the scene list (console sceneui over noiseui) and through the playing ring
# (simnoise own: a started scene owns the player, its ring's list cell is the scene page). Scene 0 (夜雨) is custom; 30 scenes so that the list scrolls.
SC_ROW="180 186"
SC_LIST="screen on; @0.5; vol 0; simnoise 30 3 custom; simnoise own; noiseui; @0.3; sceneui 0; @0.3; uistack"
SC_RING="screen on; @0.5; vol 0; simnoise 30 3 custom; simnoise own; noiseui; @0.3; simtap $SC_ROW; @0.5; nowmenu -2; nowmenu 1; @0.3; uistack"
# the scene page's rows are a list (UI unify T6): their places are read from uiboxes (the knob scrolls the ones below the fold into view first)
nowrun ff_scene_places "$SC_LIST; simknob 9; @0.2; uiboxes" && { SC_SAVE=$(scxy '存成新情境\|Save as new'); SC_DEL=$(scxy "$DELRE"); }
nowrun ff_scene_places_add "$SC_LIST; simknob 2; @0.2; uiboxes" && SC_ADD=$(scxy '＋ 加聲音\|＋ Add a sound')
nowrun ff_scene_places_cat "$SC_LIST; simknob 2; simtap $SC_ADD; @0.3; uiboxes" && SC_PLACES=$(scxy 'Places  (3)')
nowrun ff_scene_places_snd "$SC_LIST; simknob 2; simtap $SC_ADD; @0.3; simtap $SC_PLACES; @0.3; uiboxes" && SC_BIRDS=$(scxy 'Birds')
nowrun ff_scene_places_play "$SC_LIST; simknob 2; simtap $SC_ADD; @0.3; simtap $SC_PLACES; @0.3; simtap $SC_BIRDS; @0.5; simknob 9; @0.2; uiboxes" && SC_PLAY=$(scxy '▶ 播放\|▶ Play')
for how in list ring; do
    if [ $how = list ]; then pre=$SC_LIST; st0="menu noise scene"; else pre=$SC_RING; st0="menu noise now scene"; fi
    # save: the scene list once, on top, rebuilt (自訂 1 / Custom 1 is a row); the knob scrolls it; a tap on a row plays (R17: the cover page); Back; the knob
    nowrun "ff_save_$how" "$pre; simknob 9; @0.2; simtap $SC_SAVE; @0.5; uistack; uiboxes; simknob 2; @0.1; uiboxes; simtap $SC_ROW; @0.5; uistack; back; @0.3; uistack; simknob 1; @0.1" && {
        [ "$(uis)" = "$st0|menu noise|menu noise now|menu noise|" ] || { EVID="$(uis)"; fail "ff_save_${how}_stack"; }
        grep -q '^SIM noise saved 1$' <<<"$out" && grep -q ',\(自訂 2\|Custom 2\),' <<<",$(scrows 1)" || { EVID="rows $(scrows 1 | cut -c1-120)"; fail "ff_save_${how}_rebuilt"; }
        [ "$(block_rows "$out" 1 | sed -n 2p | cut -d' ' -f2)" != "$(block_rows "$out" 2 | sed -n 2p | cut -d' ' -f2)" ] || fail "ff_save_${how}_knob"; }
    # delete (two taps): the scene list once, on top, without the deleted scene (夜雨 gone); a row plays; Back; the knob
    nowrun "ff_del_$how" "$pre; simknob 9; @0.2; simtap $SC_DEL; @0.5; simtap $SC_DEL; @0.5; uistack; uiboxes; simtap $SC_ROW; @0.5; uistack; back; @0.3; uistack; simknob 1; @0.1" && {
        [ "$(uis)" = "$st0|menu noise|menu noise now|menu noise|" ] || { EVID="$(uis)"; fail "ff_del_${how}_stack"; }
        grep -q '^SIM noise delete 0$' <<<"$out" && ! grep -q ',夜雨,' <<<",$(scrows 1)" || { EVID="rows $(scrows 1 | cut -c1-80)"; fail "ff_del_${how}_rebuilt"; }; }
    # add a sound (Places -> Birds): one scene page with the new layer; Back + knob + the play button act on it
    nowrun "ff_add_$how" "$pre; simknob 2; simtap $SC_ADD; @0.3; simtap $SC_PLACES; @0.3; simtap $SC_BIRDS; @0.5; uistack; uiboxes; simknob 9; @0.2; simtap $SC_PLAY; @0.3; simnoise ?; back; @0.3; uistack; simknob 1; @0.1" && {
        [ "$(cut -d'|' -f1-2 <<<"$(uis)")" = "$st0|$st0" ] || { EVID="$(uis)"; fail "ff_add_${how}_stack"; }
        grep -q '^APP uibox button .*|[●・] Birds' <<<"$out" || { EVID="$(grep '^APP uibox label' <<<"$out" | tr '\n' ' ' | cut -c1-200)"; fail "ff_add_${how}_layer"; }; }
done
# M4 (F4): the owner changes under an open playing ring (the phone starts streaming: simbt stream; or the console starts the radio): the ring closes at once
# (its knob is the volume again), and a cell tap that would follow finds no ring. Cells: the MP3 ring's prev, list, order, next, details and the centre (pause).
nowrun ff_cells "screen on; vol 0; mp3 all; @1; nowmenu -2; nowmenu -5" && CELLS=$(grep '^APP now cells' <<<"$out" | head -1 | sed 's/.* items=//' | tr '|,' '\n\n')
ci() { grep -n "^$1\$" <<<"$CELLS" | cut -d: -f1 | awk '{print $1 - 1}'; }
for cell in prev list order next info pause; do
    [ $cell = pause ] && i=-1 || i=$(ci $cell)
    nowrun "ff_owner_$cell" "screen on; @0.5; vol 0; mp3 all; @1; nowmenu -2; nowmenu -5; simbt stream; @0.1; nowmenu -5; nowmenu $i; @0.3; nowmenu -5; uistack; simvol; simknob 1; @0.1; simvol" && {
        [ "$(nomenu)" = 0 ] && [ "$(grep '^APP now menu=' <<<"$out" | sed -n 2p | cut -c1-15)" = "APP now menu=0 " ] || { EVID="ring after the owner change: $(grep '^APP now menu=' <<<"$out" | tr '\n' ' ')"; fail "ff_owner_${cell}_closed"; }
        [ "$(vols)" = "0 2 " ] && [ "$(uis)" = "menu now|" ] || { EVID="vol $(vols) stack $(uis)"; fail "ff_owner_${cell}_knob"; }; }
done
nowrun ff_owner_radio "screen on; @0.5; vol 0; mp3 all; @1; nowmenu -2; radio 0; nowmenu 1; @0.3; nowmenu -5; simvol; simknob 1; @0.1; simvol" && {
    [ "$(nomenu)" = 0 ] && [ "$(vols)" = "0 2 " ] || { EVID="ring $(nomenu) vol $(vols)"; fail ff_owner_radio; }; }
# M5: a chip file that cannot be opened (a zip with an .nsf the fake decoder refuses): the "cannot play" message stays over the song list, then the song list
mkdir -p "$T/chipfail"; rm -rf "$T/chipfail/root"; cp -r sim/fixture "$T/chipfail/root"; mkdir -p "$T/chipfail/root/chiptune"
python3 -c "import zipfile, sys; z = zipfile.ZipFile(sys.argv[1], 'w'); z.writestr('Broken Game.nsf', b'NESM\x1a' + bytes(200)); z.close()" "$T/chipfail/root/chiptune/Test.zip"
out=$($SIM --headless --root "$T/chipfail/root" --cmd "screen on; @0.5; chipui; @0.3; simtap 180 82; @0.3; simtap 180 82; @0.3; simtap 180 82; @0.3; uistack; @3; uistack" 2>&1)
grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out" && { EVID="$(grep -m2 'ERROR\|SUMMARY' <<<"$out" | tr '\n' ' ')"; fail ff_chip_fail_clean; }
[ "$(uis)" = "menu chip chip_games chip_tracks message|menu chip chip_games chip_tracks|" ] && grep -q '^APP chip stop reason=failures' <<<"$out" || { EVID="$(uis)"; fail ff_chip_fail; }
# M6: the alarm with the stack full (8 pages): it goes home and pushes its page; a tap stops the ring
nowrun ff_alarm_full "screen on; @0.5; alarm 21 31; simowner; simowner; simowner; simowner; simowner; simowner; simowner; uistack; @61; uistack; simtap 180 180; @0.3; uistack" && {
    [ "$(uis)" = "menu simowner simowner simowner simowner simowner simowner simowner|menu ring|menu|" ] && grep -q '^APP clock ring stop' <<<"$out" || { EVID="$(uis)"; fail ff_alarm_full; }; }
# S1: "next" in the playing ring fails at once (every other song is gone: the start cannot open them): the message stays over the cover page until it
# leaves by itself (2.5 s; the ring does not pop it), then the cover page goes too (R40)
fresh; echo mp3_order=1 >> "$T/fix/_player/config/app.txt"
find "$T/fix/export" -name '*.mp3' ! -name 01_harbor_light.mp3 -delete
out=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; vol 0; mp3 all; @1; nowmenu -2; nowmenu $(ci next); @0.1; uistack; @1.5; uistack; nowmenu -5; @1.5; uistack" 2>&1)
grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out" && fail ff_ring_next_fails_clean
[ "$(uis)" = "menu now message|menu now message|menu|" ] && grep -q '^APP mp3 stop reason=failures' <<<"$out" && [ "$(nomenu)" = 0 ] || { EVID="$(uis) ring $(nomenu)"; fail ff_ring_next_fails; }
# S2: the scan ends while the alarm page covers the scan page (simtaskhold: the scan task waits): the alarm page stays; stopped by a tap, the scan page leaves
nowrun ff_scan_covered "screen on; @0.5; alarm 21 31; setui; @0.3; @58; simtaskhold 1; settings key rescan; @0.3; settings key rescan; @0.3; uistack; @2; uistack; simtaskhold 0; @0.5; uistack; simtap 180 180; @0.5; uistack" && {
    [ "$(uis)" = "menu settings setgroup scan|menu settings setgroup scan ring|menu settings setgroup scan ring|menu settings setgroup|" ] && grep -q '^APP scan n=' <<<"$out" || { EVID="$(uis)"; fail ff_scan_covered; }; }
# S3: the bar-width axis with voices stored and nothing voiced playing (fallback bars are drawn): its cell taken (a first tap only picks it), the knob changes the width
NOWCFG=viz_style=voices nowrun ff_voices_barw "screen on; @0.5; vol 0; color 0; @3; viz menu; viz pick 4; simknob 1; @0.1; viz stat" && {
    [ "$(grep '^APP viz axes barw=' <<<"$out" | sed 's/.*barw=\([0-9]*\).*/\1/' | tr '\n' ' ')" = "7 7 9 " ] || { EVID="$(grep '^APP viz axes' <<<"$out" | tr '\n' ' ')"; fail ff_voices_barw; }; }
# S4: a page over the cover page (a message) closes the playing ring at once (not when its 4 s run out)
nowrun ff_ring_suspend "screen on; @0.5; vol 0; track 3; @2; nowmenu -2; nowmenu -5; uipush message; nowmenu -5" && {
    [ "$(grep '^APP now menu=' <<<"$out" | cut -c1-15 | tr '\n' ' ')" = "APP now menu=1  APP now menu=0  " ] || { EVID="$(grep '^APP now menu=' <<<"$out" | tr '\n' ' ')"; fail ff_ring_suspend; }; }
# --- end final-review fix round ---
# Wi-Fi holders: the console holds as manual (hold=4), `wifi 0` drops every holder at once
nowrun wifi_hold "wifi st; wifi 1; wifi st; wifi 0; wifi st" && {
    [ "$(grep '^APP wifi on=' <<<"$out" | sed 's/ up=.*hold=/ hold=/' | tr '\n' '|')" = "APP wifi on=0 hold=0|APP wifi on=1 hold=4|APP wifi on=0 hold=0|" ] || { EVID="$(grep '^APP wifi' <<<"$out" | tr '\n' '|')"; fail wifi_hold; }; }

# alarm on, Wi-Fi never connects: the boot sync repeats (5 min after a hold ended) so the alarm can get its time; with the alarm off it does not
NOWCFG="alarm_on=1" nowrun alarm_sync_retry "simtime 2020-01-01 00:00:00; @61; wifi st; @125; wifi st; @200; wifi st" && {
    [ "$(grep '^APP wifi on=' <<<"$out" | sed 's/ up=.*hold=/ hold=/' | tr '\n' '|')" = "APP wifi on=1 hold=0|APP wifi on=0 hold=0|APP wifi on=1 hold=2|" ] || { EVID="$(grep '^APP wifi' <<<"$out" | tr '\n' '|')"; fail alarm_sync_retry; }; }
NOWCFG="alarm_on=0" nowrun alarm_off_no_retry "simtime 2020-01-01 00:00:00; @61; wifi st; @125; wifi st; @200; wifi st" && {
    [ "$(grep -c 'hold=2' <<<"$out")" = 0 ] || { EVID="$(grep '^APP wifi' <<<"$out" | tr '\n' '|')"; fail alarm_off_no_retry; }; }
# alarm st: the console shows a pending snooze (fix9); sim clock starts at 21:30:xx Taipei, so alarm 21 31 rings within the minute
nowrun alarm_st_snooze "screen on; vol 0; alarm 21 31; alarm st; @61; alarm st; simknob 1; @1; alarm st; @545; alarm st; simtap 180 180; @1; alarm st" && {
    l=$(grep '^APP alarm on=' <<<"$out")
    [ "$(sed -n 1p <<<"$l")" = "APP alarm on=1 at=21:31 days=127 ringing=0 snooze=-" ] && [ "$(sed -n 2p <<<"$l")" = "APP alarm on=1 at=21:31 days=127 ringing=1 snooze=-" ] || { EVID="$l"; fail alarm_st_before_and_ringing; }
    grep -q '^APP alarm on=1 at=21:31 days=127 ringing=0 snooze=21:4[01]$' <<<"$(sed -n 3p <<<"$l")" || { EVID="$l"; fail alarm_st_snooze_pending; }
    [ "$(sed -n 4p <<<"$l")" = "APP alarm on=1 at=21:31 days=127 ringing=1 snooze=-" ] || { EVID="$l"; fail alarm_st_rings_again; }
    [ "$(sed -n 5p <<<"$l")" = "APP alarm on=1 at=21:31 days=127 ringing=0 snooze=-" ] || { EVID="$l"; fail alarm_st_after_stop; }; }
nowrun alarm_st_off_cancels "screen on; vol 0; alarm 21 31; @61; simknob 1; @1; alarm st; alarm off; alarm st" && {
    l=$(grep '^APP alarm on=' <<<"$out")
    grep -q 'snooze=21:4[01]$' <<<"$(sed -n 1p <<<"$l")" && [ "$(sed -n 2p <<<"$l")" = "APP alarm on=0 at=21:31 days=127 ringing=0 snooze=-" ] || { EVID="$l"; fail alarm_st_off_cancels_snooze; }; }

