# SPDX-License-Identifier: LGPL-2.1-or-later
# --- ideas G4: 猜歌 / Guess the song (app/main/ui/scr_game_guess.c on ui_sectors; rules in components/player_core/src/pc_game_guess.c); sourced by sim/test.sh ---
# The page: the HEAD row (questions 5 / 10 / 20 at 119,68; the rule at 240,68 opens the game's ring), four sectors with candidate titles round the hub (開始 / 重聽), the progress ring.
# Clips go through mp3_ctl_play_clip (the fake audio plays any file for 180 s unless `simaudiolen`/`simaudiofail` say otherwise; `APP mp3 clip` shows the converted start).
# Console: `games right|wrong` tap the right / another sector, `games state`, `games golden 1|0|auto`, `games clip <track> <pct>`, `games count <n>`; the page prints "APP game guess ask|clip|answer|note|restore|over|blocked ...".
gs_f() { sed 's/.* '"$1"'=\([^ ]*\).*/\1/' <<<"$2"; }
gs_play() { local s="" i; for i in $(seq "$1"); do s+="games right; @1.5; "; done; printf '%s' "$s"; }     # $1 questions, every answer right (each reveal is 1.4 s)

# open: ready, the hub says Start, four normal sectors, no clip, nothing noted
nowrun guess_open "screen on; vol 0; games open guess; @0.3; uitop; sectors; games state; mp3cur; frame hold; uiboxes" && {
    d=$(grep '^APP sectors n=' <<<"$out" | tail -1)
    [ "$(tp)" = "tools_game_guess " ] && [ "$(gs_f n "$d")" = 4 ] && [ "$(gs_f hub "$d")" = Start ] && [ "$(gs_f progress "$d")" = 0/10 ] && [ "$(gs_f en "$d")" = 0 ] \
      && grep -qx 'APP game guess state phase=0 asked=0 correct=0 streak=0 points=0 taken=0 had=0' <<<"$out" && ! grep -q '^APP game guess clip' <<<"$out" || { EVID="$(tp) $d"; fail guess_open; }
    grep -q '^APP uibox label .*|Tap to start$' <<<"$out" && grep -q '^APP uibox button .*|10 Qs$' <<<"$out" && grep -q '^APP uibox button .*|Classic$' <<<"$out" || { EVID="$(grep '^APP uibox' <<<"$out" | tr '\n' '|')"; fail guess_open_head; }; }
# a question: a clip of the target track through the player (APP track = the target), four different labels, the right one among them; the sectors take taps; the clip ends after 5 s (the player stops)
nowrun guess_question "screen on; vol 0; games open guess; @0.3; sectors hub; @0.2; games state; mp3cur; @5; games state; mp3cur; sectors" && {
    a=$(grep '^APP game guess ask ' <<<"$out"); t=$(gs_f target "$a"); l=$(gs_f labels "$a")
    [ "$(tr -cd '|' <<<"$l" | wc -c)" = 3 ] && [ "$(sort -u < <(tr '|' '\n' <<<"$l") | wc -l)" = 4 ] && grep -q "^APP game guess clip idx=$t pct=[0-9]* ms=5000 ok=1" <<<"$out" && grep -q "^APP track idx=$t " <<<"$out" || { EVID="$a"; fail guess_question; }
    [ "$(grep '^APP game guess state' <<<"$out" | sed -n 1p | sed 's/ points.*//')" = "APP game guess state phase=1 asked=1 correct=0 streak=0" ] && [ "$(grep '^APP mp3 cur=' <<<"$out" | sed -n 1p | cut -d' ' -f3)" = "cur=$t" ] \
      && [ "$(grep '^APP game guess state' <<<"$out" | sed -n 2p | cut -d' ' -f5)" = phase=2 ] && [ "$(grep '^APP mp3 cur=' <<<"$out" | sed -n 2p | cut -d' ' -f3)" != "cur=$t" ] || { EVID="$(grep '^APP game guess state\|^APP mp3 cur' <<<"$out" | tr '\n' '|')"; fail guess_clip_ends; }
    [ "$(gs_f en "$(grep '^APP sectors n=' <<<"$out" | tail -1)")" = 1 ] || fail guess_sectors_enabled_after_clip; }
