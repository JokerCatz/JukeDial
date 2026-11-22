#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Writes gapless_a.mp3 + gapless_b.mp3: one continuous 440 Hz sine (amplitude 0.5, 44.1 kHz mono) cut in two,
50000 + 40000 samples. A is CBR 96k (LAME "Info" tag), B is VBR (LAME "Xing" tag); both carry the LAME
encoder delay / padding. Needs ffmpeg with libmp3lame. Run from anywhere; the files land next to it.
With an output folder argument it writes the board-test set instead: 1_gl_a.mp3 / 2_gl_b.mp3 (5 s + 4 s stereo,
ReplayGain track -6 dB, peak 0.5), 3_gl_a.flac / 4_gl_b.flac (same sine; track -3 dB peak 0.5, album +2 dB peak 0.9)
and an untagged 48 kHz 5_gl_c48.mp3 (the rate-change path). Played in order, A->B joins the sine without a seam."""
import math
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def sine(n0, n, fs=44100, f=440.0, amp=0.5):
    return struct.pack("<%dh" % n, *(int(round(32767 * amp * math.sin(2 * math.pi * f * (n0 + i) / fs))) for i in range(n)))


def enc(raw, out, args, fs=44100, ch=1):
    with tempfile.NamedTemporaryFile(suffix=".raw") as t:
        t.write(raw)
        t.flush()
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "s16le", "-ar", str(fs), "-ac", str(ch), "-i", t.name,
                        "-map_metadata", "-1", "-fflags", "+bitexact"] + args + [out], check=True)


def stereo(mono):
    s = struct.unpack("<%dh" % (len(mono) // 2), mono)
    return struct.pack("<%dh" % (2 * len(s)), *(v for x in s for v in (x, x)))


if len(sys.argv) > 1:
    d = sys.argv[1]
    a, b = stereo(sine(0, 5 * 44100)), stereo(sine(5 * 44100, 4 * 44100))
    rg_mp3 = ["-id3v2_version", "3", "-metadata", "REPLAYGAIN_TRACK_GAIN=-6.00 dB", "-metadata", "REPLAYGAIN_TRACK_PEAK=0.5"]
    rg_flac = ["-metadata", "REPLAYGAIN_TRACK_GAIN=-3.00 dB", "-metadata", "REPLAYGAIN_TRACK_PEAK=0.5",
               "-metadata", "REPLAYGAIN_ALBUM_GAIN=+2.00 dB", "-metadata", "REPLAYGAIN_ALBUM_PEAK=0.9"]
    enc(a, os.path.join(d, "1_gl_a.mp3"), ["-c:a", "libmp3lame", "-b:a", "192k", "-metadata", "title=GL A mp3"] + rg_mp3, ch=2)
    enc(b, os.path.join(d, "2_gl_b.mp3"), ["-c:a", "libmp3lame", "-q:a", "2", "-metadata", "title=GL B mp3"] + rg_mp3, ch=2)
    enc(a, os.path.join(d, "3_gl_a.flac"), ["-c:a", "flac", "-metadata", "title=GL A flac"] + rg_flac, ch=2)
    enc(b, os.path.join(d, "4_gl_b.flac"), ["-c:a", "flac", "-metadata", "title=GL B flac"] + rg_flac, ch=2)
    enc(stereo(sine(0, 3 * 48000, fs=48000)), os.path.join(d, "5_gl_c48.mp3"), ["-c:a", "libmp3lame", "-b:a", "192k", "-metadata", "title=GL C 48k"],
        fs=48000, ch=2)
else:
    enc(sine(0, 50000), os.path.join(HERE, "gapless_a.mp3"), ["-c:a", "libmp3lame", "-b:a", "96k"])
    enc(sine(50000, 40000), os.path.join(HERE, "gapless_b.mp3"), ["-c:a", "libmp3lame", "-q:a", "4"])
