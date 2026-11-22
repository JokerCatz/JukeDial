# SPDX-License-Identifier: LGPL-2.1-or-later
# --- CARDTEST: 記憶卡診斷 (app/main/card_test.c, ui/scr_cardtest.c, the base design notes): the settings row 「Card diagnostics」, the card facts, the raw read test (3 regions, 16 KB + 64 KB),
# the file test (two taps; /sdcard/_player/cardtest.tmp), the table of formats. The fake card (sim/fake_sd.c): "simsd speed <raw16> [<raw64> [<file write> [<file read>]]]" (MB/s x 100,
# default 350 800 400 380), "simsd free <bytes>"; "cardtest raw|file" starts a test (page on top: its timer runs it), "cardtest tap raw|file" clicks the page's rows, "cardtest page" prints its texts,
# "cardtest show" the last results (APP cardtest ... lines, the verdict rows too).
ctv() { grep "^APP cardtest verdict $1=" <<<"$out" | head -1 | sed 's/.*=\([a-z]*\) need=.*/\1/'; }   # the first verdict of that format
# the sim has no icon font: the leading verdict / arrow icons of the rows are not in this text (the verdicts are the 'cardtest verdict' lines, ctv)
rungpos() { grep "^APP cardtest page rung" <<<"$out" | grep -n "rung\* This card $1" | cut -d: -f1; }   # a marker's place among the ladder rows (1 = top); $1: "raw" or "file read-back"
OPEN="setui; @0.3; settings key cardtest; @0.3"
fresh
out=$($SIM --headless --root "$T/fix" --cmd "setui; @0.3; settings group card; settings rows; $OPEN; uitop; cardtest page" 2>&1)
{ grep -qx 'APP settings row=Card diagnostics' <<<"$out" && grep -qx 'APP uitop cardtest' <<<"$out" && grep -qx 'APP cardtest page Capacity: 31.9 GB' <<<"$out" && grep -qx 'APP cardtest page Free: 12.0 GB' <<<"$out" \
  && grep -qx 'APP cardtest page File system: FAT32' <<<"$out" && grep -qx 'APP cardtest page Card: SIMSD' <<<"$out" && grep -qx 'APP cardtest page Bus clock: 40 MHz' <<<"$out" \
  && grep -qx 'APP cardtest page rung\* This card raw: not tested yet' <<<"$out" && grep -qx 'APP cardtest page rung\* This card file read-back: not tested yet' <<<"$out"; } || { EVID="$(grep '^APP cardtest page\|^APP uitop' <<<"$out" | tr '\n' '|' | cut -c1-300)"; fail cardtest_page_info; }
# a fast card (3.5 MB/s: the FakePod's): everything but the 384 kHz WAV is fine; the result is on the page; nothing was written
fresh
out=$($SIM --headless --root "$T/fix" --cmd "$OPEN; cardtest raw; @6; cardtest show; cardtest page" 2>&1)
{ grep -qx 'APP cardtest raw avg16=3.50 slowest_region=3.50 max64=8.00 slowest_read_ms=4.6' <<<"$out" && [ "$(grep -c '^APP cardtest raw region=' <<<"$out")" = 6 ] \
  && [ "$(ctv 'FLAC 384k/24')" = yes ] && [ "$(ctv 'WAV 384k/32')" = marginal ] && [ "$(ctv 'MP3 / AAC / Opus 320k')" = yes ] && grep -qx 'APP cardtest basis=raw speed=3.50' <<<"$out" \
  && grep -qx 'APP cardtest page 16 KB reads: 3.50 MB/s (slowest region 3.50)' <<<"$out" && grep -qx 'APP cardtest page rung FLAC 384k/24  needs 2.60 MB/s' <<<"$out" && grep -qx 'APP cardtest page rung\* This card raw 3.50 MB/s' <<<"$out" && [ "$(rungpos raw)" = 2 ] && [ "$(rungpos file)" = 9 ] \
  && grep -qx 'APP cardtest page Card maximum (64 KB): 8.00 MB/s' <<<"$out" && [ ! -e "$T/fix/_player/cardtest.tmp" ]; } || { EVID="$(grep '^APP cardtest raw\|^APP cardtest basis' <<<"$out" | tr '\n' '|' | cut -c1-300)"; fail cardtest_raw_fast; }
