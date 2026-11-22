#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Fake music index for the library stress test (make lib-stress): writes a valid _player/index/library.bin (JCLB, version 7, the byte layout
of pc_library_save) for N tracks in F folders, with no audio files behind it. Deterministic for a given --seed.

usage: make_fake_library.py OUT_LIBRARY_BIN [--tracks N] [--folders F] [--big-folder N] [--long-names] [--deep] [--seed S]
  --big-folder N  one folder holds N tracks; the other tracks (--tracks minus N) are spread over the other --folders-1 folders
  --long-names    folder / file / tag strings as long as the format and the card allow (path <= 511 bytes, title 127, artist 95, album 127), CJK + ASCII
  --deep          a real sub-folder tree: Genre/Artist/Album[/Disc n] (the folders are the leaves)
  --root-songs N  N of the tracks lie directly in the music root
The limits come from pc_library.c: path buffer 512 (/sdcard/export/<folder>/<file>), pc_id3_t title[128] artist[96] album[128], tracks <= 1e6,
folders <= 65535, pool <= 64 MiB. Paths use the firmware's /sdcard/export prefix."""
import argparse, os, random, struct, sys

EXPORT = "/sdcard/export"
CJK = "夜航星圖山間小火車紙飛機工廠港口的燈海風與雲夏天午後河堤約定明年再來櫻花雨後微光城市旅人時間沉默故事森林漂流島嶼晨曦黃昏電波搖籃曲"
WORDS = ("Harbor Light Star Chart Paper Plane Night Train Summer Tide Echo River Morning Lantern Velvet Orbit Quiet Garden Signal Neon Dust "
         "Window Paper Moon Silver Rain Ocean Slow Dance Wild Heart Small Town Blue Hour Polaroid Static Weather Drift Ember").split()
ARTISTS = ["白鷺合唱團", "Night Owls", "山間小火車", "The Quiet Hours", "Mio & the Tides", "雲端電台", "Paper Planes", "夜航星圖", "Low Tide Club", "小森林"]


def cut(s, nbytes):
    """longest prefix of s that fits nbytes of UTF-8 (never splits a character)"""
    b = s.encode()
    if len(b) <= nbytes: return s
    return b[:nbytes].decode("utf-8", "ignore")


def phrase(rng, nbytes, cjk_share=0.5):
    out = ""
    while len(out.encode()) < nbytes:
        out += (("".join(rng.choice(CJK) for _ in range(rng.randint(2, 8)))) if rng.random() < cjk_share else rng.choice(WORDS)) + (" " if rng.random() < 0.5 else "")
    return cut(out.rstrip(), nbytes)


def short_name(rng):
    n = rng.randint(2, 5)
    return (" ".join(rng.choice(WORDS) for _ in range(n))) if rng.random() < 0.6 else "".join(rng.choice(CJK) for _ in range(rng.randint(3, 9)))


