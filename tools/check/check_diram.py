#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""DIRAM guard: check_diram.py round|fakepod|round-stock|fakepod-stock [map]   (make size / size-fakepod / size-stock / size-fakepod-stock run it; --selftest tests it).
The internal-RAM image = .iram0.text + .iram0.data + .iram0.bss + .dram0.data + .dram0.bss of the linker map (the same number `idf.py size` prints as DIRAM).
It must not exceed its ceiling in tools/check/diram_budget.txt ("<target> <bytes>  # reason"). Over: exit 1 and print the biggest .bss/.data symbols of the map, the ones that
are new or grew since the last run first (the last run's symbols are kept next to the map as .diram_syms). Raising a ceiling = editing that file with a reason."""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MAPS = {"round": "app/build/jc_player.map", "fakepod": "app/build-fakepod/jc_player.map",
        "round-stock": "app/build-stock/jc_player.map", "fakepod-stock": "app/build-fakepod-stock/jc_player.map"}
SECS = (".iram0.text", ".iram0.data", ".iram0.bss", ".dram0.data", ".dram0.bss")


def budget(target, path=None):
    for ln in open(path or os.path.join(ROOT, "tools/check/diram_budget.txt")):
        m = re.match(r"([\w-]+)\s+(\d+)\s*(#.*)?$", ln.strip())
        if m and m[1] == target:
            if not m[3]: sys.exit(f"check_diram: {target} ceiling has no reason comment")
            return int(m[2])
    sys.exit(f"check_diram: no ceiling for {target} in diram_budget.txt")


def parse_map(text):
    """-> (total bytes, {symbol-or-object: size} for .dram0.data/.bss input sections)"""
    total, syms, cur = 0, {}, None
    pend = None
    for ln in text.splitlines():
        m = re.match(r"^(\.\S+)\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s*$", ln) or re.match(r"^(\.\S+)\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s", ln)
        if m and ln[0] == ".":
            cur = m[1]
            if cur in SECS: total += int(m[2], 16)
            continue
        if ln and ln[0] != " ": cur = None if not ln.startswith(".") else cur
        if cur not in (".dram0.data", ".dram0.bss"): continue
        m = re.match(r"^ (\.\S+)\s*$", ln)                      # name on its own line, size on the next
        if m: pend = m[1]; continue
        m = re.match(r"^\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+)", ln) if pend else re.match(r"^ (\.\S+)\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(\S+)", ln)
        if not m: continue
        name, sz, obj = (pend, int(m[1], 16), m[2]) if pend else (m[1], int(m[2], 16), m[3])
        pend = None
        if sz: syms[f"{name} {os.path.basename(obj)}"] = syms.get(f"{name} {os.path.basename(obj)}", 0) + sz
    return total, syms


def check(target, map_path, bpath=None, state=None):
    cap = budget(target, bpath)
    total, syms = parse_map(open(map_path, errors="replace").read())
    state = state or os.path.join(os.path.dirname(map_path), ".diram_syms")
    old = {}
    if os.path.exists(state):
        for ln in open(state):
            k, _, v = ln.rstrip("\n").rpartition("\t")
            if k: old[k] = int(v)
    if total <= cap:
        print(f"check_diram: {target} DIRAM {total} <= {cap} (headroom {cap - total})")
        open(state, "w").write("".join(f"{k}\t{v}\n" for k, v in syms.items()))
        return 0
    print(f"check_diram: FAIL {target} DIRAM {total} > ceiling {cap} (+{total - cap}); fix by moving statics to PSRAM (EXT_RAM_BSS_ATTR),"
          f" or raise the ceiling in tools/check/diram_budget.txt with a reason line")
    grew = sorted(((v - old.get(k, 0), k, v) for k, v in syms.items() if v > old.get(k, 0)), reverse=True)[:12]
    print("  new / grown .bss/.data symbols (bytes grown, size, symbol object):" if old else "  biggest .bss/.data symbols (no earlier run to compare):")
    for g, k, v in (grew if old else [(v, k, v) for k, v in sorted(syms.items(), key=lambda x: -x[1])[:12]]):
        print(f"  +{g:<7} {v:<7} {k}")
    return 1


def selftest():
    import tempfile
    d = tempfile.mkdtemp()
    mp, bp, st = (os.path.join(d, n) for n in ("m.map", "b.txt", "s"))
    open(bp, "w").write("t 1000  # test\nnoreason 5\n")
    def mapf(big):
        return (".iram0.text     0x2f000000      0x100\n.iram0.data     0x2f000100        0x0\n.dram0.data     0x2f000100      0x100\n"
                " .data.a        0x2f000100       0x40 x/liba.a(a.c.obj)\n.dram0.bss      0x2f000200      0x%x\n .bss.b         0x2f000200       0x10 x/libb.a(b.c.obj)\n"
                " .bss.big\n                0x2f000210      0x%x x/libc.a(c.c.obj)\n.flash.text     0x40000000     0x9999\n" % (0x100 + big, big))
    open(mp, "w").write(mapf(0)); assert check("t", mp, bp, st) == 0 and parse_map(mapf(0))[0] == 0x300
    open(mp, "w").write(mapf(0x400)); assert check("t", mp, bp, st) == 1
    s = parse_map(mapf(0x400))[1]; assert s.get(".bss.big libc.a(c.c.obj)".replace("libc.a(c.c.obj)", "libc.a(c.c.obj)")) == 0x400, s
    try: budget("noreason", bp); assert False
    except SystemExit: pass
    print("check_diram: selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]: selftest(); sys.exit(0)
    t = sys.argv[1]
    sys.exit(check(t, sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, MAPS[t])))
