#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Copy files from the board's TF card over USB Serial/JTAG through the app's console (the reverse of tf_put.py).
usage: tf_get.py PORT /sdcard/remote/path local_file [remote2 local2 ...]
       tf_get.py PORT --sha /sdcard/path [...]      only print each file's SHA-256 (computed on the board)
Protocol: "get <path> [offset len]" -> "GET <offset> <bytes> <base64>" lines, then "GET end size=N sha256=H".
Another task's log line can cut a data line: the missing ranges are asked for again, and the whole local copy is
checked against the board's size and SHA-256. Never resets the board (plain open, DTR/RTS untouched)."""
import base64, binascii, hashlib, re, sys, time
if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
    print(__doc__); sys.exit(0)
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders
from boardport import open_port

CHUNK = 768
port, rest = sys.argv[1], sys.argv[2:]
s = open_port(port)


def run(cmd, got=None, seconds=180):
    s.reset_input_buffer()
    s.write(f"{cmd}\n".encode())
    buf, end = b"", time.time() + seconds
    while time.time() < end:
        buf += s.read(s.in_waiting or 1)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.rstrip(b"\r")
            if line == b"GET fail":
                raise SystemExit(f"{cmd}: the board could not open the file")
            m = re.search(rb"GET end size=(\d+) sha256=([0-9a-f]{64})", line)
            if m:
                return int(m.group(1)), m.group(2).decode()
            m = re.search(rb"GET (\d+) (\d+) ([A-Za-z0-9+/=]+)$", line)
            if m and got is not None:
                try:
                    chunk = base64.b64decode(m.group(3), validate=True)
                except binascii.Error:
                    continue
                if len(chunk) == int(m.group(2)):
                    got[int(m.group(1))] = chunk
    raise SystemExit(f"{cmd}: timeout")


if rest and rest[0] == "--sha":
    for remote in rest[1:]:
        size, sha = run(f"sha {remote}")
        print(f"{sha}  {size:9d}  {remote}")
    sys.exit(0)
for remote, local in zip(rest[0::2], rest[1::2]):
    t0, got, refetched = time.time(), {}, 0
    size, sha = run(f"get {remote}", got)
    for attempt in range(5):
        missing = [o for o in range(0, size, CHUNK) if o not in got]
        if not missing:
            break
        refetched += len(missing)
        for o in missing:
            run(f"get {remote} {o} {min(CHUNK, size - o)}", got, 20)
    data = b"".join(got.get(o, b"") for o in range(0, size, CHUNK))
    if len(data) != size or hashlib.sha256(data).hexdigest() != sha:
        raise SystemExit(f"{remote}: got {len(data)} bytes, board says {size}; sha mismatch")
    open(local, "wb").write(data)
    print(f"{remote} -> {local}: {size} bytes, sha256 {sha}, {time.time() - t0:.1f} s, {refetched} chunks refetched")
