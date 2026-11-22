#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Generates sim/fixture/: the synthetic data root of the desktop simulator (spec 2026-11-11-desktop-sim-design.md 3.3).
usage: sim_fixture.py [OUTDIR]     (default: sim/fixture, its generated directories rebuilt from scratch; a directory with anything else in it is refused; the output is byte-for-byte the same every run)

Everything is invented: 12 songs in 3 albums (one very long Chinese title, one title in kana, one track without tags), each a tiny
MP3 (ID3v2.4 tag, a Xing frame that declares the length, three silent frames - about 1.7 KB), the library index exactly as
library_svc / pc_library_scan would write it (tests rescan and compare), 2 favourites, 3 radio stations, a config with the volume.
No real music, covers or fonts."""
import os, shutil, struct, sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROOT = "/sdcard"                         # the firmware's name for the card; paths in the index use it
EXPORT = ROOT + "/export"

# (folder = album, artist, [(file, title or None, seconds, year-month-day, cover)])
LONG = "在那個沒有風也沒有雲的夏天午後我們一起走過長長的河堤並且約定明年再來"
ALBUMS = [
    ("夜航星圖", "白鷺合唱團", [
        ("01_harbor_light.mp3", "港口的燈", 214, "2024-03-15", True),
        ("02_star_chart.mp3", "星圖", 187, "2024-03-15", True),
        ("03_late_tide.mp3", "遲到的潮汐", 242, "2024-03-15", True),
        ("04_long_summer.mp3", LONG, 256, "2024-03-15", True)]),
    ("紙飛機工廠", "小橘子樂團", [
        ("01_paper_plane.mp3", "紙飛機", 156, "2023-09-02", True),
        ("02_nap.mp3", "午睡", 201, "2023-09-02", True),
        ("03_sakura_kaze.mp3", "さくらの風", 178, "2023-09-02", True),
        ("04_untitled_demo.mp3", None, 169, None, False)]),          # no tags at all: title = file name, artist = folder, no cover
    ("山間小火車", "阿拓與風", [
        ("01_first_train.mp3", "頭班車", 193, "2022-11-20", True),
        ("02_tunnel.mp3", "隧道裡的口哨", 225, "2022-11-20", True),
        ("03_station_tea.mp3", "月台上的熱茶", 207, "2022-11-20", True),
        ("04_last_stop.mp3", "終點站", 238, "2022-11-20", True)]),
]
FAVS = [(0, 2, 5), (2, 0, 3)]            # (album, song, plays): "mp3:<path>=1,<plays>"
RADIO = [("Sim FM 一號", "http://127.0.0.1:8000/one.mp3"), ("Sim 新聞台", "http://127.0.0.1:8000/news.aac"),
         ("Sim 古典", "http://127.0.0.1:8000/classic.mp3")]
VOLUME = 40
SUNRISE_THEME = '# test theme of the simulator: a warm base, a gold of its own, a third accent for the 說明 cells, one palette entry (dice face)\nbase = miku\nbg = #1A0F0A\npanel = #2B1A12\ngold = #FFE066\nhelp = #5A3A20\nhelp_text = #FFE8C8\ndice.0 = #FFF4D6\n'

# the sound presets of /sdcard/_player/config/sound (fx apps T3: migration, import): warm = the host test's preset_basic.toml, mixed = four apps with non-default values
MIXED_TOML = """format = 1
name = "Mixed"

[[music]]
kind = "gain"
params = { gain_db = -2.0 }

[[music]]
kind = "compressor"
params = { threshold_db = -20.0, ratio = 3.0 }

[[music]]
kind = "crossfeed"