# the three regions: the start, the middle, near the end of the card (4 MB aligned, 16 MB before the end), all inside the card
S0=$(grep '^APP cardtest raw region=0 ' <<<"$out" | sed 's/.*sector=\([0-9]*\).*/\1/' | head -1); S1=$(grep '^APP cardtest raw region=1 ' <<<"$out" | sed 's/.*sector=\([0-9]*\).*/\1/' | head -1); S2=$(grep '^APP cardtest raw region=2 ' <<<"$out" | sed 's/.*sector=\([0-9]*\).*/\1/' | head -1)
SECTORS=$((31914983424 / 512))
{ [ "$S0" = 0 ] && [ "$S1" -gt $((SECTORS / 2 - 8192)) ] && [ "$S1" -le $((SECTORS / 2)) ] && [ "$S2" -gt $((SECTORS - 40000)) ] && [ "$S2" -lt $((SECTORS - 20000)) ] && [ $((S1 % 8192)) = 0 ] && [ $((S2 % 8192)) = 0 ]; } \
  || { EVID="$S0 $S1 $S2 of $SECTORS"; fail cardtest_raw_regions; }
# a slow card (1.5 MB/s): the table follows; 384k FLAC does not fit, 192k FLAC does, 192k WAV is marginal
fresh
out=$($SIM --headless --root "$T/fix" --cmd "simsd speed 150 400; $OPEN; cardtest raw; @14; cardtest show; cardtest page" 2>&1)
{ grep -qx 'APP cardtest raw avg16=1.50 slowest_region=1.50 max64=4.00 slowest_read_ms=10.9' <<<"$out" && [ "$(ctv 'FLAC 384k/24')" = no ] && [ "$(ctv 'FLAC 192k/24')" = yes ] && [ "$(ctv 'WAV 192k/24')" = marginal ] \
  && [ "$(ctv 'WAV 384k/32')" = no ] && grep -qx 'APP cardtest page rung FLAC 384k/24  needs 2.60 MB/s' <<<"$out" && [ "$(rungpos raw)" = 4 ]; } || { EVID="$(grep '^APP cardtest raw avg\|^APP cardtest verdict' <<<"$out" | tr '\n' '|' | cut -c1-400)"; fail cardtest_raw_slow; }
# a raw read past the end fails the test, not the player: a card of 40 MB is too small
fresh
out=$($SIM --headless --root "$T/fix" --cmd "simsd size 41943040; cardtest raw; cardtest show" 2>&1)
grep -qx 'APP cardtest raw refused fail=2' <<<"$out" || { EVID="$(grep '^APP cardtest' <<<"$out" | tr '\n' '|')"; fail cardtest_raw_small; }
# the file test is refused without the second tap: one tap changes the row's text only, nothing is written or created
fresh
out=$($SIM --headless --root "$T/fix" --cmd "$OPEN; cardtest tap file; @1; cardtest show; cardtest page" 2>&1)
{ ! grep -q '^APP cardtest file write=' <<<"$out" && [ ! -e "$T/fix/_player/cardtest.tmp" ] && grep -qx 'APP cardtest page Not run yet' <<<"$out"; } || { EVID="$(grep '^APP cardtest' <<<"$out" | tr '\n' '|' | cut -c1-300)"; fail cardtest_file_needs_confirm; }
# a leftover temp file goes at the start of a test; the file exists while the test runs and is gone after it; the result: write, read-back, verify
fresh
head -c 100000 /dev/zero > "$T/fix/_player/cardtest.tmp"
out=$($SIM --headless --root "$T/fix" --cmd "$OPEN; cardtest tap file; cardtest tap file; @0.2; cardtest tmp" 2>&1)
SZ=$(grep '^APP cardtest tmp' <<<"$out" | sed 's/.*size=//; s/.*none/none/')
{ [ "$SZ" != none ] && [ "$SZ" != 100000 ] && [ "$SZ" -gt 0 ] && [ "$SZ" -le 4194304 ]; } || { EVID="size $SZ"; fail cardtest_file_created; }
fresh
out=$($SIM --headless --root "$T/fix" --cmd "simsd speed 350 800 400 300; $OPEN; cardtest tap file; cardtest tap file; @10; cardtest tmp; cardtest show; cardtest page; cardtest raw; @6; cardtest page" 2>&1)
{ grep -qx 'APP cardtest file write=4.00 read=3.00 verify=ok bytes=4194304' <<<"$out" && grep -qx 'APP cardtest tmp none' <<<"$out" && grep -qx 'APP cardtest page File write: 4.00 MB/s' <<<"$out" \
  && grep -qx 'APP cardtest page Data check: OK' <<<"$out" && grep -qx 'APP cardtest page File read-back: 3.00 MB/s' <<<"$out"; } || { EVID="$(grep '^APP cardtest file' <<<"$out" | tr '\n' '|') $(ls "$T/fix/_player")"; fail cardtest_file_runs_and_deletes; }
