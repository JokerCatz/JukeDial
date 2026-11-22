#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""tools/gen/fx_vectors.py: two runs are byte-identical, four files, each a float32 48000 Hz stereo WAV, and every file's sha256 is
the one in fixtures/fx/vectors.sha256 (a generator change shows up there). The fx-lab renders (outside git) that are present
are checked against the same list: listed -> must match ("stale render" otherwise); not listed -> a line says so.
usage: test_fxvectors.py <repo root> <vectors.sha256> <renders dir (may be empty or missing)>"""
import hashlib, os, struct, subprocess, sys, tempfile

root, shafile, renders = sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else ""
tool = os.path.join(root, "tools", "gen", "fx_vectors.py")
listed = {}
for line in open(shafile):
    if line.strip():
        h, n = line.split()
        listed[n] = h
NAMES = ["in_imp.wav", "in_noise.wav", "in_sine.wav", "in_square.wav"]
sha = lambda b: hashlib.sha256(b).hexdigest()
with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
    for d in (a, b):
        subprocess.run([sys.executable, "-I", tool, d], check=True)
    assert sorted(os.listdir(a)) == NAMES, os.listdir(a)
    for n in NAMES:
        da, db = open(os.path.join(a, n), "rb").read(), open(os.path.join(b, n), "rb").read()
        assert da == db, n + ": two runs differ"
        assert da[:4] == b"RIFF" and da[8:12] == b"WAVE" and da[12:16] == b"fmt ", n
        fmt, ch, rate, _, _, bits = struct.unpack("<HHIIHH", da[20:36])
        assert (fmt, ch, rate, bits) == (3, 2, 48000, 32), (n, fmt, ch, rate, bits)
        assert listed.get(n) == sha(da), n + ": generated file differs from vectors.sha256 (generator changed?)"
nr = 0
if renders and os.path.isdir(renders):
    for n in sorted(os.listdir(renders)):
        if not n.endswith(".wav"):
            continue
        h = sha(open(os.path.join(renders, n), "rb").read())
        nr += 1
        if n in listed:
            assert listed[n] == h, n + ": stale render (sha256 differs from vectors.sha256)"
        else:
            print("render %s is not listed in vectors.sha256 (compared anyway by the host test)" % n)
print("fx_vectors ok: 4 files, float32 48000 Hz 2 ch, deterministic, sha256 as listed; %d render(s) checked" % nr)
