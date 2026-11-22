#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Built-in chimes (spec 2026-11-10-clock-design.md): soft bell tones made here, so the alarm never depends on the card.
Writes app/main/sounds/alarm.mp3 (~10 s, loops) and chime.mp3 (~2 s) through lame (64 kbps mono CBR, 44.1 kHz).
Pure Python (no numpy)."""
import math, os, struct, subprocess, sys, tempfile, wave
if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
    print(__doc__); sys.exit(0)

RATE = 44100

def bell(freq, start, length, gain, buf):
    """a bell partial set with an exponential decay, added into buf"""
    partials = ((1.0, 1.0), (2.0, 0.35), (3.01, 0.12), (4.2, 0.05))
    n0, n = int(start * RATE), int(length * RATE)
    for i in range(n):
        t = i / RATE
        env = math.exp(-t * 2.2) * min(1.0, t * 400)          # 2.5 ms attack, gentle tail
        s = sum(a * math.sin(2 * math.pi * freq * k * t) for k, a in partials)
        if n0 + i < len(buf):
            buf[n0 + i] += gain * env * s

def render(notes, seconds, path):
    buf = [0.0] * int(seconds * RATE)
    for f, start, length, gain in notes:
        bell(f, start, length, gain, buf)
    peak = max(abs(x) for x in buf) or 1.0
    pcm = b"".join(struct.pack("<h", int(x / peak * 0.6 * 32767)) for x in buf)   # -4.4 dBFS peak: gentle
    with tempfile.TemporaryDirectory() as d:
        w = os.path.join(d, "x.wav")
        with wave.open(w, "wb") as o:
            o.setnchannels(1); o.setsampwidth(2); o.setframerate(RATE); o.writeframes(pcm)
        subprocess.run(["lame", "--quiet", "-b", "64", "-m", "m", "--resample", "44.1", w, path], check=True)

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "app", "main", "sounds")
C6, E6, G6, C7 = 1046.5, 1318.5, 1568.0, 2093.0
arp = lambda t0: [(C6, t0, 2.5, 0.5), (E6, t0 + 0.35, 2.5, 0.45), (G6, t0 + 0.7, 2.5, 0.42), (C7, t0 + 1.05, 2.8, 0.35)]
render(arp(0.0) + arp(4.0) + arp(7.0), 10.0, os.path.join(out, "alarm.mp3"))
render([(G6, 0.0, 1.8, 0.5), (C7, 0.25, 1.8, 0.4)], 2.0, os.path.join(out, "chime.mp3"))
print("ok", out)