# the table follows the file read-back once a file test ran (even with a faster raw read); the earlier file result stays on the page after a raw test
{ grep -qx 'APP cardtest basis=file speed=3.00' <<<"$out" && ! grep -q '^APP cardtest basis=raw' <<<"$out" && grep -c '^APP cardtest page File write: 4.00 MB/s' <<<"$out" | grep -qx 2; } \
  || { EVID="$(grep '^APP cardtest basis' <<<"$out" | tr '\n' '|')"; fail cardtest_basis_file_wins; }
# no room: the file test is refused with its reason, nothing is created
fresh
out=$($SIM --headless --root "$T/fix" --cmd "simsd free 5000000; $OPEN; cardtest tap file; cardtest tap file; @1; cardtest page" 2>&1)
{ grep -qx 'APP cardtest page Less than 8 MB free, the file test cannot run' <<<"$out" && [ ! -e "$T/fix/_player/cardtest.tmp" ]; } || { EVID="$(grep '^APP cardtest' <<<"$out" | tr '\n' '|' | cut -c1-300)"; fail cardtest_file_no_room; }
# leaving the page cancels the test and removes the temp file; no result stays
fresh
out=$($SIM --headless --root "$T/fix" --cmd "$OPEN; cardtest tap file; cardtest tap file; @0.4; back; @0.3; uitop; cardtest show" 2>&1)
{ [ ! -e "$T/fix/_player/cardtest.tmp" ] && ! grep -q '^APP cardtest file write=' <<<"$out" && grep -qx 'APP uitop setgroup' <<<"$out"; } || { EVID="$(grep '^APP cardtest\|^APP uitop' <<<"$out" | tr '\n' '|' | cut -c1-300)"; fail cardtest_cancel_on_leave; }
# playback stops when a test starts
fresh
out=$($SIM --headless --root "$T/fix" --cmd "vol 0; track 1; @1; cardfmt; cardtest raw; @0.1; cardfmt" 2>&1)
{ [ "$(grep '^APP cardfmt view=' <<<"$out" | sed -n 1p | grep -o 'playing=[0-9]')" = playing=1 ] && [ "$(grep '^APP cardfmt view=' <<<"$out" | sed -n 2p | grep -o 'playing=[0-9]')" = playing=0 ]; } || { EVID="$(grep '^APP cardfmt view=' <<<"$out" | tr '\n' '|' | cut -c1-300)"; fail cardtest_stops_playback; }
# the result is saved on the card (one line per mode, the card's serial and size, the time "-" while the clock is not set) and shown as 「Last run」 after the page is opened again
fresh
$SIM --headless --root "$T/fix" --cmd "cardtest raw; cardtest file" >/dev/null 2>&1
out=$($SIM --headless --root "$T/fix" --cmd "$OPEN; cardtest page; cardtest show" 2>&1)
{ grep -q '^raw serial=00005D01 size=31914983424 time=[-0-9T:]* avg16=350 slow16=350 max64=800 worst_us=4681 reg0=350 reg1=350 reg2=350$' "$T/fix/_player/cardtest.txt" && grep -q '^file serial=00005D01 size=31914983424 time=[-0-9T:]* write=400 read=380 verify=1$' "$T/fix/_player/cardtest.txt" \
  && [ "$(wc -l < "$T/fix/_player/cardtest.txt")" = 2 ] && [ ! -e "$T/fix/_player/cardtest.tmp" ] && grep -q '^APP cardtest page Last run: ' <<<"$out" && [ "$(grep -c '^APP cardtest page Last run: ' <<<"$out")" = 2 ] \
  && grep -qx 'APP cardtest page 16 KB reads: 3.50 MB/s (slowest region 3.50)' <<<"$out" && grep -qx 'APP cardtest page File read-back: 3.80 MB/s' <<<"$out" && grep -qx 'APP cardtest page rung\* This card raw 3.50 MB/s (last run)' <<<"$out" && grep -qx 'APP cardtest page rung\* This card file read-back 3.80 MB/s (last run)' <<<"$out" \
  && grep -q '^APP cardtest raw avg16=3.50 .* saved=1$' <<<"$out"; } || { EVID="$(cat "$T/fix/_player/cardtest.txt" | tr '\n' '|') $(grep '^APP cardtest page' <<<"$out" | tr '\n' '|' | cut -c1-300)"; fail cardtest_saved_shown_again; }
