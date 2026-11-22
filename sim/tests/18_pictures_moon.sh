# SPDX-License-Identifier: LGPL-2.1-or-later
# --- Task 4: bigger pictures, no VU, first frame (waterfall colour rule: owner's correction 2026-11-13, quiet = the page background) ---
# first frame (step 2): the picture buffer is shown by LVGL as soon as the page is up, before the first tick renders anything. What it holds then must be
# the page background (the current theme's), never the default theme's near-black that pc_vd_init paints. Nothing is played and no time passes between the
# command that opens the page / changes the kind and the shot, so no frame has been drawn: every pixel of the picture rectangle (rows under 60 are the title
# line, which overlaps the big pictures for its first 10 s) is the page background, the waterfall included (quiet is the background again). "theme next" steps prove that it is the current theme and not one fixed colour.
for th in 0 1 2; do
    first_frame "bars_th$th" $th "" "viz bars" - 30 80 330 280
    first_frame "ring_th$th" $th "" "viz ring" - 10 10 350 350
    first_frame "scope_th$th" $th "" "viz scope" - 20 100 340 260
    first_frame "fall_th$th" $th "" "viz fall" - 52 80 308 280
    first_frame "phase_th$th" $th "" "viz phase" - 30 30 330 330
done
# the same when the kind or the variant changes on an open page (the circular scope is another picture size than the line scope)
first_frame sw_ring 1 "viz bars; @11; " "viz ring" - 10 10 350 350
first_frame sw_fall 1 "viz bars; @11; " "viz fall" - 52 80 308 280
first_frame sw_phase 2 "viz scope; @11; " "viz phase" - 30 30 330 330
first_frame sw_circle 1 "viz scope; @11; " "viz var 2" - 10 10 350 350
first_frame sw_bars_var 1 "viz bars; @11; " "viz var 2" - 30 80 330 280
# the kind axis with nothing that has voices: exactly six kinds (bars, ring, scope, fall, phase, lyrics), six detents come back to the start, one back from bars is lyrics; VU is gone from every answer (voices: Task 10)
nowrun t4_kind_walk "screen on; vol 0; color 0; @3; viz bars; viz menu; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob -1; viz stat" && {
    [ "$(ax | tr ' ' '\n' | cut -d: -f1 | tr '\n' ' ' | cut -d' ' -f3-)" = "ring scope fall phase lyrics phase " ] || { EVID="$(ax)"; fail t4_kind_walk; }; }
nowrun t4_no_vu_console "screen on; vol 0; @1; viz bars; viz vu; viz stat; viz next; viz next; viz next; viz next; viz next; viz next; viz next" && {
    ! grep -q 'kind=vu' <<<"$out" && [ "$(grep -c '^APP cmd viz: bars|ring|scope|fall|phase|voices|rows|roll|lyrics|next|var <n|next>|stat$' <<<"$out")" = 1 ] || { EVID="$(grep -m3 'kind=vu\|APP cmd viz' <<<"$out")"; fail t4_no_vu_console; }
    # seven "viz next" from bars: the console walks the kinds in the enum order (voices, rows and roll too, whatever plays; the page then shows bars)
    [ "$(grep '^APP viz open' <<<"$out" | sed -E 's/^APP viz open=[01] kind=([a-z]+) .*/\1/' | tail -7 | tr '\n' ' ')" = "ring scope fall phase voices rows roll " ] || { EVID="$(ax)"; fail t4_next_kinds; }; }