# the song that was playing: noted at the first clip, the history and the recent list never grow (hist=1 recent=1 as before), put back where it was (resume at its byte) when Back leaves
nowrun guess_restore "screen on; vol 0; track 3; @3; mp3cur; games open guess; @0.3; sectors hub; @0.2; games right; @1.5; games wrong; @1.5; mp3cur; back; @0.6; mp3cur" && {
    n=$(grep '^APP game guess note ' <<<"$out"); b=$(gs_f byte "$n")
    [ "$(gs_f idx "$n")" = 3 ] && grep -qx "APP game guess restore idx=3 byte=$b paused=0" <<<"$out" && grep -q "^APP mp3 resume at byte $b of " <<<"$out" || { EVID="$n $(grep 'restore\|resume' <<<"$out" | tr '\n' '|')"; fail guess_restore_song; }
    [ "$(grep '^APP mp3 cur=' <<<"$out" | grep -c ' hist=1 back=0 recent=1 ')" = 3 ] && [ "$(grep '^APP mp3 cur=' <<<"$out" | tail -1 | cut -d' ' -f3)" = cur=3 ] || { EVID="$(grep '^APP mp3 cur' <<<"$out" | tr '\n' '|')"; fail guess_restore_hist; }; }
# nothing was playing: Back stops the last clip's player; nothing is restarted
nowrun guess_restore_idle "screen on; vol 0; games open guess; @0.3; sectors hub; @0.2; games right; @0.3; mp3cur; back; @0.6; mp3cur" && {
    grep -qx 'APP game guess restore idle' <<<"$out" && [ "$(grep '^APP mp3 cur=' <<<"$out" | tail -1 | cut -d' ' -f3)" = cur=-1 ] || { EVID="$(grep 'restore\|mp3 cur' <<<"$out" | tr '\n' '|')"; fail guess_restore_idle; }; }
# the clip start is the player's conversion per backend (APP mp3 clip idx= pct= kind= at=): the page asks for a percentage, never a byte. The fixture's mp3 files are tiny (below 64 KB: from
# their start, kind 1 = bytes); the replay (hub) starts the same clip again at the same percentage
nowrun guess_clip_start_replay "screen on; vol 0; games open guess; @0.3; sectors hub; @1; sectors hub; @1; sectors hub; @0.3; back; @0.5" && {
    c=$(grep '^APP game guess clip idx=' <<<"$out"); m=$(grep '^APP mp3 clip idx=' <<<"$out")
    [ "$(wc -l <<<"$c")" = 3 ] && [ "$(sed 's/.* pct=\([0-9]*\) .*/\1/' <<<"$c" | sort -u | wc -l)" = 1 ] && [ "$(sed 's/.* pct=\([0-9]*\) .*/\1/' <<<"$c" | head -1)" -ge 10 ] && [ "$(wc -l <<<"$m")" = 3 ] \
      && ! grep -qv 'kind=1 at=0$' <<<"$m" && [ "$(grep -c '^APP game guess ask ' <<<"$out")" = 1 ] || { EVID="$c $m"; fail guess_clip_start_replay; }; }
