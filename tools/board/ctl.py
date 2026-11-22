#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Talk the settings-page protocol over the console (no reset): ctl.py PORT '{"op":"status"}' ...
Each argument is one JSON request; prints the matching CTL answer. Use the IDF python (pyserial)."""
import json, sys, time
if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
    print(__doc__); sys.exit(0)
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders
from boardport import open_port, find_ctl

port, reqs = sys.argv[1], sys.argv[2:]
s = open_port(port)        # DTR/RTS both high: no reset; sends "\n" and drops the stale log fragment the port held while closed
buf = b""
for n, raw in enumerate(reqs, 1):
    q = json.loads(raw)
    if not isinstance(q, dict): print(f"skip {raw}"); continue
    q["id"] = n
    t0 = time.time(); ans = None; sent = 0
    while time.time() - t0 < 25 and ans is None:
        # once more if the first went missing - only for status: the others change things or take long (probe)
        if sent == 0 or (sent == 1 and q["op"] == "status" and time.time() - t0 > 3):
            s.write((json.dumps(q, ensure_ascii=False) + "\n").encode()); sent += 1
        buf += s.read(4096)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            r = find_ctl(line)                      # anywhere in the line: a stale fragment may be glued in front
            if r and r.get("id") == n: ans = r
    out = json.dumps(ans, ensure_ascii=False) if ans else "(no answer)"
    print(f"{q['op']}: {out[:400]}{'...' if len(out) > 400 else ''}  [{(time.time()-t0)*1000:.0f} ms]")
