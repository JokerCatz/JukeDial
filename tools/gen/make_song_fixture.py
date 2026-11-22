#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Synthetic stand-ins for the "song" test media (nothing here comes from a real recording).
Writes
  components/player_core/host_test/fixtures/song_head.mp3   60 KB: 48 kHz stereo, CBR 256 kbps, no tag, no Xing/Info frame,
                                                            cut to whole frames (78 x 768 B = 59904 B, about 1.87 s)
  an internal spike                               9 s, 44.1 kHz stereo Vorbis, 192 kbps (decode-cost spike input)
The sound is a deterministic mix of bell partials, a slow chord and band-limited noise (fixed seed), different on L and R.
Needs `lame` (MP3 path) and `ffmpeg` with libvorbis (Ogg path).  Exact commands:
  lame --quiet -t -b 256 --cbr -m j --noreplaygain in48k.wav out.mp3        (then truncated to whole frames)
  ffmpeg -v error -y -i in44k.wav -map_metadata -1 -fflags +bitexact -c:a libvorbis -b:a 192k out.ogg
Pure Python (no numpy).  Usage: make_song_fixture.py [--mp3-only]"""
import math, os, random, struct, subprocess, sys, tempfile, wave
if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
    print(__doc__); sys.exit(0)

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
CHORD = ((261.63, 329.63, 392.0), (220.0, 261.63, 329.63), (174.61, 220.0, 261.63), (196.0, 246.94, 392.0))

def render(rate, seconds):
    rnd = random.Random(20261122)
    n = int(rate * seconds)
    out = []
    lp = [0.0, 0.0]
    for i in range(n):
        t = i / rate
        ch = CHORD[int(t / 1.5) % 4]
        env = 0.5 + 0.5 * math.exp(-((t % 1.5) * 1.8))
        s = []
        for side in (0, 1):
            v = 0.0
            for k, f in enumerate(ch):
                f *= 1.0 + 0.002 * (side * 2 - 1)           # slight detune: L and R differ
                v += math.sin(2 * math.pi * f * t) + 0.3 * math.sin(2 * math.pi * 2.01 * f * t + k)
            lp[side] += 0.15 * (rnd.uniform(-1, 1) - lp[side])   # low-passed noise, so the encoder has real work
            v = 0.16 * env * v + 0.12 * lp[side]
            s.append(int(max(-1.0, min(1.0, v)) * 0.7 * 32767))
        out.append(struct.pack("<hh", *s))
    return b"".join(out)

def wav(path, rate, pcm):
    with wave.open(path, "wb") as o:
        o.setnchannels(2); o.setsampwidth(2); o.setframerate(rate); o.writeframes(pcm)

mp3_only = "--mp3-only" in sys.argv
with tempfile.TemporaryDirectory() as d:
    w = os.path.join(d, "in48k.wav")
    wav(w, 48000, render(48000, 2.2))
    m = os.path.join(d, "out.mp3")
    subprocess.run(["lame", "--quiet", "-t", "-b", "256", "--cbr", "-m", "j", "--noreplaygain", w, m], check=True)
    data = open(m, "rb").read()
    frame = 144 * 256000 // 48000                          # 768 bytes per frame at 256 kbps / 48 kHz
    assert data[:2] == b"\xff\xfb" or data[0] == 0xFF, "unexpected leading bytes"
    dst = os.path.join(ROOT, "components", "player_core", "host_test", "fixtures", "song_head.mp3")
    with open(dst, "wb") as f:
        f.write(data[:78 * frame])
    print("ok", dst, 78 * frame)
    if not mp3_only:
        w = os.path.join(d, "in44k.wav")
        wav(w, 44100, render(44100, 9.0))
        dst = os.path.join(ROOT, "spikes", "decode", "main", "loop.ogg")
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", w, "-map_metadata", "-1", "-fflags", "+bitexact",
                        "-c:a", "libvorbis", "-b:a", "192k", dst], check=True)
        print("ok", dst)