# a run again replaces the line of its mode (still one line per mode); the numbers are the new ones
$SIM --headless --root "$T/fix" --cmd "simsd speed 200 500; cardtest raw" >/dev/null 2>&1
{ [ "$(wc -l < "$T/fix/_player/cardtest.txt")" = 2 ] && grep -q '^raw .* avg16=200 ' "$T/fix/_player/cardtest.txt" && grep -q '^file ' "$T/fix/_player/cardtest.txt"; } || { EVID="$(cat "$T/fix/_player/cardtest.txt" | tr '\n' '|')"; fail cardtest_saved_replaced; }
# another card (another CID serial): its file is ignored, the page shows nothing from it; a run on it drops the first card's lines
out=$($SIM --headless --root "$T/fix" --cmd "simsd swap; $OPEN; cardtest page" 2>&1)
{ ! grep -q '^APP cardtest page Last run' <<<"$out" && grep -qx 'APP cardtest page rung\* This card raw: not tested yet' <<<"$out" && grep -qx 'APP cardtest page rung\* This card file read-back: not tested yet' <<<"$out" && [ "$(grep -c '^APP cardtest page Not run yet' <<<"$out")" = 2 ]; } || { EVID="$(grep '^APP cardtest page' <<<"$out" | tr '\n' '|' | cut -c1-300)"; fail cardtest_other_card_ignored; }
$SIM --headless --root "$T/fix" --cmd "simsd swap; cardtest file" >/dev/null 2>&1
{ [ "$(wc -l < "$T/fix/_player/cardtest.txt")" = 1 ] && grep -q '^file serial=00005D02 ' "$T/fix/_player/cardtest.txt"; } || { EVID="$(cat "$T/fix/_player/cardtest.txt" | tr '\n' '|')"; fail cardtest_other_card_replaces_file; }
# a ladder with no test yet: every format with what it needs, heaviest first, the marker last; the saved one is not there on a card without a file
fresh
out=$($SIM --headless --root "$T/fix" --cmd "$OPEN; cardtest page" 2>&1)
{ [ "$(grep '^APP cardtest page rung' <<<"$out" | head -1)" = 'APP cardtest page rung WAV 384k/32  needs 4.61 MB/s' ] && [ "$(rungpos raw)" = 8 ] && [ "$(rungpos file)" = 9 ] && grep -qx 'APP cardtest page rung MP3 / AAC / Opus 320k  needs 0.06 MB/s' <<<"$out"; } || { EVID="$(grep '^APP cardtest page rung' <<<"$out" | tr '\n' '|' | cut -c1-400)"; fail cardtest_ladder_untested; }
# a result that cannot be saved (the card is full of a folder where the file goes): the page says so in one line, the numbers still show
fresh
mkdir -p "$T/fix/_player/cardtest.txt"
out=$($SIM --headless --root "$T/fix" --cmd "$OPEN; cardtest raw; @8; cardtest page" 2>&1)
{ grep -qx 'APP cardtest page Done, but the result could not be saved on the card' <<<"$out" && grep -qx 'APP cardtest page 16 KB reads: 3.50 MB/s (slowest region 3.50)' <<<"$out"; } || { EVID="$(grep '^APP cardtest' <<<"$out" | tr '\n' '|' | cut -c1-300)"; fail cardtest_save_failure_said; }
# how the numbers are derived: six plain lines at the end of the ladder, the values printed from the pure function's own constants (1.5 x, 75 %, 75 %); the same in the help page
fresh
out=$($SIM --headless --root "$T/fix" --cmd "$OPEN; cardtest page; help open cardtest; @0.3; help show" 2>&1)
SAFE=$(sed -n 's/^#define PC_CT_SAFETY_X100 *\([0-9]*\).*/\1/p' components/player_core/include/pc/pc_cardtest.h); FR=$(sed -n 's/^#define PC_CT_FLAC_RATIO_PCT *\([0-9]*\).*/\1/p' components/player_core/include/pc/pc_cardtest.h); MG=$(sed -n 's/^#define PC_CT_MARGINAL_PCT *\([0-9]*\).*/\1/p' components/player_core/include/pc/pc_cardtest.h)
SAFE_TXT=$(python3 -I -c "import sys; v=int(sys.argv[1]); print(('%g' % (v / 100)))" "$SAFE")
{ [ "$(grep -c '^APP cardtest page note ' <<<"$out")" = 8 ] && grep -qx 'APP cardtest page note The marks follow the file read-back marker; without a file test, the raw one' <<<"$out" && grep -qx "APP cardtest page note Minimum = the format's data per second x $SAFE_TXT (margin)" <<<"$out" \
  && grep -qx "APP cardtest page note FLAC is counted at $FR% of uncompressed; real files are usually smaller" <<<"$out" && grep -qx "APP cardtest page note Marginal = at least $MG% of the minimum: may play, may stutter" <<<"$out" \
  && grep -qx 'APP explain id=cardtest title=Card diagnostics paras=7 qr=0 web=0' <<<"$out" && grep -q '^APP cardtest page note Raw read measures the card itself' <<<"$out" && grep -q '^APP cardtest page note The limits were calibrated on one card only' <<<"$out" && grep -q '^APP cardtest page note Only three regions are read' <<<"$out" && grep -q '^APP cardtest page note The raw marker uses the slowest region' <<<"$out"; } \
  || { EVID="$SAFE $FR $MG | $(grep '^APP cardtest page note' <<<"$out" | tr '\n' '|' | cut -c1-300)"; fail cardtest_disclosure_rows; }
