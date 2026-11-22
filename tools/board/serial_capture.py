#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Reset the board through USB Serial/JTAG and print serial output for N seconds.
Usage: serial_capture.py /dev/ttyACM0 [seconds] [--no-reset] [--grep PATTERN] [--send T:CMD ...]
--send 12:"mp3 all" writes the line "mp3 all" 12 s after the capture starts (repeatable)."""
import argparse, re, sys, time
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders
from boardport import open_port, SerialException

ap = argparse.ArgumentParser()
ap.add_argument("port"); ap.add_argument("seconds", nargs="?", type=float, default=6)
ap.add_argument("--no-reset", action="store_true"); ap.add_argument("--grep")
ap.add_argument("--send", action="append", default=[])
a = ap.parse_args()
pat = re.compile(a.grep) if a.grep else None
s = open_port(a.port, drain_s=0)      # records everything: no newline, no drain
if not a.no_reset:  # same sequence esptool uses for a hard reset on USB Serial/JTAG
    s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False
    s.close(); time.sleep(1.0)
    for _ in range(50):
        try: s = open_port(a.port, drain_s=0); break
        except SerialException: time.sleep(0.1)
t0 = time.time(); end = t0 + a.seconds; buf = b""
sends = sorted((float(x.split(":", 1)[0]), x.split(":", 1)[1]) for x in a.send)
while time.time() < end:
    while sends and time.time() - t0 >= sends[0][0]:
        s.write((sends.pop(0)[1] + "\n").encode())
    buf += s.read(4096)
    *lines, buf = buf.split(b"\n")
    for ln in lines:
        t = ln.decode("utf-8", "replace").rstrip("\r")
        if not pat or pat.search(t): print(t, flush=True)
