#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Copy files onto the board's TF card over USB Serial/JTAG through the app's console. Does not reset the board
(DTR/RTS untouched). Big files (multi-MB decks) are quicker in USB drive mode; this is for when the cable is all there is.

usage: tf_put.py PORT [--fresh] [--drop P] [--chunk N] local_file /sdcard/remote/path [local2 remote2 ...]
  --fresh    start from 0 even when the card holds a <remote>.new from an earlier, interrupted try
  --drop P   fault injection: pretend this share (0..1) of the replies got lost (tests the resend path)
  --chunk N  raw bytes per line (default 2048, the device's maximum)

Protocol "pput" (app/main/console.c; log an internal note): the device writes <remote>.new;
every chunk carries its offset and every reply the size of <remote>.new, so after a lost reply the host asks the size
("pput ?") and goes on from there - a chunk the device already has is acknowledged without being written again. At the
end the device checks the SHA-256 of the whole file and only then renames <remote>.new over <remote>."""
import base64, hashlib, random, re, sys, time
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders
from boardport import open_port

args, opts = [], {"fresh": False, "drop": 0.0, "chunk": 2048}
argv = sys.argv[1:]
while argv:
    a = argv.pop(0)
    if a == "--fresh":
        opts["fresh"] = True
    elif a == "--drop":
        opts["drop"] = float(argv.pop(0))
    elif a == "--chunk":
        opts["chunk"] = int(argv.pop(0))
    else:
        args.append(a)
if len(args) < 3 or len(args) % 2 == 0:
    sys.exit(__doc__)
port, pairs = args[0], args[1:]
s = open_port(port)
buf = b""
stats = dict(lost=0, injected=0, resent_have=0, gaps=0, bad=0)
audio = []    # underruns of the player's 10 s "APP audio" lines seen meanwhile


def wait(pattern, seconds=3.0):
    """The first match after what was already consumed, or None on timeout."""
    global buf
    end = time.time() + seconds
    while True:
        m = re.search(pattern, buf)
        if m:
            audio.extend(int(u) for u in re.findall(rb"APP audio blocks=\d+ [^\r\n]*?underruns=(\d+)", buf[:m.end()]))
            buf = buf[m.end():]
            return m
        if time.time() > end:
            return None
        buf += s.read(s.in_waiting or 1)


def ask(cmd, pattern, seconds=3.0, tries=8):
    """Send cmd until pattern answers (the command must be safe to repeat)."""
    for _ in range(tries):
        s.write(cmd)
        m = wait(pattern, seconds)
        if m:
            return m
        stats["lost"] += 1
    raise SystemExit("no answer to %r" % cmd)


def size_now():
    return int(ask(b"pput ?\n", rb"APP pput size=(\d+)\r?\n").group(1))


def put(data, remote, fresh):
    sha = hashlib.sha256(data).hexdigest()
    m = ask(b"pput %s%s\n" % (b"-f " if fresh else b"", remote.encode()), rb"APP pput (ready|fail) size=(\d+)\r?\n")
    if m.group(1) != b"ready":
        raise SystemExit("%s: the device cannot write it" % remote)
    off = int(m.group(2))
    if off > len(data):
        return put(data, remote, True)
    resumed, step, fails, werr = off, opts["chunk"], 0, 0
    while off < len(data):
        c = data[off:off + step]
        s.write(b"@%d %d " % (off, len(c)) + base64.b64encode(c) + b"\n")
        m = wait(rb"APP pput (ok|gap|bad|err|fail) size=(\d+)\r?\n")
        if m and opts["drop"] and random.random() < opts["drop"]:
            stats["injected"] += 1
            if random.random() < 0.5:
                continue                         # as if lost: send the same chunk again (the device has it: "ok", no write)
            m = None                             # as if lost: ask the size
        if not m:
            fails += 1
            if fails > 20:
                raise SystemExit("%s: no progress at %d" % (remote, off))
            stats["lost"] += 1
            off = size_now()
            continue
        what, got = m.group(1), int(m.group(2))
        if what == b"err":                       # the card refused the write (full or failing): resending the same chunk cannot help
            werr += 1
            if werr >= 3:
                raise SystemExit("%s: card write error at offset %d (%d bytes on the card): the card is full or failing; stopped (exit 1)" % (remote, off, got))
        elif what != b"bad":
            werr = 0
        if what == b"fail":                      # the device closed the file (another command, 10 s silence): reopen
            return put(data, remote, False)
        if what == b"ok":
            fails = 0
            if got != off + len(c):
                stats["resent_have"] += 1        # the device had it already (an earlier reply was lost)
        elif what == b"gap":
            stats["gaps"] += 1
        else:
            stats["bad"] += 1                    # garbled line or a card write error: the device says where to go on
        off = got
    s.write(b"pput end %s\n" % sha.encode())     # not repeated: after "done" the .new is gone
    return wait(rb"APP pput (done|badsum|fail|busy)[^\r\n]*?size=(\d+)(?: sha=(\w*))?[^\r\n]*\r?\n", 120), sha, resumed


for local, remote in zip(pairs[0::2], pairs[1::2]):
    data = open(local, "rb").read()
    s.reset_input_buffer()
    buf = b""
    t0 = time.time()
    m, sha, resumed = put(data, remote, opts["fresh"])
    if m and m.group(1) == b"busy":
        raise SystemExit("%s: open on the device's screen (a deck or list): leave that screen and run this again (it resumes)" % remote)
    if m and m.group(1) == b"badsum":            # the .new held something else (an old try): once more from 0
        m, sha, resumed = put(data, remote, True)
    ok = bool(m) and m.group(1) == b"done"
    if not m:                                    # the "done" reply itself got lost: look at the file
        g = ask(b"sha %s\n" % remote.encode(), rb"GET end size=(\d+) sha256=(\w+)", 120, 2)
        ok = int(g.group(1)) == len(data) and g.group(2).decode() == sha
    dt = time.time() - t0
    print("%s: %d bytes in %.1f s = %.1f KB/s, sha256 %s; resumed at %d, replies lost %d (injected %d), "
          "already-there acks %d, gaps %d, bad %d" % (remote, len(data), dt, len(data) / 1024 / dt, "ok" if ok else "MISMATCH",
                                                     resumed, stats["lost"], stats["injected"], stats["resent_have"],
                                                     stats["gaps"], stats["bad"]))
    if audio:
        print("  player during the upload: %d \"APP audio\" lines, underruns per line %s" % (len(audio), audio))
    del audio[:]
    if not ok:
        raise SystemExit(1)