# a FLAC candidate: its start is a PCM frame index from STREAMINFO (30 % of 26 460 000 frames = 7 938 000), never a byte of the file; a track of another backend with no seek (OGG) starts at its start
gs_fresh() { fresh; mkdir -p "$T/fix/export/zz_flac"; python3 -c "
import sys,struct
t=26460000
h=b'fLaC'+bytes([0,0,0,34])+bytes(10)+bytes([0x0A,0xC4,0x42,0xF0|((t>>32)&15)])+struct.pack('>I',t&0xFFFFFFFF)
open(sys.argv[1],'wb').write(h+bytes(200))" "$T/fix/export/zz_flac/01_big.flac"; cp "$T/fix/export/zz_flac/01_big.flac" "$T/fix/export/zz_flac/02_small.ogg"; }
NOWFRESH=gs_fresh nowrun guess_clip_flac "screen on; vol 0; scan; @1; games open guess; @0.3; sectors hub; @0.2; games clip 0 30; mp3cur; games clip 1 30; mp3cur; games clip 3 30; mp3cur; back; @0.5" && {
    grep -qx 'APP mp3 clip idx=0 pct=30 kind=2 at=7938000' <<<"$out" && grep -qx 'APP mp3 clip idx=1 pct=30 kind=0 at=0' <<<"$out" && grep -qx 'APP mp3 clip idx=3 pct=30 kind=1 at=0' <<<"$out" \
      && [ "$(grep '^APP mp3 cur=' <<<"$out" | grep -c ' hist=0 back=0 recent=0 ')" = 3 ] || { EVID="$(grep 'mp3 clip\|mp3 cur\|library loaded' <<<"$out" | tr '\n' '|')"; fail guess_clip_flac; }; }
# a clip that will not open (the audio task reports AUD_ERROR): the player does NOT skip on to another song (no APP skip, no history / recent / count), the page takes the question back and asks
# another track; after four failures in a row the page says so and puts the song back
nowrun guess_clip_open_fails "screen on; vol 0; track 3; @3; games open guess; @0.3; simaudiofail 1; sectors hub; @0.5; games state; mp3cur; back; @0.6; mp3cur" && {
    grep -q '^APP game guess clip failed fails=1' <<<"$out" && ! grep -q '^APP skip reason=decode' <<<"$out" && [ "$(grep -c '^APP game guess ask ' <<<"$out")" = 2 ] && grep -q 'state phase=1 asked=1 ' <<<"$out" \
      && [ "$(grep '^APP mp3 cur=' <<<"$out" | grep -c ' hist=1 back=0 recent=1 ')" = 2 ] || { EVID="$(grep 'guess\|skip\|mp3 cur' <<<"$out" | tr '\n' '|')"; fail guess_clip_open_fails; }; }
nowrun guess_clip_open_fails_all "screen on; vol 0; track 3; @3; games open guess; @0.3; simaudiofail 4; sectors hub; @1.5; games state; back; @0.6; mp3cur" && {
    grep -q '^APP game guess blocked reason=These songs will not play' <<<"$out" && grep -q 'restore idx=3 ' <<<"$out" && [ "$(grep -c 'clip failed fails=' <<<"$out")" -ge 4 ] && ! grep -q '^APP skip' <<<"$out" || { EVID="$(grep 'guess\|skip' <<<"$out" | tr '\n' '|')"; fail guess_clip_open_fails_all; }; }
# a song that ends inside the clip (a 1.2 s file): the clip ends there (AUD_ENDED), no next song starts (no APP track after it, history and recent unchanged), the page waits for the answer
nowrun guess_clip_short_file "screen on; vol 0; track 3; @3; games open guess; @0.3; simaudiolen 1200 1; sectors hub; @2; games state; mp3cur; games right; @0.3; back; @0.6; mp3cur" && {
    grep -q '^APP mp3 clip ended' <<<"$out" && [ "$(grep -c '^APP track idx=' <<<"$out")" = 3 ] && grep -qx 'APP game guess state phase=2 asked=1 correct=0 streak=0 points=0 taken=1 had=1' <<<"$out" \
      && [ "$(grep '^APP mp3 cur=' <<<"$out" | sed -n 1p | cut -d' ' -f3)" = cur=-1 ] && grep -q 'result=1 streak=1' <<<"$out" || { EVID="$(grep 'guess\|APP track\|mp3 clip\|mp3 cur' <<<"$out" | cut -c1-110 | tr '\n' '|')"; fail guess_clip_short_file; }; }
# a rescan in the middle of a question swaps the library: the game ends (the song is put back), the answer taps do nothing on the old track numbers
nowrun guess_rescan_mid_question "screen on; vol 0; track 3; @3; games open guess; @0.3; sectors hub; @0.5; scan; @1; games state; games right; games state; sectors hub; @0.3; games state; back; @0.6; mp3cur" && {
    grep -q '^APP game guess library changed' <<<"$out" && grep -q '^APP game guess restore idx=3 ' <<<"$out" && grep -q 'state phase=0 asked=0 ' <<<"$out" && ! grep -q '^APP game guess answer=' <<<"$out" \
      && grep -q '^APP game guess start ' <<<"$out" || { EVID="$(grep 'guess\|library' <<<"$out" | cut -c1-120 | tr '\n' '|')"; fail guess_rescan_mid_question; }; }
# answers: right = +100 (combo x1), the streak and the points follow; wrong = the streak resets, the status names the right title; taps during the reveal do nothing
nowrun guess_answers "screen on; vol 0; games open guess; @0.3; games golden 0; sectors hub; @0.2; games right; games wrong; @0.1; games state; @1.5; games right; @1.5; games right; @1.5; games wrong; @1.5; games state; back; @0.5" && {
    grep -q '^APP game guess answer=[0-3] right=[0-3] result=1 streak=1 points=100$' <<<"$out" && grep -q 'result=1 streak=2 points=200$' <<<"$out" && grep -q 'result=2 streak=0 points=' <<<"$out" \
      && [ "$(grep -c '^APP game guess answer=' <<<"$out")" = 4 ] && [ "$(grep -c '^APP game guess wrong=-1$' <<<"$out")" = 1 ] || { EVID="$(grep '^APP game guess answer\|wrong=' <<<"$out" | tr '\n' '|')"; fail guess_answers; }; }
# a whole 5-question game, every answer right: 100 100 200 200 300 = 900, best streak 5, the card (score 900), the song put back, again starts a new game
nowrun guess_game "screen on; vol 0; track 2; @2; games open guess; @0.3; games golden 0; games mode 0; sectors hub; @0.2; $(gs_play 5)@0.8; games result; mp3cur; games result tap; @0.5; games state; back; @0.5" && {
    grep -qx 'APP game guess over correct=5 rounds=5 points=900 streak=5 best_streak=5' <<<"$out" && grep -q '^APP game_result up=1 shown=1 title=Right 5/5  Streak 5 moves=900 ' <<<"$out" \
      && grep -q '^APP game guess restore idx=2 ' <<<"$out" && [ "$(grep -c '^APP game guess start ' <<<"$out")" = 2 ] || { EVID="$(grep '^APP game guess over\|^APP game_result\|restore' <<<"$out" | tr '\n' '|')"; fail guess_game_card; }
    grep -q 'sound=win' <<<"$out" && grep -q '^APP game guess state phase=1 asked=1 ' <<<"$out" || { EVID="$(grep 'sound=\|guess state' <<<"$out" | tr '\n' '|')"; fail guess_game_again; }; }
# the golden question doubles (a forced one: 200 for the first right answer), its sound is the sparkle
nowrun guess_golden "screen on; vol 0; sfx; games open guess; @0.3; games golden 1; sectors hub; @0.2; games right; @0.3; sfx; back; @0.5" && {
    grep -q '^APP game guess ask .* golden=1 ' <<<"$out" && grep -q 'result=1 streak=1 points=200$' <<<"$out" && grep -q '^APP sfx .*sparkle' <<<"$out" || { EVID="$(grep 'ask\|answer=\|sfx' <<<"$out" | tr '\n' '|')"; fail guess_golden; }; }
# the rules: 短 = a 2 s clip (the player stops after 2 s), 藝人 = artists as the answers (the fixture's artists are shared by albums: enough different ones or the page says why)
nowrun guess_rule_short "screen on; vol 0; games open guess; @0.3; games rule 1; sectors hub; @2.2; games state; mp3cur; back; @0.5" && {
    grep -q '^APP game guess clip .* ms=2000 ok=1' <<<"$out" && grep -qx 'APP game guess state phase=2 asked=1 correct=0 streak=0 points=0 taken=1 had=0' <<<"$out" && [ "$(grep '^APP mp3 cur=' <<<"$out" | tail -1 | cut -d' ' -f3)" = cur=-1 ] || { EVID="$(grep 'clip\|state' <<<"$out" | tr '\n' '|')"; fail guess_rule_short; }; }
nowrun guess_rule_artist "screen on; vol 0; games open guess; @0.3; games rule 2; sectors hub; @0.3; games state; back; @0.5" && {
    grep -q '^APP game guess ask .* labels=' <<<"$out" || grep -q '^APP game guess blocked reason=Needs songs by at least 4 artists' <<<"$out" || { EVID="$(grep 'guess' <<<"$out" | tr '\n' '|')"; fail guess_rule_artist; }; }
# too few songs (an empty card): the page says why, the field does nothing, the hub does not start
rm -rf "$T/empty"; mkdir "$T/empty"
out=$($SIM --headless --root "$T/empty" --cmd "screen on; vol 0; games open guess; @0.3; sectors hub; @0.3; sectors; games state; back; @0.3" 2>&1); rc=$?
d=$(grep '^APP sectors n=' <<<"$out" | tail -1)
[ $rc = 0 ] && grep -q '^APP game guess blocked reason=Needs at least 4 songs with different titles' <<<"$out" && [ "$(gs_f hub "$d")" = '?' ] && [ "$(gs_f en "$d")" = 0 ] && ! grep -q '^APP game guess clip\|Sanitizer\|^APP ui leak' <<<"$out" || { EVID="rc=$rc $d $(grep 'guess' <<<"$out" | tr '\n' '|')"; fail guess_too_few; }
# another source playing (the radio): the page says so and plays nothing (it would have to stop the radio for good)
nowrun guess_other_source "screen on; vol 0; radio 0; @1; games open guess; @0.3; sectors hub; @0.3; games state; back; @0.3" && {
    grep -q '^APP game guess blocked reason=Stop what is playing first' <<<"$out" && ! grep -q '^APP game guess clip\|^APP game guess note' <<<"$out" && grep -qx 'APP game guess state phase=5 asked=0 correct=0 streak=0 points=0 taken=0 had=0' <<<"$out" || { EVID="$(grep 'guess' <<<"$out" | tr '\n' '|')"; fail guess_other_source; }; }
# the best streak is kept (pc_gamesave key guess.bs<rule>), the card's best score is too (the game's bests)
nowrun guess_gamesave "screen on; vol 0; games open guess; @0.3; games golden 0; games mode 0; sectors hub; @0.2; $(gs_play 5)@0.8; gamesave get guess.bs0; gamesave dump; back; @0.5" && {
    grep -q 'guess.bs0=5' <<<"$out" || { EVID="$(grep 'gamesave' <<<"$out" | tr '\n' '|')"; fail guess_gamesave; }; }
# Back mid-clip: the timer and the clip end with the page (no stray stop later), the pieces' pictures are not leaked (nowrun fails on "APP ui leak")
nowrun guess_back_mid_clip "screen on; vol 0; track 1; @2; games open guess; @0.3; sectors hub; @1; back; @0.5; mp3cur; @6; mp3cur; uitop" && {
    [ "$(grep '^APP mp3 cur=' <<<"$out" | sed -n 1p | cut -d' ' -f3)" = cur=1 ] && [ "$(grep '^APP mp3 cur=' <<<"$out" | sed -n 2p | cut -d' ' -f3)" = cur=1 ] || { EVID="$(grep '^APP mp3 cur' <<<"$out" | tr '\n' '|')"; fail guess_back_mid_clip; }; }
# the keys of a device without a knob (FakePod): the sector cursor works as on the other sector games
nowrun guess_keys "screen on; vol 0; games open guess; @0.3; sectors keys 1; sectors hub; @0.3; sectors key 1; sectors key -1; @0.3; games state; back; @0.5" && {
    grep -q '^APP game guess answer=' <<<"$out" || { EVID="$(grep 'guess' <<<"$out" | tr '\n' '|')"; fail guess_keys; }; }
# --- end guess ---