# a stored VU kind opens as bars with variant 0 (its own remembered variant is ignored; bars' own variant 3 is not used either, the old setting is replaced); every other stored kind keeps its meaning
for st in "viz_style=vu" "viz_style=vu"$'\n'"viz_var_vu=1" "viz_style=vu"$'\n'"viz_var_vu=1"$'\n'"viz_var_bars=3"; do
    fresh; printf 'lang=0\n%s\n' "$st" > "$T/fix/_player/config/app.txt"
    out=$($SIM --headless --root "$T/fix" --cmd "cfg viz_style; cfg viz_var_bars; viz stat; viz bars; @1; viz stat; @4; shot $T/vu_stored.png; cfg viz_style; cfg viz_var_bars" 2>&1)
    [ "$(ax | tr ' ' '\n' | cut -d: -f1,2 | sort -u | tr '\n' ' ')" = "bars:0 " ] && ! grep -q 'kind=vu\|Sanitizer\|runtime error' <<<"$out" || { EVID="$st | $(ax)"; fail t4_stored_vu; }
    # the stored values themselves: before the first read the file holds vu (and bars' variant as it was), after it the kind is bars and bars' variant 0 (a stored vu is rewritten once, not only read as bars)
    [ "$(grep '^APP cfg viz_style=' <<<"$out" | tr '\n' ' ')" = "APP cfg viz_style=vu APP cfg viz_style=bars " ] && [ "$(grep '^APP cfg viz_var_bars=' <<<"$out" | tail -1)" = "APP cfg viz_var_bars=0" ] \
        && [ "$(grep '^APP cfg viz_var_bars=' <<<"$out" | head -1)" = "APP cfg viz_var_bars=$([[ "$st" == *viz_var_bars=3 ]] && echo 3)" ] || { EVID="$st | $(grep '^APP cfg viz_' <<<"$out" | tr '\n' ' ')"; fail t4_stored_vu_rewritten; }
done
for k in bars ring scope fall phase; do
    fresh; printf 'lang=0\nviz_style=%s\nviz_var_%s=1\n' "$k" "$k" > "$T/fix/_player/config/app.txt"
    out=$($SIM --headless --root "$T/fix" --cmd "viz stat" 2>&1)
    [ "$(ax | cut -d: -f1,2)" = "$k:1" ] || { EVID="$k: $(ax)"; fail t4_stored_$k; }
done
# the ring map is a PSRAM allocation: when it fails the ring kind is shown as bars (the stored setting stays ring), and works again when memory is there
nowrun t4_ringmap_alloc_fail "screen on; vol 0; @1; viz bars; simallocfail 240000; viz ring; @0.3; viz stat; simallocfail 0; viz bars; viz ring; @0.3; viz stat" && {
    sh=$(grep '^APP viz shown=' <<<"$out" | tr '\n' ' ')   # every viz command prints one: bars, ring (failed: bars), stat, bars, ring (built: ring), stat
    [ "$(grep '^APP viz shown=' <<<"$out" | sed -n '2p;3p;5p;6p' | tr '\n' ' ')" = "APP viz shown=bars APP viz shown=bars APP viz shown=ring APP viz shown=ring " ] && grep -q '^APP viz open=1 kind=ring ' <<<"$out" || { EVID="$sh"; fail t4_ringmap_alloc_fail; }; }
# the waterfall colour rule (2026-11-13): silence is the page background (every pixel of the picture area, after 2 s of scrolling rows too); a loud track draws
# bands that are not the background and also leaves background cells (the blend's continuity is a host unit test: test_fall_rule)
nowrun t4_fall_silent_bg "screen on; vol 0; @1; viz fall; @2; shot $T/fall_silent.png" && {
    [ "$(pic_off "$T/fall_silent.png" 52 80 308 280)" = 0 ] || { EVID="silent waterfall is not the page background"; fail t4_fall_silent_bg; }; }
