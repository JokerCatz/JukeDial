#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Synthesise the instrument app's demo kit (stage 4): 8 short drum-ish WAVs + kit.txt, and one tone + tone.sfz
(the SFZ subset, pitched across the keyboard). Mostly 48 kHz mono 16-bit; the clap is 44.1 kHz and the tone
24 kHz so the resampler gets used. Total about 240 KB.
usage: make_demo_kit.py <out_dir>   then upload to /sdcard/instrument/kits/demo/ with tools/board/tf_put.py"""
import math, os, random, struct, sys, wave
if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
    print(__doc__); sys.exit(0)

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
rnd = random.Random(7)


def write(name, samples, rate=48000):
    peak = max(1e-9, max(abs(x) for x in samples))
    g = 0.89 / peak                                   # about -1 dBFS
    with wave.open(os.path.join(out, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, x * g)) * 32767)) for x in samples))


def env(t, decay):
    return math.exp(-t / decay)


def noise_hp(n, a=0.85):
    """white noise through a one-pole high-pass"""
    y, prev_x, prev_y = [], 0.0, 0.0
    for _ in range(n):
        x = rnd.uniform(-1, 1)
        prev_y = a * (prev_y + x - prev_x)
        prev_x = x
        y.append(prev_y)
    return y


def sweep(dur, f0, f1, decay, rate=48000, click=0.0):
    n, ph, s = int(dur * rate), 0.0, []
    for i in range(n):
        t = i / rate
        f = f1 + (f0 - f1) * math.exp(-t / 0.04)
        ph += 2 * math.pi * f / rate
        x = math.sin(ph) * env(t, decay)
        if click and t < 0.004:
            x += click * rnd.uniform(-1, 1) * (1 - t / 0.004)
        s.append(x)
    return s


R = 48000
write("kick.wav", sweep(0.40, 160, 45, 0.12, click=0.6))
n = int(0.25 * R)
hp = noise_hp(n, 0.7)
write("snare.wav", [0.8 * hp[i] * env(i / R, 0.06) + 0.6 * math.sin(2 * math.pi * 190 * i / R) * env(i / R, 0.035) for i in range(n)])
n = int(0.08 * R)
hp = noise_hp(n, 0.95)
write("hat_closed.wav", [hp[i] * env(i / R, 0.018) for i in range(n)])
n = int(0.35 * R)
hp = noise_hp(n, 0.95)
write("hat_open.wav", [hp[i] * env(i / R, 0.11) for i in range(n)])
RC = 44100
n = int(0.25 * RC)
hp = noise_hp(n, 0.8)
clap = []
for i in range(n):
    t = i / RC
    e = sum(env(t - d, 0.008) for d in (0.0, 0.011, 0.022) if t >= d) + 0.6 * (env(t - 0.03, 0.07) if t >= 0.03 else 0)
    clap.append(hp[i] * e)
write("clap.wav", clap, RC)
write("tom_low.wav", sweep(0.35, 130, 85, 0.13))
write("tom_high.wav", sweep(0.30, 210, 150, 0.11))
n = int(0.05 * R)
write("rim.wav", [(0.7 * math.sin(2 * math.pi * 820 * i / R) + 0.5 * rnd.uniform(-1, 1)) * env(i / R, 0.008) for i in range(n)])
# tone: C4 (261.63 Hz), a few harmonics, plucked envelope, 24 kHz
RT = 24000
n = int(0.8 * RT)
f = 261.63
write("tone.wav", [sum(a * math.sin(2 * math.pi * f * h * i / RT) for h, a in ((1, 1.0), (2, 0.4), (3, 0.2), (4, 0.1)))
                   * min(1.0, i / 48) * env(i / RT, 0.25) for i in range(n)], RT)

open(os.path.join(out, "kit.txt"), "w").write("""# demo kit (tools/gen/make_demo_kit.py): GM drum notes, channel 10
36=kick.wav
37=rim.wav
38=snare.wav
39=clap.wav
42=hat_closed.wav
45=tom_low.wav
46=hat_open.wav
50=tom_high.wav
pad1=36
pad2=38
pad3=42
pad4=46
pad5=39
pad6=37
pad7=45
pad8=50
""")
open(os.path.join(out, "tone.sfz"), "w").write("""// demo: one synthesised tone across the keyboard (the SFZ subset: sample, lokey, hikey, pitch_keycenter)
<region> sample=tone.wav lokey=36 hikey=96 pitch_keycenter=60
""")
total = sum(os.path.getsize(os.path.join(out, f)) for f in os.listdir(out))
print(f"demo kit: {len(os.listdir(out))} files, {total} bytes in {out}")
