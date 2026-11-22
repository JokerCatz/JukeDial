# SPDX-License-Identifier: LGPL-2.1-or-later
# (prepared in 00_helpers.sh: mk_t11root, mk_t12root)
mk_t11root; mk_t12root
# --- UI unify T13 ---
# safe mode (sim --mode safe): one static screen, built-in fonts, black and white; no touch, no knob, no theme; one 60 s timer turns the backlight off
# t13_safe_screen: exactly three labels with the fixed texts, no button
out=$(t13 $SIM --headless --mode safe --root "$T/t13r" --cmd "@0.5; uiboxes; simtimers")
{ [ "$(grep -c '^APP uibox label' <<<"$out")" = 3 ] && ! grep -q '^APP uibox button' <<<"$out" && grep -q '^APP uibox label .*|SAFE MODE$' <<<"$out" \
  && grep -q '^APP uibox label .*|3 consecutive crashes/count 3$' <<<"$out" && grep -q '^APP uibox label .*|Reflash over USB$' <<<"$out" && ! t12bad; } \
  || { EVID="$(grep '^APP uibox\|^SIM mode' <<<"$out" | cut -d'|' -f2 | tr '\n' '|')"; fail t13_safe_screen; }
# t13_safe_one_timer: ui_safe_show adds exactly one LVGL timer, a one-shot of 60 s (the simulator prints the count just before the call)
pre=$(sed -n 's/^SIM timers pre=//p' <<<"$out")
post=$(sed -n 's/^SIM timers n=\([0-9]*\) .*/\1/p' <<<"$out")
per=$(sed -n 's/^SIM timers n=.* periods=//p' <<<"$out")
{ [ -n "$pre" ] && [ "$post" = "$((pre + 1))" ] && grep -q '\(^\|,\)60000x1\(,\|$\)' <<<"$per"; } || { EVID="pre=$pre post=$post periods=$per"; fail t13_safe_one_timer; }   # the added one is a 60 s one-shot (x1)
# t13_safe_dark: 61 s later the backlight is off; a tap and a knob turn do not wake it (nothing is wired to them)
out=$(SIM_TRACE=1 t13 $SIM --headless --mode safe --root "$T/t13r" --cmd "@61; simtap 180 180; simknob 1; @0.5")
{ grep -qx 'SIM backlight 0' <<<"$out" && [ "$(grep '^SIM backlight' <<<"$out" | tail -1)" = 'SIM backlight 0' ] && ! t12bad; } || { EVID="$(grep '^SIM backlight' <<<"$out" | tr '\n' '|')"; fail t13_safe_dark; }

# --- batch C fix ---
# tcfix_pagesets_match (review I-1): every Makefile line that shoots pictures (ui_baseline.py sim ..., sim_diff.py ...) takes $(SIM_PAGESETS), and that list names each mode
# ui_baseline.py can shoot, so sim-base and both halves of sim-diff cover the same pages
bad=""
while IFS= read -r l; do case "$l" in *'$(SIM_PAGESETS)'*) ;; *) bad="$bad|${l:0:60}";; esac; done < <(grep -o 'python3 tools/simtools/\(ui_baseline.py sim\|sim_diff.py\)[^|;]*' Makefile)
modes=$(sed -n 's/^SIM_PAGESETS *= *//p' Makefile)
for f in $(grep -o '"--[a-z]*", action="store_true"' tools/simtools/ui_baseline.py | cut -d'"' -f2) ; do case " $modes " in *" $f "*) ;; *) bad="$bad|mode $f missing from SIM_PAGESETS";; esac; done
n=$(grep -c 'python3 tools/simtools/\(ui_baseline.py sim\|sim_diff.py\)' Makefile)
{ [ -z "$bad" ] && [ "$n" -ge 4 ]; } || { EVID="n=$n bad=$bad"; fail tcfix_pagesets_match; }

