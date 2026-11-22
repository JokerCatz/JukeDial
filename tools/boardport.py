#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The one place that opens the board's serial port (USB Serial/JTAG console), and the one way to read a reply.

Why (docs: the internal notes, "序列埠：開埠後第一筆回覆"): while no program holds the port, the board's log lines fill the 256-byte
USB TX ring and the rest is dropped, so the ring keeps a fragment that ends mid-line. The next open delivers it about 50 ms
later. open_port() therefore sends one "\\n", reads and throws away everything that arrives for drain_s seconds, and only then
returns. find_ctl() finds a "CTL {json}" reply wherever it starts in a line, so a fragment glued in front does not hide it.

  open_port(port, timeout=0.2, drain_s=0.3, unopened=False)  -> an open serial.Serial (no DTR/RTS change: no reset)
  open_port(port, drain_s=0, lines_low=True)                 -> the same for a devkit behind a USB-serial bridge (the peer board):
                                                                DTR and RTS are set low before open(), the caller pulses RTS itself
  find_ctl(line: bytes)                                      -> dict or None
  python3 tools/boardport.py --selftest                      (no pyserial, no port)

tools/check_boardport.py (run by `make lint`) fails when any other script under tools/ calls serial.Serial( itself."""
import json
import sys
import time

try:
    from serial import SerialException      # re-exported: tools catch boardport.SerialException and never import pyserial themselves
except ImportError:                          # no pyserial (the selftest, the simulator tools): a stand-in nothing raises
    class SerialException(Exception):
        pass


def drain(s, drain_s):
    """Send one newline, read and discard for drain_s seconds. Returns the discarded bytes (for the selftest)."""
    s.write(b"\n")                                  # ends a half line the console still holds
    got, end = b"", time.time() + drain_s
    while time.time() < end:
        got += s.read(4096)
    return got


def open_port(port, timeout=0.2, drain_s=0.3, unopened=False, serial_mod=None, lines_low=False):
    """Open the board's port the way every tool does: DTR/RTS untouched (pyserial leaves both high, the board does not reset).
    unopened=True builds serial.Serial() first and sets port/baudrate/timeout before open() (what crash_decode, shot and
    ui_baseline did; the same result, kept for them). drain_s=0 is a plain open: no newline, no drain (serial_capture, whose
    job is to record everything). serial_mod: the selftest passes a fake; pyserial is imported here so --selftest needs none.
    lines_low=True is only for a devkit behind a USB-serial bridge (tools/bench/hires_bench/capture.py on the peer board): it builds
    the port unopened and sets DTR and RTS low before open(); never used on the main board (USB Serial/JTAG)."""
    if serial_mod is None:
        import serial as serial_mod
    if unopened or lines_low:
        s = serial_mod.Serial()
        s.port, s.baudrate, s.timeout = port, 115200, timeout
        if lines_low:      # a bridge chip wires DTR/RTS to IO0/EN: both low before open() = no reset and no download mode
            s.dsrdtr = s.rtscts = False
            s.dtr = s.rts = False
        s.open()
    else:
        s = serial_mod.Serial(port, 115200, timeout=timeout)
    if drain_s > 0:
        drain(s, drain_s)
    return s


def find_ctl(line):
    """The JSON object after the first "CTL {" that parses, anywhere in the line; None when there is none.
    Two "CTL {" in one line: the first that parses wins (text after a parsed object is ignored, so a second reply behind
    it is not seen; one printf per reply plus the leading newline of ctl.c makes that case a stale fragment only)."""
    i = line.find(b"CTL {")
    while i >= 0:
        try:
            r, _ = json.JSONDecoder().raw_decode(line[i + 4:].decode("utf-8", "replace"))
            if isinstance(r, dict):
                return r
        except ValueError:
            pass
        i = line.find(b"CTL {", i + 1)
    return None


class _FakePort:
    """What the selftest drains: holds bytes to deliver, records writes, never blocks."""
    def __init__(self, held=b""):
        self.held, self.writes, self.opened = held, [], False
        self.port = self.baudrate = self.timeout = None

    def write(self, b): self.writes.append(b)

    def read(self, n):
        out, self.held = self.held[:n], self.held[n:]
        return out

    def open(self): self.opened = True


class _FakeSerialMod:
    """serial.Serial stand-in: remembers how it was built (plain call or unopened + attributes + open())."""
    last = None

    class Serial(_FakePort):
        def __init__(self, port=None, baud=None, timeout=None):
            _FakePort.__init__(self, b"stale-fragment")
            if port is not None:
                self.port, self.baudrate, self.timeout, self.opened = port, baud, timeout, True
            self.dtr_touched = False
            _FakeSerialMod.last = self


def configured_ports(root=None):
    """(main, peer) by-id paths of YOUR boards, or None for one that is not set: the PORT / PEER_ID environment variables, else
    .local/board.mk (this checkout, or the main checkout when this is a worktree). The same file the Makefile reads; nothing is built in."""
    import os, re, subprocess
    root = root or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cands = [os.path.join(root, ".local", "board.mk")]
    try:
        common = subprocess.run(["git", "-C", root, "rev-parse", "--path-format=absolute", "--git-common-dir"], capture_output=True, text=True).stdout.strip()
        if common:
            cands.append(os.path.join(os.path.dirname(common), ".local", "board.mk"))
    except OSError:
        pass
    mk = {}
    for c in cands:
        if os.path.isfile(c):
            for line in open(c, encoding="utf-8", errors="replace"):
                m = re.match(r"^\s*(PORT|PEER_ID)\s*[:?]?=\s*(\S+)", line)
                if m and m.group(1) not in mk:
                    mk[m.group(1)] = m.group(2)
    main = os.environ.get("PORT") or mk.get("PORT")
    pid = os.environ.get("PEER_ID") or mk.get("PEER_ID")
    return (main or None), (("/dev/serial/by-id/usb-%s-if00" % pid) if pid else None)


def selftest():
    bad = []

    def check(name, got, want):
        if got != want: bad.append("%s: got %r, wanted %r" % (name, got, want))

    check("reply at line start", find_ctl(b'CTL {"id":1,"ok":true}'), {"id": 1, "ok": True})
    check("reply glued after a stale fragment",
          find_ctl(b'psram_free=15264344 lCTL {"id":100,"ok":true}'), {"id": 100, "ok": True})
    check("trailing CR", find_ctl(b'CTL {"id":2}\r'), {"id": 2})
    check("two CTL { in one line: the first that parses wins",
          find_ctl(b'CTL {"id":1} CTL {"id":2}'), {"id": 1})
    check("first one broken, second parses", find_ctl(b'xx CTL {"id": CTL {"id":3}'), {"id": 3})
    check("CTL then broken JSON", find_ctl(b'CTL {"id":1,"ok":'), None)
    check("only the letters CTL", find_ctl(b'MEM CTL state ok'), None)
    check("CTL without brace", find_ctl(b'CTL status'), None)
    check("empty", find_ctl(b''), None)
    check("utf-8 body", find_ctl('CTL {"id":4,"n":"測"}'.encode()), {"id": 4, "n": "測"})

    p = _FakePort(b"MEM periodic interna")
    got = drain(p, 0.02)
    check("drain returns what the port held", got, b"MEM periodic interna")
    check("drain leaves nothing", p.held, b"")
    check("drain sends exactly one newline", p.writes, [b"\n"])

    m = _FakeSerialMod
    p = open_port("/dev/x", 0.5, 0.02, serial_mod=m)
    check("plain open: built with port, 115200, timeout", (p.port, p.baudrate, p.timeout, p.opened), ("/dev/x", 115200, 0.5, True))
    check("plain open drains and sends one newline", (p.held, p.writes), (b"", [b"\n"]))
    p = open_port("/dev/y", drain_s=0.02, unopened=True, serial_mod=m)
    check("unopened: attributes set then open()", (p.port, p.baudrate, p.timeout, p.opened), ("/dev/y", 115200, 0.2, True))
    check("unopened drains too", (p.held, p.writes), (b"", [b"\n"]))
    p = open_port("/dev/z", drain_s=0, serial_mod=m)
    check("drain_s=0: no newline, nothing read", (p.held, p.writes), (b"stale-fragment", []))
    p = open_port("/dev/w", drain_s=0, lines_low=True, serial_mod=m)
    check("lines_low: unopened build, DTR and RTS low, then open()", (p.port, p.opened, p.dtr, p.rts, p.writes), ("/dev/w", True, False, False, []))
    p = open_port("/dev/y", drain_s=0, unopened=True, serial_mod=m)
    check("without lines_low the lines are never touched", (hasattr(p, "dtr"), hasattr(p, "rts")), (False, False))

    if bad:
        for b in bad: print("boardport selftest FAIL:", b)
        return 1
    print("boardport selftest: ok")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    sys.exit(__doc__)
