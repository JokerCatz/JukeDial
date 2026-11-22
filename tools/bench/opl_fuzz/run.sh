#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Fuzz every OPL extension in parallel.  usage: tools/bench/opl_fuzz/run.sh [-t secs per ext] [-p plateau secs] [-j jobs] [-s seed] [-3 (32-bit size_t simulation)] [ext ...]
# Needs the real corpus only as seeds (main checkout's .local/chiptest/opl/adplug, never committed). Work files: .local/oplfuzz/{build,out,corpus,log}.
set -u
cd "$(dirname "$0")/../../.."
T=60; P=30; J=$(nproc); SEED=1; T32=0
while getopts t:p:j:s:3 o; do case $o in t) T=$OPTARG;; p) P=$OPTARG;; j) J=$OPTARG;; s) SEED=$OPTARG;; 3) T32=1;; esac; done
shift $((OPTIND - 1))
common=$(git rev-parse --path-format=absolute --git-common-dir); MAIN=$(dirname "$common")
CORP=${OPLF_CORPUS:-$MAIN/.local/chiptest/opl/adplug}
W=$(realpath -m ${OPLF_W:-.local/oplfuzz}); mkdir -p $W/out $W/corpus $W/log
cmake -S tools/bench/opl_fuzz -B $W/build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build $W/build -j"$J" >/dev/null || exit 2
EXTS=${*:-"rad hsc hsp imf wlf a2m a2t amd xms bam cmf adl d00 dfm ksm mad mus mdy ims mdi sci laa mkj cff dmo dtm mtk mtr rac sat sa2 xad bmf lds plx rol xsm dro pis msc rix mkf got sop hsq sqx sdb agd ha2 sng"}
export ASAN_OPTIONS=handle_abort=1:detect_leaks=0:allocator_may_return_null=1:abort_on_error=0
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
export OPLF_TRUNC32=$T32
one() { e=$1; mkdir -p $W/corpus/$e
  timeout $((T + 60)) $W/build/opl_fuzz .$e -c "$CORP" -k $W/corpus/$e -o $W/out -t $T -p $P -s $SEED > $W/log/$e.txt 2>&1; echo $? > $W/log/$e.rc; }
export -f one; export W CORP T P SEED T32
printf '%s\n' $EXTS | xargs -P"$J" -I{} bash -c 'one {}'
for e in $EXTS; do
  rc=$(cat $W/log/$e.rc 2>/dev/null)
  if [ "$rc" = 0 ]; then echo "ok    $(tail -1 $W/log/$e.txt)"
  else echo "FAIL  .$e rc=$rc $(grep -m1 -E 'SUMMARY|runtime error|abort|saved to' $W/log/$e.txt | cut -c1-170)"; fi
done
