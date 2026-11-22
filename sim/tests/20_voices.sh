# SPDX-License-Identifier: LGPL-2.1-or-later
# --- Task 9: voice levels ---
# the console prints the snapshot of the fake source: off by default, then the five NES voices, levels moving with the virtual clock
nowrun voices_off_default "voices" && { grep -qx 'APP voices n=0' <<<"$out" || { EVID="$(grep '^APP voices' <<<"$out")"; fail voices_off_default; }; }
nowrun voices_nes "simvoices nes; voices; @0.5; voices; @0.25; voices; simvoices off; voices" && {
    v1=$(grep '^APP voices' <<<"$out" | sed -n 1p); v2=$(grep '^APP voices' <<<"$out" | sed -n 2p); v3=$(grep '^APP voices' <<<"$out" | sed -n 3p)
    [[ "$v1" =~ ^APP\ voices\ n=5\ S1=[0-9]+\ S2=[0-9]+\ T=[0-9]+\ N=[0-9]+\ D=[0-9]+$ ]] || { EVID="$v1"; fail voices_nes_names; }
    [ "$v1" != "$v2" ] || { EVID="$v1 | $v2"; fail voices_levels_move; }
    [ "$v2" != "$v3" ] || { EVID="$v2 | $v3"; fail voices_levels_move2; }
    for v in "$v1" "$v2" "$v3"; do grep -q '=0\( \|$\)' <<<"$v" || { EVID="$v"; fail voices_one_silent; }; done
    [ "$(grep '^APP voices' <<<"$out" | sed -n 4p)" = 'APP voices n=0' ] || fail voices_off_again; }
nowrun voices_sets "simvoices gb; voices; simvoices spc; voices; simvoices max; voices; simvoices bogus" && {
    [ "$(grep '^APP voices' <<<"$out" | sed -n 1p | cut -d' ' -f3-7 | sed 's/=[0-9]*//g')" = 'n S1 S2 W N' ] || { EVID="$(grep '^APP voices' <<<"$out" | sed -n 1p)"; fail voices_gb_names; }
    [ "$(grep '^APP voices' <<<"$out" | sed -n 2p | sed 's/=[0-9]*//g')" = 'APP voices n 1 2 3 4 5 6 7 8' ] || { EVID="$(grep '^APP voices' <<<"$out" | sed -n 2p)"; fail voices_spc_names; }
    [ "$(grep '^APP voices' <<<"$out" | sed -n 3p | cut -d' ' -f3)" = 'n=32' ] || fail voices_max_n
    [ "$(grep '^APP voices' <<<"$out" | sed -n 3p | wc -w)" = 35 ] || fail voices_max_names
    grep -qx 'SIM voices unknown bogus' <<<"$out" || fail voices_unknown_set; }
# a paused source reads all 0 (names and n stay); resume: they move again
nowrun voices_paused "screen on; vol 0; color 2; @3; simvoices nes; voices; $NOWMENU; $PAUSE; @0.2; voices; @0.45; voices; $NOWMENU; $PAUSE; @0.2; voices; @0.45; voices" && {
    vp=$(grep '^APP voices' <<<"$out")
    [ "$(sed -n 1p <<<"$vp" | grep -c '=[1-9]')" = 1 ] || { EVID="$(sed -n 1p <<<"$vp")"; fail voices_before_pause_move; }
    for k in 2 3; do sed -n ${k}p <<<"$vp" | grep -qx 'APP voices n=5 S1=0 S2=0 T=0 N=0 D=0' || { EVID="$(sed -n ${k}p <<<"$vp")"; fail voices_paused_zero; }; done
    [ "$(sed -n 4,5p <<<"$vp" | grep -c '=[1-9]')" -ge 1 ] || { EVID="$(sed -n 4,5p <<<"$vp" | tr '\n' '|')"; fail voices_resume_move; }; }
# --- end Task 9 ---

