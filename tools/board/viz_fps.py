#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Spectrum page ("viz") frame rates on the main board, compute and drawing apart (board batch 2026-11-12, an internal note).
usage: viz_fps.py A|cmp|B [--out DIR] [--only bars_v0,ring_o_zigzag_g4] [--dry]      viz_fps.py table FILE.jsonl
  A    every row of rows() (30 on the round board; names below): the kind x variant rows (bars 6, ring 4, scope 3, fall 3, phase 2; every one with the stored defaults order seq, gap 2, size box), the
       waterfall at full size (fall_v<n>_full), and the nine ring order x gap combinations on ring variant 0 (ring_o<seq|zigzag|spread>_g<2|0|4>; ring_oseq_g2 repeats ring_v0 on purpose: the grid is complete). The voices kind
       (3 variants) needs a chip track: measure it by hand on an NSF/GBS/SPC, `viz voices`, `viz stat`. Per row: restart track 3 (trackat 3 20: no track change inside the window), viz order/gap/size, viz <kind>, viz var <n>, 13 s (the title
       hides after 10 s), viz stat (kind/var/axes/title=0 hint=0 checked; zeroes the draw counters), uistat reset, 20 s, uistat + viz stat + mem.
       A row that does not check out is repeated (up to 3 times).
  cmp  bars/ring variant 0 with the title visible (2 x 9 s after re-entering), with the style ring kept open (viz menu every 3 s), the cover page.
  B    VIZ_STEREO verdict: 3 rounds of viz bars 20 s / viz phase 20 s; compare the APP audio blk_max_us medians, underruns must be 0.
  --dry  no board: a scripted stand-in for the console on a virtual clock (the rows, the checks, the restore and the table run for real); what the script itself is tested with.
  --sim  no board: the desktop simulator (tools/simtools/sim_con.py) on its virtual clock; its numbers mean nothing, it checks the steps and that every setting is put back
         (sim/test.sh fixBoardA_vizfps_restores; exit 1 when a setting is left changed).
