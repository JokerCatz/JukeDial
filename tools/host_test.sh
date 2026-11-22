#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Build and run the player_core host tests. Default: ASan + LeakSanitizer + UBSan (any leak, bad access or
# undefined behaviour fails the test). FAST=1 skips the sanitizers. Extra args go to cmake configure.
# FXREF = directory of the fx-lab reference renders (outside git; default .local/fxref of the main checkout, whichever
# worktree this is). The run ends with test_fxref's own summary (ctest hides the lines of passing tests).
set -e
cd "$(dirname "$0")/.."
if [ "${FAST:-0}" = 1 ]; then B=components/player_core/build-host; SAN=-DPC_ASAN=OFF
else B=components/player_core/build-host-asan; SAN=-DPC_ASAN=ON; fi
if [ -z "${FXREF+x}" ]; then
    common=$(git rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)
    FXREF=${common:+$(dirname "$common")/.local/fxref}
fi
cmake -S components/player_core/host_test -B "$B" $SAN "-DFXREF=$FXREF" "$@" >/dev/null
bash tools/quiet_build.sh "$B/build.log" cmake --build "$B" -j"$(nproc)"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir "$B" --output-on-failure -j"$(nproc)"
echo "--- fx reference gate (test_fxref) ---"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$B/test_fxref" | grep -E '^(SKIP )?fx ref'
python3 tools/simtools/test_ui_baseline.py
python3 tools/simtools/test_sim_diff.py
python3 tools/check/check_face_centering.py
python3 tools/fonts/test_add_glyphs.py   # skips itself when fontTools is not installed (tools/requirements-fonts.txt)
