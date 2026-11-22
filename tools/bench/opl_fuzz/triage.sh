#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Re-run every saved finding in .local/oplfuzz/out once and print one line each: kind, file, first report line, first libadplug frame.
cd "$(dirname "$0")/../../.."
W=$(realpath -m ${OPLF_W:-.local/oplfuzz}); mkdir -p $W/tmp; export ASAN_OPTIONS=handle_abort=1:detect_leaks=0:allocator_may_return_null=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
for f in $W/out/*; do
  [ -f "$f" ] || continue
  ext=.${f##*.}
  out=$(cd $W/out && OPLF_BLOCK_MS=${OPLF_BLOCK_MS:-600} OPLF_OPEN_MS=${OPLF_OPEN_MS:-3000} timeout 30 $W/build/opl_fuzz $ext -r "$(basename $f)" -o $W/tmp 2>&1); rc=$?
  if [ $rc = 0 ]; then echo "fixed  $(basename $f)"; rm -f "$f"; continue; fi
  top=$(echo "$out" | grep -m1 -E 'SUMMARY|runtime error|^opl_fuzz: (hang|slow|leak)' | sed 's|.*/adplug/||;s|.*/components/||' | cut -c1-150)
  fr=$(echo "$out" | grep -m1 -E '#[0-9]+ .* in .*(adplug|binio)/' | sed 's|.* in ||;s|/home/[^ ]*/libadplug/||' | cut -c1-150)
  echo "rc=$rc $(basename $f) | $top | $fr"
done
