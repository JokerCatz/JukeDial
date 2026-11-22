#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Pulls the settings, library index, lists and fonts of the board's TF card into a simulator data root (default .local/sim_root).
usage: sim_root.py PORT [OUTDIR]       (PORT: the main board's by-id path; opens it like tf_get.py does: no reset)
       sim_root.py --placeholders ROOT  (no board: only step 3)
1. _player/config/app.txt, stats.txt, radio.txt, fx.txt (the effects' state; a card that has none yet is skipped with one line) and
   _player/index/library.bin through tf_get.py. Never _player/config/wifi.txt: _player/config/ is listed only to see whether fx.txt is there (names with "wifi" dropped at once, nothing else taken from it) and the Wi-Fi
   file is not in any list; names with "wifi" are also skipped in every listing.
2. every file in /sdcard/lists and /sdcard/_player/fonts, and every *.toml in /sdcard/_player/config/sound (the effect presets) (listed with the console's "ls").
3. the music itself (export/) is NOT copied (gigabytes): an empty placeholder file is made for every path in the library index, so the
   track pages show the real library; playing one plays a synthetic 180 s (the fake audio cannot read the data anyway)."""
import os, re, shutil, struct, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders

from boardport import configured_ports

HERE = os.path.dirname(os.path.abspath(__file__))
FILES = ["_player/config/app.txt", "_player/config/stats.txt", "_player/config/radio.txt", "_player/index/library.bin"]
OPTIONAL = ["_player/config/fx.txt"]                       # may not exist yet on a card that never saved an effect state
DIRS = (("lists", None), ("_player/fonts", None), ("_player/config/sound", ".toml"))   # (directory under /sdcard, required name suffix or None)
MAIN_PORT = configured_ports()[0]   # the only board this tool talks to: PORT / .local/board.mk
LS = re.compile(r"APP ls (.+) size=(-?\d+)\s*$")


def board_ls(port, remote):
    from boardport import open_port
    s = open_port(port)                              # plain open: DTR/RTS untouched, no reset; drops the stale fragment
    s.write(f"\nls {remote}\n".encode())
    buf, names, end = b"", [], time.time() + 20
    while time.time() < end:
        buf += s.read(s.in_waiting or 1)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.rstrip(b"\r").decode(errors="replace")
            if line.endswith("APP ls end"):
                s.close()
                return names
            m = LS.search(line)
            if m and m.group(1) not in (".", ".."): names.append((m.group(1), int(m.group(2))))
    s.close()
    raise SystemExit(f"ls {remote}: timeout")


def index_paths(index):
    """The track paths in a library.bin (the layout of pc_library_save: 32-byte header, 40-byte tracks, 32-byte folders, string pool, tail)."""
    d = open(index, "rb").read()
    magic, ver, n, nf, plen, newest, root_own, root_nchild = struct.unpack_from("<4sIIIIIII", d, 0)
    if magic != b"JCLB" or ver != 7: raise SystemExit(f"{index}: not an index this tool knows")
    pool = d[32 + n * 40 + nf * 32: 32 + n * 40 + nf * 32 + plen]
    out = []
    for i in range(n):
        p = struct.unpack_from("<I", d, 32 + i * 40)[0]
        out.append(pool[p:pool.index(b"\0", p)].decode())
    return out


def placeholders(root):
    dests = []
    for p in index_paths(os.path.join(root, "_player", "index", "library.bin")):      # validate every path before anything is created
        rel = p[len("/sdcard/"):]
        if not p.startswith("/sdcard/") or not rel or rel.startswith("/") or ".." in rel.split("/") or "\0" in p:
            sys.exit(f"index path not accepted (outside /sdcard, absolute after it, or has ..): {p!r}")
        base = os.path.realpath(root)
        dest = os.path.join(root, rel)
        if not os.path.realpath(dest).startswith(base + os.sep):     # a symlink under root must not lead out either
            sys.exit(f"index path not accepted (resolves outside {root}): {p!r}")
        dests.append(dest)
    made = 0
    for dest in dests:
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        if not os.path.exists(dest): open(dest, "wb").close(); made += 1
    print(f"placeholders: {made} empty files")


def main():
    if sys.argv[1:2] == ["--placeholders"]:
        return placeholders(sys.argv[2])
    port = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.dirname(HERE)), ".local", "sim_root")
    if port != MAIN_PORT: sys.exit("only the main board's by-id path is accepted (PORT in .local/board.mk)")
    jobs = [("/sdcard/" + f, f) for f in FILES]
    have = {n for n, _ in board_ls(port, "/sdcard/_player/config") if "wifi" not in n.lower()}   # only to see whether fx.txt exists: the names are not kept
    for f in OPTIONAL:
        if f.split("/")[-1] in have: jobs.append(("/sdcard/" + f, f))
        else: print(f"skip {f}: not on the card")
    for d, suffix in DIRS:
        for name, size in board_ls(port, f"/sdcard/{d}"):
            if size >= 0 and "wifi" not in name.lower() and "/" not in name and name not in ("", ".", "..") and (suffix is None or name.endswith(suffix)):
                jobs.append((f"/sdcard/{d}/{name}", f"{d}/{name}"))
    assert not any("wifi" in r.lower() or "wifi" in l.lower() for r, l in jobs)
    stage = tempfile.mkdtemp(prefix="sim_root_stage_")      # tf_get writes flat files here; directories under OUT are made only as files arrive
    try:
        staged = [(r, os.path.join(stage, str(i))) for i, (r, _) in enumerate(jobs)]
        cmd = [sys.executable, os.path.join(HERE, "..", "board", "tf_get.py"), port] + [x for j in staged for x in j]
        if subprocess.call(cmd) != 0: sys.exit("tf_get failed")
        for (_, tmp), (_, rel) in zip(staged, jobs):
            dest = os.path.join(out, rel)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.move(tmp, dest)
    finally:
        shutil.rmtree(stage, ignore_errors=True)
    placeholders(out)


if __name__ == "__main__":
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    main()
