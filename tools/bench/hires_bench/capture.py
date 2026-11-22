#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Reset the peer through RTS (EN) and log its console until 'HB done' or the time limit.
Usage: capture.py <by-id port> <out.txt> [seconds]   (run with the IDF venv python: it has pyserial)
Opens the port with DTR/RTS low (does not reset the CH343 devkit), then pulses RTS once.
The port is opened by tools/boardport.py like every other tool (make lint checks it)."""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
from boardport import open_port

port, out = sys.argv[1], sys.argv[2]
limit = float(sys.argv[3]) if len(sys.argv) > 3 else 900
s = open_port(port, timeout=0.2, drain_s=0, lines_low=True)
s.rts = True; time.sleep(0.1); s.rts = False
t0 = time.time(); buf = b""
with open(out, "w") as f:
    f.write(f"# capture {time.strftime('%Y-%m-%d %H:%M:%S')} port={port}\n")
    while time.time() - t0 < limit:
        buf += s.read(4096)
        *lines, buf = buf.split(b"\n")
        for ln in lines:
            t = ln.decode("utf-8", "replace").rstrip("\r")
            f.write(t + "\n"); f.flush()
            print(t, flush=True)
            if t.startswith("HB done"):
                s.close(); sys.exit(0)
s.close()
print("capture: time limit reached", file=sys.stderr); sys.exit(1)
