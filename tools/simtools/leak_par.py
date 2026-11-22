#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run the leak lines of a list file (one bash line each, # comments) in parallel: python3 tools/simtools/leak_par.py tools/simtools/leak_lines.txt [-P N].
Each line's output is buffered and printed whole, in list order, when the run is done; the exit code is 1 if any line failed (same verdict as
running the lines one after the other). Every leak_cycles run has its own temp root (SimCon mkdtemp), so the runs share nothing.
Slow lines go first in the file so that the pool starts them early."""
import argparse, concurrent.futures as cf, os, subprocess, sys, time

def run(i, line):
    t = time.time()
    p = subprocess.run(["bash", "-c", "set -e; " + line], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    return i, line, p.returncode, p.stdout, time.time() - t

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("list"); ap.add_argument("-P", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    a = ap.parse_args()
    lines = [l.rstrip("\n") for l in open(a.list, encoding="utf-8") if l.strip() and not l.lstrip().startswith("#")]
    t0 = time.time(); res = {}; nxt = 0; bad = []
    with cf.ThreadPoolExecutor(a.P) as ex:
        futs = [ex.submit(run, i, l) for i, l in enumerate(lines)]
        for f in cf.as_completed(futs):
            i, l, rc, out, dt = f.result(); res[i] = (l, rc, out, dt)
            while nxt in res:                     # print in list order as soon as the prefix is complete
                l, rc, out, dt = res.pop(nxt)
                print(f"=== [{nxt + 1}/{len(lines)}] {l.split('#')[0].strip()[:200]}  ({dt:.0f} s, rc={rc})", flush=True)
                sys.stdout.write(out); sys.stdout.flush()
                if rc: bad.append(nxt + 1)
                nxt += 1
    print(f"leak_par: {len(lines)} runs, -P {a.P}, {time.time() - t0:.0f} s wall, {'FAILED runs ' + str(bad) if bad else 'all ok'}")
    return 1 if bad else 0

sys.exit(main())