nowrun t4_fall_loud_bands "screen on; vol 0; track 3; @3; viz fall; @3; shot $T/fall_loud.png" && {
    python3 -c "
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; from png_diff import read_png
_, _, r = read_png(sys.argv[1]); bg = r[180][6:9]
cells = [r[y][3 * x:3 * x + 3] for y in range(82, 278) for x in range(54, 306)]
nb = sum(1 for c in cells if c != bg)
sys.exit(0 if nb > 2000 and any(c == bg for c in cells) else 1)" "$T/fall_loud.png" || { EVID="the playing waterfall shows no bands, or no background"; fail t4_fall_loud_bands; }; }
# the bigger pictures reach into the 360 px circle: the ring picture shows pixels far beyond its old radius 118, and never beyond 170; the circular scope's line swings out to radius ~155 (it was 110)
# (the phase picture's size is checked by the unit tests: the simulator's fake stereo signal stays near the centre)
nowrun t4_big_pictures "screen on; vol 0; track 3; @3; viz ring; @2; shot $T/big_ring.png; viz scope; viz var 2; @2; shot $T/big_circle.png" && {
    python3 -c "
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; from png_diff import read_png
def far(f, lo, hi):   # pixels differing from the page background at a distance of lo..hi px from the centre
    _, _, r = read_png(f); bg = r[180][6:9]
    return sum(1 for y in range(360) for x in range(360) if lo <= ((x - 179.5) ** 2 + (y - 179.5) ** 2) ** 0.5 <= hi and r[y][3 * x:3 * x + 3] != bg and y > 60)
assert far(sys.argv[1], 125, 170) > 100, 'the ring has nothing beyond radius 125 (the old outer radius was 118)'
assert far(sys.argv[1], 171, 260) == 0, 'the ring draws beyond radius 170'
assert far(sys.argv[2], 120, 170) > 20, 'the circular scope has nothing beyond radius 120'
assert far(sys.argv[2], 171, 260) == 0, 'the circular scope draws beyond radius 170'" "$T/big_ring.png" "$T/big_circle.png" || { EVID="the round pictures do not use the bigger size"; fail t4_big_pictures; }; }
# --- end Task 4 ---

# --- Task 5: moon face ---
# seven clock faces, the moon last: "clockface next" from rings goes to moon, then round to info
nowrun clockface_seven "clockface rings; clockface next; clockface next; clockface moon; clockface bogus" && {
    [ "$(grep '^APP clockface set=' <<<"$out" | sed 's/^APP clockface set=//' | tr '\n' ' ')" = "rings moon hud moon info " ] || { EVID="$(grep '^APP clockface set=' <<<"$out")"; fail clockface_seven; }; }
# the face shows the phase at a fixed instant: mean new moon (epoch 2026-02-17 03:22:09) and mean full moon (+ half a synodic month = 2026-03-03 21:44:10)
moonrun moon_face_new "2026-02-17 03:22:09" && {
    l=$(grep -m1 '^APP clockface moon phase=' <<<"$out"); [ "$l" = "APP clockface moon phase=New age=0.0 lit=0 next=full in=15" ] || { EVID="$l"; fail moon_face_new; }
    [ "$(grep -c '^APP clockface moon phase=' <<<"$out")" = 2 ] && grep -q '^APP uitop clock$' <<<"$out" || { EVID="$(grep -c '^APP clockface moon phase=' <<<"$out") lines"; fail moon_face_new_lines; }; }
moonrun moon_face_full "2026-03-03 21:44:10" && {
    l=$(grep -m1 '^APP clockface moon phase=' <<<"$out"); [ "$l" = "APP clockface moon phase=Full age=14.8 lit=100 next=new in=15" ] || { EVID="$l"; fail moon_face_full; }; }
# the picture of the face differs between the two dates (the disc is drawn at creation)
fresh; $SIM --headless --root "$T/fix" --cmd "simtime 2026-02-17 03:22:09; @1.5; clock; clockface moon; @1; shot $T/mf_new.png" >/dev/null 2>&1
fresh; $SIM --headless --root "$T/fix" --cmd "simtime 2026-03-03 21:44:10; @1.5; clock; clockface moon; @1; shot $T/mf_full.png" >/dev/null 2>&1
{ [ -s "$T/mf_new.png" ] && ! cmp -s "$T/mf_new.png" "$T/mf_full.png"; } || fail moon_face_pictures_differ
# the facts line is printed on request only ("clockface moon?"), not every minute; the disc is redrawn when the hour changes, not before (the line counts only the draws)
nowrun moon_face_hourly "simtime 2026-02-17 03:22:09; @1.5; clock; clockface moon; @1; simtime 2026-02-17 03:50:00; @1.5; simtime 2026-02-17 04:05:00; @1.5; uitop" && {
    [ "$(grep -c '^APP clockface moon draw' <<<"$out")" = 2 ] || { EVID="$(grep '^APP clockface moon' <<<"$out")"; fail moon_face_hourly; }
    [ "$(grep -c '^APP clockface moon phase=' <<<"$out")" = 0 ] || { EVID="$(grep '^APP clockface moon phase=' <<<"$out")"; fail moon_face_no_periodic_print; }; }
# the tools ring: 11 items (遊戲 from games G0, 音效庫 from sfx task 2, 計數器 from ideas G2), two pages (turning the page goes to page 1, the last cells are 捏泡泡紙, 遊戲 and 音效庫); 月相 is not among them; "tools moon" opens the clock on the moon face, cfg untouched
nowrun tools_ring_eight "tools; tools ring" && {
    [ "$(grep '^APP tools ring ' <<<"$out")" = "APP tools ring items=14 page=1 pages=2" ] || { EVID="$(grep '^APP tools ring' <<<"$out")"; fail tools_ring_eight; }; }
# the flashlight (ui_torch.c): the live backlight is 100 while the page is on top, cfg brightness is never written; a page over it (the quick settings) and every way out show / restore the saved value.
# (the first cfg line is seti's own, before the torch: 60 = the live value then) bl = the last "SIM backlight" value before each "APP cfg brightness" line (SIM_TRACE=1), cfgs = the cfg values themselves
SIM_TRACE=1 nowrun torch_ways "screen on; @0.5; seti brightness 35; tools torch; @0.3; uitop; cfg brightness; simflick down; @0.3; cfg brightness; simflick up; @0.3; cfg brightness; simflick up; @0.3; uitop; cfg brightness; tools torch; @0.3; screen off; @0.2; cfg brightness; screen on; @0.2; cfg brightness; simpress 180 180 3100; @0.3; uitop; cfg brightness; tools torch; @0.3; uipush settings; @0.3; cfg brightness; back; @0.3; cfg brightness; uihome; @0.3; cfg brightness" && {
    [ "$(torch_bl)" = "60|100|35|100|35|0|100|35|35|100|35|" ] || { EVID="live backlight $(torch_bl)"; fail torch_live_backlight; }
    [ "$(torch_cfg)" = "35|35|35|35|35|35|35|35|35|35|35|" ] || { EVID="cfg brightness $(torch_cfg)"; fail torch_cfg_untouched; }
    [ "$(grep -c '^APP torch on' <<<"$out")" = 3 ] && [ "$(grep -c '^APP torch off' <<<"$out")" = 3 ] || { EVID="$(grep -c '^APP torch' <<<"$out") torch lines"; fail torch_on_off_pairs; }
    grep -q '^APP uitop tools_torch' <<<"$out" && grep -q '^APP uitop tools$' <<<"$out" && grep -q '^APP uitop menu$' <<<"$out" || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail torch_exits; }; }
# the tuner (調音器, scr_tuner.c, tuner_svc.c, pc_tuner.c): the listen page reads the simulator's synthetic microphone (console `simtone <Hz> [cents] | noise | off`); "tuner shown" = "APP tuner shown title=<note  Hz> hub=<cents> tuned=<0|1> ..."
nowrun tuner_accuracy "screen on; vol 0; tools tuner; tuner listen; @0.3; simtone 440; @1.5; tuner shown; simtone 82.41; @1.5; tuner shown; simtone 329.6; @1.5; tuner shown; simtone 440 7; @1.5; tuner shown; simtone 440 -12; @1.5; tuner shown; simtone noise; @1.5; tuner shown; simtone off; @1.5; tuner shown; simtone 196 -30; @1.5; tuner shown; simtone 1046.5; @1.5; tuner shown" && {
    [ "$(tn_title 1)" = "A4  440.0 Hz" ] && [ "$(tn_hub 1)" = 0 ] && [ "$(tn_field 1 tuned)" = 1 ] || { EVID="A4: $(tn_title 1) hub=$(tn_hub 1)"; fail tuner_a4_440; }
    [ "$(tn_title 2)" = "E2  82.4 Hz" ] && [ "$(tn_hub 2)" -ge -1 ] && [ "$(tn_hub 2)" -le 1 ] || { EVID="E2: $(tn_title 2) hub=$(tn_hub 2)"; fail tuner_e2; }
    [ "$(tn_title 3)" = "E4  329.6 Hz" ] && [ "$(tn_hub 3)" -ge -1 ] && [ "$(tn_hub 3)" -le 1 ] || { EVID="E4: $(tn_title 3) hub=$(tn_hub 3)"; fail tuner_high_e; }
    [ "$(tn_hub 4)" -ge 6 ] && [ "$(tn_hub 4)" -le 8 ] && [ "$(tn_field 4 tuned)" = 0 ] && [[ "$(tn_title 4)" == "A4  "* ]] || { EVID="+7: $(tn_title 4) hub=$(tn_hub 4) tuned=$(tn_field 4 tuned)"; fail tuner_plus7; }
    [ "$(tn_hub 5)" -ge -13 ] && [ "$(tn_hub 5)" -le -11 ] && [ "$(tn_field 5 tuned)" = 0 ] || { EVID="-12: $(tn_title 5) hub=$(tn_hub 5)"; fail tuner_minus12; }
    [ "$(tn_title 6)" = "No note heard" ] && [ "$(tn_title 7)" = "No note heard" ] && [ "$(tn_field 6 tuned)" = 0 ] || { EVID="noise: $(tn_title 6) | silence: $(tn_title 7)"; fail tuner_noise_no_note; }
    [ "$(tn_title 8)" = "G3  196.0 Hz" ] || [[ "$(tn_title 8)" == "G3  193."* ]] || [[ "$(tn_title 8)" == "G3  192."* ]] || { EVID="G3 -30: $(tn_title 8) hub=$(tn_hub 8)"; fail tuner_g3_minus30; }
    [ "$(tn_hub 8)" -ge -31 ] && [ "$(tn_hub 8)" -le -29 ] || { EVID="G3 -30 hub=$(tn_hub 8)"; fail tuner_g3_cents; }
    [ "$(tn_title 9)" = "C6  1047 Hz" ] || { EVID="C6: $(tn_title 9)"; fail tuner_c6; }; }
# the A4 reference moves the cents and the note names: 432 Hz as A4 reads 440 Hz as +32 cents; the reference is clamped to 430..450
nowrun tuner_a4_reference "screen on; vol 0; tools tuner; tuner listen; @0.3; tuner a4 432; simtone 432; @1.5; tuner shown; simtone 440; @1.5; tuner shown; tuner a4 999; tuner; tuner a4 1; tuner" && {
    [ "$(tn_title 1)" = "A4  432.0 Hz" ] && [ "$(tn_hub 1)" = 0 ] || { EVID="432: $(tn_title 1) hub=$(tn_hub 1)"; fail tuner_a4_432; }
    [ "$(tn_hub 2)" -ge 31 ] && [ "$(tn_hub 2)" -le 33 ] || { EVID="440 vs 432: hub=$(tn_hub 2)"; fail tuner_a4_shift; }
    [ "$(grep -c '^APP tuner on=1 a4=450 ' <<<"$out")" = 1 ] && [ "$(grep -c '^APP tuner on=1 a4=430 ' <<<"$out")" = 1 ] || { EVID="$(grep '^APP tuner on=' <<<"$out" | cut -c1-40 | tr '\n' '|')"; fail tuner_a4_clamp; }; }
# the borrowed knob steps the A4 reference from the page's menu (a tap on the page shows the popup, its Menu cell opens the ring: a tap on the A4 cell steps +1, the knob +3 more)
nowrun tuner_a4_ring "screen on; vol 0; tools tuner; tuner listen; @0.3; simtap 180 180; @0.3; simtaprow 0; @0.3; popring 0; @0.2; simknob 3; @0.2; tuner" && {
    grep -q '^APP tuner on=1 a4=444 ' <<<"$out" || { EVID="$(grep '^APP tuner on=' <<<"$out" | cut -c1-40 | tr '\n' '|')"; fail tuner_a4_knob; }; }
# music: it pauses while listening and goes on after, on every way out (back, hold home, a page over it, the screen off); a paused player stays paused
nowrun tuner_music_back "screen on; vol 0; track 3; @2; tools tuner; tuner listen; @0.5; simtone 440; @1; tuner shown; back; @0.3; tuner shown; tuner" && {
    [ "$(tn_music 1)" = paused ] && [ "$(tn_field 1 paused_by_page)" = 1 ] && [ "$(tn_music 2)" = go ] && [ "$(tn_field 2 paused_by_page)" = 0 ] || { EVID="$(grep '^APP tuner shown' <<<"$out" | tr '\n' '|')"; fail tuner_music_back; }
    grep -q '^APP tuner on=0 ' <<<"$out" && [ "$(grep -c '^APP tuner mic start' <<<"$out")" = 1 ] && [ "$(grep -c '^APP tuner mic stop' <<<"$out")" = 1 ] || { EVID="$(grep -c '^APP tuner mic' <<<"$out") mic lines"; fail tuner_mic_stopped_back; }; }
nowrun tuner_music_ways "screen on; vol 0; track 3; @2; tools tuner; tuner listen; @0.5; uipush settings; @0.5; tuner shown; back; @0.5; tuner shown; screen off; @0.5; tuner shown; screen on; @0.5; tuner shown; simpress 180 180 3100; @0.5; uitop; tuner shown; tuner" && {
    [ "$(tn_field 1 mic)" = 0 ] && [ "$(tn_music 1)" = go ] || { EVID="over: $(grep '^APP tuner shown' <<<"$out" | sed -n 1p)"; fail tuner_page_over_stops_mic; }
    [ "$(tn_field 2 mic)" = 1 ] && [ "$(tn_music 2)" = paused ] || { EVID="back: $(grep '^APP tuner shown' <<<"$out" | sed -n 2p)"; fail tuner_page_back_listens; }
    [ "$(tn_field 3 mic)" = 0 ] && [ "$(tn_music 3)" = go ] || { EVID="screen off: $(grep '^APP tuner shown' <<<"$out" | sed -n 3p)"; fail tuner_screen_off_stops_mic; }
    [ "$(tn_field 4 mic)" = 1 ] && [ "$(tn_music 4)" = paused ] || { EVID="screen on: $(grep '^APP tuner shown' <<<"$out" | sed -n 4p)"; fail tuner_screen_on_listens; }
    [ "$(grep -c '^APP tuner mic start' <<<"$out")" = 3 ] && [ "$(grep -c '^APP tuner mic stop' <<<"$out")" = 3 ] && grep -q '^APP uitop menu$' <<<"$out" && grep -q '^APP tuner on=0 ' <<<"$out" || { EVID="$(grep '^APP tuner mic\|^APP uitop' <<<"$out" | tr '\n' '|')"; fail tuner_hold_home_stops_mic; }; }
nowrun tuner_music_stays_paused "screen on; vol 0; track 3; @2; nowmenu -2; nowmenu -1; @1; tools tuner; tuner listen; @0.5; tuner shown; back; @0.3; tuner shown" && {
    [ "$(tn_field 1 paused_by_page)" = 0 ] && [ "$(tn_music 1)" = paused ] && [ "$(tn_music 2)" = paused ] || { EVID="$(grep '^APP tuner shown' <<<"$out" | tr '\n' '|')"; fail tuner_paused_stays_paused; }; }
# no microphone (the start fails): the page says so, the music is not touched, nothing is left running
nowrun tuner_no_mic "screen on; vol 0; track 3; @2; simallocfail 11000 12500; tools tuner; tuner listen; @0.5; tuner shown; simallocfail 0; back" && {
    [ "$(tn_field 1 failed)" = 1 ] && [ "$(tn_field 1 mic)" = 0 ] && [ "$(tn_music 1)" = go ] && [ "$(tn_title 1)" = "Microphone unavailable" ] || { EVID="$(grep '^APP tuner shown' <<<"$out" | tr '\n' '|')"; fail tuner_no_mic; }; }
# play a note: the tone is added by sfx_mix (blocks counted), the music pauses meanwhile and comes back, the guitar cell steps E2 A2 D3 G3 B3 E4, every way out silences it
nowrun tuner_tone "screen on; vol 0; track 3; @2; tools tuner; tuner tone; @0.3; tuner tonestate; tuner play; @0.5; tuner tonestate; tuner guitar 1; tuner tonestate; tuner guitar 3; tuner tonestate; tuner guitar 2; tuner tonestate; tuner wave 3; tuner a4 432; @0.3; tuner tonestate; tuner stop; @0.3; tuner tonestate; audio info; back; @0.3; tuner tonestate" && {
    [ "$(tn_tone 1 play)" = 0 ] && [ "$(tn_tone 1 sfx)" = 0 ] && [ "$(tn_tone 1 blocks)" = 0 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 1p)"; fail tuner_tone_idle; }
    [ "$(tn_tone 2 play)" = 1 ] && [ "$(tn_tone 2 sfx)" = 1 ] && [ "$(tn_tone 2 blocks)" -ge 20 ] && [ "$(tn_tone 2 paused_by_page)" = 1 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 2p)"; fail tuner_tone_plays; }
    [ "$(tn_tone 3 note)" = 45 ] && [ "$(tn_tone 3 hz)" = 110.000 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 3p)"; fail tuner_guitar_a2; }
    [ "$(tn_tone 4 note)" = 59 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 4p)"; fail tuner_guitar_b3; }
    [ "$(tn_tone 5 note)" = 40 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 5p)"; fail tuner_guitar_wrap; }
    [ "$(tn_tone 6 wave)" = 3 ] && [ "$(tn_tone 6 a4)" = 432 ] && [ "$(tn_tone 6 hz)" = 80.909 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 6p)"; fail tuner_tone_wave_a4; }
    b1=$(tn_tone 7 blocks); [ "$(tn_tone 7 play)" = 0 ] && [ "$(tn_tone 7 paused_by_page)" = 0 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 7p)"; fail tuner_tone_stops; }
    [ "$(tn_tone 8 play)" = 0 ] && [ "$(tn_tone 8 sfx)" = 0 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 8p)"; fail tuner_tone_back; }; }
nowrun tuner_tone_ways "screen on; vol 0; track 3; @2; tools tuner; tuner tone; @0.3; tuner play; @0.3; screen off; @0.5; tuner tonestate; screen on; tuner play; @0.3; uipush settings; @0.5; tuner tonestate; back; @0.3; tuner play; @0.3; tuner voice; @0.5; tuner tonestate; back; @0.3; tuner tonestate; simpress 180 180 3100; @0.5; uitop; tuner tonestate" && {
    [ "$(tn_tone 1 play)" = 0 ] && [ "$(tn_tone 1 sfx)" = 0 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 1p)"; fail tuner_tone_screen_off; }
    [ "$(tn_tone 2 play)" = 0 ] && [ "$(tn_tone 2 sfx)" = 0 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 2p)"; fail tuner_tone_page_over; }
    [ "$(tn_tone 3 play)" = 1 ] && [ "$(tn_tone 3 sfx)" = 1 ] && [ "$(tn_tone 4 play)" = 1 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 3,4p | tr '\n' '|')"; fail tuner_tone_voice_page_keeps; }
    [ "$(tn_tone 5 play)" = 0 ] && [ "$(tn_tone 5 sfx)" = 0 ] && [ "$(tn_tone 5 paused_by_page)" = 0 ] && grep -q '^APP uitop menu$' <<<"$out" || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | sed -n 5p)"; fail tuner_tone_hold_home; }; }
# the voice list: a row sets the voice (the tone keeps sounding with it) and goes back; the listen page and the list leave no timer or object (nowrun fails on "APP ui leak")
nowrun tuner_voice_list "screen on; vol 0; tools tuner; tuner tone; @0.3; tuner voice; @0.3; uistack; tuner wave 4; tuner tonestate; back; @0.3; tuner tonestate" && {
    [ "$(tn_tone 1 wave)" = 4 ] && [ "$(tn_tone 2 wave)" = 4 ] || { EVID="$(grep '^APP tuner tonestate' <<<"$out" | tr '\n' '|')"; fail tuner_voice_list; }; }
nowrun tools_moon_is_face "simtime 2026-02-17 03:22:09; @1.5; clockface classic; tools moon; @1; uitop; clockface moon?; clockface next" && {
    grep -q '^APP clockface moon phase=New' <<<"$out" && grep -q '^APP clockface set=minimal$' <<<"$out" && grep -q '^APP tools top=clock$' <<<"$out" || { EVID="$(grep '^APP tools top\|^APP clockface' <<<"$out" | tr '\n' '|')"; fail tools_moon_is_face; }; }
# --- end Task 5 ---