def make(tracks, folders, big, long_names, deep, seed, root_songs=0):
    """the index of a tree (JCLB version 7, the layout of pc_library_save): tracks and folders depth first, a folder's own songs first, then its sub-folders in name order.
    Flat: F folders under the root. deep: F leaf folders under Genre/Artist/Album[/Disc n] (real sub-folders, a few songs at the intermediate levels too).
    root_songs of the tracks lie directly in the music root."""
    rng = random.Random(seed)
    folders = max(1, min(folders, 60000))
    tracks_left = max(0, tracks - root_songs)
    if big: counts = [big] + _spread(max(0, tracks_left - big), folders - 1, rng) if folders > 1 else [big]
    else: counts = _spread(tracks_left, folders, rng)
    leaves, seen = [], set()
    for i in range(len(counts)):
        if long_names: path = (f"{i:05d} " + phrase(rng, 150, 0.6),)             # folder 150 B: path = 15 + 1 + 150 + 1 + file 250 = 417 B, under 511
        elif deep: path = (rng.choice(["Rock", "Jazz", "古典", "電子", "Folk", "OST"]), rng.choice(ARTISTS), short_name(rng), f"Disc {rng.randint(1, 3)}" if rng.random() < 0.5 else f"{i:05d}")
        else: path = (f"{i:05d} " + short_name(rng),)
        if path in seen: path = path[:-1] + (path[-1] + f"-{i}",)
        seen.add(path); leaves.append(path)
    own = {}                                                                      # path tuple -> own song count; () = the music root
    for pth, c in zip(leaves, counts): own[pth] = own.get(pth, 0) + c
    if root_songs: own[()] = root_songs
    kids = {}
    for pth in list(own):                                                         # every ancestor exists, with no songs of its own unless it was a leaf too
        for d in range(len(pth)):
            own.setdefault(pth[:d], 0)
    for pth in own:
        if pth: kids.setdefault(pth[:-1], []).append(pth)
    for v in kids.values(): v.sort(key=lambda x: x[-1].encode())                  # the scan sorts by strcmp
    pool, trk, fol = bytearray(), [], []

    def add(sv):
        off = len(pool)
        pool.extend(sv.encode() + b"\0")
        return off
    stats = dict(newest=0, biggest=max(counts + [root_songs]))

    def songs(pth, fidx):
        name = pth[-1] if pth else ""
        files, files_seen = [], set()
        for k in range(own[pth]):
            if long_names: base = f"{k + 1:05d} " + phrase(rng, 240, 0.6)
            else: base = f"{k + 1:02d} " + short_name(rng)
            f = base + ".mp3"
            if f in files_seen: f = f"{base}-{k}.mp3"
            files_seen.add(f); files.append(f)
        files.sort(key=lambda x: x.encode())
        album = (phrase(rng, 127, 0.6) if long_names else short_name(rng))
        artist = rng.choice(ARTISTS) if not long_names else phrase(rng, 95, 0.5)
        for k, f in enumerate(files):
            path = "/".join((EXPORT,) + pth + (f,))
            assert len(path.encode()) <= 511, len(path.encode())
            title = phrase(rng, 127, 0.6) if long_names else (f[3:-4] if rng.random() < 0.7 else short_name(rng))
            tr = rng.choice(ARTISTS) if (not long_names and rng.random() < 0.2) else artist
            date = (rng.randint(2012, 2026) * 10000 + rng.randint(1, 12) * 100 + rng.randint(1, 28)) if rng.random() < 0.8 else 0
            if rng.random() < 0.1: date = 20261101 + rng.randint(0, 12)   # a tenth are recent: the weighted shuffle has weights 4/2/1
            rg = (rng.randint(-1200, 300), rng.randint(-1200, 300), rng.randint(3000, 10000), rng.randint(3000, 10000)) if rng.random() < 0.5 else (-32768, -32768, 0, 0)
            apic = (rng.randint(2000, 60000), rng.randint(20000, 400000)) if rng.random() < 0.6 else (0, 0)
            trk.append(struct.pack("<IIIIIHHIIhhHH", add(path), add(title), add(tr), add(album), date, (k + 1) & 0xFFFF, fidx if pth else 0xFFFF, apic[0], apic[1], *rg))
            stats["newest"] = max(stats["newest"], date)

    def walk(pth, parent):
        """pre-order; returns the folder's number (None for the root); a folder with no song anywhere below is not made (as the scan does)"""
        if not pth:
            songs((), -1)
            nchild = sum(1 for c in kids.get((), []) if walk(c, -1) is not None)
            return nchild
        # does anything playable live below? (an empty sub-tree is skipped, like pc_library_scan)
        if not any(own[q] for q in own if q[:len(pth)] == pth): return None
        fidx = len(fol); first = len(trk)
        fol.append(None)
        nm, pa = add(pth[-1]), add("/".join((EXPORT,) + pth))
        songs(pth, fidx)
        nown = len(trk) - first
        nchild = sum(1 for c in kids.get(pth, []) if walk(c, fidx) is not None)
        fol[fidx] = struct.pack("<IIiiiiii", nm, pa, first, nown, len(trk) - first, parent, len(fol) - fidx, nchild)
        return fidx
    root_nchild = walk((), -1)
    root_own = own.get((), 0)
    assert len(pool) < 64 << 20 and len(fol) < 65535
    hdr = b"JCLB" + struct.pack("<IIIIIII", 7, len(trk), len(fol), len(pool), stats["newest"], root_own, root_nchild)
    return hdr + b"".join(trk) + b"".join(fol) + bytes(pool) + b"BLCJ", dict(tracks=len(trk), folders=len(fol), pool=len(pool), biggest=stats["biggest"])


def _spread(total, n, rng):
    """total tracks over n folders, 1..many each (uneven, like a real library); every folder gets at least one if total >= n"""
    if n <= 0: return []
    base = [1 if total >= n else 0] * n
    for _ in range(total - sum(base)): base[min(n - 1, int(rng.random() ** 2 * n))] += 1   # skewed: low indexes get more
    rng.shuffle(base)
    return base


def main():
    a = argparse.ArgumentParser()
    a.add_argument("out"); a.add_argument("--tracks", type=int, default=2000); a.add_argument("--folders", type=int, default=200)
    a.add_argument("--big-folder", type=int, default=0); a.add_argument("--long-names", action="store_true"); a.add_argument("--deep", action="store_true")
    a.add_argument("--seed", type=int, default=1); a.add_argument("--root-songs", type=int, default=0)
    o = a.parse_args()
    if o.tracks > 1_000_000: sys.exit("the format allows 1,000,000 tracks")
    data, info = make(o.tracks, o.folders, o.big_folder, o.long_names, o.deep, o.seed, o.root_songs)
    os.makedirs(os.path.dirname(os.path.abspath(o.out)), exist_ok=True)
    with open(o.out, "wb") as f: f.write(data)
    print(f"{o.out}: tracks={info['tracks']} folders={info['folders']} pool={info['pool']} biggest_folder={info['biggest']} file={len(data)}")


if __name__ == "__main__":
    main()