# two markers, each at its own place; the marks follow the file read-back (raw 3.50 passes 384k FLAC, file read-back 1.50 does not)
fresh
out=$($SIM --headless --root "$T/fix" --cmd "simsd speed 350 800 400 150; $OPEN; cardtest raw; @6; cardtest file; @10; cardtest page" 2>&1)
{ [ "$(rungpos raw)" = 2 ] && [ "$(rungpos 'file read-back')" = 5 ] && grep -qx 'APP cardtest page rung FLAC 384k/24  needs 2.60 MB/s' <<<"$out" && grep -qx 'APP cardtest page rung WAV 192k/24  needs 1.73 MB/s' <<<"$out" \
  && grep -qx 'APP cardtest page rung FLAC 192k/24  needs 1.30 MB/s' <<<"$out" && grep -qx 'APP cardtest page rung\* This card file read-back 1.50 MB/s' <<<"$out"; } || { EVID="$(grep '^APP cardtest page rung' <<<"$out" | tr '\n' '|' | cut -c1-500)"; fail cardtest_two_markers_follow_file; }
# the file one faster than the raw one: its marker is above the raw marker and the marks follow it
fresh
out=$($SIM --headless --root "$T/fix" --cmd "simsd speed 150 400 400 350; $OPEN; cardtest raw; @14; cardtest file; @10; cardtest page" 2>&1)
{ [ "$(rungpos 'file read-back')" -lt "$(rungpos raw)" ] && grep -qx 'APP cardtest page rung FLAC 384k/24  needs 2.60 MB/s' <<<"$out"; } || { EVID="$(grep '^APP cardtest page rung' <<<"$out" | tr '\n' '|' | cut -c1-500)"; fail cardtest_file_marker_above_raw; }
# the page explains the conservative raw verdict (night2): three regions on their own row, the verdict is the slowest, a disclosure line; saved and shown again after the page is reopened
fresh
out=$($SIM --headless --root "$T/fix" --cmd "simsd region 300 900 1100; $OPEN; cardtest raw; @8; cardtest page; cardtest show" 2>&1)
{ grep -qx 'APP cardtest page Start / mid / end: 3.00 / 9.00 / 11.00 MB/s' <<<"$out" && grep -qx 'APP cardtest page 16 KB reads: 5.60 MB/s (slowest region 3.00)' <<<"$out" \
  && grep -qx 'APP cardtest page note The raw marker uses the slowest region; where your files actually sit decides, the file test shows that; each region is read twice and the second pass counts (the first wakes the card)' <<<"$out" && grep -qx 'APP cardtest basis=raw speed=3.00' <<<"$out" \
  && [ "$(ctv 'WAV 384k/32')" = no ]; } || { EVID="$(grep '^APP cardtest page Regions\|^APP cardtest page 16\|^APP cardtest basis' <<<"$out" | tr '\n' '|' | cut -c1-300)"; fail cardtest_regions_shown; }
out=$($SIM --headless --root "$T/fix" --cmd "$OPEN; cardtest page" 2>&1)
{ grep -q '^raw serial=00005D01 .* reg0=300 reg1=900 reg2=1100$' "$T/fix/_player/cardtest.txt" && grep -qx 'APP cardtest page Start / mid / end: 3.00 / 9.00 / 11.00 MB/s' <<<"$out"; } || { EVID="$(cat "$T/fix/_player/cardtest.txt" | tr '\n' '|' | cut -c1-300)"; fail cardtest_regions_saved; }
