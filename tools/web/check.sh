#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# Settings page check without a board: syntax, then the real connection code against a pretend serial port (?mockserial)
# in headless Chrome. Prints PASS or the reason.
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
# one command a line: under `set -e` a failing command inside an `a && b && c` list does NOT stop the script (only the last one would), so a stale fontsets.js used to skip the rest of the line and still end in PASS
node --check page.js
node --check pagemock.js
node --check boot.js
python3 testdata/make_testfont.py
python3 testdata/make_merged.py
python3 ../gen/web_fontsets_gen.py --check
node --check fontsubset.js
node --check fontput.js
node --check fontbuild.js
node --check fonttab.js
node --check fontworker.js
node fontbuild_test.mjs
node fontput_test.mjs
node --check conn.js
node conn_test.mjs
node --check flasher.js
node flasher_test.mjs
node --check flashtab.js
node --check glyphpack.js
node --check lists.js
node --check regions.js
python3 ../gen/gen_regions.py --check
python3 ../gen/web_i18n_gen.py --check
python3 ../check/check_web_flasher.py
python3 ../check/check_web_publish.py
python3 ../check/check_release.py
python3 ../check/check_web_security.py
PORT=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')   # a free port: two webchecks (main + a worktree) can run at once
python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1 & S=$!
sleep 1
# the 14 headless-Chrome runs go in parallel, each with its own profile dir; the page's answer (its <title>) lands in $T/<name>
CP=
chrome() { timeout "$2" google-chrome --headless=new --disable-gpu --no-sandbox --user-data-dir="$T/prof$1" "$3" --dump-dom "http://127.0.0.1:$PORT/$4" 2>/dev/null >"$T/$1" || true; }
chrome OUT 40 --virtual-time-budget=6000 "?lang=tw&mockserial" & CP="$CP $!"
chrome OUT2 40 --virtual-time-budget=6000 "?lang=tw&mockserial&disconnect" & CP="$CP $!"
chrome OUT3 40 --virtual-time-budget=8000 "?lang=tw&mockserial&buttons" & CP="$CP $!"
chrome OUT4 60 --virtual-time-budget=20000 "?lang=tw&mockserial&lists" & CP="$CP $!"
chrome OUT5 60 --virtual-time-budget=10000 "?lang=tw&mockserial&fx" & CP="$CP $!"
chrome OUT7 40 --virtual-time-budget=10000 "?lang=tw&mockserial&radio" & CP="$CP $!"
chrome OUT8 40 --virtual-time-budget=10000 "?lang=tw&mockserial&tz" & CP="$CP $!"
chrome OUT9 40 --virtual-time-budget=10000 "?lang=tw&mockserial&settings" & CP="$CP $!"
chrome OUT10 40 --virtual-time-budget=10000 "?lang=tw&mockserial&pause" & CP="$CP $!"
chrome OUT11 60 --virtual-time-budget=20000 "?mockserial&langs" & CP="$CP $!"
chrome OUT12 60 --virtual-time-budget=40000 "?lang=tw&mockserial&flash" & CP="$CP $!"
chrome OUT14 90 --virtual-time-budget=200000 "?lang=tw&mockserial&fonts&fontsrc=testdata/fonts/" & CP="$CP $!"
chrome OUT6 40 --virtual-time-budget=6000 "?lang=tw&mockserial&early" & CP="$CP $!"
wait $CP 2>/dev/null || true   # only the Chrome runs (not the web server)
kill $S
OUT=$(cat "$T/OUT")
OUT2=$(cat "$T/OUT2")
OUT3=$(cat "$T/OUT3")
OUT4=$(cat "$T/OUT4")
OUT5=$(cat "$T/OUT5")
OUT7=$(cat "$T/OUT7")
OUT8=$(cat "$T/OUT8")
OUT9=$(cat "$T/OUT9")
OUT10=$(cat "$T/OUT10")
OUT11=$(cat "$T/OUT11")
OUT12=$(cat "$T/OUT12")
OUT14=$(cat "$T/OUT14")
OUT6=$(cat "$T/OUT6")
rm -rf "$T"
echo "$OUT3" | grep -q "<title>MOCK-BUTTONS PLAY-OK EDIT-OK</title>" || { echo "FAIL: station play / edit buttons ($(echo "$OUT3" | grep -o '<title>[^<]*'))"; exit 1; }
echo "$OUT4" | grep -q "<title>MOCK-LISTS UPLOAD-OK RELOAD-OK RENDER-OK</title>" || { echo "FAIL: lists build / upload / reload ($(echo "$OUT4" | grep -o '<title>[^<]*'))"; exit 1; }
echo "$OUT5" | grep -q "<title>MOCK-FX EXPORT-OK APPS-OK NOMIX-OK IMPORT-OK</title>" || { echo "FAIL: fx tab export / apps / import ($(echo "$OUT5" | grep -o '<title>[^<]*'))"; exit 1; }
echo "$OUT7" | grep -q "<title>MOCK-RADIO ROTATE-OK SERVERS-OK ORDER-TW-OK ORDER-CN-OK ORDER-EN-OK NOFLAG-OK PICK-OK LISTS-OK SWITCH-OK ALTS-OK</title>" || { echo "FAIL: radio search servers / region order / lists / backups ($(echo "$OUT7" | grep -o '<title>[^<]*'))"; exit 1; }
echo "$OUT8" | grep -q "<title>MOCK-TZ ZONES-OK BROWSER-OK GUIDE-HIDDEN-OK FIXED-OK GUIDE-OK SUN-SET-OK SUN-CLEAR-OK</title>" || { echo "FAIL: time zone from the browser / sunrise location steps ($(echo "$OUT8" | grep -o '<title>[^<]*'))"; exit 1; }
echo "$OUT9" | grep -q "<title>MOCK-SETTINGS CTRL-OK RANGE-OK HAPTIC-NOOFF-OK SET-OK GROUPS-OK</title>" || { echo "FAIL: moved settings controls ($(echo "$OUT9" | grep -o '<title>[^<]*'))"; exit 1; }
echo "$OUT10" | grep -q "<title>MOCK-PAUSE PAUSED-OK BUSY-OK RESUME-OK</title>" || { echo "FAIL: pause / resume of the shared connection ($(echo "$OUT10" | grep -o '<title>[^<]*'))"; exit 1; }
echo "$OUT11" | grep -q "<title>MOCK-LANGS FOLLOW-DEV-OK DEV-EN-OK PICK-OK PICK-WINS-OK TEXT-OK CSP-OK</title>" || { echo "FAIL: page language (device, menu, text per tab) ($(echo "$OUT11" | grep -o '<title>[^<]*'))"; exit 1; }
echo "$OUT12" | grep -q "<title>MOCK-FLASH LOAD-OK NODEFAULT-OK DLHINT-OK ART-OK FILE-OK BOARD-PICK-REFUSED-OK NAME-VS-CONTENT-OK NAME-NOT-NEEDED-OK HASH-REFUSED-OK CHECK-OK SKIP-GATE-OK WRITE-OK RESUMED-OK FACTORY-OK CHIP-REFUSED-OK CANCEL-OK SMALL-REFUSED-OK PT-MOVED-OK MISMATCH-CANCEL-OK MISMATCH-YES-OK FAKEPOD-WDT-OK VERIFY-FAIL-OK BACKUP-VERIFY-OK BACKUP-CONFIRM-OK</title>" || { echo "FAIL: flasher tab flow ($(echo "$OUT12" | grep -o '<title>[^<]*'))"; exit 1; }
echo "$OUT14" | grep -q "<title>MOCK-FONTS SRC-OK CHARSET-OK SPLIT-OK SIZES-OK SEND-OK SEND-SET-FAIL-OK MISSING-LISTED-OK</title>" || { echo "FAIL: font builder tab ($(echo "$OUT14" | grep -o '<title>[^<]*'))"; exit 1; }
echo "$OUT6" | grep -q "<title>MOCK-EARLY ANSWERED</title>" || { echo "FAIL: a request at connect time was dropped ($(echo "$OUT6" | grep -o '<title>[^<]*'))"; exit 1; }
echo "$OUT2" | grep -q "<title>MOCK-DISCONNECTED 未連線 連線</title>" || { echo "FAIL: disconnect did not release the port"; exit 1; }
echo "$OUT" | grep -q "<title>MOCK-CONNECTED" && echo "$OUT" | grep -q 'id="conn-pill">已連線' && echo "$OUT" | grep -q "Retro Lofi" \
  && echo PASS || { echo "FAIL: the page did not connect to the pretend port"; exit 1; }