# --- Task 10: voices kind ---
# The voices picture (300x200 at 30,80, bars) and the ring picture (340x340 at 10,10): measured on shots. A voice rests part of the time (fake_audio_svc.c), so every
# count is taken over the union of several shots 0.3 s apart. Names: columns of glyph pixels in the 14 rows 184..197 under the bars; bars: columns of coloured pixels above.
# kind voices with a chip: five bars with names for nes, four for gb; off: bars again within a tick; on again: voices
fresh
vshots v10_nes "simvoices nes; viz voices; @0.1; viz stat"
sh=$(grep '^APP viz shown=' <<<"$out" | tail -1); vn=$(grep '^APP viz voices' <<<"$out" | tail -1)
[ "$sh" = "APP viz shown=voices" ] && [ "$vn" = "APP viz voices n=5 var=0" ] && grep -q '^APP viz open=1 kind=voices var=0 ' <<<"$out" || { EVID="$sh | $vn"; fail t10_nes_shown; }
[ "$(vbars 5 $T/v10_nes_[0-5].png)" = "5 5" ] || { EVID="bars/names: $(vbars 5 $T/v10_nes_[0-5].png)"; fail t10_nes_five_bars; }
cmp -s "$T/v10_nes_0.png" "$T/v10_nes_1.png" && { EVID="the bars did not move"; fail t10_nes_moves; }
nowrun t10_switch "screen on; vol 0; @1; simvoices nes; viz voices; @0.1; viz stat; simvoices off; @0.1; viz stat; simvoices gb; @0.1; viz stat; shot $T/v10_gb.png; simvoices off; @0.1; viz stat; simvoices spc; @0.1; viz stat" && {
    [ "$(grep '^APP viz shown=' <<<"$out" | tail -5 | tr '\n' ' ')" = "APP viz shown=voices APP viz shown=bars APP viz shown=voices APP viz shown=bars APP viz shown=voices " ] || { EVID="$(grep '^APP viz shown=' <<<"$out" | tr '\n' ' ')"; fail t10_switch_shown; }
    [ "$(grep '^APP viz voices' <<<"$out" | tail -5 | cut -d' ' -f4 | tr '\n' ' ')" = "n=5 n=0 n=4 n=0 n=8 " ] || { EVID="$(grep '^APP viz voices' <<<"$out" | tr '\n' ' ')"; fail t10_switch_n; }
    [ "$(grep '^APP viz open' <<<"$out" | sed -E 's/^APP viz open=[01] kind=([a-z]+) .*/\1/' | sort -u | tr '\n' ' ')" = "voices " ] || { EVID="the stored kind changed"; fail t10_switch_stored; }; }