# tcfix_pads_labels (batch B board run, B-1): the pad labels of the board's demo kit (the fake kit's labels are the board's: kick, snare, hat closed, hat open, clap,
# rim, tom low, tom high) are tight boxes that stay >= 4 px apart (no two labels in one row that read as one phrase, "hat | open") and inside the glass
# (with the repo's CJK fonts linked: the board's fonts; the English fallback font is wider and is not what the board shows)
if [ -d fonts ]; then
rm -rf "$T/tcfr"; cp -r sim/fixture "$T/tcfr"; mkdir -p "$T/tcfr/_player"; ln -s "$PWD/fonts" "$T/tcfr/_player/fonts"
out=$(tcf "inst page 1; @1; uiboxes")
bad=$(grep '^APP uibox label' <<<"$out" | python3 -c '
import sys
want = {"kick", "snare", "hat/closed", "hat/open", "clap", "rim", "tom/low", "tom/high"}
bx = []
for l in sys.stdin:
    a, _, t = l.rstrip("\n").partition("|")
    if t in want:
        x1, y1, x2, y2 = map(int, a.split()[3:7]); bx.append((t, x1, y1, x2, y2))
bad = [] if len(bx) == 8 else ["found %d of 8 labels" % len(bx)]
for i, a in enumerate(bx):
    for b in bx[i + 1:]:
        if b[3] + 4 > a[1] and a[3] + 4 > b[1] and b[4] + 4 > a[2] and a[4] + 4 > b[2]:
            bad.append("%s ~ %s" % (a[0], b[0]))
    for x, y in ((a[1], a[2]), (a[3], a[2]), (a[1], a[4]), (a[3], a[4])):
        if (x - 180) ** 2 + (y - 180) ** 2 > 176 ** 2: bad.append("%s outside the glass" % a[0])
print("|".join(bad))')
{ [ -z "$bad" ] && ! t11bad; } || { EVID="$bad"; fail tcfix_pads_labels; }
# tcfix_metro_centre (B-2): the metronome ring's centre with a loop ("開始" + "速度與拍號跟著循環", the board's text) wraps in balanced lines: the label is narrower than the
# 168 px that left the ninth character alone
out=$(tcf "inst loop rec; @2.1; siminst note 60 100; siminst note 60 0; inst loop rec; @0.1; inst page 2; @0.5; popring -2; @0.3; uiboxes; shot $T/tcf_metro.png")
w=$(grep '^APP uibox label .*|開始/速度與拍號跟著循環$' <<<"$out" | awk '{print $6 - $4 + 1}')
box=$(grep '^APP uibox label .*|開始/速度與拍號跟著循環$' <<<"$out" | awk '{print $4, $5, $6, $7}')
# the last line is not one character alone: its ink (the bottom 29 px of the label box) is wider than 40 px (a character is about 21 px)
ink=$(python3 - "$T/tcf_metro.png" $box <<'PY'
import sys
sys.path[:0] = ["tools", "tools/simtools", "tools/gen", "tools/check", "tools/board"]
import png_diff
w, h, rows = png_diff.read_png(sys.argv[1])
x1, y1, x2, y2 = map(int, sys.argv[2:6])
bg = rows[y2 + 6][3 * 180:3 * 180 + 3]      # the disc just below the text
xs = [x for y in range(y2 - 24, y2 - 3) for x in range(x1, x2 + 1) if rows[y][3 * x:3 * x + 3] != bg]
print(max(xs) - min(xs) + 1 if xs else 0)
PY
)
{ [ -n "$w" ] && [ "$w" -le 126 ] && [ "${ink:-0}" -ge 40 ] && ! t11bad; } || { EVID="centre label width=$w last-line ink=$ink"; fail tcfix_metro_centre; }
# tcfix_centre_codepoints (review m3): a one-character Chinese centre text and one over 96 bytes (no crash, the text is kept whole, no orphan fix for the long one: its box is the full 168 px wide)
LONG=$(python3 -c "print('字' * 40)")
for t in "環" "$LONG"; do
    out=$(tcp "screen on; @0.6; uipush poptest; @0.3; poptest centre $t; @0.3; popring -5")
    c=$(sed -n 's/^APP popring open=1 .* centre=//p' <<<"$out" | tail -1)
    # the console line cuts a long text (at a code point, final review I-3): compare the part it prints
    { [ "${c:0:${#t}}" = "${t:0:${#c}}" ] && [ -n "$c" ] && { [ "$t" != "環" ] || [ "$c" = "$t" ]; } && ! t11bad; } || { EVID="centre=${c:0:20} len=${#c}"; fail tcfix_centre_codepoints; }
done
out=$(tcp "screen on; @0.6; uipush poptest; @0.3; poptest centre $LONG; @0.3; uiboxes")
lw=$(grep "^APP uibox label .*|$LONG\$" <<<"$out" | awk '{print $6 - $4 + 1}')
[ "$lw" = 168 ] || { EVID="long centre box width=$lw"; fail tcfix_centre_codepoints; }
fi
# tcfix_mem_settle (B-4): the test-only console line `mem settle` answers with the same MEM line as `mem`, in the player's console and the instrument's (the simulator has no DMA bounce
# buffers to free: it is the plain mem there); an unknown word after `mem` is still unknown
for mode in player inst; do
    if [ $mode = inst ]; then o=$(t11 "mem; mem settle; mem bogus"); else o=$($SIM --headless --root "$T/t11root" --cmd "mem; mem settle; mem bogus" 2>&1); fi
    [ "$(grep -c '^MEM cmd internal_free=' <<<"$o")" = 2 ] && grep -qx 'APP cmd mem settle' <<<"$o" && ! grep -q 'Sanitizer\|runtime error' <<<"$o" && grep -q '^APP cmd unknown' <<<"$o" || { EVID="$mode: $(grep -c '^MEM cmd' <<<"$o") MEM lines"; fail tcfix_mem_settle; }
done

# rateguard, behaviour (fake_audio_svc.c takes the firmware's accept decision, pc_src_rate + pc_pipe_plan): a source reporting 768000 is refused with the board's line and ends as a failed play,
# the next playable file plays, five in a row stop through the fail streak, a 48 kHz source is not refused (control), a bad knob argument is refused
nowrun rateguard_refuse "screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 768000 1; mp3 1; @0.5; uistack" && {
    [ "$(grep -c '^APP play refused rate=768000 max=384000$' <<<"$out")" = 1 ] && grep -q '^APP skip reason=decode idx=7$' <<<"$out" || { EVID="$(grep -c 'refused' <<<"$out") refused"; fail rateguard_refused_skip; }
    [ "$(grep -c '^APP track idx=' <<<"$out")" = 2 ] && ! grep -q '^APP mp3 stop reason=failures' <<<"$out" || fail rateguard_next_plays
}
nowrun rateguard_streak "screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 768000 5; mp3 1; @1; simaudiorate 768000 x; simaudiorate 768000 -1" && {
    [ "$(grep -c '^APP play refused rate=768000 max=384000$' <<<"$out")" = 5 ] && grep -q '^APP mp3 stop reason=failures$' <<<"$out" || { EVID="$(grep -c 'refused' <<<"$out") refused"; fail rateguard_streak_stops; }
    [ "$(grep -c '^SIM audiorate bad$' <<<"$out")" = 2 ] || fail rateguard_knob_bad
}
nowrun rateguard_control "screen on; vol 0; @0.5; mp3ui; @0.3; simaudiorate 48000 1; mp3 1; @0.5" && {
    ! grep -q 'APP play refused' <<<"$out" && ! grep -q '^APP skip reason=decode' <<<"$out" && [ "$(grep -c '^APP track idx=' <<<"$out")" = 1 ] || fail rateguard_control_48k
}
# rateguard, source scan only where no behaviour can show it (the real audio_svc.c / bt_svc.c are not built into the simulator): every backend rate enters the audio task through src_rate()
# (no direct backend rate() call left in audio_svc.c), the block is never longer than the buffers (the pipeline block from s_rate, the source block through the halving stage and
# clamped to the source buffer), the rate switch checks its result, the Bluetooth sink never gets more than PC_SINK_MAX_BLOCK frames per call
[ "$(grep -c -- '->rate(' $AS)" = 0 ] && [ "$(grep -c 'rate = src_rate(' $AS)" = 3 ] && grep -q 'pc_mixer_rate_ok(rate)' $AS || fail rateguard_entry_points
grep -q 'int frames = pc_block_frames(s_rate)' $AS && grep -q 'P.halve ? pc_halve_in_frames(P.halve, frames) : frames;' $AS && grep -q 'if (!up && sf > SBUF_FRAMES) sf = SBUF_FRAMES' $AS && grep -q 'define MAX_BLOCK PC_AUDIO_MAX_BLOCK' $AS && grep -q 'esp_err_t e = bsp_audio_set_rate(rate)' $AS && grep -q 'APP audio rate fail' $AS || fail rateguard_block_and_switch
grep -q 'frames > PC_SINK_MAX_BLOCK' app/main/bt_svc_a2dp.c && grep -q 'pc_f32_to_s16_tpdf' app/main/bt_svc_a2dp.c && grep -q 'k += PC_SINK_MAX_BLOCK' $AS || fail rateguard_hp_sink
# hires T10b (H29), source scan only (bt_svc_a2dp.c is not built into the simulator): the polyphase table (about 26 ms on the board) is never built on the
# audio task. hp_sink, the audio task's sink, takes its table through pc_rsq_swap_take and never calls pc_rsq_init or allocates; the only pc_rsq_init
# call in bt_svc_a2dp.c is in the builder task (rsq_build_task, its own low-priority task); the swap rule itself is host-tested (test_rsq_swap)
BT=app/main/bt_svc_a2dp.c
HS=$(sed -n '/^void hp_sink(/,/^}/p' $BT)
{ [ -n "$HS" ] && grep -q 'pc_rsq_swap_take(&s_hp_rs.sw, rate, link, &rq)' <<<"$HS" && ! grep -q 'pc_rsq_init\|malloc\|calloc\|heap_caps' <<<"$HS" \
  && [ "$(grep -c 'pc_rsq_init(' $BT)" = 1 ] && sed -n '/^static void rsq_build_task(/,/^}/p' $BT | grep -q 'int err = pc_rsq_init(q, p.in, p.out);' \
  && grep -q 'xTaskCreatePinnedToCoreWithCaps(rsq_build_task, "rsq", ' $BT; } || fail hr10b_rsq_off_audio_task
# fix round 1, the look-ahead through the fake's sink model (fake_audio_svc.c runs hp_sink's and the builder's pc_rsq_swap rules each tick: the block
# first, then the builder, as on the board where a build takes longer than a block): a sink on a 48k link, a 44.1k track, the queued gapless next one at 32k.
# Its table is built from audio_svc_info().next_rate before the join, the join swaps with nothing dropped and no block waits for 32k.
nowrun hr10b_lookahead_gapless "vol 0; simsink 1; simaudiorate 44100 1; track 1; @1; simaudiorate 32000 1; @250" && {
    li=$(grep -n '^APP rsq init in=32000 out=48000 ' <<<"$out" | head -1 | cut -d: -f1)
    lj=$(grep -n '^APP track idx=.* gapless$' <<<"$out" | head -1 | cut -d: -f1)
    { [ -n "$li" ] && [ -n "$lj" ] && [ "$li" -lt "$lj" ] && grep -q '^APP rsq swap in=32000 out=48000 dropped=0$' <<<"$out" \
      && ! grep -q '^APP rsq wait in=32000' <<<"$out" && grep -q '^APP rsq init in=44100 out=48000 us=[0-9]* task=sim$' <<<"$out"; } \
      || { EVID="init32=$li join=$lj $(grep '^APP rsq\|^APP track' <<<"$out" | tr '\n' '|')"; fail hr10b_lookahead_gapless; }; }
# ... and the board's side of it (audio_svc.c is not built into the simulator): next_rate is published with the other info fields, at the queue (CMD_QUEUE)
# where s_next is set; the builder pairs it with the link through the sink plan (pc_rsq_hints -> pc_sink_pipe_rate, host-tested in test_pipe_rules),
# the same call in bt_svc_a2dp.c's builder and in the simulator's model (fix 2, review m4: no hand copy of the rule)
{ grep -q '.released = s_released, .next_rate = s_next ? s_next->rate : 0 };' $AS && sed -n '/case CMD_QUEUE: {/,/case CMD_OPENED: {/p' $AS | grep -A1 '        s_next = n;' | grep -q 'info_update();' \
  && sed -n '/^static void rsq_build_task(/,/^}/p' $BT | grep -q 'pc_rsq_hints(s_hp_rs.on, ai.src_rate, ai.pipe_rate, ai.next_rate, s_link_rate, &now, &next);' \
  && grep -q 'pc_rsq_hints(true, ai.src_rate, ai.pipe_rate, ai.next_rate, SIM_LINK, &now, &next);' sim/fake_audio_svc.c && grep -q 'out->next_rate = s_nxt.used' sim/fake_audio_svc.c; } || fail hr10b_next_rate_published

# hires Task 10c: a two-table cache and the sink start building the table before the first block. (a) a sink started over a playing 44.1k track on the
# 48k link: the table is built at the sink start ("rsq sink-start ... ready=1"), no block waits, the first swap drops nothing. (b) manual skips 44.1k -> 32k
# -> 44.1k -> 32k -> 44.1k with the sink on: only the first visit of each rate waits (one block in the sim, a build on the board); every later change is a
# pointer swap from the cache (dropped=0, no wait line). The fake runs the board's pc_rsq_swap rules (the same code); the board's hp_rsq_sink wait and
# the rsqdrop= status field are pinned below by text.
nowrun hr10c_sink_start_no_drop "vol 0; simaudiorate 44100 1; track 1; @1; simsink 1; @1" && {
    { grep -q '^APP rsq sink-start in=44100 out=48000 ready=1 ' <<<"$out" && ! grep -q '^APP rsq wait' <<<"$out" && grep -q '^APP rsq swap in=44100 out=48000 dropped=0$' <<<"$out"; } \
      || { EVID="$(grep '^APP rsq' <<<"$out" | tr '\n' '|')"; fail hr10c_sink_start_no_drop; }; }
nowrun hr10c_skip_cache "vol 0; simsink 1; simaudiorate 44100 1; track 1; @0.5; simaudiorate 32000 1; track 2; @0.5; simaudiorate 44100 1; track 1; @0.5; simaudiorate 32000 1; track 2; @0.5; simaudiorate 44100 1; track 1; @0.5" && {
    nw=$(grep -c '^APP rsq wait' <<<"$out"); ns=$(grep -c '^APP rsq swap ' <<<"$out"); nd=$(grep '^APP rsq swap ' <<<"$out" | grep -vc 'dropped=0$')
    { [ "$nw" -le 2 ] && [ "$ns" -ge 5 ] && [ "$nd" -le 2 ] && [ "$(grep -c '^APP rsq init ' <<<"$out")" = 2 ]; } \
      || { EVID="waits=$nw swaps=$ns nonzero=$nd $(grep '^APP rsq' <<<"$out" | tr '\n' '|')"; fail hr10c_skip_cache; }; }
# fix round 1: a FULL cache holding the wrong pair at the sink start (44.1k and 32k held; the sink off and on over a 22.05k source): no wait for a table nobody can
# hand back before a block exists (the board's LVGL task would block the whole 150 ms); the first block waits as in T10b
nowrun hr10c_full_cache_wrong_pair "vol 0; simsink 1; simaudiorate 44100 1; track 1; @0.5; simaudiorate 32000 1; track 2; @0.5; simsink 0; simaudiorate 22050 1; track 1; @0.5; simsink 1; @0.5" && {
    { grep -q '^APP rsq sink-start in=22050 out=48000 skipped (full cache)' <<<"$out" && ! grep -q '^APP rsq sink-start in=22050 out=48000 ready' <<<"$out"; } \
      || { EVID="$(grep '^APP rsq' <<<"$out" | tr '\n' '|')"; fail hr10c_full_cache_wrong_pair; }; }
{ sed -n '/^void hp_rsq_sink(/,/^}/p' $BT | grep -q 'pc_rsq_swap_has(&s_hp_rs.sw, now)' && sed -n '/^void hp_rsq_sink(/,/^}/p' $BT | grep -q 'pc_rsq_swap_spare_busy' && sed -n '/^void hp_rsq_sink(/,/^}/p' $BT | grep -q 'pc_rsq_hints(true, ' \
  && [ "$(cat $BT app/main/bt_svc_cast_tx.c | grep -c 'rsqdrop=%lu')" = 3 ] && sed -n '/^void hp_sink(/,/^}/p' $BT | grep -q 's_hp_rs.dropped_total++'; } || fail hr10c_board_wait_and_status