Results: one JSON line per window in DIR/<mode>.jsonl, the raw console of each window in DIR/rows/<name>.txt (default DIR .local/viz_fps).
Screen on (ui_wake every 8 s: console commands do not count as activity for the screen timeout), volume 0; the kind and every variant are
put back to what they were at the start. uistat's frame time does not include the page's draw function (viz stat line 2): cost per tick = both.
Needs pyserial (PY=$(ls -d ~/.espressif/python_env/*/bin/python | head -1)). Main board by-id port only."""
import argparse, json, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "simtools"))   # tools/simtools: sibling tools live in other folders
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cfg_keep

PORT = None     # your main board: PORT / .local/board.mk (tools/boardport.py configured_ports)
RED = re.compile(r"\b(ssid|bssid|ip|mac|pass|psk|password)\b([=: ]+)\S+", re.I)
class Con:
    def __init__(self, logf=None, echo=True):
        from boardport import open_port, configured_ports       # the table mode runs without pyserial (boardport imports it when it opens)
        port = PORT or configured_ports()[0]
        if not port: sys.exit("no board port: set PORT in .local/board.mk or the environment")
        self.s = open_port(port); self.buf = b""; self.logf = logf; self.echo = echo; self.all = []
    def pump(self, secs):
        out, end = [], time.time() + secs
        while time.time() < end:
            self.buf += self.s.read(4096)
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1)
                t = RED.sub(r"\1\2<redacted>", l.rstrip(b"\r").decode("utf-8", "replace"))
                out.append(t); self.all.append((time.time(), t))
                if self.echo: print("  |", t, flush=True)
                if self.logf: self.logf.write(t + "\n"); self.logf.flush()
        return out
    def cmd(self, c, wait=0.6):
        if self.logf: self.logf.write(">> " + c + "\n")
        self.s.write((c + "\n").encode()); return self.pump(wait)
    def close(self): self.s.close()
TRACK = "trackat 3 20"
KINDS = [("bars", 6), ("ring", 4), ("scope", 3), ("fall", 3), ("phase", 2)]   # variants of the round board (pc_vd_variants_on(k, true)); voices: by hand
ORDERS = ("seq", "zigzag", "spread")        # viz order
GAPS = (2, 0, 4)                            # viz gap (the stored default first)
DEF = dict(order="seq", gap=2, size="box")  # every row starts from these unless it says otherwise
# the settings the steps write (viz kind, each kind's variant, order, gap, waterfall size, vol 0): read first, put back at the end, also on error
# (board batch A F6: the viz commands put the values back but stored keys that had not been set; tools/simtools/cfg_keep.py)
KEEP = ["viz_style"] + ["viz_var_" + k for k, _ in KINDS] + ["viz_order", "viz_gap", "viz_fall", "volume"]

def rows():
    """(name, kind, variant, settings) of mode A, in measuring order"""
    out = [("%s_v%d" % (k, v), k, v, dict(DEF)) for k, nv in KINDS for v in range(nv)]
    out += [("fall_v%d_full" % v, "fall", v, dict(DEF, size="full")) for v in range(3)]
    out += [("ring_o%s_g%d" % (o, g), "ring", 0, dict(DEF, order=o, gap=g)) for g in GAPS for o in ORDERS]
    return out
R_UI = re.compile(r"APP uistat screen=(\S+) frames=(\d+) frame_avg_us=(\d+) frame_max_us=(\d+) areas=([\d.]+) px_avg=(\d+)")
R_V1 = re.compile(r"APP viz open=(\d) kind=(\S+) var=(\d+) ramp=(\S+) ring=(\d) knob=(\S+) period_ms=(\d+) stereo=(\d)")
R_V2 = re.compile(r"APP viz draw live=(\d) frames=(\d+) avg_us=(\d+) max_us=(\d+)")
R_AX = re.compile(r"APP viz axes barw=(\d+) floor=(-?\d+) order=(\S+) gap=(\d+) size=(\S+)")
R_V3 = re.compile(r"APP viz ring hint=(\d) title=(\d)")
R_AU = re.compile(r"APP audio blocks=(\d+) blk_max_us=(\d+) underruns=(\d+) cpu0=(\d+) cpu1=(\d+)")
R_MEM = re.compile(r"MEM cmd internal_free=(\d+)")
R_PW = re.compile(r"APP power state=(\w+)")

ROWS = None

class FakeCon:
    """--dry: answers the console lines the script reads from a small state, on a virtual clock (time.time is replaced, nothing sleeps)"""
    def __init__(self):
        self.vt = 1000.0; time.time = lambda: self.vt
        self.kind, self.var = "scope", {k: 1 for k, _ in KINDS}; self.var["voices"] = 0
        self.order, self.gap, self.size = "zigzag", 4, "full"      # not the defaults: the restore has to bring them back
        self.sent = []; self.cfg = {}
    def pump(self, secs):
        self.vt += secs
        return ["APP audio blocks=100 blk_max_us=900 underruns=0 cpu0=20 cpu1=10 state=1 sink=0"]
    def cmd(self, c, wait=0.6):
        self.sent.append(c); out = []
        w = c.split()
        if w[0] == "cfg": out.append("APP cfg %s=%s" % (w[1], self.cfg.get(w[1], "")))
        elif w[0] == "cfgset": self.cfg[w[1]] = w[2]
        elif w[0] == "cfgdel": self.cfg.pop(w[1], None)
        if w[0] == "viz" and len(w) == 2 and w[1] in [k for k, _ in KINDS] + ["voices"]: self.kind = w[1]
        elif c.startswith("viz var "): self.var[self.kind] = int(w[2])
        elif c.startswith("viz order "): self.order = w[2]
        elif c.startswith("viz gap "): self.gap = int(w[2])
        elif c.startswith("viz size "): self.size = w[2]
        if w[0] == "viz" and (len(w) == 2 and w[1] == "stat" or w[1] in [k for k, _ in KINDS] or w[1] == "var"):
            out += ["APP viz open=1 kind=%s var=%d ramp=fire ring=0 knob=vol period_ms=33 stereo=0 map=height" % (self.kind, self.var[self.kind]),
                    "APP viz axes barw=7 floor=-48 order=%s gap=%d size=%s peaks=1" % (self.order, self.gap, self.size),
                    "APP viz draw live=1 frames=600 avg_us=4000 max_us=9000", "APP viz shown=%s" % self.kind,
                    "APP viz ring hint=0 title=0 line=- cells=6 detail=0"]
        elif c == "uistat": out += ["APP uistat screen=viz frames=600 frame_avg_us=3000 frame_max_us=8000 areas=1.00 px_avg=60000"]
        elif c == "mem": out += ["MEM cmd internal_free=123456 internal_min=100000 internal_largest=50000"]
        self.vt += wait
        return out + self.pump(0)
    def close(self): pass

class SimVizCon:
    """--sim: the simulator behind Con's interface, on a virtual clock (time.time is replaced; a wait is the simulator's "@s")"""
    def __init__(self):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from sim_con import SimCon
        self.vt = 1000.0; time.time = lambda: self.vt
        self.c = SimCon(cfg_guard=True); self.failed = None
    def pump(self, secs):
        self.vt += secs
        return self.c.drain(secs).decode("utf-8", "replace").splitlines()
    def cmd(self, c, wait=0.6):
        return self.c.cmd(c, 0).decode("utf-8", "replace").splitlines() + self.pump(wait)
    def close(self):
        rc = self.c.close()
        if rc or self.c.settings_changed:
            self.failed = f"simulator exit code {rc}" + "".join("\n  settings changed: " + l for l in self.c.settings_changed)
            print("FAILED: " + self.failed, flush=True)

class M:
    def __init__(self, dry=False, sim=False):
        self.c = FakeCon() if dry else SimVizCon() if sim else Con(echo=False); self.power = "?"
    def lines(self, out):
        for l in out:
            m = R_PW.search(l)
            if m: self.power = m.group(1)
        return out
    def cmd(self, c, w=0.6): return self.lines(self.c.cmd(c, w))
    def pump(self, s): return self.lines(self.c.pump(s))

def last(rx, out):
    r = None
    for l in out:
        m = rx.search(l)
        if m: r = m.groups()
    return r

def window(m, name, secs=20.0, keep_alive=None, pre=None):
    """reset uistat + viz draw counters, collect secs, read. keep_alive: command sent every 3 s (its viz stat output is aggregated)."""
    raw = []
    s0 = m.cmd("viz stat", 0.5) if pre is None else pre; raw += s0
    v1, v3, ax = last(R_V1, s0), last(R_V3, s0), last(R_AX, s0)
    raw += m.cmd("screen on", 0.3)          # ui_wake: resets the screen timeout (60 s USB, 20 s batt); the screen is already on
    raw += m.cmd("uistat", 0.3)
    t0 = time.time(); nwake = t0 + 8.0; p0 = m.power; ptime = {}; plast = (t0, m.power)
    dsum = dn = dmax = 0; seg = []
    while time.time() - t0 < secs - 0.05:
        step = min(3.0 if keep_alive else 1.0, secs - (time.time() - t0))
        if keep_alive and time.time() - t0 + 3.0 < secs:
            o = m.pump(step); raw += o; o = m.cmd(keep_alive, 0.3); raw += o
            g = last(R_V2, o)
            if g: seg.append(g)
        else:
            o = m.pump(step); raw += o
        if time.time() > nwake and time.time() - t0 < secs - 1.0:
            raw += m.cmd("screen on", 0.2); nwake = time.time() + 8.0
        if m.power != plast[1]:
            ptime[plast[1]] = ptime.get(plast[1], 0) + time.time() - plast[0]; plast = (time.time(), m.power)
    el = time.time() - t0
    ptime[plast[1]] = ptime.get(plast[1], 0) + time.time() - plast[0]
    raw += (u := m.cmd("uistat", 0.4))
    raw += (s1 := m.cmd("viz stat", 0.5))
    raw += (mm := m.cmd("mem settle", 0.8))                  # comparable internal RAM: the display's last DMA bounce buffers freed first (the internal notes, 內部記憶體怎麼量)
    if not last(R_MEM, mm): raw += (mm := m.cmd("mem", 0.8))     # firmware before `mem settle`
    ui = last(R_UI, u); v2 = last(R_V2, s1); v1e = last(R_V1, s1); v3e = last(R_V3, s1)
    for g in seg + ([v2] if v2 else []):
        n, a, mx = int(g[1]), int(g[2]), int(g[3]); dn += n; dsum += n * a; dmax = max(dmax, mx)
    win = [l for l in raw]
    au = [tuple(int(x) for x in a.groups()) for a in (R_AU.search(l) for l in win) if a]
    tracks = [l for l in win if "APP track idx" in l]
    leaks = [l for l in win if "ui leak" in l]
    offs = [l for l in win if "APP screen off" in l]
    me = last(R_MEM, mm)
    with open(os.path.join(ROWS, name + ".txt"), "w") as f: f.write("\n".join(raw) + "\n")
    r = dict(name=name, secs=round(el, 2), power_start=p0, power_s={k: round(v, 1) for k, v in ptime.items()},
             start=dict(v1=v1, title_hint=v3, axes=ax), end=dict(v1=v1e, title_hint=v3e),
             uistat=ui, draw=dict(frames=dn, avg_us=dsum // dn if dn else 0, max_us=dmax),
             audio=au, tracks=tracks, leaks=leaks, screen_off=offs, internal_free=int(me[0]) if me else None)
    if ui:
        fr, fa, fm, ar, px = int(ui[1]), int(ui[2]), int(ui[3]), float(ui[4]), int(ui[5])
        r.update(fps=round(fr / el, 2), frame_avg_us=fa, frame_max_us=fm, areas=ar, px_avg=px,
                 ms_per_kpx=round(fa / px, 3) if px else None)
    print(json.dumps(r, ensure_ascii=False), flush=True)
    return r

def prep_row(m, kind, var, settle=13.0, st=DEF):
    """same start for every row: restart the track (no change inside the window), set the ring order / gap and the waterfall size, enter the kind, set the variant, wait for the title to hide"""
    m.cmd("screen on", 0.3)
    m.cmd(TRACK, 1.5)
    m.cmd("viz order " + st["order"], 0.3); m.cmd("viz gap %d" % st["gap"], 0.3); m.cmd("viz size " + st["size"], 0.3)
    m.cmd("viz " + kind, 0.6)
    o = m.cmd("viz var %d" % var, 0.6)
    m.pump(settle)

def window_ok(r, k, v, st=DEF):
    s = r["start"]; ax = s.get("axes")
    return bool(s["v1"] and s["v1"][1] == k and int(s["v1"][2]) == v and s["title_hint"] == ("0", "0") and not r["tracks"] and not r["screen_off"]
                and ax and (ax[2], int(ax[3]), ax[4]) == (st["order"], st["gap"], st["size"]))

def table(path):
    rows = [json.loads(l) for l in open(path)]
    print("row            fps  ask   f_avg  f_max  areas  px_avg  c_avg  c_max  cyc_ms  ms/kpx  und blkmax cpu0  int_free  batt_s ok")
    for r in rows:
        au = r["audio"]; und = sum(a[2] for a in au); bm = max([a[1] for a in au] or [0]); c0 = "/".join(str(a[3]) for a in au)
        batt = r["power_s"].get("batt", 0); ask = 1000 / 33 * (1 - batt / r["secs"] / 3)   # battery: pc_viz_should_draw skips every 3rd tick
        cyc = (r["frame_avg_us"] + r["draw"]["avg_us"]) / 1000
        print("%-13s %5.1f %4.1f %7d %6d %5.2f %7d %6d %6d %6.1f %7.3f %4d %6d %5s %8s %5.1f %s" % (r["name"], r["fps"], ask, r["frame_avg_us"], r["frame_max_us"],
              r["areas"], r["px_avg"], r["draw"]["avg_us"], r["draw"]["max_us"], cyc, r["ms_per_kpx"] or 0, und, bm, c0, r["internal_free"], batt, r.get("ok", "-")))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["A", "cmp", "B", "table"]); ap.add_argument("file", nargs="?")
    ap.add_argument("--out", default=".local/viz_fps"); ap.add_argument("--only", default=""); ap.add_argument("--dry", action="store_true"); ap.add_argument("--sim", action="store_true")
    a = ap.parse_args()
    if a.mode == "table": return table(a.file)
    global ROWS
    ROWS = os.path.join(a.out, "rows"); os.makedirs(ROWS, exist_ok=True)
    out = os.path.join(a.out, a.mode + ".jsonl")
    def save(r):
        with open(out, "a") as f: f.write(json.dumps(r, ensure_ascii=False) + "\n")
    m = M(dry=a.dry, sim=a.sim)
    kept = cfg_keep.read(m.cmd, KEEP)
    ax0 = kind0 = None; was = {}
    try:                                      # from here on every setting this run moves (vol 0, each viz kind) is put back, also on Ctrl-C in the first seconds
        m.cmd("vol 0"); m.cmd("screen on", 0.5)
        ax0 = last(R_AX, m.cmd("viz stat", 0.5))     # order, gap and waterfall size to put back
        kind0 = last(R_V1, m.cmd("viz stat", 0.5))   # the kind (and its variant, in was[]) to put back: read BEFORE the loop below visits every kind
        for k, _ in KINDS:                    # what to put back at the end
            g = last(R_V1, m.cmd("viz " + k, 0.5))
            if g: was[k] = int(g[2])
        if a.mode == "A":
            only = [x for x in a.only.split(",") if x]
            for nm, k, v, st in rows():
                if only and nm not in only: continue
                for attempt in range(3):
                    prep_row(m, k, v, st=st)
                    r = window(m, nm); r["ok"] = window_ok(r, k, v, st); save(r)
                    if r["ok"]: break
        elif a.mode == "cmp":
            for k in ("bars", "ring"):
                for i in range(2):            # title visible: re-enter the page, skip ~0.9 s (the full first draw), 9 s (the title shows 10 s)
                    prep_row(m, k, 0)
                    m.cmd("screen on", 0.3); m.cmd("back", 0.8); m.cmd("viz " + k, 0.3)
                    r = window(m, "%s_v0_title_%d" % (k, i), secs=9.0); r["ok"] = r["start"]["title_hint"][1] == "1"; save(r)
                prep_row(m, k, 0)             # ring open: viz menu every 3 s (ring_mark: the 4 s again, nothing applied)
                pre = m.cmd("viz menu", 0.5)
                r = window(m, "%s_v0_ringopen" % k, keep_alive="viz menu", pre=pre); r["ok"] = r["end"]["v1"][4] == "1"; save(r)
                m.pump(7)
            prep_row(m, "bars", 0); m.cmd("back", 1.0); m.pump(13)   # the cover page under it
            st = m.cmd("uistack", 0.4)
            r = window(m, "cover", pre=st); r["stack"] = [l for l in st if "uistack" in l]; save(r)
        elif a.mode == "B":
            for rnd in range(3):
                for k in ("bars", "phase"):
                    prep_row(m, k, 0)
                    r = window(m, "B%d_%s" % (rnd + 1, k)); save(r)
    finally:
        try:                                  # a failure while putting the viz state back still reaches cfg_keep.restore
            for k, v in was.items(): m.cmd("viz " + k, 0.4); m.cmd("viz var %d" % v, 0.4)
            if ax0: m.cmd("viz order " + ax0[2], 0.3); m.cmd("viz gap " + ax0[3], 0.3); m.cmd("viz size " + ax0[4], 0.3)
            if kind0: m.cmd("viz " + kind0[1], 0.4)
            m.cmd("stop", 0.5)
        finally:
            cfg_keep.restore(m.cmd, kept)     # what was not set stays not set; the volume comes back too
            m.c.close()
    table(out)
    if getattr(m.c, "failed", None): sys.exit(1)

if __name__ == "__main__":
    main()
