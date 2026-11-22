#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Regenerates the zip/gzip fixtures for test_zip.c (run from this directory)."""
import gzip, zipfile

text = b"".join(b"line %04d of a chiptune fixture\n" % i for i in range(120))   # ~4 KB, compresses well
with zipfile.ZipFile("stored.zip", "w", zipfile.ZIP_STORED) as z:
    z.writestr("b.txt", b"bee")
    z.writestr("a/hello.txt", b"hello world")
    z.writestr("ゲーム/曲.nsf", b"NESM\x1a")
with zipfile.ZipFile("deflate.zip", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("text.txt", text)
    z.writestr("empty.txt", b"")
# same as deflate.zip but with one byte of the compressed data flipped -> CRC mismatch
d = bytearray(open("deflate.zip", "rb").read())
i = d.find(b"text.txt") + len("text.txt") + 20          # inside the first entry's data
d[i] ^= 0x01
open("badcrc.zip", "wb").write(bytes(d))
open("text.txt.expected", "wb").write(text)
with open("tune.vgz", "wb") as f:
    f.write(gzip.compress(b"Vgm " + bytes(range(256)) * 4, mtime=0))
