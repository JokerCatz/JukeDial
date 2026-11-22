#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Library stress test (make lib-stress): fake indexes of 2k..50k tracks, measured in the host harness (tools/bench/lib_stress/lib_stress.c) and in the
desktop simulator. Prints one summary table. Not a gate; nothing here changes player behaviour.

usage: lib_stress.py [--sim PATH] [--only S1,S3] [--keep] [--no-sim]
  --sim PATH   simulator binary (default: .local/sim/perfbuild/sim = -O2 without sanitizers when present, else sim/build/sim = ASan, ~2-4x slower)
Work dir: .local/lib-stress/ (indexes are cached by scenario and generator arguments; the fixture copies live in a temp dir).
Host numbers are not the board's: the table also gives the PSRAM bytes the index takes on the board (sizeof structs + pool, exact) and the
allocation count of the scan (blocks < 1 KiB go to internal RAM there: CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024)."""
import argparse, os, re, shutil, struct, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "simtools"))   # tools/simtools: sibling tools live in other folders
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "gen"))   # tools/gen: sibling tools live in other folders

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import sim_con  # noqa: E402
import make_fake_library as mk  # noqa: E402

WORK = os.path.join(REPO, ".local", "lib-stress")
# name, label, make() arguments
SCEN = [
    ("S1", "2k tracks / 200 folders", dict(tracks=2000, folders=200)),
    ("S2", "20k / 2k", dict(tracks=20000, folders=2000)),
    ("S3", "1 folder of 5000", dict(tracks=5000, folders=1, big=5000)),
    ("S4", "50k / 5k", dict(tracks=50000, folders=5000)),
    ("S5", "long names 5k / 500", dict(tracks=5000, folders=500, long_names=True)),
    ("S6", "real sub-folder tree 5k / 500 leaves", dict(tracks=5000, folders=500, deep=True)),
    ("S7", "real tree 50k / 5k leaves + 200 root songs", dict(tracks=50000, folders=5000, deep=True, root_songs=200)),
]
HARNESS_SRC = ["pc_library", "pc_chipseq", "pc_id3", "pc_rg", "pc_utf", "pc_io", "pc_shuffle", "pc_stats", "pc_i18n", "pc_i18n_data"]


def build_harness():
    exe = os.path.join(WORK, "lib_stress")
    srcs = [os.path.join(REPO, "tools", "bench", "lib_stress", "lib_stress.c")] + [os.path.join(REPO, "components", "player_core", "src", s + ".c") for s in HARNESS_SRC]
    if os.path.exists(exe) and all(os.path.getmtime(exe) > os.path.getmtime(s) for s in srcs): return exe
    os.makedirs(WORK, exist_ok=True)
    subprocess.check_call(["gcc", "-O2", "-Wall", "-Wextra", "-I" + os.path.join(REPO, "components", "player_core", "include"), *srcs,
                           "-Wl,--wrap=malloc,--wrap=realloc,--wrap=calloc,--wrap=free,--wrap=strdup", "-lm", "-o", exe])
    return exe


def kv(text):
    return {k: v for k, v in (l.split("=", 1) for l in text.splitlines() if "=" in l)}


def make_index(name, a):
    d = os.path.join(WORK, name)
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, "library.bin")
    data, info = mk.make(a["tracks"], a["folders"], a.get("big", 0), a.get("long_names", False), a.get("deep", False), 1, a.get("root_songs", 0))
    if not (os.path.exists(p) and open(p, "rb").read() == data): open(p, "wb").write(data)
    return p, info


def lv(out):
    m = re.findall(rb"lv_bytes=(-?\d+)", out)
    return int(m[-1]) if m else None


def rss_kb(pid):
    try:
        for l in open(f"/proc/{pid}/status"):
            if l.startswith("VmHWM"): return int(l.split()[1])
    except OSError: pass
    return None


def sim_run(binary, index, big_folder):
    """-> dict of measurements; keys with 'err' when the simulator died"""
    fx = tempfile.mkdtemp(prefix="libstress_fx_")
    r = {}
    try:
        src = os.path.join(REPO, "sim", "fixture")
        shutil.copytree(src, fx, dirs_exist_ok=True, symlinks=True)
        shutil.rmtree(os.path.join(fx, "export"), ignore_errors=True)
        os.makedirs(os.path.join(fx, "export"))                                  # no audio files at all: the index is the whole library
        shutil.copyfile(index, os.path.join(fx, "_player", "index", "library.bin"))
        sim_con.CMD_TIMEOUT = 300.0
        t0 = time.perf_counter()
        con = sim_con.SimCon(binary=binary, src_root=fx, cfg_guard=False)
        cmd, last = None, None
        try:
            def run(c, wait=0.3):
                nonlocal cmd
                cmd = c
                t = time.perf_counter()
                out = con._send(c)
                if wait: out += con.drain(wait)
                return (time.perf_counter() - t) * 1000, out
            ms, out = run("uitop", 0)
            r["boot_ms"] = (time.perf_counter() - t0) * 1000
            m = re.search(rb"APP library loaded rc=(-?\d+) n=(\d+) folders=(\d+) ms=(\d+)", out)
            r["load_rc"] = int(m.group(1)) if m else None
            r["sim_n"] = int(m.group(2)) if m else None
            r["load_ms_sim"] = int(m.group(4)) if m else None
            r["rescanned"] = b"library rescan" in out
            _, o = run("mem", 0); r["lv_boot"] = lv(o)
            r["rss_boot_mb"] = (rss_kb(con.p.pid) or 0) / 1024
            png = os.path.join(fx, "shot.png")
            def shot(key):                                    # lv_snapshot: layout + a full render of the screen (headless draws only on demand), PNG write included
                r[key] = run(f"shot {png}", 0)[0]
            run("@1", 0)                                      # the simulator's fake cover service walks every track once in the first virtual tick (1.1 s of host CPU at 50k, nothing of the UI): let it finish before timing a page
            # folder list page
            ms, o = run("mp3ui"); r["folders_open_ms"] = ms
            shot("folders_shot_ms")
            _, o = run("mem", 0); r["lv_folders"] = lv(o)
            t = time.perf_counter(); n = 5
            for _ in range(n): run("flick left", 0.1); run(f"shot {png}", 0)
            r["page_turn_ms"] = (time.perf_counter() - t) * 1000 / n              # a side flick (next page: the rows of one page rebuilt) + draw
            _, o = run("mem", 0); r["lv_folders_p"] = lv(o)
            ms, o = run("knob 100000"); shot("folders_end_ms")      # one jump to the last row of the page, then the draw of that view
            t = time.perf_counter(); n = 20
            for _ in range(n): run("knob -1", 0); run(f"shot {png}", 0)
            r["folders_step_ms"] = (time.perf_counter() - t) * 1000 / n        # 20 detents up from the end, each followed by a draw
            run("back")
            _, o = run("mem", 0); r["lv_folders_closed"] = lv(o)
            # biggest folder's track list
            ms, o = run(f"uipush tracks{big_folder}"); r["tracks_open_ms"] = ms
            shot("tracks_shot_ms")
            _, o = run("mem", 0); r["lv_tracks"] = lv(o)
            run("knob 100000"); shot("tracks_end_ms")
            run("back")
            ms, o = run("uipush tracks-1"); r["all_tracks_open_ms"] = ms        # the music root level again (tracks-1), as the playing page's list opens a level
            _, o = run("mem", 0); r["lv_all_tracks"] = lv(o)
            run("back")
            _, o = run("mem", 0); r["lv_after"] = lv(o)
            r["rss_peak_mb"] = (rss_kb(con.p.pid) or 0) / 1024
            run("uitop", 0)
        except sim_con.SimCrash as e:
            r["crash"] = f"{cmd!r}: " + str(e).splitlines()[0]
            r["crash_tail"] = "\n".join(str(e).splitlines()[1:12])
        finally:
            con.close()
            r["sanitizer"] = con.sanitizer_output()[:3]
    finally:
        shutil.rmtree(fx, ignore_errors=True)
    return r


def scan_test(exe, kind):
    d = tempfile.mkdtemp(prefix="libstress_tree_", dir=WORK)
    try:
        if kind == "one5000":
            os.makedirs(os.path.join(d, "Album"))
            for i in range(5000): open(os.path.join(d, "Album", f"{i + 1:04d} track name {i}.mp3"), "w").close()
        elif kind == "tree3":                                                      # 10 genres x 20 artists x 10 albums x 3 files: 2,210 folders, 6,000 files, 3 levels
            for g in range(10):
                for a in range(20):
                    for b in range(10):
                        os.makedirs(os.path.join(d, f"Genre {g}", f"Artist {a:02d}", f"Album {b}"))
                        for j in range(3): open(os.path.join(d, f"Genre {g}", f"Artist {a:02d}", f"Album {b}", f"{j + 1:02d} song.mp3"), "w").close()
        else:
            for i in range(2000):
                os.makedirs(os.path.join(d, f"Album {i:04d}"))
                for j in range(2): open(os.path.join(d, f"Album {i:04d}", f"{j + 1:02d} song.mp3"), "w").close()
        return kv(subprocess.run([exe, "scan", d], capture_output=True, text=True, check=True).stdout)
    finally:
        shutil.rmtree(d, ignore_errors=True)


def f(x, fmt="{:.0f}"):
    return "-" if x is None else fmt.format(x)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim"); ap.add_argument("--only"); ap.add_argument("--no-sim", action="store_true")
    o = ap.parse_args()
    perf = os.path.join(REPO, ".local", "sim", "perfbuild", "sim")
    binary = o.sim or (perf if os.path.exists(perf) else os.path.join(REPO, "sim", "build", "sim"))
    if not o.no_sim and not os.path.exists(binary): sys.exit(f"lib_stress: no simulator at {binary} (make sim)")
    exe = build_harness()
    only = set(o.only.split(",")) if o.only else None
    rows = []
    for name, label, a in SCEN:
        if only and name not in only: continue
        idx, info = make_index(name, a)
        h = kv(subprocess.run([exe, "load", idx], capture_output=True, text=True, check=True).stdout)
        s = {} if o.no_sim else sim_run(binary, idx, int(h["biggest_folder"]))
        rows.append((name, label, info, h, s))
        print(f"{name} done", file=sys.stderr, flush=True)
    base = None if o.no_sim else sim_run(binary, os.path.join(REPO, "sim", "fixture", "_player", "index", "library.bin"), 0)

    print(f"\nlib-stress (sim: {'' if o.no_sim else os.path.relpath(binary, REPO)}; host harness gcc -O2; sim wall ms are real time, virtual time is not counted)\n")
    cols = [("", lambda n, l, i, h, s: n), ("tracks/folders", lambda n, l, i, h, s: f"{i['tracks']}/{i['folders']}"),
            ("idx file KB", lambda n, l, i, h, s: f(os.path.getsize(os.path.join(WORK, n, 'library.bin')) / 1024)),
            ("board PSRAM KB", lambda n, l, i, h, s: f(int(h["psram_est"]) / 1024)),
            ("pool KB", lambda n, l, i, h, s: f(int(h["pool_bytes"]) / 1024)),
            ("load ms (host)", lambda n, l, i, h, s: f(float(h["load_ms"]), "{:.1f}")),
            ("path_order ms", lambda n, l, i, h, s: f(float(h["path_order_ms"]), "{:.1f}")),
            ("pick ms all/nostats", lambda n, l, i, h, s: f(float(h["pick_all_nostats_per_ms"]), "{:.3f}")),
            ("pick ms all/favs", lambda n, l, i, h, s: f(float(h["pick_all_stats_per_ms"]), "{:.3f}")),
            ("pick ms bigfolder", lambda n, l, i, h, s: f(float(h["pick_bigfolder_stats_per_ms"]), "{:.3f}")),
            ("1000 picks s (favs)", lambda n, l, i, h, s: f(float(h["pick_all_stats_total_ms"]) / 1000, "{:.2f}")),
            ("pool weights build ms (favs)", lambda n, l, i, h, s: f(float(h["wsbuild_all_stats_ms"]), "{:.3f}")),
            ("NEW pick ms all/nostats", lambda n, l, i, h, s: f(float(h["wspick_all_nostats_per_ms"]), "{:.4f}")),
            ("NEW pick ms all/favs", lambda n, l, i, h, s: f(float(h["wspick_all_stats_per_ms"]), "{:.4f}")),
            ("NEW pick ms bigfolder", lambda n, l, i, h, s: f(float(h["wspick_bigfolder_stats_per_ms"]), "{:.4f}"))]
    if not o.no_sim:
        simcols = [("boot ms", "boot_ms"), ("lv boot", "lv_boot"), ("folders open ms", "folders_open_ms"), ("folders +draw ms", "folders_shot_ms"), ("lv folders", "lv_folders"), ("page turn+draw ms", "page_turn_ms"), ("lv after turns", "lv_folders_p"),
                   ("folders end+draw ms", "folders_end_ms"), ("draw per detent ms", "folders_step_ms"), ("lv closed", "lv_folders_closed"),
                   ("tracks open ms", "tracks_open_ms"), ("tracks +draw ms", "tracks_shot_ms"), ("lv tracks", "lv_tracks"), ("tracks end+draw ms", "tracks_end_ms"),
                   ("root via tracks-1 ms", "all_tracks_open_ms"), ("RSS boot MB", "rss_boot_mb"), ("RSS peak MB", "rss_peak_mb")]
        for t, k in simcols: cols.append((t, (lambda k: lambda n, l, i, h, s: f(s.get(k), "{:.1f}" if "ms" in k or "mb" in k else "{:.0f}"))(k)))
        cols.append(("sim n", lambda n, l, i, h, s: f(s.get("sim_n"))))
        cols.append(("crash", lambda n, l, i, h, s: "no" if "crash" not in s else "YES"))
    table = [[c[0] for c in cols]] + [[c[1](*r) for c in cols] for r in rows]
    table = [list(r) for r in zip(*table)]                      # metrics down, scenarios across
    table[0][0] = "metric"
    w = [max(len(r[i]) for r in table) for i in range(len(rows) + 1)]
    for r in table: print(r[0].ljust(w[0]) + "  " + "  ".join(c.rjust(w[i + 1]) for i, c in enumerate(r[1:])))
    print("scenarios: " + "; ".join(f"{n} = {l}" for n, l, *_ in rows))
    if base and not o.no_sim:
        print("\nreference, the 12-song fixture: " + ", ".join(f"{k}={f(base.get(k), '{:.1f}')}" for k in
              ["boot_ms", "lv_boot", "folders_open_ms", "folders_shot_ms", "lv_folders", "folders_end_ms", "folders_step_ms", "tracks_open_ms", "tracks_shot_ms", "lv_folders_closed", "rss_boot_mb", "rss_peak_mb"]))
    print("\nlv_* = LVGL bytes (MEM lines) with the page open; lv closed = after Back (the leak check). +draw = the same step followed by a screen render (shot, PNG write included, ~constant); end = one knob jump to the last row + draw; per detent = one detent + draw.")
    for name, label, info, h, s in rows:
        if s.get("crash"): print(f"\n{name} CRASH: {s['crash']}\n{s.get('crash_tail', '')}")
        if s.get("sanitizer"): print(f"{name} sanitizer: {s['sanitizer']}")
        if s.get("rescanned"): print(f"{name}: the simulator rescanned (index rejected)")
    print("\nscan allocations (host, pc_library_scan on empty .mp3 files; blocks < 1 KiB are internal RAM on the board):")
    hdrs = ["tree", "scan ms", "tracks", "small allocs", "small bytes total", "peak live small B", "peak live blocks", "peak live (16 B aligned)", "big allocs", "peak live all B"]
    t2 = [hdrs]
    for kind, lab in (("one5000", "1 folder x 5000 files"), ("many2000", "2000 folders x 2 files"), ("tree3", "tree 10x20x10 folders x 3 files")):
        k = scan_test(exe, kind)
        t2.append([lab, k["scan_ms"], k["n"], k["small_allocs"], k["small_bytes_total"], k["small_peak_live_bytes"], k["small_peak_live_blocks"], k["small_peak_live_16"], k["big_allocs"], k["peak_live_all"]])
    w = [max(len(str(r[i])) for r in t2) for i in range(len(hdrs))]
    for r in t2: print("  ".join(str(c).rjust(w[i]) for i, c in enumerate(r)))


if __name__ == "__main__":
    main()
