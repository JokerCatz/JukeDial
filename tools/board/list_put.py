#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Upload a glyph-pack list (zip) to /sdcard/lists/ with the settings-page ops list_put_begin / list_put /
list_put_end (spec an internal note section 7), the same way the web page does,
and time it. Does not reset the board (DTR/RTS untouched). Use the IDF python (pyserial).

usage: list_put.py PORT pack.zip [name.zip]      also: list_put.py PORT --ls | --rm name.zip
A lost reply: list_put_stat says how much the device has, the upload goes on from there (DROP=0.1 in the
environment pretends 10 % of the replies got lost).
"""
import base64, json, os, random, sys, time, zlib
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders
from boardport import open_port, find_ctl

CHUNK = 2048
DROP = float(os.environ.get("DROP", "0"))   # fault injection: share of replies treated as lost


class Ctl:
    def __init__(self, port):
        self.s = open_port(port)
        self.buf, self.n = b"", 0
        self.audio = []          # the player's periodic "APP audio ... underruns=N" lines seen meanwhile

    def req(self, q, timeout=20):
        self.n += 1
        q = dict(q, id=self.n)
        self.s.write((json.dumps(q, ensure_ascii=False) + "\n").encode())
        t0 = time.time()
        while time.time() - t0 < timeout:
            self.buf += self.s.read(max(1, self.s.in_waiting))   # not read(8192): that waits out the 0.2 s timeout every time
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                if line.startswith(b"APP audio blocks="):
                    self.audio.append(line.decode("latin1").strip())
                r = find_ctl(line)
                if r and r.get("id") == self.n:
                    return r
        raise TimeoutError(q["op"])


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    c = Ctl(sys.argv[1])
    if sys.argv[2] == "--ls":
        print(json.dumps(c.req({"op": "list_ls"}), ensure_ascii=False))
        return
    if sys.argv[2] == "--rm":
        print(json.dumps(c.req({"op": "list_rm", "name": sys.argv[3]}), ensure_ascii=False))
        return
    data = open(sys.argv[2], "rb").read()
    name = sys.argv[3] if len(sys.argv) > 3 else os.path.basename(sys.argv[2])
    t0 = time.time()
    r = c.req({"op": "list_put_begin", "name": name, "size": len(data)})
    if not r.get("ok"):
        sys.exit("begin: %s" % r)
    step = int(r.get("chunk", CHUNK))
    worst, off, fails, lost = 0.0, 0, 0, 0
    while off < len(data):
        t = time.time()
        try:
            r = c.req({"op": "list_put", "off": off, "data": base64.b64encode(data[off:off + step]).decode()}, timeout=4)
            if DROP and random.random() < DROP:
                raise TimeoutError("injected")   # fault injection: pretend the reply got lost
        except TimeoutError:                     # lost reply: ask how much the device has and go on from there
            lost += 1
            fails += 1
            if fails > 5:
                c.req({"op": "list_put_abort"})
                sys.exit("no progress at %d" % off)
            try:
                off = c.req({"op": "list_put_stat"}, timeout=4).get("got", off)
            except TimeoutError:
                pass
            continue
        worst = max(worst, time.time() - t)
        if "got" not in r:
            c.req({"op": "list_put_abort"})
            sys.exit("chunk at %d: %s" % (off, r))
        off, fails = r["got"], 0                 # a refusal ("out of order") carries got too
    r = c.req({"op": "list_put_end", "crc": zlib.crc32(data) & 0xFFFFFFFF}, timeout=60)
    dt = time.time() - t0
    print("%s: %d B in %.1f s = %.1f KB/s, slowest chunk %.0f ms, replies lost %d, end: %s" % (name, len(data), dt, len(data) / 1024 / dt, worst * 1000, lost,
                                                                            json.dumps(r, ensure_ascii=False)))
    for a in c.audio:
        print("  during upload:", a)
    if not r.get("ok"):
        sys.exit(1)


if __name__ == "__main__":
    main()
