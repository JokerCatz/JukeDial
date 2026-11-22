#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Build the FLAC fixture blob for tools/bench/hires_bench (written into the "fix" partition at flash time).
Usage: gen_fixtures.py <level 0..12> <out.bin>
Encoder: ffmpeg's FLAC encoder (the host has no `flac` CLI), -compression_level <level>, fixed block size
(-frame_size, 4096 like libFLAC's default; one 192k file with 16384 for the decoder-RAM scaling). 4 s per file.
Signal (deterministic, no download): partials with slow amplitude motion (L: 220 Hz harmonic stack + 329.63 Hz +
3.52 kHz + 12 kHz; R: 110 Hz stack + 392 Hz + 5 kHz + 15 kHz) + pink noise (anoisesrc seed 12345, amplitude 0.03)
so the LPC residual is not trivial. 24-bit for 96k/192k/384k, 16-bit for the 44.1k reference.
Blob: 'HBFX' u32 count, then count x 48 B {char name[24]; u32 fs, bits, level, blocksize, offset, size}; files 4 KB aligned."""
import os, struct, subprocess, sys, tempfile

lvl = int(sys.argv[1]); out = sys.argv[2]
SETS = [(44100, 16, 4096), (96000, 24, 4096), (192000, 24, 4096), (384000, 24, 4096)]
if lvl == 5:
    SETS.append((192000, 24, 16384))
EXPR = ("0.12*(0.6+0.4*sin(2*PI*0.7*t))*(sin(2*PI*220*t)+0.5*sin(2*PI*440*t)+0.33*sin(2*PI*660*t)+0.25*sin(2*PI*880*t))"
        "+0.08*sin(2*PI*329.63*t)*abs(sin(2*PI*1.3*t))+0.03*sin(2*PI*3520*t)+0.01*sin(2*PI*12000*t)|"
        "0.10*(0.6+0.4*sin(2*PI*0.9*t))*(sin(2*PI*110*t)+0.5*sin(2*PI*220*t)+0.33*sin(2*PI*330*t))"
        "+0.07*sin(2*PI*392*t)*abs(sin(2*PI*1.7*t))+0.02*sin(2*PI*5000*t)+0.008*sin(2*PI*15000*t)")
files = []
with tempfile.TemporaryDirectory() as td:
    for fs, bits, bs in SETS:
        p = os.path.join(td, f"{fs}_{bits}_{bs}.flac")
        graph = (f"aevalsrc=exprs='{EXPR}':s={fs}:d=4[a];anoisesrc=color=pink:seed=12345:amplitude=0.03:r={fs}:d=4,"
                 f"pan=stereo|c0=c0|c1=c0[n];[a][n]amix=inputs=2:normalize=0")
        fmt = ["-sample_fmt", "s16"] if bits == 16 else ["-sample_fmt", "s32", "-bits_per_raw_sample", "24"]
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-bitexact", "-filter_complex", graph, "-c:a", "flac", *fmt,
                        "-compression_level", str(lvl), "-frame_size", str(bs), p], check=True)
        files.append((f"{fs}_{bits}_l{lvl}_b{bs}", fs, bits, bs, open(p, "rb").read()))
hdr = 8 + len(files) * 48
off = (hdr + 4095) & ~4095
blob = bytearray(b"HBFX" + struct.pack("<I", len(files)))
body = bytearray()
for name, fs, bits, bs, data in files:
    blob += struct.pack("<24s6I", name.encode(), fs, bits, lvl, bs, off + len(body), len(data))
    print(f"fixture {name}: {len(data)} B ({len(data) * 8 / 4 / 1000:.0f} kbps)")
    body += data + b"\0" * ((-len(data)) % 4096)
blob += b"\0" * (off - len(blob)) + body
open(out, "wb").write(blob)
print(f"blob {out}: {len(blob)} B")
