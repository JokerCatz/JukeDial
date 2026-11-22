#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# gcc -fanalyzer over every first-party components/player_core/src/*.c (the pc_lint_objs object library of host_test/CMakeLists.txt),
# at -O0 and at -O2 (the firmware is built at -O2; the analyzer reports differ). Nothing is run. Each level is a from-scratch build in
# components/player_core/build-lint-O0 / build-lint-O2 (git-ignored by **/build-*/). -k lists every warning at once; a build failure
# or any warning:/error: text at either level exits non-zero. Also tools/fatfs_exfat/make_fatfs.sh --selftest (the patch step on fixtures). The last line says which levels passed.
set -u
cd "$(dirname "$0")/../.." || exit 2
bash tools/check/apll_guard.sh --selftest && bash tools/check/apll_guard.sh || { echo "lint: FAILED (apll_guard)"; exit 1; }   # hires T12: the APLL has one owner
# the tools open the board port in one place (tools/boardport.py): its selftest, the scan that keeps the others off hand-made opens, and the scan's selftest
for c in "python3 tools/boardport.py --selftest" "python3 tools/check_boardport.py --selftest" "python3 tools/check_boardport.py"; do
  echo "== $c =="; $c || { echo "lint: FAILED ($c)"; exit 1; }
done
ok=""; bad=""
# the two levels are independent from-scratch builds in their own directories: run side by side, print each one's report in order
lint_level() {
  L=$1; B=components/player_core/build-lint-$L
  rm -rf "$B"
  echo "== lint -$L =="
  cmake -S components/player_core/host_test -B "$B" -DPC_ANALYZER=ON -DPC_ASAN=OFF -DCMAKE_C_FLAGS="-$L" >/dev/null || { echo "LEVEL-BAD (configure)"; return; }
  OUT=$(cmake --build "$B" --target pc_lint_objs -j"$(nproc)" -- -k 2>&1)
  RC=$?
  printf '%s\n' "$OUT"
  if [ $RC -ne 0 ] || printf '%s\n' "$OUT" | grep -Eq 'warning:|error:'; then echo "LEVEL-BAD"; else echo "LEVEL-OK"; fi
}
LD=$(mktemp -d); trap 'rm -rf "$LD"' EXIT
for L in O0 O2; do lint_level $L >"$LD/$L.txt" 2>&1 & done
wait
for L in O0 O2; do
  grep -v '^LEVEL-' "$LD/$L.txt"
  if grep -qx 'LEVEL-OK' "$LD/$L.txt"; then ok="$ok -$L"; else bad="$bad -$L"; fi
done
echo "== make_fatfs.sh --selftest =="
if bash tools/fatfs_exfat/make_fatfs.sh --selftest; then ok="$ok fatfs-selftest"; else bad="$bad fatfs-selftest"; fi
if [ -n "$bad" ]; then echo "lint: FAILED at$bad (passed:${ok:- none})"; exit 1; fi
echo "lint: ok at$ok"
