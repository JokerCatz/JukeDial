#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# xmp_fuzz build + run. usage:
#   tools/bench/xmp_fuzz/run.sh build [asan|plain|cov]     -> tools/bench/xmp_fuzz/build-<kind>/xmp_fuzz
#   tools/bench/xmp_fuzz/run.sh check SEEDDIR              every seed must load (asan build)
#   tools/bench/xmp_fuzz/run.sh fuzz SEEDDIR ITERS SEED [32]   ASan+UBSan fuzz (32 = 32-bit size_t simulation); a crash keeps its input in
#                                                    $OUT/crash-<seed>-<n>.bin (default OUT=/tmp/xmp_fuzz_out) and the run goes on
# Seeds: tools/bench/xmp_fuzz/make_seeds.py (synthetic, generated, never committed) plus the tracker files of the main checkout's .local/chiptest.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
COMP=$ROOT/components/libxmp
kind=${2:-asan}
OUT=${OUT:-/tmp/xmp_fuzz_out}
build() {
  local k=$1 d=$HERE/build-$1 flags
  mkdir -p "$d"
  printf 'include(%s/xmp_sources.cmake)\nstring(REPLACE ";" " " S "${XMP_SRCS}")\nfile(WRITE %s/srcs.txt "${S}")\n' "$COMP" "$d" > "$d/list.cmake"
  cmake -P "$d/list.cmake" || exit 1
  case $k in
    asan) flags="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer";;
    plain) flags="-O2";;
    cov) flags="-O0 -g --coverage";;
  esac
  # the harness and the allocator are compiled WITHOUT the force-include; libxmp with it
  mkdir -p "$d/o"; local objs=()
  for s in $(cat "$d/srcs.txt"); do
    o="$d/o/$(echo "$s" | sed "s#$COMP/libxmp/src/##; s#/#_#g").o"; objs+=("$o")
    if [ ! -f "$o" ] || [ "$s" -nt "$o" ]; then
      gcc $flags -w -c -DLIBXMP_STATIC -DLIBXMP_NO_PAULA -include "$HERE/fz_alloc.h" -I"$COMP/libxmp/include" "$s" -o "$o" &
      while [ "$(jobs -r | wc -l)" -ge "${XF_JOBS:-8}" ]; do sleep 0.05; done
    fi
  done
  wait
  gcc $flags -w -c -DLIBXMP_STATIC -I"$COMP/libxmp/include" "$HERE/xmp_fuzz.c" -o "$d/h.o"
  gcc $flags -w -c "$HERE/fz_alloc.c" -o "$d/a.o"
  gcc $flags "${objs[@]}" "$d/h.o" "$d/a.o" -o "$d/xmp_fuzz" -lm || exit 1
  echo "built $d/xmp_fuzz"
}
case ${1:-} in
  build) build "$kind";;
  check) "$HERE/build-asan/xmp_fuzz" --run "$2"/*;;
  fuzz)
    seeds=$2; iters=$3; seed=$4; mode32=${5:-}
    mkdir -p "$OUT"
    [ "$mode32" = 32 ] && export FZ_TRUNC32=1
    export ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:allocator_may_return_null=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
    export FZ_CUR=$OUT/cur-$seed$mode32.bin
    # a crash aborts the process: keep the input, advance the seed, go on until the iteration budget of the seed is spent
    "$HERE/build-asan/xmp_fuzz" --fuzz "$seeds" "$iters" "$seed" > "$OUT/log-$seed$mode32.txt" 2>&1
    rc=$?
    if [ $rc -ne 0 ]; then cp "$FZ_CUR" "$OUT/crash-$seed$mode32.bin"; echo "CRASH rc=$rc seed=$seed$mode32 input=$OUT/crash-$seed$mode32.bin"; grep -m3 -E 'ERROR|runtime error|HANG|LEAK|BAD' "$OUT/log-$seed$mode32.txt"; else tail -n 3 "$OUT/log-$seed$mode32.txt"; fi
    exit $rc;;
  *) sed -n 2,9p "$0";;
esac
