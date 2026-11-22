#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Writes the four reference input WAVs of the fx reference test (spec 2026-11-13 fx-apps §8.5).

usage: fx_vectors.py <outdir>

All files: 48 kHz, stereo, float32 (WAVE_FORMAT_IEEE_FLOAT), written with the stdlib only. Deterministic: two runs
give byte-identical files.
  in_imp.wav     0.5 s, a unit impulse on the left at frame 100 and on the right at frame 2100
  in_sine.wav    1 s, 1 kHz at -6 dBFS, left only (right silent)
  in_noise.wav   1 s, uniform in [-0.5, 0.5), independent sides. Generator: SplitMix64 (state += 0x9E3779B97F4A7C15, then
                 the usual two xor-shift-multiply rounds), seed NOISE_SEED; the 24 top bits of each output, / 2^24, - 0.5;
                 drawn in the order frame 0 left, frame 0 right, frame 1 left, ...
  in_square.wav  1 s, 100 Hz square at +-0.7 on both sides (480 frames per period, high for the first 240)
"""
import math
import os
import struct
import sys

RATE = 48000
NOISE_SEED = 0x5EED0001
M64 = (1 << 64) - 1


def splitmix64(state):
    """Yields the SplitMix64 outputs of the seed."""
    while True:
        state = (state + 0x9E3779B97F4A7C15) & M64
        z = state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        yield z ^ (z >> 31)


def wav_bytes(frames):
    """frames: list of (left, right) floats -> a float32 stereo WAV."""
    data = b"".join(struct.pack("<ff", l, r) for l, r in frames)
    fmt = struct.pack("<HHIIHHH", 3, 2, RATE, RATE * 8, 8, 32, 0)   # IEEE float, 2 ch, byte rate, block align, bits, cbSize
    fact = struct.pack("<I", len(frames))
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"fact" + struct.pack("<I", len(fact)) + fact \
        + b"data" + struct.pack("<I", len(data)) + data
    return b"RIFF" + struct.pack("<I", len(body)) + body


def imp():
    n = RATE // 2
    f = [(0.0, 0.0)] * n
    f[100] = (1.0, 0.0)
    f[2100] = (0.0, 1.0)
    return f


def sine():
    a = 10 ** (-6 / 20)
    return [(a * math.sin(2 * math.pi * 1000 * i / RATE), 0.0) for i in range(RATE)]


def noise():
    g = splitmix64(NOISE_SEED)
    u = lambda: (next(g) >> 40) / float(1 << 24) - 0.5
    f = []
    for _ in range(RATE):
        l = u()
        r = u()
        f.append((l, r))
    return f


def square():
    return [((0.7 if (i % 480) < 240 else -0.7),) * 2 for i in range(RATE)]


def main():
    if len(sys.argv) == 2 and sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        return
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, make in (("in_imp", imp), ("in_sine", sine), ("in_noise", noise), ("in_square", square)):
        with open(os.path.join(out, name + ".wav"), "wb") as fh:
            fh.write(wav_bytes(make()))


if __name__ == "__main__":
    main()
