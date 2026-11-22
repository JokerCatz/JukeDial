#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# gcc -fanalyzer over the firmware's own sources (app/main/**, cross-compiled at the sdkconfig's -O2; nothing is linked, flashed or run).
# Own build dir app/build-lint with its own sdkconfig (the defaults + bt + sdkconfig.ldac, like the default image: the vendor-codec code is analysed too); the flag is added to the main component only (app/main/CMakeLists.txt,
# -DJC_LINT_APP=1), so the normal build is untouched. ESP-IDF, managed components and components/player_core are not analysed.
# Every run recompiles all of app/main (about a minute). Warnings located in app/main/third_party/ (vendored) are ignored.
# Prints each distinct analyzer warning (file:line:col) and a count; exits 1 on any, else prints "lint-app: ok".
set -u
cd "$(dirname "$0")/../.." || exit 2
bash tools/check/apll_guard.sh --selftest && bash tools/check/apll_guard.sh || { echo "lint-app: FAILED (apll_guard)"; exit 1; }   # hires T12: the APLL has one owner
B=build-lint
mkdir -p app/$B
tools/bt_ldac/make_bt.sh --if-stale && python3 tools/check_sdkconfig.py build-lint || { echo "lint-app: FAILED (stale sdkconfig)"; exit 1; }
command -v idf.py >/dev/null || { echo "lint-app: FAILED (no IDF environment: run it as 'make lint-app')"; exit 2; }
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
# a full reconfigure is ~30 s of the ~50 s run: only when the source list changed (file(GLOB): tools/src_stamp.sh, the same stamp as every build) or there is no build yet; edits to existing files and CMakeLists are caught by ninja itself
[ -e app/$B/build.ninja ] && grep -q "^JC_LDAC:.*=1" app/$B/CMakeCache.txt 2>/dev/null || rm -f app/$B/.srclist   # (re)configure when there is no build yet or it was made without the vendor codecs
tools/src_stamp.sh app/$B "cd app && idf.py --preview -B $B -DSDKCONFIG=$B/sdkconfig -DJC_LINT_APP=1 -DJC_LDAC=1 \"-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.bt;sdkconfig.ldac\" reconfigure" >"$LOG" 2>&1 || { tail -20 "$LOG"; echo "lint-app: FAILED (configure)"; exit 2; }
# the analyzer runs at compile time: drop the objects of the component so every file is analysed again; -k 0 reports every file
find app/$B/esp-idf/main -name '*.obj' -delete 2>/dev/null
(cd app && cmake --build $B --target __idf_main -- -k 0) >"$LOG" 2>&1
RC=$?
# positive check: how many translation units of main were really compiled (analysed) in this run; none = nothing was checked
NFILES=$(grep -Ec 'Building C(XX)? object .*__idf_main\.dir/' "$LOG")
if [ "$NFILES" -eq 0 ]; then tail -5 "$LOG"; echo "lint-app: nothing was analysed"; exit 2; fi
ANA='\[-W(error=)?analyzer-[a-z0-9-]+\]'
# one line per distinct site: path:line:col: class; vendored files out
HITS=$(grep -E "^[^ ]+:[0-9]+:[0-9]+: (warning|error): .*$ANA" "$LOG" | grep -v '/third_party/' \
       | sed -E 's#^.*/app/main/##; s#: (warning|error): (.*) \[CWE-[0-9]+\] \[-W(error=)?(analyzer-[a-z0-9-]+)\]$#: \4: \2#' | sort -u)
OTHER=$(grep -E '^[^ ]+:[0-9]+:[0-9]+: error: ' "$LOG" | grep -Ev "$ANA" | grep -v '/third_party/')
if [ -n "$OTHER" ]; then printf '%s\n' "$OTHER"; echo "lint-app: FAILED (compile error, see above)"; exit 2; fi
if [ -z "$HITS" ]; then
  if [ $RC -ne 0 ]; then tail -20 "$LOG"; echo "lint-app: FAILED (build failed without an analyzer warning)"; exit 2; fi
  echo "lint-app: ok ($NFILES files)"; exit 0
fi
printf '%s\n' "$HITS"
echo "-- by class:"; printf '%s\n' "$HITS" | sed -E 's/^[^ ]+ (analyzer-[a-z0-9-]+):.*/\1/' | sort | uniq -c | sort -rn
echo "(full text with the analyzer's paths: re-run the compile of one file, e.g. ninja -C app/$B esp-idf/main/CMakeFiles/__idf_main.dir/<file>.obj)"
echo "lint-app: $(printf '%s\n' "$HITS" | wc -l) analyzer warning(s) ($NFILES files)"
exit 1
