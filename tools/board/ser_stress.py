#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Console reply-loss test (an internal note). Does not reset the board (DTR/RTS untouched).

usage: ser_stress.py PORT ping N        N x "ping k" (answered by the console task), counts missing "APP pong k"
       ser_stress.py PORT ctl N         N x {"op":"status"} over the settings protocol (answered by the LVGL task)
       ser_stress.py PORT putold KB     the old tf_put protocol into /sdcard/sertest.bin; a lost "APP put ok" is counted and
                                        skipped (the file content does not matter), then the file is removed
A missing reply is waited for 2 s. Prints serstat (device-side counters) before and after."""
import base64, json, os, re, sys, time
if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
    print(__doc__); sys.exit(0)
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders
from boardport import open_port

port, mode, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
s = open_port(port)
buf = b""


def wait(pattern, seconds=2.0):
    global buf
    end = time.time() + seconds
    while True:
        m = re.search(pattern, buf)
        if m:
            buf = buf[m.end():]
            return m
        if time.time() > end:
            return None
        buf += s.read(s.in_waiting or 1)


def serstat(tag):
    s.write(b"serstat\n")
    m = wait(rb"APP serstat [^\r\n]*\r?\n")
    print(tag, m.group(0).decode().strip() if m else "(no serstat reply)")


s.reset_input_buffer()
serstat("before:")
lost, t0, lat = [], time.time(), []
if mode == "ping":
    for k in range(n):
        t = time.time()
        s.write(b"ping %d\n" % k)
        if not wait(rb"APP pong %d\r?\n" % k):
            lost.append(k)
        else:
            lat.append(time.time() - t)
elif mode == "ctl":
    for k in range(n):
        t = time.time()
        s.write((json.dumps({"op": "status", "id": 100000 + k}) + "\n").encode())
        if not wait(rb'CTL \{[^\r\n]*"id":%d[,}]' % (100000 + k)):
            lost.append(k)
        else:
            lat.append(time.time() - t)
elif mode == "putold":
    data = os.urandom(n * 1024)
    s.write(b"put /sdcard/sertest.bin\n")
    if not wait(rb"APP put ready", 5):
        sys.exit("no put ready")
    for k, i in enumerate(range(0, len(data), 768)):
        c = data[i:i + 768]
        t = time.time()
        s.write(b"%d " % len(c) + base64.b64encode(c) + b"\n")
        if not wait(rb"APP put (ok|bad)"):
            lost.append(i)
        else:
            lat.append(time.time() - t)
    s.write(b"end\n")
    wait(rb"APP put done", 5)
    s.write(b"rm /sdcard/sertest.bin\n")
    wait(rb"APP rm rc=", 5)
dt = time.time() - t0
total = n if mode != "putold" else (n * 1024 + 767) // 768
lat.sort()
print("%s: %d requests, %d replies lost (%.2f %%) in %.1f s; latency median %.1f ms, max %.1f ms; lost at %s" % (
    mode, total, len(lost), 100.0 * len(lost) / total, dt, 1000 * lat[len(lat) // 2] if lat else 0, 1000 * lat[-1] if lat else 0,
    lost[:20]))
serstat("after: ")
