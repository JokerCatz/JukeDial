#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Pack font files into the image for the "fonts" flash partition (format: components/player_core/include/pc/pc_fontpak.h).

usage: make_fontpak.py <out.bin> <partition size> file [file ...]
       make_fontpak.py --check <image.bin>      print the directory and verify every CRC
Each file is stored under its base name (the app opens /flashfont/<name>). Data is 16-byte aligned.
The partition size (e.g. 0x400000) is only checked against: the image is not padded."""
import os, struct, sys, zlib

MAGIC, VERSION, HDR, ENTRY, NAME, MAX = b"JCFP", 1, 16, 48, 32, 8


def build(files):
    if not 0 < len(files) <= MAX:
        sys.exit(f"1..{MAX} files")
    off = HDR + ENTRY * len(files)
    entries, blobs = b"", []
    for path in files:
        name = os.path.basename(path).encode()
        if len(name) >= NAME:
            sys.exit(f"name too long: {name!r}")
        data = open(path, "rb").read()
        off = (off + 15) & ~15
        entries += name.ljust(NAME, b"\0") + struct.pack("<IIII", off, len(data), zlib.crc32(data), 0)
        blobs.append((off, data))
        off += len(data)
    img = bytearray(off)
    img[HDR:HDR + len(entries)] = entries
    for o, d in blobs:
        img[o:o + len(d)] = d
    struct.pack_into("<4sHH", img, 0, MAGIC, VERSION, len(files))
    struct.pack_into("<I", img, 12, len(img))
    struct.pack_into("<I", img, 8, zlib.crc32(img[12:HDR + len(entries)]))
    return bytes(img)


def check(img):
    magic, ver, n, crc, size = struct.unpack_from("<4sHHII", img, 0)
    assert magic == MAGIC and ver == VERSION and 0 < n <= MAX, "bad header"
    assert zlib.crc32(img[12:HDR + ENTRY * n]) == crc, "directory CRC"
    assert size == len(img), "image size"
    for i in range(n):
        name, o, sz, c, _ = struct.unpack_from(f"<{NAME}sIIII", img, HDR + ENTRY * i)
        ok = zlib.crc32(img[o:o + sz]) == c and o + sz <= size
        print(f"{name.rstrip(b'\0').decode():24s} offset=0x{o:06x} size={sz:8d} crc={c:08x} {'ok' if ok else 'BAD'}")
        assert ok


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        check(open(sys.argv[2], "rb").read())
        return
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    out, part, files = sys.argv[1], int(sys.argv[2], 0), sys.argv[3:]
    img = build(files)
    if len(img) > part:
        sys.exit(f"image {len(img)} bytes > partition {part}")
    open(out, "wb").write(img)
    check(img)
    print(f"{out}: {len(img)} bytes ({100 * len(img) / part:.0f}% of the partition)")


if __name__ == "__main__":
    main()