vshots v10_gb "simvoices gb; viz voices"
[ "$(vbars 4 $T/v10_gb_[0-5].png)" = "4 4" ] || { EVID="bars/names: $(vbars 4 $T/v10_gb_[0-5].png)"; fail t10_gb_four_bars; }
# a stored kind voices with nothing that has voices: bars (never an empty picture), the stored value stays
fresh; printf 'lang=0\nviz_style=voices\n' > "$T/fix/_player/config/app.txt"
vshots v10_stored "viz voices"
[ "$(grep '^APP viz shown=' <<<"$out" | tail -1)" = "APP viz shown=bars" ] && grep -q '^APP viz open=[01] kind=voices ' <<<"$out" || { EVID="$(grep '^APP viz shown=\|^APP viz open' <<<"$out" | tr '\n' ' ')"; fail t10_stored_voices_bars; }
NOWCFG=viz_style=voices nowrun t10_stored_playing "screen on; vol 0; track 3; @3; viz voices; @0.5; shot $T/v10_stored_bars.png; simvoices nes; @0.1; viz stat" && {
    [ "$(grep '^APP viz shown=' <<<"$out" | tr '\n' ' ')" = "APP viz shown=bars APP viz shown=voices " ] || { EVID="$(grep '^APP viz shown=' <<<"$out" | tr '\n' ' ')"; fail t10_stored_playing; }
    python3 -c "
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; from png_diff import read_png
w, h, rows = read_png(sys.argv[1]); bg = rows[180][6:9]
sys.exit(0 if sum(1 for y in range(80, 280) for x in range(30, 330) if rows[y][3 * x:3 * x + 3] != bg) > 2000 else 1)" "$T/v10_stored_bars.png" || { EVID="the fallback picture of a playing MP3 is empty"; fail t10_fallback_not_empty; }; }
# the kind axis: nine entries with voices (a full turn of 9 detents is back at bars; voices, rows and roll are the sixth to eighth stops, lyrics the ninth), six without
fresh
nowrun t10_axis_on "screen on; vol 0; color 0; @3; simvoices nes; viz bars; viz menu; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob 1; viz stat; knob -1; viz stat" && {
    [ "$(ax | tr ' ' '\n' | cut -d: -f1 | tr '\n' ' ' | cut -d' ' -f3-)" = "ring scope fall phase voices rows voices " ] || { EVID="$(ax)"; fail t10_axis_six; }; }
nowrun t10_axis_off "screen on; vol 0; color 0; @3; simvoices nes; viz bars; viz menu; knob 1; knob 1; knob 1; knob 1; viz stat; simvoices off; @0.1; knob 1; viz stat; knob 1; viz stat; knob -1; knob -1; knob -1; knob -1; knob -1; viz stat" && {
    # four detents to phase (the fifth stop is voices while the chip plays); chip gone: the next is lyrics (voices, rows and roll are not offered), then bars; five back from bars is ring (six kinds)
    [ "$(ax | tr ' ' '\n' | cut -d: -f1 | tr '\n' ' ' | cut -d' ' -f3-)" = "phase lyrics bars ring " ] || { EVID="$(ax)"; fail t10_axis_five; }; }
# a stored voices with nothing that has voices is not on the axis: one step on goes to bars, one back to lyrics (the last stop); with a chip playing voices is followed by rows
NOWCFG=viz_style=voices nowrun t10_axis_enter_fwd "screen on; vol 0; color 0; @3; viz menu; knob 1; viz stat" && {
    [ "$(ax | tr ' ' '\n' | cut -d: -f1 | tr '\n' ' ')" = "voices bars " ] || { EVID="$(ax)"; fail t10_axis_enter_fwd; }; }
NOWCFG=viz_style=voices nowrun t10_axis_enter_back "screen on; vol 0; color 0; @3; viz menu; knob -1; viz stat" && {
    [ "$(ax | tr ' ' '\n' | cut -d: -f1 | tr '\n' ' ')" = "voices lyrics " ] || { EVID="$(ax)"; fail t10_axis_enter_back; }; }
NOWCFG=viz_style=voices nowrun t10_axis_enter_playing "screen on; vol 0; color 0; @3; simvoices nes; viz menu; knob 1; viz stat; knob -1; viz stat" && {
    [ "$(ax | tr ' ' '\n' | cut -d: -f1 | tr '\n' ' ')" = "voices rows voices " ] || { EVID="$(ax)"; fail t10_axis_enter_playing; }; }
# ---- vizb (2026-11-17): voice rows, piano roll, mute / solo, lyrics, the help cell ----
# the kinds exist only with voices (rows, roll) or always (lyrics); without voices a stored rows kind is drawn as bars and the kind axis skips it
nowrun vizb_rows_roll_kinds "screen on; vol 0; color 0; @3; simvoices nes; viz rows; viz stat; viz roll; viz stat; simvoices off; @0.2; viz stat" && {
    sh=$(grep '^APP viz shown=' <<<"$out" | tr '\n' ' ')
    [ "$sh" = "APP viz shown=rows APP viz shown=rows APP viz shown=roll APP viz shown=roll APP viz shown=bars " ] || { EVID="$sh"; fail vizb_rows_roll_kinds; }; }
# mute / solo through the console: cycle on -> muted -> solo -> on (a muted voice keeps its mute while another is soloed and gets it back after); solo mutes every other voice; one solo only; a new source (the fake source reset) puts everything back on
nowrun vizb_mute_solo "screen on; vol 0; color 0; @3; simvoices spc; viz rows; vmute; vmute 2 mute; vmute 5 cycle; vmute 5 cycle; vmute 5 cycle; vmute 5 cycle; vmute 3 solo; vmute 6 solo; vmute 6 on; vmute clear; vmute 1 mute; simvoices gb; @0.2; vmute" && {
    vm=$(grep '^APP vmute' <<<"$out" | sed -E 's/^APP vmute n=([0-9]+) mask=(0x[0-9a-f]+) solo=(-?[0-9]+) states=(.*)$/\1 \2 \3 \4/' | tr '\n' '|')
    [ "$vm" = "8 0x0 -1 --------|8 0x4 -1 --m-----|8 0x24 -1 --m--m--|8 0xdf 5 --m--s--|8 0x4 -1 --m-----|8 0x24 -1 --m--m--|8 0xf7 3 --ms-m--|8 0xbf 6 --m--ms-|8 0x24 -1 --m--m--|8 0x0 -1 --------|8 0x2 -1 -m------|4 0x0 -1 ----|" ] || { EVID="$vm"; fail vizb_mute_solo; }; }
# the voices' extras reach the console: note names for the pitched voices, strikes, pan (the spc set has a pan, the nes set none), the unpitched ones flagged
nowrun vizb_voicex "screen on; vol 0; @3; simvoices nes; voicex; simvoices spc; voicex" && {
    vxl=$(grep '^APP voicex' <<<"$out")
    grep -qE '^APP voicex 0 S1 lv=[0-9]+ note=[A-G]#?[0-9]|^APP voicex 0 S1 lv=[0-9]+ note=-- ' <<<"$vxl" && grep -qE '^APP voicex 3 N lv=[0-9]+ note=-- cents=-1 strikes=[0-9]+ pan=-128 flags=2$' <<<"$vxl" && grep -qE '^APP voicex 7 8 lv=[0-9]+ note=.* pan=-?[0-9]+ flags=1$' <<<"$vxl" && ! grep -qE '^APP voicex 7 8 .* pan=-128' <<<"$vxl" || { EVID="$(head -12 <<<"$vxl" | tr '\n' '|')"; fail vizb_voicex; }; }
# the style ring of the voice kinds: cell 4 picks the voice (the first tap only selects the cell), cell 5 steps its state (on -> mute -> solo), the knob steps the current cell (solo -> on)
nowrun vizb_ring_voice_axes "screen on; vol 0; color 0; @3; simvoices spc; viz rows; viz menu; viz pick 4; viz pick 4; viz pick 4; viz stat; viz pick 5; viz pick 5; viz pick 5; viz stat; knob 1; viz stat; vmute" && {
    vst=$(grep '^APP viz extra' <<<"$out" | sed -E 's/^APP viz extra .* sel=([0-9]+) mask=(0x[0-9a-f]+) solo=(-?[0-9]+) .*$/\1:\2:\3/' | tr '\n' ' ')
    [ "$vst" = "0:0x0:-1 0:0x0:-1 0:0x0:-1 1:0x0:-1 2:0x0:-1 2:0x0:-1 2:0x0:-1 2:0x4:-1 2:0xfb:2 2:0xfb:2 2:0x0:-1 " ] || { EVID="$vst"; fail vizb_ring_voice_axes; }; }
nowrun vizb_ring_help "screen on; vol 0; color 0; @3; viz bars; viz menu; viz pick 6; uitop; back; uitop" && {
    [ "$(grep '^APP uitop' <<<"$out" | tr '\n' '|')" = "APP uitop explain|APP uitop viz|" ] && grep -q '^APP viz ring .*cells=7 ' <<<"$out" || { EVID="$(grep '^APP uitop\|^APP viz ring' <<<"$out" | tr '\n' '|')"; fail vizb_ring_help; }; }
nowrun vizb_help_text "help open viz; help show" && { grep -q '^APP explain id=viz ' <<<"$out" || { EVID="$(grep '^APP explain' <<<"$out")"; fail vizb_help_text; }; }
# lyrics: the sample .lrc next to the first song (tools/simtools/sim_fixture.py) is parsed once (14 lines), the line follows the position; another song has none; a GBK file says so
nowrun vizb_lyrics "screen on; vol 0; color 0; track 0; @1; viz lyrics; @1; viz stat; @21; viz stat; track 1; @1.5; viz stat" && {
    ly=$(grep '^APP viz extra' <<<"$out" | sed -E 's/^APP viz extra .* lyr=([a-z]+) lines=([0-9]+) idx=(-?[0-9]+) .*$/\1:\2:\3/' | tr '\n' ' ')
    [ "$ly" = "none:0:-2 ok:14:-1 ok:14:4 none:0:-3 " ] || { EVID="$ly"; fail vizb_lyrics; }; }
fresh; printf '[00:01.00]\xC4\xE3\xBA\xC3\xC4\xE3\xBA\xC3\n' > "$T/fix/export/夜航星圖/01_harbor_light.lrc"
out=$($SIM --headless --root "$T/fix" --cmd "screen on; vol 0; track 0; @1; viz lyrics; @1; viz stat" 2>&1)
grep -q '^APP viz extra .* lyr=encoding lines=0 ' <<<"$out" || { EVID="$(vx)"; fail vizb_lyrics_encoding; }
# the lyrics view wraps the long line (a shot of the 18 s line) and the variant 1 shows two lines each side; no leak, no sanitizer text
nowrun vizb_lyrics_shots "screen on; vol 0; color 0; track 0; @1; viz lyrics; @17.5; shot $T/ly0.png; viz var 1; @0.5; shot $T/ly1.png" && {
    [ -s "$T/ly0.png" ] && ! cmp -s "$T/ly0.png" "$T/ly1.png" || { EVID="lyrics shots missing or equal"; fail vizb_lyrics_shots; }; }
# a rows shot with 32 voices and a roll shot are not empty
nowrun vizb_rows_roll_shots "screen on; vol 0; color 0; @1; simvoices max; viz rows; @1.5; shot $T/rw.png; viz roll; @3; shot $T/rl.png" && {
    python3 -c "
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; from png_diff import read_png
for f in sys.argv[1:]:
    w, h, rows = read_png(f); bg = rows[180][6:9]
    sys.exit(1) if sum(1 for y in range(80, 280) for x in range(30, 330) if rows[y][3 * x:3 * x + 3] != bg) < 1500 else None" "$T/rw.png" "$T/rl.png" || { EVID="rows / roll picture empty"; fail vizb_rows_roll_shots; }; }
# the size switch: the round board has no zoom axis (cells stay 7 with the help cell last) and the setting is ignored (zoom stays 256)
nowrun vizb_zoom_round "screen on; vol 0; color 0; @3; viz bars; viz zoom orig; viz stat; viz zoom zoom; viz stat" && {
    zs=$(grep '^APP viz extra' <<<"$out" | sed -E 's/^APP viz extra zoom=([0-9]+) size=([a-z]+) .*$/\1:\2/' | tr '\n' ' ')
    [ "$zs" = "256:orig 256:orig 256:orig " ] || { EVID="$zs"; fail vizb_zoom_round; }; }

# every variant draws for n = 1, 5, 8, 16 (the fake "sixteen" set; "max" is 32 voices now): n bars with names (the union of the shots), the ring with n groups; the variant really drawn is printed
for set_n in one:1 nes:5 spc:8 sixteen:16; do
    set=${set_n%%:*}; n=${set_n##*:}
    for var in 0 1 2; do
        fresh
        vshots v10_${set}_$var "simvoices $set; viz voices; viz var $var; @0.1; viz stat"
        grep -q "^APP viz voices n=$n var=$var\$" <<<"$out" && [ "$(grep '^APP viz shown=' <<<"$out" | tail -1)" = "APP viz shown=voices" ] || { EVID="$(grep '^APP viz voices\|^APP viz shown' <<<"$out" | tail -2 | tr '\n' ' ')"; fail t10_${set}_${var}_shown; continue; }
        if [ $var = 2 ]; then [ "$(vring $T/v10_${set}_${var}_[0-5].png)" = "$n" ] || { EVID="ring groups: $(vring $T/v10_${set}_${var}_[0-5].png)"; fail t10_${set}_${var}_groups; }
        else [ "$(vbars $n $T/v10_${set}_${var}_[0-5].png)" = "$n $n" ] || { EVID="bars/names: $(vbars $n $T/v10_${set}_${var}_[0-5].png)"; fail t10_${set}_${var}_bars; }
        fi
    done
done
# the ring map is a PSRAM allocation: when it fails, variant 2 of voices is drawn as variant 0 (the stored variant stays 2); with memory it is the ring again
nowrun t10_ringmap_alloc_fail "screen on; vol 0; @1; simvoices nes; viz bars; simallocfail 240000; viz voices; viz var 2; @0.3; viz stat; simallocfail 0; viz var 2; @0.3; viz stat" && {
    [ "$(grep '^APP viz voices' <<<"$out" | sed -n '2p;3p;5p;6p' | tr '\n' ' ')" = "APP viz voices n=5 var=0 APP viz voices n=5 var=0 APP viz voices n=5 var=2 APP viz voices n=5 var=2 " ] && [ "$(grep '^APP viz shown=' <<<"$out" | sed -n '3p;6p' | sort -u)" = "APP viz shown=voices" ] && grep -q '^APP viz open=1 kind=voices var=2 ' <<<"$out" || { EVID="$(grep '^APP viz voices\|^APP viz shown' <<<"$out" | tr '\n' ' ')"; fail t10_ringmap_alloc_fail; }; }
# no FFT while voices are drawn: the page does not ask for the spectrum (simspec counts the asks); the bars kind does
nowrun t10_no_fft "screen on; vol 0; @1; simvoices nes; viz voices; @0.3; simspec; @1; simspec; viz bars; @1; simspec" && {
    sc=$(grep '^SIM spec' <<<"$out" | sed 's/.*=//' | tr '\n' ' ')
    set -- $sc; [ "$1" = "$2" ] && [ "$3" -gt "$2" ] || { EVID="spectrum calls: $sc"; fail t10_no_fft; }; }
# the sensitivity axis means nothing for voices: inert (the floor stays) while they are shown, a step for bars; the bar width axis too
nowrun t10_floor_inert "screen on; vol 0; @1; simvoices nes; viz voices; viz menu; viz pick 5; viz pick 5; viz pick 5; viz stat; viz pick -1; @0.3; simvoices off; @0.1; viz menu; viz pick 5; viz pick 5; viz stat" && {
    # the stat lines of the two explicit "viz stat" are the 6th and the 11th of the viz commands' answers
    [ "$(grep '^APP viz axes' <<<"$out" | sed -n '6p;11p' | tr '\n' ' ')" = "APP viz axes barw=7 floor=-48 order=seq gap=2 size=box peaks=1 APP viz axes barw=7 floor=-36 order=seq gap=2 size=box peaks=1 " ] || { EVID="$(grep '^APP viz axes' <<<"$out" | tr '\n' ' ')"; fail t10_floor_inert; }; }
# the colour mapping applies to the voices as to bars: BAND colours every voice differently, HEIGHT does not
nowrun t10_map_band "screen on; vol 0; @1; simvoices nes; viz voices; viz map height; @1; shot $T/v10_h.png; viz map band; @1; shot $T/v10_b.png" && {
    python3 -c "
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; from png_diff import read_png
def cols(f):
    w, h, rows = read_png(f); bg = rows[180][6:9]; s = set()
    for y in range(80, 80 + 180):
        for x in range(30, 330):
            c = rows[y][3 * x:3 * x + 3]
            if c != bg: s.add(c)
    return len(s)
sys.exit(0 if cols(sys.argv[2]) < 8 and cols(sys.argv[1]) > 20 else 1)" "$T/v10_h.png" "$T/v10_b.png" || { EVID="BAND does not give few flat colours / HEIGHT does not give a ramp"; fail t10_map_band; }; }
# first frames (Task 4's rule, kept for every re-apply): the voices kind opens and switches with no black picture; theme change on an open page; back from a page above
for th in 0 1 2; do
    first_frame "voices_th$th" $th "simvoices nes; " "viz voices" - 30 80 330 280
done
first_frame voices_ring 1 "simvoices nes; viz voices; @1; " "viz var 2" - 10 10 350 350
first_frame voices_from_bars 1 "simvoices nes; viz bars; @1; " "viz voices" - 30 80 330 280
first_frame theme_open_bars 0 "viz bars; @1; " "theme next" - 30 80 330 280
first_frame theme_open_ring 0 "viz ring; @1; " "theme next" - 10 10 350 350
first_frame theme_open_fall 0 "viz fall; @1; " "theme next" - 52 80 308 280
first_frame theme_open_phase 1 "viz phase; @1; " "theme next" - 30 30 330 330
# back_*: regression guards (the covered page keeps its picture while covered, so they pass even without the blanking call), not proofs of it
first_frame back_bars 0 "viz bars; @1; uipush settings; @0.5; " "back" - 30 80 330 280
first_frame back_ring 1 "viz ring; @1; uipush settings; @0.5; " "back" - 10 10 350 350
first_frame back_fall 2 "viz fall; @1; uipush settings; @0.5; " "back" - 52 80 308 280
# --- end Task 10 ---