[[master]]
kind = "limiter"
params = { ceiling_db = -1.0 }
"""
KITS = ["demo", "tabla"]                           # /sdcard/instrument/kits/<name>/
LOOPS = ["loop_001", "loop_002", "loop_003"]      # /sdcard/_player/loops/<name>.jcl


MARKER = ".sim_fixture"   # written by main(); a directory without it is never wiped

def syncsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def frame(fid, body):
    return fid.encode() + syncsafe(len(body)) + b"\0\0" + body


def text(fid, s):
    return frame(fid, b"\x03" + s.encode())


def id3(title, artist, album, track, date, cover):
    body = b"".join([text("TIT2", title), text("TPE1", artist), text("TALB", album), text("TRCK", f"{track}/4"), text("TDRC", date)])
    if cover:    # a stand-in for a JPEG: only the start and end markers; nothing ever decodes it
        body += frame("APIC", b"\x00image/jpeg\x00\x03\x00" + b"\xff\xd8\xff\xe0" + b"SIMCOVER" * 3 + b"\xff\xd9")
    return b"ID3\x04\x00\x00" + syncsafe(len(body)) + body


def mp3_frames(seconds):
    """MPEG-1 layer III, 44.1 kHz, 128 kbit/s (417-byte frames): a Xing frame with the frame count, then three silent frames."""
    hdr = b"\xff\xfb\x90\x00"
    n = round(seconds * 44100 / 1152)
    xing = hdr + b"\0" * 32 + b"Xing" + struct.pack(">II", 1, n)
    return xing.ljust(417, b"\0") + (hdr + b"\0" * 413) * 3


def song_file(album, artist, s, track):
    f, title, secs, date, cover = s
    tag = id3(title, artist, album, track, date, cover) if title is not None else b""
    return tag + mp3_frames(secs)


def build_index(songs):
    """songs: [(folder, file, title, artist, album, date_int, track, apic_off, apic_len)] in scan order; the byte layout of pc_library_save."""
    pool, tracks, folders = bytearray(), [], []

    def add(s):
        off = len(pool)
        pool.extend(s.encode() + b"\0")
        return off
    newest = 0
    for folder in sorted({s[0] for s in songs}, key=lambda x: x.encode()):
        mine = sorted([s for s in songs if s[0] == folder], key=lambda s: s[1].encode())
        first, name, fpath = len(tracks), add(folder), add(f"{EXPORT}/{folder}")
        for (fo, fi, title, artist, alb, date, track, ao, al) in mine:
            if not title: title = fi[:fi.rindex(".")]
            if not artist: artist = fo
            p = add(f"{EXPORT}/{fo}/{fi}")
            t, a, b = add(title), add(artist), add(alb)
            tracks.append(struct.pack("<IIIIIHHIIhhHH", p, t, a, b, date, track, len(folders), ao, al, -32768, -32768, 0, 0))
            newest = max(newest, date)
        n = len(tracks) - first                  # flat tree: no sub-folders, a child of the music root
        folders.append(struct.pack("<IIiiiiii", name, fpath, first, n, n, -1, 1, 0))
    hdr = b"JCLB" + struct.pack("<IIIIIII", 7, len(tracks), len(folders), len(pool), newest, 0, len(folders))   # v7: + the root's own songs (0) and sub-folders
    return hdr + b"".join(tracks) + b"".join(folders) + bytes(pool) + b"BLCJ"


# CC0 sample lyrics for the visualiser's lyrics kind (written for the simulator: no real song): one file next to the first song of the first album
LRC_SAMPLE = """[ti:港口的燈 (sample lyrics)]
[ar:JukeDial test fixture]
[by:CC0, written for the simulator: no real song]
[offset:0]
[00:02.00]港口的燈一盞一盞亮起來
[00:06.00]The harbour wakes up slowly
[00:10.00]潮水把白天的聲音帶走
[00:14.00]Small boats rock in the amber light
[00:18.00][01:10.00]在那個沒有風也沒有雲的夏天午後我們一起走過長長的河堤，並且約定明年再來一次，這一行故意寫得很長，用來看自動換行
[00:24.00]Hold the line, hold the line
[00:28.00]
[00:30.00]遠處的汽笛輕輕說晚安
[00:34.00]Count the stars above the pier, one two three four five six seven
[00:40.00]燈塔轉了一圈又一圈
[00:45.00]Lanterns hang along the quay
[00:50.00]船影慢慢靠岸
[00:55.00]And the night begins
"""

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "sim", "fixture")
    # only a directory this generator made (it holds MARKER) or an empty one is regenerated, and only the directories it generates are removed;
    # any other content means OUTDIR is not a fixture: refuse
    known = {"_player", "export", "instrument"}
    if os.path.isdir(out):
        have = set(os.listdir(out))
        extra = sorted(have - known - {MARKER})
        if extra: sys.exit(f"sim_fixture: {out} holds {extra}, not a fixture directory; refusing to touch it")
        if have and MARKER not in have: sys.exit(f"sim_fixture: {out} has no {MARKER} (this generator did not make it); refusing to touch it")
        for d in known: shutil.rmtree(os.path.join(out, d), ignore_errors=True)
    elif os.path.exists(out): sys.exit(f"sim_fixture: {out} is not a directory")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, MARKER), "w", encoding="utf-8") as f:
        f.write("generated by tools/simtools/sim_fixture.py; this directory may be regenerated\n")
    for d in ("config", "index"):
        os.makedirs(os.path.join(out, "_player", d))   # the device-owned folder (pc_paths.h PC_INT)
    songs, paths = [], []
    for album, artist, items in ALBUMS:
        os.makedirs(os.path.join(out, "export", album))
        paths.append([])
        for i, s in enumerate(items, 1):
            data = song_file(album, artist, s, i)
            with open(os.path.join(out, "export", album, s[0]), "wb") as f:
                f.write(data)
            has_tag = s[1] is not None
            apic_off = apic_len = 0
            if has_tag and s[4]:   # where id3() put the cover data: the APIC body after "\0image/jpeg\0" + type + empty description
                at = data.index(b"\xff\xd8\xff\xe0")
                apic_off, apic_len = at, len(data[at:].split(b"\xff\xd9")[0]) + 2
            date = int(s[3].replace("-", "")) if has_tag else 0
            songs.append((album, s[0], s[1] if has_tag else "", artist if has_tag else "", album if has_tag else "", date, i if has_tag else 0, apic_off, apic_len))
            paths[-1].append(f"mp3:{EXPORT}/{album}/{s[0]}")
            if album == ALBUMS[0][0] and i == 1:   # the lyrics sample next to the first song (song.mp3 -> song.lrc)
                with open(os.path.join(out, "export", album, s[0].rsplit(".", 1)[0] + ".lrc"), "w", encoding="utf-8", newline="\n") as f:
                    f.write(LRC_SAMPLE)
    for kit in KITS:           # the instrument app's data (--mode inst): two sample kits (a kit.txt each) and three looper files (inst_loopfile.h, inst_kit.h)
        os.makedirs(os.path.join(out, "instrument", "kits", kit))
        with open(os.path.join(out, "instrument", "kits", kit, "kit.txt"), "w", encoding="utf-8") as f:
            f.write(f"# synthetic kit {kit}: no samples\n")
    os.makedirs(os.path.join(out, "_player", "loops"))
    for name in LOOPS:
        with open(os.path.join(out, "_player", "loops", name + ".jcl"), "wb") as f:
            f.write(b"JCL synthetic loop " + name.encode() + b"\n")
    with open(os.path.join(out, "_player", "index", "library.bin"), "wb") as f:
        f.write(build_index(songs))
    with open(os.path.join(out, "_player", "config", "app.txt"), "w", encoding="utf-8") as f:
        f.write(f"volume={VOLUME}\nlang=0\n")   # i18n B: a cfg without "lang" is a first boot (the language picker)
    os.makedirs(os.path.join(out, "_player", "themes"))     # THEME: a test theme on the card (sim/test.sh THEME)
    with open(os.path.join(out, "_player", "themes", "sunrise.txt"), "w", encoding="utf-8", newline="\n") as f: f.write(SUNRISE_THEME)
    os.makedirs(os.path.join(out, "_player", "config", "sound"))
    shutil.copyfile(os.path.join(REPO, "components", "player_core", "host_test", "fixtures", "preset_basic.toml"), os.path.join(out, "_player", "config", "sound", "warm.toml"))
    with open(os.path.join(out, "_player", "config", "sound", "mixed.toml"), "w", encoding="utf-8") as f:
        f.write(MIXED_TOML)
    with open(os.path.join(out, "_player", "config", "radio.txt"), "w", encoding="utf-8") as f:
        f.write("# synthetic stations (nothing listens on these ports)\n" + "".join(f"{n}|{u}\n" for n, u in RADIO))
    with open(os.path.join(out, "_player", "config", "stats.txt"), "w", encoding="utf-8") as f:
        for a, i, plays in FAVS:
            f.write(f"{paths[a][i]}=1,{plays}\n")


if __name__ == "__main__":
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    main()
