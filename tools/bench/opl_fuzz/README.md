# opl_fuzz: coverage-guided fuzzing of the AdPlug backend

No clang here (no libFuzzer): `fuzz.cpp` is a small in-process fuzzer over GCC's `-fsanitize-coverage=trace-pc,trace-cmp` (edge coverage + a
dictionary of compared constants), ASan + fatal UBSan, over the real backend path (`pc_backend_opl.c`: open, scan, render, sub-songs, mute, close).
Every malloc goes through a wrapper: `OPLF_TRUNC32=1` truncates sizes to 32 bits (32-bit `size_t` simulation), allocations over `OPLF_MAXALLOC`
(8 MiB) fail, and a malloc/free balance check per input reports leaks. Seeds: synthetic headers built in `fuzz.cpp` plus (outside git) the real
files of the main checkout's `.local/chiptest/opl/adplug` with the same extension. Kept corpora live in `$OPLF_W/corpus` (default `.local/oplfuzz`, never git).

    tools/bench/opl_fuzz/run.sh [-t secs] [-p plateau secs] [-j jobs] [-s seed] [-3] [ext ...]   # all 48 extensions by default; ok/FAIL line per ext
    tools/bench/opl_fuzz/triage.sh                                                              # re-run every saved finding in $OPLF_W/out: one line each, deletes fixed ones
    $OPLF_W/build/opl_fuzz .ext -r FILE                                                   # reproduce one input (ASAN_OPTIONS=handle_abort=1:detect_leaks=0:allocator_may_return_null=1)

Findings land in `$OPLF_W/out/`: `crash-*` (sanitizer / abort), `hang-*` (8 s alarm), `slow-*` (open > OPLF_OPEN_MS or a 10 ms block > OPLF_BLOCK_MS,
wall time: noisy when many jobs run), `leak-*`. `OPLF_W=/some/dir` gives each job its own build, corpus and findings.
