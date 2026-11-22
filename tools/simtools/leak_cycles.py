#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Repeated-operation memory-leak test (2026-11-11). Drives the player over the console, repeats one action many
times and samples the settled internal heap ("mem") every N cycles; fits a slope (bytes/cycle).

  leak_cycles.py ACTION [--cycles 60] [--warm 5] [--every 5] [--settle 1.5] [--log FILE]
                       (--warm default: 5, but 120 for "next": on the board lv_bytes rises for the first ~100 track
                       changes, FreeType glyph caches filling with new CJK glyphs, so no slope is fitted before that)
  leak_cycles.py --list
  leak_cycles.py ACTION[:N],... --port sim [--mode inst|ext] [--root DIR]   the desktop simulator instead of the board (see SimBoard)

Main board by-id path only (no DTR/RTS: opening the port never resets it). Before playback after a reboot send
any plain command first (here: "mem"), so the test playback is not counted (stats_svc_test_mode).
A sample = median of 3 "mem" readings taken after the action settled. Verdict: slope over the measured cycles
and over the second half (a cache that fills once flattens in the second half; a leak does not)."""
import argparse, os, re, statistics, sys, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders
from boardport import open_port, configured_ports      # imports pyserial only when it opens a port: the simulator path needs none

PORT = configured_ports()[0]       # your main board (PORT / .local/board.mk); None: only --port sim works
MEM = re.compile(rb"MEM cmd internal_free=(\d+) internal_min=(\d+) internal_largest=(\d+) psram_free=(\d+)(?: lv_bytes=(-?\d+))?")
SIMRES = re.compile(rb"^SIM res esp_timers=(\d+)$")
REDACT = re.compile(r"\b(ssid|bssid|ip|mac|pass|psk|password)\b([=: ]+)\S+", re.I)


class Board:
    def __init__(self, port=PORT, echo=False, logf=None):
        self.s = open_port(port)
        self.buf = b""
        self.leaks = 0
        self.echo = echo
        self.logf = logf

    def pump(self, secs, until=None):
        """read for secs; returns the lines; stops early when a line matches until (bytes regex)"""
        out, end = [], time.time() + secs
        while time.time() < end:
            self.buf += self.s.read(4096)
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1)
                l = l.rstrip(b"\r")
                out.append(l)
                if b"APP ui leak" in l:      # the firmware's leak tripwire: always shown, counted in RESULT
                    self.leaks += 1; print("   !!", l.decode("utf-8", "replace"), flush=True)
                if self.echo or self.logf:
                    t = REDACT.sub(r"\1\2<redacted>", l.decode("utf-8", "replace"))
                    if self.echo: print("  |", t, flush=True)
                    if self.logf: self.logf.write(t + "\n")
                if until is not None and until.search(l): return out
        return out

    def cmd(self, c, wait=0.4, until=None):
        """one command; waits for its echo first (lv_async_call runs queued calls newest first: never queue two)"""
        self.s.write((c + "\n").encode())
        out = self.pump(4.0, re.compile(b"APP cmd " + re.escape(c.encode()) + b"$"))
        return out + self.pump(wait, until)

    settle = True               # the firmware has `mem settle` (frees the display's last DMA bounce buffers first: comparable readings); cleared once it does not answer

    def mem(self):
        for _ in range(3):
            for l in self.cmd("mem settle" if self.settle else "mem", 3.0, MEM):
                m = MEM.search(l)
                if m: return tuple(int(x or 0) for x in m.groups())   # lv_bytes: 0 on firmware without it
            self.settle = False  # firmware before `mem settle`: the plain `mem` (the internal notes, 內部記憶體怎麼量)
        raise RuntimeError("no MEM answer")

    def settled(self, settle):
        """median of three readings 0.5 s apart, after settle seconds"""
        self.pump(settle)
        r = [self.mem() for _ in range(3)]
        top = [l for l in self.cmd("uitop", 0.3, re.compile(rb"APP uitop")) if b"APP uitop" in l]
        self.top = top[-1].decode(errors="replace").split()[-1] if top else "?"
        return tuple(int(statistics.median(x[i] for x in r)) for i in range(5))


class SimBoard:
    """The same interface as Board on the desktop simulator (tools/simtools/sim_con.py SimCon: a fresh copy of sim/fixture, or --root, headless,
    virtual time: every wait is instant). Free internal RAM does not exist there; the checks are the exit code, the sanitizer output and
    that lv_bytes and the live esp_timer count ("simres": the shim's census; LSan sees neither) return to the same value after each cycle."""
    def __init__(self, root=None, mode=None):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from sim_con import SimCon
        self.mode = mode                      # "inst": sim --mode inst (the instrument app; its actions are the inst_* ones)
        self.c = SimCon(src_root=root, mode=mode)
        self.leaks, self.top, self.s = 0, "?", None
        self.timers = []                      # live esp_timers at every settled() sample

    def _lines(self, out):
        lines = out.split(b"\n")
        for l in lines:
            if b"APP ui leak" in l:
                self.leaks += 1; print("   !!", l.decode("utf-8", "replace"), flush=True)
        return [l.rstrip(b"\r") for l in lines]

    def pump(self, secs, until=None):
        return self._lines(self.c.drain(secs))

    def cmd(self, c, wait=0.4, until=None):
        return self._lines(self.c.cmd(c, wait))

    def mem(self):
        from sim_con import SimCrash
        for l in self.cmd("mem", 0.0):
            m = MEM.search(l)
            if m:
                if not m.group(5) or int(m.group(5)) == 0:   # a build that stops printing lv_bytes must not read as "constant 0"
                    raise SimCrash(f"no usable lv_bytes in the mem answer: {l.decode(errors='replace')!r}")
                return tuple(int(x) for x in m.groups())
        raise SimCrash("no MEM answer")

    def res_timers(self):
        from sim_con import SimCrash
        for l in self.cmd("simres", 0.0):
            m = SIMRES.search(l)
            if m: return int(m.group(1))
        raise SimCrash("no 'SIM res esp_timers=' answer to simres")

    def settled(self, settle):
        self.pump(settle)
        r = self.mem()
        self.timers.append(self.res_timers())
        self.top = self.c.top()
        return r

    def close(self):
        rc = self.c.close()
        return rc, self.c.sanitizer_output()


def slope(xs, ys):
    n = len(xs)
    if n < 2: return 0.0
    mx, my = sum(xs) / n, sum(ys) / n
    d = sum((x - mx) ** 2 for x in xs)
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / d if d else 0.0


VIZ_FOUND = [0]   # the ring kind's variant as the run found it (vizpage changes it; the teardown puts it back)

def viz_found_var(b):
    for l in b.cmd("viz ring", 0.5):
        m = re.search(rb"APP viz open=\d kind=ring var=(\d+)", l)
        if m: VIZ_FOUND[0] = int(m.group(1))
    b.cmd("back", 0.5)

def viz_restore_var(b):
    b.cmd(f"viz var {VIZ_FOUND[0]}", 0.5)

# ---- actions: a function of (board, i) doing one cycle; setup/teardown around the run ----
def seq(*cmds, wait=0.3):
    def f(b, i):
        for c in cmds:
            c = c(i) if callable(c) else c
            if c.startswith("@"): b.pump(float(c[1:]))
            else: b.cmd(c, wait)
    return f

def until_back(cmd, pat, tries=40):
    """teardown: repeat cmd until the board prints pat (theme / ramp back to JC's value)"""
    def f(b):
        for _ in range(tries):
            if any(re.search(pat, l) for l in b.cmd(cmd, 1.0)): return
        print(f"WARN {cmd}: {pat!r} not reached", flush=True)
    return f

def do(b, c):
    if callable(c): c(b)
    elif c.startswith("@"): b.pump(float(c[1:]))
    else: b.cmd(c, 0.6)

KIT = ["demo"]
PEER = configured_ports()[1]   # the peer board (PEER_ID in .local/board.mk), never the main board

def peer_reset(b, i):
    """headphones mode: reset the peer (its auto-reset lines), wait for the player to connect again"""
    p = open_port(PEER, drain_s=0)               # the peer's reset lines are toggled below: no newline, no drain
    p.dtr = False; p.rts = True; time.sleep(0.1); p.rts = False
    p.close()
    t0 = time.time()
    lines = b.pump(40, re.compile(rb"APP bt hp state=2"))
    ok = any(b"bt hp state=2" in l for l in lines)
    print(f"   peer reset {i}: {'connected' if ok else 'NOT connected'} after {time.time() - t0:.1f}s", flush=True)
    b.pump(3)
VOCAB_BUILTIN = [False]   # set by vocab_setup: the open deck is the built-in sample (only then answers/reset are safe)

def vocab_setup(b):
    """open the vocab home, save the progress as it is, remember whether the open deck is the built-in sample"""
    lines = b.cmd("vocab", 1.0) + b.cmd("vocab save", 1.0)
    VOCAB_BUILTIN[0] = any(b"APP vocab stat deck=builtin:vocab " in l for l in lines)
    print(f"   vocab deck is {'the built-in sample: answers + reset allowed' if VOCAB_BUILTIN[0] else 'NOT the built-in sample: open only, no answers'}", flush=True)

def vocab_cycle(b, i):
    """open home, start the quiz, one right + one wrong answer (built-in sample deck only), home, back"""
    b.cmd("vocab", 0.5); b.cmd("vocab start", 0.8)
    if VOCAB_BUILTIN[0]:
        for c in ("vocab right", "vocab next", "vocab wrong", "vocab next"): b.cmd(c, 0.4)
    b.cmd("vocab", 0.5); b.cmd("back", 0.5)

def vocab_teardown(b):
    b.cmd("vocab", 0.5)                                 # reset only acts while the deck is open (closed: V.gp is NULL)
    if VOCAB_BUILTIN[0]: b.cmd("vocab reset", 1.0)      # the sample deck's progress is test data
    b.cmd("back", 0.5)
FORT = ["kuji", "jiao", "dice", "wheel", "gacha", "tarot"]
FACES = ["classic", "minimal", "digital", "info", "sun", "rings", "moon"]
# the spectrum page's kinds and how many variants each has (the firmware's pc_vd_variants: bars 4, ring 4, scope 3, fall 3, phase 2, voices 3);
# the voices kind draws only while a source has voices: the actions below switch on the simulator's fake chip (simvoices nes) and off again (the board answers it as an unknown command)
VIZ_VARIANTS = [("bars", 6), ("ring", 4), ("scope", 3), ("fall", 3), ("phase", 2), ("voices", 3)]
VIZ_COLOUR_AXIS = 2                        # the style ring has four axis cells: kind, variant, colour, map; a second tap on the current axis steps it
# Task 13: one cycle of the "pickers" action. The rescan row's place comes from uiboxes (fonts change row heights); a tap that did not arm it raises.
RESCAN_ROW = re.compile(rb"^APP uibox button (-?\d+) (-?\d+) (-?\d+) (-?\d+) \|(?:\xee[\x80-\x9f][\x80-\xbf] )?(\xe9\x87\x8d\xe6\x96\xb0\xe6\x8e\x83\xe6\x8f\x8f|Rescan library)")   # 重新掃描 (Rescan)
RESCAN_ARMED = ("再點一次：重新掃描".encode(), b"Tap again: rescan")   # hires T8e: a rescan no longer stops playback
def pickers_cycle(b):
    for page in ("uipush radio", "noiseui", "chipui", "mp3ui"):
        b.cmd(page, 0.3); b.cmd("knob 40", 0.2); b.cmd("knob -40", 0.2); b.cmd("back", 0.3)
    for wait in (0.3, 4.5):                       # leave while armed; leave after the arm ran out
        b.cmd("mp3ui", 0.3); b.cmd("knob 40", 0.2)
        rows = [m for m in (RESCAN_ROW.match(l) for l in b.cmd("uiboxes", 0)) if m]
        if not rows: raise RuntimeError("pickers: no rescan row in uiboxes")
        x1, y1, x2, y2 = (int(v) for v in rows[-1].groups()[:4])
        b.cmd(f"simtap {(x1 + x2) // 2} {(y1 + y2) // 2}", 0.3)
        if not any(t in l for l in b.cmd("uiboxes", 0) for t in RESCAN_ARMED): raise RuntimeError("pickers: the tap did not arm the rescan row")
        b.pump(wait); b.cmd("back", 0.3)

# LIBTREE: the song tree. The rows' places come from uiboxes (fonts change row heights). Data root: sim/tree_root.sh (many/ = a sub-folder + 100 songs: 3 pages).
def tree_row(b, text):
    for l in b.cmd("uiboxes", 0):
        m = re.match(rb"^APP uibox button (-?\d+) (-?\d+) (-?\d+) (-?\d+) \|(?:\xee[\x80-\x9f][\x80-\xbf] )?" + re.escape(text.encode()) + rb"$", l)
        if m:
            x1, y1, x2, y2 = (int(v) for v in m.groups())
            return (x1 + x2) // 2, (y1 + y2) // 2
    raise RuntimeError(f"tree: no row {text!r} in uiboxes")
def tree_cycle(b):
    b.cmd("mp3ui", 0.3); b.cmd("simknob 1", 0.2)
    x, y = tree_row(b, "deep"); b.cmd(f"simtap {x} {y}", 0.3)        # a folder of sub-folders only (songs come first in a level: many/sub is on its 3rd page)
    x, y = tree_row(b, "a"); b.cmd(f"simtap {x} {y}", 0.3)           # a level two deep
    b.cmd("flick up", 0.3); b.cmd("flick up", 0.3)                    # up to deep, up to the root
    x, y = tree_row(b, "many"); b.cmd(f"simtap {x} {y}", 0.3)        # a paged level (100 songs + sub: 3 pages)
    for _ in range(3): b.cmd("flick left", 0.2)                       # pages 2, 3, 1 (wraps)
    b.cmd("knob 100", 0.2); b.cmd("knob 100", 0.2); b.cmd("knob -100", 0.2); b.cmd("knob -100", 0.2)   # the knob runs on into the next / the previous page
    b.cmd("flick right", 0.2)
    b.cmd("flick up", 0.3)                                            # up to the root
    b.cmd("uipush tracks3", 0.3); b.cmd("flick left", 0.2); b.cmd("back", 0.3)   # the playing page's song list opens a level of its own, on top of the music page
    b.cmd("back", 0.3)

def bt_help_tap(b, i):
    """tap the last cell of the Bluetooth ring (read n from the `ring` line): the merge adds a cell before it and nothing here changes"""
    n = 0
    for l in b.cmd("ring", 0.2):
        m = re.search(rb" n=(\d+)$", l)
        if m: n = int(m.group(1))
    b.cmd(f"bt tap {n - 1}", 0.3)


def seq_parts(*fs):
    def f(b, i):
        for g in fs: g(b, i)
    return f


def games_ids():
    """the game ids of app/main/ui/games.def (one GAME(id, ...) line each): the leak action covers every game without a line of its own"""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "app", "main", "ui", "games.def")
    text = re.sub(r"/\*.*?\*/", "", open(path, encoding="utf-8").read(), flags=re.S)
    return re.findall(r"^\s*GAME\(\s*(\w+)\s*,", text, flags=re.M)


def exit_cmds():
    c = ["tools bubbles", "@1", "simknob 1", "@0.3", "exit tap back", "@0.3", "back", "@0.3",
         "tools bubbles", "@1", "simknob 1", "@4.6", "back", "@0.3", "back", "@0.3"]
    for g in games_ids():
        c += [f"games open {g}", "@0.3", "board tap 4", "exit pause", "@0.4", "exit tap resume", "@0.4", "exit pause", "exit tap back", "@0.3"]
    c += ["games open breakout", "@0.3", "simtap 180 193", "@0.3", "simtap 180 193", "@0.2", "exit tap resume", "@0.3", "simtap 180 193", "@0.2", "exit tap back", "@0.3"]
    return c + ["back", "@0.3", "back", "@0.3"]


def games_cmds():
    c = ["games help", "@0.3", "back", "@0.3"]                   # the hub's 說明 page, open and Back
    for g in games_ids():
        if g == "spiro":   # 萬花尺 draws a new random curve (gear / pen digits, lobes, 金色！) at every open and tap: fix its seed and golden roll so the FreeType glyph cache sees the same text every cycle
            c += ["games gold 0", "games open spiro", "@0.3", "games count 0", "@0.1", "back", "@0.3", "games gold auto"]
        c += [f"games open {g}", "@0.3", "board tap 4", "@1.5", "board tap 0", "@1.5",      # a move, the device's answer, another move
              "frame show", "simtaprow 0", "@0.2", "simknob 1", "@0.3", "board tap 4", "@0.3", "board tap 8", "@0.3",   # the mode button (tap, knob), moves in the new mode
              "frame show", "simtaprow 1", "@0.3", "popring 3", "@0.3", "back", "@0.3",   # the game's ring, its 說明 (last cell) and Back to the game
              "back", "@0.3",                                              # leave the page (hub)
              f"games open {g}", "@0.3", "board tap 4", "back", "@0.3"]   # and leave while the device is still thinking
        # G0b game feel: every rule and style (the ring: style borrowed + knob, 重來), two players' moves that end the game on the first board cells
        # (effects, the card on its delay), Back while the effects are mid-flight; then a game to its card, a tap on it (again), a golden game; rule and style back to 0
        taps = [f"board tap {i}" for i in (0, 3, 1, 4, 2, 0, 1, 2, 3, 5, 6)]
        for r in range(3):
            c += [f"games open {g}", "@0.3", "games mode 2", f"games rule {r}", f"games style {r}", "frame show", "simtaprow 1", "@0.2", "popring 1", "simknob 1", "@0.2",
                  "popring 2", "@0.2"] + taps + ["@0.1", "back", "@0.3"]
        c += ["games gold 1", f"games open {g}", "@0.3", "games mode 2"] + taps + ["@1", "games result tap", "@0.2", "games gold auto",
              "games rule 0", "games style 0", "back", "@0.3"]
    # fix round 1 M3: the paths only one game has: 四子棋 彈出 (pops mid-drop: a burst and the column's drops at once), 井字棋 盲棋 (a mark hidden after 2 s, a refused tap)
    c += ["games open c4", "@0.3", "games mode 2", "games rule 2", "board tap 3", "board tap 3", "board tap 3", "board tap 38", "@0.1", "board tap 38", "@0.6",
          "games rule 0", "back", "@0.3",
          "games open ttt", "@0.3", "games mode 2", "games rule 2", "board tap 4", "@2.3", "board tap 4", "@0.1", "games rule 0", "back", "@0.3"]
    return c + ["games gold auto", "back", "@0.3", "back", "@0.3"]


def simon_cmds():
    """G1 四色記憶 (ui_sectors): play rounds by the game's own words (right / wrong), the milestone, the card and again, 練習's replay, 雙人, 洗牌's move,
    every style, keys mode, Back while the device plays and while the card waits"""
    def rounds(n):
        c = []
        for l in range(1, n + 1): c += [f"@{1.5 + 0.6 * l:.1f}"] + ["games right"] * l
        return c
    # every roll golden (games gold 1): the golden path runs in every cycle, so its first card (new glyphs in the cache) falls in the warm-up instead of a
    # random cycle (fix round 1: one more roll a cycle made the seed's first golden game land in cycle 25: +1336 lv_bytes once, then flat)
    c = ["games gold 1", "games open simon", "@0.3", "sectors hub"] + rounds(5) + ["@4.5", "games wrong", "@1", "games result tap"] + rounds(1) + ["@2.1", "games wrong", "@0.2", "back", "@0.3"]
    c += ["games open simon", "@0.3", "games mode 2", "sectors hub"] + rounds(1) + ["@2.7", "games wrong", "@0.5", "back", "@0.3"]   # 練習: the replay timer is running
    c += ["games open simon", "@0.3", "games mode 1", "games rule 2", "sectors hub"] + rounds(6) + ["@0.5", "games style 1", "games style 2", "games style 0",
          "frame show", "simtaprow 1", "@0.2", "popring 1", "simknob 1", "@0.2", "popring 2", "@0.9", "back", "@0.3"]   # 洗牌 moved, styles, 重來, Back mid-show
    c += ["games open simon", "@0.3", "sectors keys 1", "sectors key 1", "sectors key -1", "@1.4", "games wrong", "@0.7", "sectors tap 0", "@1.4", "games wrong", "@0.3",
          "games rule 0", "games style 0", "back", "@0.3"]   # keys, a field tap after the end (again), a rule change before the card; rule and style back to the first (kept while the device is on)
    return c + ["games gold auto", "back", "@0.3", "back", "@0.3"]


def fpkeys_cmds():
    """FakePod (the square simulator, SIM_FP=1; make sim-leak-fp): the main menu by the two keys (a page, a hold with repeats), a drag turn, the status strip (a message that runs out,
    one that is tapped away, a warning), and a key-only run of 疊塔 (the keys are the game's input, the pause overlay's keys are the volume) and 四子棋 (the key cursor)"""
    c = ["uipush menu", "simkey top", "simkey bottom", "simkey top 700", "simdrag 300 300 100 300 150", "simdrag 100 300 300 300 150", "@0.2",
         "uimsg a message", "@0.2", "uimsg and a newer one", "@4.5", "uimsg tapped away", "@0.2", "simtap 230 394", "@0.2", "uiwarn a warning", "@5", "simtap 230 394", "@0.2", "back", "@0.3"]
    c += ["games open tower", "@0.3", "simkey top", "simkey top", "@0.5", "simkey bottom", "@0.3", "exit pause", "@0.2", "simkey top", "exit tap resume", "@0.3", "back", "@0.3",
          "games open c4", "@0.3", "simkey top", "simkey top", "simkey bottom", "@1.5", "simkey top", "simkey bottom", "@1.5", "back", "@0.3", "back", "@0.3", "back", "@0.3", "back", "@0.3"]
    return c


def frame_cmds():
    """the content-first page frame (F1a2; ui_frame.h): on each content page (CF2: both panels; no bubble) the popup by a knob turn, by a tap on the content,
    an option (it opens the page's ring), the exit's pause overlay, then Back; a page with no frame just ignores the frame lines"""
    c = []
    for page in ["show drink", "show habits", "tools ambient", "tools counter", "tools dice", "setui", "tools bubbles"]:
        c += [page, "@0.3", "frame", "frame show", "@0.2", "frame tap opt 0", "@0.3", "frame hide", "simknob 1", "@0.2", "frame tap bg", "@0.2",
              "frame hide", "exit pause", "@0.2", "exit tap resume", "@0.2", "back", "@0.3"]
        if page.startswith("tools"): c += ["back", "@0.3"]          # the 小工具 ring under the tool
    c += ["tools bubbles", "@0.3", "simpress 180 180 3100", "@0.3", "tools ambient", "@0.3", "simpress 180 180 500", "@0.3", "back", "@0.3", "back", "@0.3"]   # CF2: the hold anywhere goes home (home_hold_ms); a short press is a tap
    return c


ACTIONS = {
    # name: (setup cmds, cycle fn, teardown cmds, doc)
    "screens": (["screen on"], seq("uipush menu", "back", "uipush settings", "back", "uipush favs", "back",
                                   "uipush radio", "back", "mp3ui", "back", "list", "back", "uipush tracks0", "back",
                                   "chipui", "back", "noiseui", "back", "uipush colors", "back", "btui", "back",
                                   "uipush help", "back", "uipush meta", "back", "uipush fortune", "back"),
                ["screen off"], "push/pop every main screen and list screen (screen on)"),
    # no gacha pulls: they raise the counts in JC's /sdcard/fortune/cards.txt; no "dice N": it is saved (cfg dice_n)
    "fortune": (["screen on"], seq(*[x for n in ["kuji", "jiao", "dice", "wheel", "tarot"] for x in (f"fortune {n}", "fortune draw", "@2", "back", "back")],   # each page: the hub (or the 小工具 menu) and the page, both popped: the stack is as it was found
                                   "fortune gacha", "@0.5", "back", "back"),
                ["screen off"], "every fortune page with a draw (kuji, jiao, dice, wheel, tarot) + gacha page (no pull)"),
    "clock": (["screen on", "clock"], seq(*[f"clockface {f}" for f in FACES], "show clockmenu", "back", "show alarm",
                                          "back"),
              ["cfgdel clockface", "back", "mp3ui", "back", "screen off"], "clock faces cycle + clock sub-screens"),
    # --- Task 7: the borrowed knob ---
    "clockfaceaxis": (["screen on", "clock", "clockface classic"], seq("simtime 2026-11-16 10:08:30", "clockface ring", "@0.3", "knob 1", "@0.2", "knob -2", "@0.2", "@4.5"),   # a fixed time each cycle: the clock digits fill the glyph cache once, that is no leak
                      
                      ["cfgdel clockface", "back", "screen off"], "clock: the face ring (錶面): a tap step, the knob both ways (back to the same face: no write), the 4 s close"),
    # 播放畫面 (it does not touch the slideshow); the photo interval is pinned to 300 s for the action (the default 30 s let a fade fall on a sample), so the exact rule holds
    "albumaxis": (["screen on", "seti album_sec 300", "album", "@2"], seq("albummenu -2", "@0.3", "albummenu 2", "knob 1", "@0.2", "knob -2", "@0.2", "@4.5"),
                  ["back", "cfgdel album_sec", "screen off"], "album ring: 播放畫面 is an axis: a tap step, the knob both ways (back to the same value: cover), the 4 s close"),
    # --- UI unify T3: the tap-revealed ring (ui_popring): the playing ring closes by its 4 s and by its Back cell (which leaves the cover page); the album ring by its 4 s and with its page ---
    "gs": (["screen on", "vol 0"], seq("simflick down", "@0.3", "qs tap bright-", "qs tap bright+", "qs tap vol+", "qs tap vol-", "qs tap bt", "qs tap bt", "qs tap air", "qs tap air", "simflick up", "@0.3",
                                      "tour", "@0.3", "simflick up", "simflick down", "@0.2", "simflick up", "@0.3", "simflick left", "simpress 180 180 3100", "@0.5", "uipush settings", "@0.3", "simflick up", "@0.3"),
           ["screen off"], "GS gestures: the quick settings (flick down, - + taps, the switches, airplane both ways, flick up) and the gesture tutorial (all four steps, ended by the hold), a flick up out of a list page"),
    "popring": (["screen on", "vol 0"], seq("track 3", "@2", "nowmenu -2", "@4.5", "nowmenu -2", "popring -3", "@0.3",
                                            "album", "@2", "albummenu -2", "@4.5", "albummenu -2", "back", "@0.3",
                                            "show pomo", "popring -2", "@4.5", "popring -2", "popring -3"),
                ["stop", "screen off"], "popring: playing ring (4 s timeout, Back), album ring (4 s timeout, left open when its page is popped), pomodoro ring (4 s timeout, Back)"),
    # --- NOWFP (square panel only, simtap on the 460 panel): the static now-playing page in both styles: play / pause, next, the order button, more (the ring, its 4 s close), Back; the page is rebuilt for each style ---
    "nowfp": (["screen on", "vol 0"], seq("cfgset now_fp 0", "track 3", "@2", "simtap 230 230", "@0.3", "simtap 230 230", "simtap 383 383", "@0.5", "simtap 76 76", "simtap 76 230", "simtap 383 230", "@0.3", "simtap 230 76", "@0.5", "nowmenu -5", "@4.5", "back", "@0.3",
                                          "cfgset now_fp 1", "track 3", "@2", "simtap 230 230", "@0.3", "simtap 383 76", "@1.5", "simtap 230 230", "@0.3", "simtap 76 76", "simtap 76 230", "simtap 383 230", "simtap 76 76", "simtap 76 230", "simtap 383 230", "@0.3", "simtap 230 383", "@0.3", "simtap 230 230", "@0.3", "simtap 383 76", "@1", "@4.5", "simtap 230 230", "@0.3", "next", "@5", "back", "@0.3"),   # 2026-11-22 nine-grid: show, run the zones, the sets, next page (spectrum, back), close, the timer hide, a track change, Back
              ["stop", "cfgdel now_fp", "cfgdel mp3_order", "screen off"], "nowfp: the square panel's static now-playing page, styles A and B with the nine-grid (show / zones / sets / next page / close / timer hide, the ring behind 選單, Back)"),
    # --- UI unify T4: the page kit's five test pages (console.c): the axis button borrows the knob and is left with the borrow open (4 s) when its page goes; every other piece just comes and goes ---
    "explain": (["screen on", "vol 0"], seq("uipush help", "@0.3", "simknob 3", "back", "setui", "@0.3", "help tap", "@0.3", "back", "back", "uipush poptest", "popring -2", "popring 4", "@0.3", "back", "back"),
                ["screen off"], "explain: the settings 說明 page (QR, knob scroll), through the settings list-end row, through the popring's help cell"),
    # --- hires T7: the 現況 page opened from the playing ring's last cell (a track plays), refreshed by its timer for 1.2 s, left by Back
    "status": (["screen on", "vol 0", "track 3", "@2"], seq("nowmenu -2", "nowmenu 8", "@1.2", "back", "@0.3", "back", "@0.3", "track 3", "@0.5"),
               ["stop", "screen off"], "status: the 現況 page through the playing ring's cell, one refresh tick, Back"),
    # --- radio2: two station lists on the card (root: Makefile sim-leak writes radio.txt + radio_jazz.txt, two stations each): the chooser row (the 3rd row) taps to the other list and back, the page is built again each time ---
    "radiolists": (["screen on", "vol 0"], seq("uipush radio", "@0.3", "simtap 180 186", "@0.6", "simtap 180 186", "@0.6", "back", "@0.3"),
                   ["screen off"], "radiolists: the radio page's list chooser row (next list, page rebuilt, twice), Back"),
    # --- fx apps T14 (first half): the edit-on-the-computer entries open and Back: the Wi-Fi settings row, the radio list's end row (the tool-list chooser's end row needs a root with several packs: not in this half) ---
    # fx apps T15 (second half): the slot pay list's end row, the Bluetooth ring's last cell, the alarm list's end row, the pomodoro ring's help cell (idle), each open and Back
    "helppages": (["screen on", "vol 0"], seq_parts(
                      seq("setui", "@0.3", "settings key wifi", "@0.3", "back", "back", "uipush radio", "@0.3", "help tap", "@0.3", "back", "back",
                          "tools slot", "@0.5", "simtap 180 68", "@0.3", "help tap", "@0.3", "back", "back", "back", "back", "btui", "@0.3"),
                      bt_help_tap,                                         # the Bluetooth ring's last cell (说明), by the ring's own n: no literal index
                      seq("@0.3", "back", "back",
                          "show alarm", "@0.3", "help tap", "@0.3", "back", "back",
                          "show pomo", "@0.3", "popring -2", "popring 0", "@0.3", "back", "back")),
                  ["screen off"], "helppages: the Wi-Fi settings row's, the radio end row's, the slot pay list's, the Bluetooth ring's, the alarm list's and the pomodoro ring's help pages, open and Back"),
    # --- fx apps T7: the filter list and every filter page (spec 10.1): a parameter row borrowed, turned both ways and let run out, the reset row armed and let run out, the 說明 page, back;
    # the equalizer's band count stepped (the page rebuilds itself when the borrow ends); a preset import and the aptX mode (on, off) change the rows' looks under the open list, every cycle the same (the lv bytes depend on the state at the cycle's end)
    "fxpages": (["screen on", "vol 0"], seq("uipush fx", "@0.3", "fx import warm.toml", "@0.3", "fx mode 2", "@0.3", "fx mode 0", "@0.3", "simknob 2", "@0.2", "fxui tap -1", "@0.3", "back", "@0.3",
                                            *[c for a in ("gain", "balance", "widener", "crossfeed", "velvet_widener", "equalizer", "tone", "bass", "loudness", "tube", "compressor", "freeverb", "limiter")
                                              for c in (f"fxpage {a}", "@0.3", "fxui tap 1", "simknob 1", "@0.2", "simknob -2", "@4.5", "fxui tap -2", "@4.5", "fxui tap -2", "fxui tap -2", "fxui tap 0", "fxui tap 0",
                                                        "fxui tap -1", "@0.3", "back", "@0.3", "back", "@0.3")],
                                            "fxpage equalizer", "@0.3", "fxui tap 3", "@4.5", "@0.3", "back", "@0.3",
                                            # final review M1: velvet_widener on, its 長度 row (a REBUILD row: alloc, SWAP, old block back, free) turned both ways and let run out
                                            "fx on velvet_widener 1", "fxpage velvet_widener", "@0.3", "fxui tap 2", "simknob 3", "@0.5", "simknob -3", "@4.5", "back", "@0.3", "fx on velvet_widener 0", "@0.3",
                                            "back", "@0.3"),
                ["fx off", "fx mode 0", "screen off"], "fx pages: the list, every filter page (a row borrowed + knob both ways + timeout, reset armed + run out, reset done, the 說明 page), the equalizer count rebuild, the velvet widener length rebuild (on), preset import / aptX mode between rounds"),
    "kitpage": (["screen on", "vol 0"], seq("uipush kit", "simtap 180 68", "@0.3", "back", "uipush kit2", "back", "uipush kit3", "back", "uipush kit4", "back", "uipush kit5", "back"),
                ["screen off"], "kitpage: kit (axis button tapped, then Back with the borrow open), kit2, kit3, kit4, kit5"),
    # --- end Task 7 ---
    "clock_idle": (["screen on", "clock", "clockface digital"], seq("@10"), ["cfgdel clockface", "back", "screen off"],
                   "clock screen left alone (digital face, lit): time-driven changes only"),
    "noise": (["screen on"], seq("noiseui", "sceneui 0", "back", "back", "uipush colors", "back"), ["screen off"],
              "scene list + a scene screen + colours ring (nothing played)"),
    "next": (["screen on", "mem", "vol 0", "mp3 all", "@4"], seq("next", "@2.5"), ["stop", "back", "screen off"],
             "track change (shuffle all, now playing on screen, cover decode)"),
    "next_dark": (["screen off", "mem", "vol 0", "mp3 all", "@4"], seq("next", "@2.5"), ["stop", "back"],
                  "track change, screen off"),
    "playstop": (["screen on", "mem", "vol 0"], seq(lambda i: f"track {i % 68}", "@2.5", "stop", "@0.5", "back"),
                 ["screen off"], "play a track, stop, leave the now-playing screen"),
    "pause": (["screen on", "mem", "vol 0", "track 1", "@3"], seq("nowmenu -2", "nowmenu -1", "@1", "nowmenu -2",
                                                                    "nowmenu -1", "@1", "nowmenu 99"),
              ["stop", "back", "screen off"], "pause / resume from the now-playing menu"),
    "seek": (["screen on", "mem", "vol 0"], seq(lambda i: f"trackat 2 {(i * 137) % 900}", "@2"), ["stop", "back", "screen off"],
             "re-open the same track at another position (there is no seek: trackat)"),
    "gapless": (["screen off", "mem", "vol 0"], seq(lambda i: f"trackat {i % 68} 985", "@12"), ["stop", "back"],
                "start 1.5 % before a track end -> gapless pre-open + join into the next"),
    "theme": (["screen on"], seq("theme next", "@0.8"), [until_back("theme next", rb"theme=monokai"), "screen off"],
              "theme cycling (rebuilds every screen); ends on JC's monokai"),
    "viz": (["screen on", "mem", "vol 0", "track 3", "@3", "simvoices nes"],
            seq("viz menu", "@0.3", "viz pick 1", *[x for k, n in VIZ_VARIANTS for x in (f"viz {k}", *(["knob 1", "@0.2"] * n))],
                f"viz pick {VIZ_COLOUR_AXIS}", f"viz pick {VIZ_COLOUR_AXIS}", "@0.3", "viz pick -1", "@0.5", "back", "@0.3",   # the centre tap: MP3 closes the ring and opens the details page; back returns here
                "back", "@0.3"),
            ["viz bars", until_back("ramp next", rb"ramp=duo"), "simvoices off", "stop", "back", "back", "screen off"],   # the colour cell stepped the ramp: back to JC's duo
            "spectrum page: the style ring, every kind with every variant turned through (knob), the colour axis (second tap), the centre tap (MP3: details page and back), leave"),
    "vizpage": (["screen on", "mem", "vol 0", "track 3", "@3", viz_found_var], seq("viz bars", "@0.8", "viz ring", "@0.8", "viz var next", "@0.3", "back", "@0.3"),
                ["viz ring", viz_restore_var, "viz bars", "back", "stop", "back", "screen off"],   # the ring kind's variant is put back to the one found
                "open the spectrum page (over the now-playing page), switch bars -> ring and a variation, leave it"),
    # the playing ring on the spectrum page (MP3 cells there: 0 fav, 1 prev, 2 list, 3 style, 4 order, 5 next, 6 details): open, next, style ring, its Back to the playing ring, the 4 s timeout, leave
    "vizplay": (["screen on", "mem", "vol 0", "track 3", "@3"], seq("viz bars", "@0.3", "nowmenu -2", "nowmenu 5", "@0.5", "nowmenu 3", "@0.3", "viz pick 1", "viz pick -3", "@0.3",
                                                                    "nowmenu -5", "@4.5", "nowmenu -2", "nowmenu -1", "@0.5", "nowmenu -1", "@4.5", "back", "@0.3"),
                ["viz bars", "stop", "back", "back", "screen off"],
                "spectrum page, playing ring: open, next, the style cell (style ring), its Back (playing ring again), 4 s timeout, open + pause + resume, 4 s timeout, leave"),
    "vizkinds": (["screen on", "mem", "vol 0", "track 3", "@3", "simvoices nes"],
                 seq("viz scope", "viz var next", "@0.5", "viz fall", "viz var next", "@0.5", "viz phase", "viz var next", "@0.5",
                     "viz voices", "viz var next", "@0.3", "viz var next", "@0.3", "viz var next", "@0.3", "simvoices off", "@0.3", "simvoices nes", "@0.3",   # the three variants (the ring one builds the map), the chip going and coming back
                     "viz rows", "@0.5", "viz menu", "viz pick 4", "viz pick 4", "viz pick 5", "viz pick 5", "viz pick 5", "viz pick -1", "@0.3", "vmute clear",   # voice rows: the cursor and the state cell (mute, solo, on), the mute mask back to 0
                     "viz roll", "@0.8", "viz var next", "@0.5", "viz var next", "@0.3",
                     "track 0", "@1", "viz lyrics", "@1.5", "viz var next", "@0.5", "viz var next", "@0.3", "track 3", "@1", "@0.3",   # lyrics: the .lrc of the first fixture song (a read and a parse), both line counts, another song without one
                     "viz zoom orig", "viz bars", "@0.3", "viz zoom zoom", "@0.3", "viz menu", "viz pick 6", "@0.3", "back", "@0.3",   # the size setting (the square board's zoom), the help cell (opens the explain page, back returns)
                     "viz bars", "@0.3", "back", "@0.3"),
                 ["viz bars", "simvoices off", "stop", "back", "back", "screen off"], "open the spectrum page on scope, waterfall, phase and voices (and their variations; the stereo tap on and off; the voices kind with a chip that stops and starts), the voice rows with the mute / solo cells, the piano roll, the lyrics of a song with a .lrc, the size setting and the help cell, leave it"),
    # slot 4 of the style ring is the drawn kind's own axis: the ring kind's order x gap (9 values, a full turn; every third step rebuilds the ring map in place) and the waterfall's size (2 values)
    "vizown": (["screen on", "mem", "vol 0", "track 3", "@3"],
               seq("viz ring", "@0.3", "viz menu", "viz pick 4", *(["knob 1", "@0.2"] * 9), "viz pick -1", "@0.3",
                   "viz fall", "@0.3", "viz menu", "viz pick 4", "knob 1", "@0.3", "knob 1", "@0.3", "viz pick -1", "@0.3", "viz bars", "@0.3", "back", "@0.3"),
               ["viz bars", "viz order seq", "viz gap 2", "viz size box", "back", "stop", "back", "screen off"],
               "spectrum page: the ring kind's own axis (order x gap) turned through all nine values, the waterfall's size axis there and back, leave"),
    "vizring": (["screen on", "mem", "vol 0", "track 3", "@3"], seq("viz bars", "@0.3", "viz menu", "knob 1", "viz pick 2", "viz pick 2", "viz pick 3", "knob 4", "@0.3", "@6", "back", "@0.3"),
                ["viz bars", "viz var 0", "viz map height", until_back("ramp next", rb"ramp=duo"), "back", "stop", "back", "screen off"],   # kind, variant, map and ramp the ring stepped: back to the defaults
                "spectrum page: open the style ring, turn the knob, pick a kind and a colour, let the 4 s timeout close it (the centre tap would open the details page on MP3) and wait out the hint, leave"),
    "vizring_leave": (["screen on", "mem", "vol 0", "track 3", "@3"], seq("viz bars", "@0.3", "viz menu", "viz pick 1", "knob 1", "@0.3", "back", "@0.3"),
                      ["viz bars", "viz var 0", until_back("ramp next", rb"ramp=duo"), "back", "stop", "back", "screen off"], "spectrum page: leave it with the style ring still open (and a variant not yet stored)"),
    "cover": (["screen on", "mem", "vol 0", "track 4", "@3"], seq("coversmooth 1", "@1", "coversmooth 0", "@1"),
              ["stop", "back", "screen off"], "smooth cover on/off while the cover turns"),
    "wifi": ([], seq("wifi 1", "@8", "wifi 0", "@3"), [], "Wi-Fi on (connect) / off"),
    "radio": (["mem", "vol 0", "wifi 1", "@8"], seq(lambda i: f"radio {i % 2}", "@10", "radio stop", "@2"),
              ["back", "wifi 0"], "radio station start / stop (Wi-Fi stays up)"),
    "album": (["screen on", "album", "@2"], seq("albumnext", "@1.5"), ["back", "screen off"], "photo slideshow next"),
    "album_open": (["screen on"], seq("album", "@2", "back", "@0.5"), ["screen off"], "album screen open/close"),
    "album_build": (["screen on", "album", "@2"], seq("albumclear", "@3.5"), ["back", "screen off"],
                    "album cache cleared and rebuilt (every photo decoded again: JPEG decoder ~45 KB internal)"),
    "screen": ([], seq("screen on", "@1", "screen off", "@1"), [], "backlight on/off"),
    "lists": (["screen on"], seq("tools lunch", "tools spin", "@2.6", "tools btn 1", "@2.6", "tools people", "tools spin", "@2.6",
                                 "simtap 229 272", "@0.3",   # Task 12: 新一輪 armed (first tap), then the page is left with the arm pending
                                 "tools ideas", "tools spin", "@3.6", "back", "back"),
              ["cfgdel pick_group", "screen off"], "小工具 list toys: open lunch / people / ideas (built-in packs), one spin each"),
    "tools": (["screen on"], seq("tools dice", "tools roll", "@2.5", "tools slot", "tools spin", "@4",
                                  "simtap 180 40", "@0.3", "simknob 3", "@0.2", "back", "@0.3",   # Task 12: the pay table (a tap on the score line), scrolled, left with Back
                                  "tools fortune", "tools", "back"),
              ["screen off"], "小工具 dice (roll), slot (spin; its pay table opened, scrolled, left), fortune, back to the 小工具 menu"),
    # bubble task: the bubble field page: open, pop ten bubbles, pop the rest (the sheet is laid again 300 ms later), pop one of the new sheet, Back
    "bubbles": (["screen on"], seq("tools bubbles", *[f"bubbles pop {k}" for k in range(10)], "bubbles pop all", "@0.5", "bubbles pop 3", "back", "@0.3", wait=0.1),
                ["screen off"], "捏泡泡紙: open the page, pop ten bubbles, pop the rest of the sheet (laid again after 300 ms), pop one, Back"),
    # G4c: finger drags on the segmented rings: the 小工具 ring (2 pages) and the 遊戲 hub, next/previous by drag, a short drag (a tap), a drag from Back, long-press Back home
    "ringdrag": (["screen on"], seq("tools", "simdrag 250 180 150 180 200", "simdrag 150 180 250 180 200", "simdrag 180 100 180 150 200", "simdrag 100 180 70 180 150",
                                    "tools games", "@0.5", "simdrag 250 180 150 180 200", "simdrag 250 180 150 180 200", "simdrag 250 180 150 180 200", "simdrag 150 180 250 180 200",
                                    "simdrag 180 320 100 320 200", "simpress 180 318 3100", "@0.3"),
                 ["screen off"], "ring drags: 小工具 and 遊戲 hub pages by drag, a short drag, a drag from Back, long-press Back to home"),
    # hires T8e: the 封面快取 page from 設定 (the fake pack emptied: it builds), 暫停 and 繼續, 立即建置, 說明 and back, Back; then from 現況 (its first row) and Back twice
    "covers": (["screen on"], seq("simthumbpack clear", "setui", "@0.3", "settings key covers", "@0.3", "covers tap pause", "@0.6", "covers tap pause", "@0.6", "covers tap build", "@0.6", "help tap", "@0.3", "back", "@0.3", "back", "@0.3", "back", "@0.3",
                                  "simthumbpack clear", "simthumbpack hold 1", "help open status", "@0.3", "simtap 180 93", "@0.6", "back", "@0.3", "back", "@0.3", "simthumbpack full", wait=0.1),
               ["screen off"], "封面快取: from 設定 (an empty fake pack builds), pause / resume, build now, 說明, Back; from 現況's row, Back"),
    # sfx task 2: the 音效庫 page: open, play three entries (one a 4-track jingle), turn the list both ways, the 說明 page and back, Back
    "sfxbank": (["screen on"], seq("tools sfxbank", "@0.3", "sfxbank tap jackpot", "sfxbank tap coin", "sfxbank tap 0", "simknob 6", "@0.2", "simknob -3",
                                   "help tap", "@0.3", "back", "@0.3", "back", "@0.3", wait=0.1),
                ["screen off"], "音效庫: open the page, play three sounds, turn the list, open 說明 and come back, Back"),
    # ideas G2: 多組計數器: taps and the borrowed knob on 4 counters, the ring (count 4 -> 8 -> 2 -> 4 rebuilds the sectors, names, style, the armed zero, 說明), Back; the values are saved on every change
    "counter": (["screen on"], seq("tools counter", "@0.3", "counter set 4 0 0", "counter tap 0", "counter tap 2", "simknob 3", "simknob -1", "counter ring", "@0.2", "popring 0", "@0.2", "popring 0", "popring 0", "popring 1", "popring 2",
                                   "popring 3", "popring 3", "@0.2", "counter tap 1", "counter ring", "popring 4", "@0.3", "back", "@0.3", "back", "@0.3", wait=0.1),
                ["screen off"], "多組計數器: taps, the borrowed knob, the ring's three axes (the sectors rebuilt for 8, 2 and back to 4), the armed zero, 說明, Back"),
    # ideas G7: 喝水提醒: two sector taps (a cup, a snooze), the borrowed knob, the ring (the four axes, 提醒 on / off, the armed 少一杯, 說明), Back; the cups and settings are saved on every change
    "drink": (["screen on"], seq("show drink", "@0.3", "drink set 1 2 9 21 8 0", "drink tap 0", "drink tap 1", "drink ring", "@0.2", "popring 0", "popring 0", "popring 1", "popring 2", "popring 3", "popring 4", "popring 5", "popring 5", "@0.2",
                                 "drink ring", "popring 6", "@0.3", "back", "@0.3", "back", "@0.3", wait=0.1),
              ["screen off"], "喝水提醒: taps, the ring's four axes and the on / off cell, the armed 少一杯, 說明, Back"),
    # ideas G7: 習慣打卡: taps (a tick, a second tap that does nothing), the ring (count 4 -> 8 -> 2 -> 4 rebuilds the sectors, the name axis, the armed 取消今天, 說明), Back; the records are saved on every change
    "habits": (["screen on"], seq("show habits", "@0.3", "habit reset", "habit set 4", "habit tap 0", "habit tap 0", "habit tap 2", "habit ring", "@0.2", "popring 0", "@0.2", "popring 0", "popring 0", "popring 1", "popring 2", "popring 2", "@0.2",
                                  "habit ring", "popring 3", "@0.3", "back", "@0.3", "back", "@0.3", wait=0.1),
               ["screen off"], "習慣打卡: ticks, the ring's two axes (the sectors rebuilt for 8, 2 and back to 4), the armed 取消今天, 說明, Back"),
    # ideas G5: 氛圍頁: the four scenes (games rule), pokes, a rare event, the borrowed knob (brightness), the tone, the ring (brightness cell, 說明 and back, 重來), the exit pill, Back
    "ambient": (["screen on"], seq("tools ambient", "@0.3", "arena tap 0 20", "games rule 1", "@0.3", "arena tap 0 -60", "arena game rare", "@0.5", "games rule 2", "@0.3", "arena tap 0 0", "games rule 3", "arena tap 0 0",
                                   "arena game rare", "@0.3", "games style 1", "games mode 1", "simknob 1", "@0.2", "games ring", "@0.2", "popring 2", "popring 4", "@0.3", "back", "@0.3", "games rule 0", "games style 0", "games mode 0",
                                   "simknob 1", "exit tap back", "@0.3", "back", "@0.3", wait=0.1),
                ["screen off"], "氛圍頁: four scenes, pokes, the rare events, the brightness by the borrowed knob, tone and speed, the ring (brightness, 說明), the exit pill, Back"),
    "breath": (["screen on", "simnoise 30 3", "noise 0"], seq("uipush breath", "@0.3", "arena game follow 2", "arena step 90", "arena tap 0 0", "games rule 1", "arena step 130", "games rule 2", "arena step 40",
                                   "games style 1", "games mode 1", "simknob 1", "@0.2", "games ring", "@0.2", "popring 2", "popring 4", "@0.3", "back", "@0.3", "games mode 0", "arena game end", "arena step 40", "arena tap 0 0",
                                   "simknob 1", "exit tap back", "@0.3", "back", "@0.3", wait=0.1),
                ["screen off"], "呼吸教練 (ideas G7): the patterns and lengths, the noise follow by the borrowed knob, a session to its end and again, the ring (noise cell, 說明), the exit pill, Back"),
    # AVRCP 1.6 step 1: the phone's track on the playing page (simulator: simbtmeta stands in for the phone): title, artist, album, a cover, 詳細 open and Back,
    # another cover, no cover, everything cleared (the page goes back to the phone's name); the stream keeps the player all along
    "btmeta": (["screen on", "vol 0", "simbt stream", "uipush now", "@0.5"],
               seq("simbtmeta title Paper Planes", "simbtmeta artist Sim Band", "simbtmeta album Night Train", "simbtmeta art /sdcard/_player/config/radio.txt", "@0.5",
                   "nowmenu -2", "@0.3", "nowmenu 1", "@0.3", "back", "@0.3", "simbtmeta art /sdcard/_player/config/app.txt", "@0.5", "simbtmeta art -", "@0.3",
                   "simbtmeta title Night Owl", "@0.5", "simthumblow 1", "simbtmeta art /sdcard/_player/config/radio.txt", "@0.8", "simbtmeta hold 1",
                   "simbtmeta art /sdcard/_player/config/app.txt", "@0.3", "simbtmeta title Late", "@0.3", "simbtmeta hold 0", "@0.5",
                   "simbtmeta link 0", "@0.3", "simbtmeta link 1", "@0.3", "simbtmeta clear", "@0.5"),
               ["simbt stop", "back", "screen off"], "speaker mode, the phone's track: title / artist / album, cover, 詳細 and Back, another cover, none, "
               "a cover refused for low memory (the note), a held GET answered late after a track change, the link down and up, cleared"),
    # games G0: the hub's 說明, then every game of games.def (list-driven): moves and the device's answer, the mode button (tap, knob), leaving mid-thought
    "simon": (["screen on"], lambda b, i: seq(*simon_cmds(), wait=0.1)(b, i), ["screen off"], "四色記憶 (G1, ui_sectors): rounds, the milestone, a wrong tap and the card, again, 練習's replay, 雙人 with 洗牌's move, every style, 重來, keys, Back mid-show and before the card"),
    "frame": (["screen on", "vol 0"], lambda b, i: seq(*frame_cmds(), wait=0.1)(b, i), ["screen off"], "the page frame (F1a2): the overlay by the bubble, a knob turn and a tap, an option (opens the page's ring), 返回, the pause overlay, on the pilot pages (round) or on every kind of page (square)"),
    "fpkeys": (["screen on", "vol 0"], lambda b, i: seq(*fpkeys_cmds(), wait=0.1)(b, i), ["screen off"], "FakePod (square simulator): the menu by the keys and by drag, the status strip, 疊塔 and 四子棋 by the keys only, the pause overlay"),
    "exit": (["screen on", "vol 0"], lambda b, i: seq(*exit_cmds(), wait=0.1)(b, i), ["screen off"], "共用出口 G4b: the bubble sheet's 返回 pill (a knob turn, a tap, the 4 s run out), then every game of games.def: the pause overlay opened and resumed, opened again and left by its 返回 while the computer thinks (the page's paused timers go with it); knobs and timers"),
    "games": (["screen on"], lambda b, i: seq(*games_cmds(), wait=0.1)(b, i), ["screen off"], "遊戲: the hub's 說明 and every game of games.def (moves, the device's answer, the mode button, its ring and 說明, Back while it thinks; G0b: every rule and style, effects mid-flight, the result card, a golden game)"),
    # G1: the shared play field (ui_arena) through 環形打磚塊: a serve (the knob lent to the paddle), real-time play, the knob and side taps, the game's ring
    # (pause) and the resuming tap, every style (霓虹: the energy feed keeps the timer), every rule (雙拍, 黏球), screen off mid-rally (pause) and the tap
    # that resumes, three balls run out by fixed steps (the end: the result card, its delay timer) and a tap on the card, then Back with a burst in flight
    # G2: the shared dial (ui_dial) through 保險箱開鎖: taps and the knob lent by a tap, the target haptics, a lock, a wrong number, the open safe and its card, a tap = again,
    # every rule (專家, 計時 with its clock paused by the ring), screen off mid-play, the meter mode forced on, Back
    "dial": (["screen on", "games gold 1"], seq("games open safe", "@0.3", "games count 0", "dial tap 60 0", "dial turn 7", "dial game pos 35", "dial turn 1", "dial tap 0 0",
                                 "dial game pos 5", "dial tap 0 0", "frame show", "simtaprow 1", "@0.2", "popring -1", "@0.2", "games style 1", "games style 2", "games style 0", "games mode 1", "games mode 2",
                                 "games rule 1", "dial tap 60 0", "games rule 2", "dial tap 60 0", "dial step 200", "screen off", "@0.4", "screen on", "@0.2", "dial tap 60 0",
                                 "games rule 0", "games count 0", "dial game pos 35", "dial tap 0 0", "dial game pos 67", "dial tap 0 0", "dial game pos 84", "dial tap 0 0", "@1",
                                 "games result tap", "@0.2", "dial meter 1", "dial feed 70", "dial step 40", "dial feed 5", "dial step 200", "dial meter 0", "dial tap 60 0", "back", "@0.3", "back", "@0.3", wait=0.1),
             ["games gold auto", "screen off"], "保險箱開鎖 on the shared dial (every game golden): taps, the knob, a lock, a wrong number, the open safe and the card, again, every rule and hint, screen off, the meter mode, Back"),
    # G3: 節拍點擊 and the shared beat feed: the metronome (start, hits, a stray, the knob's tempo), the classic rule on the synthetic click track, off-beat, a whole round to the card,
    # the rules and styles, screen off, Back mid-round (the 20 Hz detector timer goes with the page)
    "beat": (["screen on", "games gold 1"], seq("games open beat", "@0.3", "games rule 1", "arena tap 0 0", "@1.8", "arena tap 0 0", "arena tap 0 0", "simtap 180 193", "simknob 3", "@0.5",
                                 "games style 1", "games style 2", "games style 0", "games mode 1", "games mode 2", "games mode 0", "beat sim 120", "games rule 0", "arena tap 0 0", "@4",
                                 "games rule 2", "arena tap 0 0", "@2", "screen off", "@0.4", "screen on", "@0.2", "arena tap 0 0", "games rule 1", "arena hold 1", "arena tap 0 0", "arena step 2400",
                                 "@1", "games result tap", "@0.2", "beat sim 0", "back", "@0.3", "back", "@0.3", wait=0.1),
             ["games gold auto", "beat sim 0", "screen off"], "節拍點擊 on the shared field and the beat feed: the metronome, classic and off-beat on the synthetic click track, the knob, styles, windows, screen off, a whole round to the card, Back"),
    # G4: 骨牌 on the shared piles (ui_cards): a tile selected and moved, the device's step, the ring, styles and thinking times, a rule, a whole game to its card and a tap = again, 五點, Back mid-game
    "dominoes": (["screen on"], seq("games open dominoes", "@0.3", "games dom deal 1", "games dom hold", "cards tap 2 6", "cards tap 1 0", "games dom dev", "frame show", "simtaprow 1", "@0.2", "popring -1", "@0.2",
                                    "games style 1", "games style 2", "games style 0", "games mode 1", "games mode 0", "games rule 1", "games dom auto 40", "@1", "games result tap", "games rule 2",
                                    "games dom auto 12", "games dom hold", "cards tap 2 0", "games rule 0", "games dom deal 1", "games dom hold", "back", "@0.3", "back", "@0.3", wait=0.1),
                 ["screen off"], "骨牌 on the shared piles: select and move, the device's step, ring, styles, thinking times, rules, a whole game to its card, a tap = again, Back"),
    # G3: 疊塔 on the shared field: start and drops (a cut with its falling piece and particles, perfects, a golden block), the camera, every rule and speed, the ring pause,
    # screen off mid-slide, the miss (the result card and its tap) and Back with a piece in flight
    # G4: 猜歌 (ui_sectors + clips through the fake player): a question, replay, right and wrong answers, a whole 5-question game to the card and again, rules, styles, Back mid-clip (the song put back)
    "guess": (["screen on", "games golden 0"], seq("track 2", "@2", "games open guess", "@0.3", "games mode 0", "sectors hub", "@0.3", "sectors hub", "games right", "@1.5", "games wrong", "@1.5",
                                 "games rule 1", "sectors hub", "@2.2", "games rule 2", "games style 1", "games style 2", "games style 0", "games rule 0", "sectors hub", "games right", "@1.5",
                                 "games right", "@1.5", "games right", "@1.5", "games right", "@1.5", "games right", "@1.5", "@1", "games result tap", "@0.3", "back", "@0.5",
                                 "games open guess", "@0.3", "sectors hub", "@1", "back", "@0.5", wait=0.1),
              ["games golden auto", "screen off"], "猜歌 on ui_sectors: clips through the player, replay, answers, a whole game to the card and again, rules, styles, Back mid-clip (the song put back)"),
    "tower": (["screen on", "games gold 1"], seq("games open tower", "@0.3", "arena hold 1", "arena tap 0 0", "arena game put 12", "arena game x 12", "arena tap 0 0", "arena step 6",
                                 "arena game gold 1", "arena tap 0 0", "arena step 20", "arena hold 0", "frame show", "simtaprow 1", "@0.2", "popring -1", "@0.2", "simtap 180 193", "@0.3",
                                 "games style 1", "games style 2", "games style 0", "games mode 1", "games mode 2", "games mode 0", "games rule 1", "arena tap 0 0", "arena game put 6",
                                 "games rule 2", "arena tap 0 0", "@0.5", "arena tap 0 0", "screen off", "@0.4", "screen on", "@0.2", "simtap 180 193", "@0.2",
                                 "arena hold 1", "arena game x 70", "arena tap 0 0", "arena step 5", "@1", "games result tap", "arena tap 0 0", "arena game x 12", "arena tap 0 0", "back", "@0.3", "back", "@0.3", wait=0.1),
              ["games gold auto", "screen off"], "疊塔 on the shared field (every game golden): drops, cuts, falling pieces, the camera, every rule and speed, ring pause, screen off, the miss and its card, Back"),
    # G4: 15 拼圖 on the board's tiles: a far tap (refused), slides (the tile in flight), the three rules (the board is rebuilt each time), every style and hints, screen off, the solved card, again, Back mid-slide
    "puzzle15": (["screen on", "games gold 1"], seq("games open puzzle15", "@0.3", "games almost 5", "board tap 0", "board tap 10", "board tap 14", "@0.3", "games style 1", "games style 2", "games style 0", "games mode 1",
                                 "games rule 1", "games rule 2", "games rule 0", "games mode 0", "games almost 40", "screen off", "board tap 14", "@0.3", "screen on", "games almost 40", "board tap 15", "@1",
                                 "games result tap", "@0.2", "games almost 5", "board tap 10", "back", "@0.3", "back", "@0.3", wait=0.1),
              ["games gold auto", "screen off"], "15 拼圖 on the board's tiles (every game golden): refused taps, slides, every rule with its rebuilt board, styles and hints, screen off, the solved card, again, Back mid-slide"),
    # G6: 旋鈕小玩意 (the dial made again on every rule change): wind-up taps, release, snap, slack; fishing zone, catch, line, golden, styles, glow, screen off, the card, Back
    "knobtoy": (["screen on"], seq("games open knobtoy", "@0.3", "games count 0", "dial tap 60 0", "dial turn 5", "dial step 60", "dial game sweet 20", "dial game set 20", "dial tap 0 0",
                                   "dial game set 39", "dial tap 60 0", "frame show", "simtaprow 1", "@0.2", "popring -1", "@0.2", "games style 1", "games style 2", "games style 0", "games mode 1", "games mode 2", "games mode 0",
                                   "games rule 1", "dial tap 60 0", "dial game fish 80", "dial game set 10", "dial step 30", "dial game line 99", "dial step 3", "games result tap", "@0.2",
                                   "games rule 0", "dial game gold", "dial game sweet 20", "dial game set 20", "dial tap 0 0", "dial game sweet 20", "dial game set 20", "dial tap 0 0",
                                   "dial game sweet 20", "dial game set 20", "dial tap 0 0", "@1", "games result tap", "@0.2", "games rule 1", "dial tap 60 0", "dial game catch 975", "dial game fish 52",
                                   "dial game set 52", "dial step 3", "@1", "screen off", "@0.4", "screen on", "@0.2", "dial tap 60 0", "games result tap", "@0.2",
                                   "games rule 2", "arena hold 1", "arena game gap 0 90", "arena tap 0 0", "arena step 40", "arena game theta 30", "simknob 1", "arena tap 60 0", "arena step 30", "games mode 1", "games mode 2", "games mode 0",
                                   "arena game marble 0 94", "arena step 2", "@1", "games result tap", "@0.2", "games rule 3", "arena tap 0 0", "arena step 30", "simknob 1", "games rule 4", "arena tap 0 0", "arena step 30",
                                   "popring -1", "@0.2", "popring -1", "@0.2", "games style 1", "games style 0", "maze grav 0 0", "games rule 5", "arena hold 1", "arena tap 0 0", "maze grav 100 0", "arena step 30",
                                   "arena game marble 52 52", "arena step 2", "@1", "games result tap", "@0.2", "screen off", "@0.4", "screen on", "@0.2", "games rule 2", "games rule 0", "back", "@0.3", "back", "@0.3", wait=0.1),
                  ["screen off"], "旋鈕小玩意: wind-up and fishing, the dial made again on each rule change, release, snap, catch, line, golden, every style and glow, screen off, the card, again; G6b the maze rules on the field (dial and field swapped on rule changes, gravity / repel / attract / tilt, drift modes, taps, knob, exit, hole, goal, ring, screen off), Back"),
    # G3: 電台調頻尋寶 on the shared dial (cue, needle shake): taps and the knob, a miss, a lock, the glow modes, every rule, screen off, a golden station, the card, Back
    "radio": (["screen on"], seq("games open radio", "@0.3", "games count 0", "dial tap 60 0", "dial turn 5", "dial step 12", "dial tap 0 0", "dial game on 0", "dial tap 0 0",
                                 "frame show", "simtaprow 1", "@0.2", "popring -1", "@0.2", "games style 1", "games style 2", "games style 0", "games mode 1", "games mode 2",
                                 "games rule 1", "dial game on 0", "dial tap 0 0", "dial step 30", "games rule 2", "dial tap 60 0", "dial step 40", "screen off", "@0.4", "screen on", "@0.2", "dial tap 60 0",
                                 "games rule 0", "games count 0", "dial game gold 1", "dial game on 0", "dial tap 0 0", "dial game on 1", "dial tap 0 0", "dial game on 2", "dial tap 0 0", "@1",
                                 "games result tap", "@0.2", "back", "@0.3", "back", "@0.3", wait=0.1),
              ["screen off"], "電台調頻尋寶 on the shared dial: taps, the knob, a miss, locks, a golden station, every rule and glow, screen off, the card, again, Back"),
    # G5: 萬花尺 on the shared field's canvas: draw, a new curve by tap, every rule, speed, gear / pen / colour axis, style, the ring pause, screen off, finish, Back mid-draw
    "spiro": (["screen on", "games gold 1"], seq("games open spiro", "@0.3", "arena hold 1", "arena step 20", "arena tap 0 0", "arena step 30", "arena game finish", "arena step 20",
                                 "frame show", "simtaprow 1", "@0.2", "popring -1", "@0.2", "simtap 180 193", "@0.3", "games style 1", "games style 2", "games style 0", "games mode 1", "games mode 2", "games mode 0",
                                 "games rule 1", "arena step 40", "games rule 2", "arena step 10", "games rule 0", "games gear 4", "games pen 3", "games colour 1", "games colour 2", "arena step 20",
                                 "arena game finish", "games colour 0", "exit pause", "exit tap resume", "exit pause", "exit tap back", "games open spiro", "@0.3", "screen off", "@0.4", "screen on", "@0.2", "simtap 180 193", "@0.2", "arena tap 0 0", "arena step 5", "back", "@0.3", "back", "@0.3", wait=0.1),
              ["games gold auto", "screen off"], "萬花尺 on the shared canvas (every game golden): drawing, new curves, every rule / speed / axis / style, ring pause, screen off, finish, Back mid-draw"),
    # G6: 旋轉擋板 on the shared field: start, taps and the knob, bounces and misses, golden shots, every rule and style, the square layout, ring pause, screen off, the end and its card, Back mid-play
    "deflect": (["screen on", "games gold 1"], seq("games open deflect", "@0.3", "arena hold 1", "arena game spawn 0", "arena tap 0 0", "arena tap -50 0", "arena tap 50 0", "simknob 2", "@0.2",
                                 "arena game shot 0 0 -75 0 1.5", "arena game shot 1 60 40 -0.8 -0.6 gold", "arena step 30", "arena game deg 90", "arena game shot 0 0 -90 0 1.5", "arena step 60",
                                 "frame show", "simtaprow 1", "@0.2", "popring -1", "@0.2", "simtap 180 193", "@0.3", "games style 1", "games style 2", "games style 0", "games mode 1", "games mode 2", "games mode 0",
                                 "games rule 1", "arena tap 0 0", "arena step 20", "games rule 2", "arena tap 0 0", "arena game shot 0 0 -75 0 1.2", "arena game shot 1 0 -92 0 0", "arena step 30",
                                 "arena game shape 1", "arena tap 0 0", "arena game shot 0 0 -85 0 1.4", "arena step 20", "arena game shape 0", "screen off", "@0.4", "screen on", "@0.2", "simtap 180 193", "@0.2",
                                 "arena tap 0 0", "arena game lives 1", "arena game deg 90", "arena game shot 0 0 -90 0 1.5", "arena step 60", "@1", "games result tap", "arena tap 0 0", "back", "@0.3", "back", "@0.3", wait=0.1),
              ["games gold auto", "screen off"], "旋轉擋板 on the shared field (every game golden): start, taps, knob, bounces, misses, golden shots, every rule / speed / style, the square layout, ring pause, screen off, the end and its card, Back"),
    # G6: 生命遊戲 on the shared field's canvas (fine grid): every cell size, glow, pattern, edge, rule, style and speed, taps (blob, pattern, the golden gun), the ring with 重新播種 and the armed 清空,
    # extinction with its card, the beat rule's clusters, a rest, screen off, Back mid-run (the grid's one PSRAM block and the beat feed must come back)
    "life": (["screen on", "games gold 1"], seq("games open life", "@0.3", "arena hold 1", "arena game seed 25 3", "arena game gen 5", "arena tap 40 -30", "arena step 30", "games pat 3", "arena tap 0 0",
                                 "games pat 1", "frame show", "simtaprow 1", "@0.2", "popring -1", "@0.2", "popring 6", "@0.2", "frame show", "simtaprow 1", "@0.2", "popring 7", "@0.2", "popring 7", "@0.2",
                                 "games cell 0", "arena game gen 3", "games cell 4", "arena game gen 3", "games cell 1", "games glow 3", "arena game gen 5", "games glow 1", "games glow 0", "games cell 2",
                                 "games pat 7", "games pat 10", "games pat 2", "games edge 1", "arena game gen 5", "games edge 0", "games style 1", "games style 2", "games style 0",
                                 "games mode 1", "arena step 10", "games mode 2", "games mode 0", "games rule 1", "arena game beat", "arena step 10", "games rule 2", "arena game gen 5", "games rule 3", "arena game gen 5", "games rule 0",
                                 "arena game seed 1 5", "arena step 60", "@1", "games result tap", "@0.2", "arena game clear", "arena game block 60 60", "arena step 300",
                                 "arena tap 0 0", "screen off", "@0.4", "screen on", "@0.2", "arena tap 0 0", "arena step 5", "back", "@0.3", "back", "@0.3", wait=0.1),
              ["games gold auto", "screen off"], "生命遊戲 on the shared canvas (every tap golden): every cell size / glow / pattern / edge / rule / style / speed, taps, 重新播種, the armed 清空, extinction and its card, the beat rule, a rest, screen off, Back mid-run"),
    "arena": (["screen on", "games gold 1"], seq("games open breakout", "@0.3", "simtap 180 193", "@0.2", "simknob 2", "@1", "arena tap 60 60", "arena tap -60 60", "@0.5",
                                 "frame show", "simtaprow 1", "@0.2", "popring -1", "@0.2", "simtap 180 193", "@0.3",
                                 "games style 1", "@0.5", "games style 2", "@0.3", "games style 0", "games rule 1", "simtap 180 193", "@0.5",
                                 "games rule 2", "simtap 180 193", "@1.5", "simtap 180 193", "@0.2",
                                 "screen off", "@0.4", "screen on", "@0.2", "simtap 180 193", "@0.3",
                                 "arena game level 5", "simtap 180 193", "@0.6", "arena game put all 3", "@0.6",   # fix round 1: a turning boss ring, a boom chain on demand
                                 "games rule 0", "arena hold 1", "arena tap 0 0", "arena step 900", "arena tap 0 0", "arena step 900", "arena tap 0 0", "arena step 900",
                                 "arena hold 0", "@1", "games result tap", "@0.2", "arena hold 1", "arena tap 0 0", "arena step 19", "back", "@0.3", "back", "@0.3", wait=0.1),
              ["games gold auto", "screen off"], "環形打磚塊 on the shared field (every game golden: its card text is in every cycle, not one in 30): serve, knob and side taps, ring pause and resume, every style and rule, screen off mid-rally, "
              "three balls out by fixed steps to the result card and its tap, Back with a burst in flight"),
    "vocab": (["screen on", vocab_setup], vocab_cycle, [vocab_teardown, "screen off"],
              "單字: home, quiz, right/wrong answers (built-in sample deck only; writes the progress file), back"),
    # --- Task 11: shared list, choosers, favourites ---
    # the two choosers need two or more packs of the kind on the card (the simulator: --root with /vocab/*.zip and /lists/*.zip); with fewer, tools lunch opens the list itself
    # the deck chooser opens scrolled to the current deck (the built-in sample, the last row): it is turned up first (Task 13 fix: "knob 2" alone was at the end and moved nothing)
    "choosers": (["screen on"], seq("vocab decks", "@0.3", "knob -2", "@0.2", "knob 2", "@0.2", "back", "back", "tools lunch", "@0.3", "knob 2", "@0.2", "back", "back"),
                 ["screen off"], "deck chooser and list chooser: open, scroll, leave (the title strips are PSRAM and freed with the rows)"),
    # --- end Task 11 ---
    # --- Task 12 ---
    # rows of the alarm page: 鬧鐘 y 83, 時 134, 分 186, 重複 238, 鈴聲 290 (its top 9 px are in the list window, 267..285: a tap at 276); after the knob scrolled to the end the sound row is at 156
    "alarmpage": (["screen on"], seq("show alarm", "@0.3", "simtap 180 276", "@0.3", "simtap 180 134", "@0.3", "simknob 2", "@0.2", "simknob -2", "@0.2",
                                     "simtap 180 276", "@0.3", "simtap 180 134", "@0.3", "@4.5", "simknob 3", "@0.2", "simtap 180 156", "@0.3", "back", "@0.3"),
                  ["screen off"], "alarm page: arm the sound row, borrow the hour row (disarms) and turn it +-2, arm again, borrow again and let it time out, scroll with the knob, arm again, leave while armed"),
    # --- UI unify T5: settings brightness (bounded axis) then the alarm hour and minute rows, each borrowed and let run out ---
    "axisrows": (["screen on"], seq("setui", "@0.3", "settings key bright", "simknob 1", "@0.2", "simknob -2", "@4.5", "show alarm", "@0.3", "simtap 180 134", "simknob 1", "@4.5", "simtap 180 186", "@4.5", "back", "@0.3", "back", "@0.3", "back", "@0.3"),
                ["screen off"], "settings brightness tap +5, knob +5 -10, let it run out; alarm hour row tap + knob, let run out; minute row tap, let run out; leave both pages"),
    # --- UI unify T6: the scene editor (a custom scene of 3 layers; rows from the top: layers y 98 / 150 / 202, add a sound 253; after the knob to the end the delete row is at 266) ---
    "scene": (["screen on", "simnoise 30 3 custom"], seq("noiseui", "@0.3", "sceneui 0", "@0.3", "simtap 180 98", "@0.3", "popring 1", "simknob 1", "@4.5",
                                                         "simtap 180 253", "@0.3", "back", "@0.3", "simknob 9", "@0.2", "simtap 180 266", "@4.5", "back", "@0.3", "back", "@0.3"),
              ["screen off"], "scene editor: a layer ring (volume tap + knob, let run out), the add-a-sound category list and back, the delete row armed and let run out, leave"),
    # --- UI unify T7: the toys (the dice count and the people group are axis buttons in the HEAD slot, y 68; the gacha ring is the last part; the pomodoro ring is part of popring) ---
    "toys": (["screen on"], seq("tools dice", "@0.3", "frame show", "simtaprow 0", "simknob 1", "@0.2", "simknob -2", "@4.5", "back", "@0.3",
                                "tools people", "@0.3", "frame show", "simtaprow 0", "simknob 1", "@0.2", "@4.5", "back", "@0.3", "back", "@0.3",
                                "uipush fortune", "@0.3", "fortune gacha", "@0.3", "back", "@0.3", "back", "@0.3"),
             ["screen off"], "toys: dice count axis (tap, knob +1 -2, let run out), people group axis (tap, knob, let run out), leave; the gacha ring (open, leave)"),
    # --- end Task 12 ---
    # --- Task 13: the four picker lists (data root: sim/pickers_root.sh; "simnoise" is simulator-only, the board answers it as an unknown command) ---
    "pickers": (["screen on", "simnoise 30 3"], lambda b, i: pickers_cycle(b),
                ["screen off"], "stations, scenes, systems, folders: open, knob to the end and back, leave; the rescan row (its place read from uiboxes) armed and left armed, then armed and let run out (4 s)"),
    # LIBTREE: tap a folder (its level opens in the page), flick through its three pages, the knob across page ends, a level two deep, up twice, the song list page over the music page
    "tree": (["screen on"], lambda b, i: tree_cycle(b),
             ["screen off"], "song tree: a folder of sub-folders, a sub-folder, up, up; a paged folder: page flicks (wrapping), the knob past page ends both ways; up; a level page over the music page; leave"),
    # --- final-review fix round (simulator: simpress is simulator-only, the board answers it as an unknown command) ---
    # settings rows 7 = 震動長度 (an axis: tap +1, knob +1 -2, net 0) and 10 = USB 隨身碟模式 (two-tap: armed, then let run out)
    "langcycle": (["screen on"], seq("setui", "@0.3", "settings key lang", "knob 1", "@0.2", "knob 1", "@0.2", "lang tw", "uipush help", "lang cn", "lang en", "lang tw", "back", "back", "@0.3", "back", "@0.3"),
                  ["screen off"], "i18n A: the language axis on the settings page (tap, knob steps), then 繁中 -> 簡中 -> English -> 繁中 over a help page (every screen is built again each time); leave"),
    "setaxis": (["screen on"], seq("setui", "@0.3", "settings key bright", "knob 1", "@0.2", "knob -2", "@0.2", "@4.5", "settings key usb", "@4.5", "back", "@0.3", "back", "@0.3"),
                ["screen off"], "settings: borrow the brightness row, step it with the knob both ways, let the borrow time out; arm the USB row and let it run out; leave"),
    # BTAUDIO: the Bluetooth audio page (a fake AAC-only headset: every row tapped, the knob borrowed, the reconnect on leaving), the Wired output page (the ceiling tapped 20 times while a 192k source plays: the audio service re-plans each time) and the details page's output block over a playing track
    "btaudio": (["screen on", "vol 0", "simbt mode 2", "simbt caps aac,sbc", "simbt connect"], seq("btaudio open", "@0.3", "btaudio key codec", "btaudio key rate", "btaudio key bits", "btaudio key chan", "btaudio key ldac", "knob 1", "@0.2", "knob -1", "@0.2", "back", "@0.3",
                "wired open", "@0.3", *(["wired key dacmax"] * 20), "back", "@0.3", "mp3ui", "@0.3", "simaudiorate 192000 1", "mp3 1", "@0.6", "uipush meta", "@0.3", "knob 4", "outstat", "back", "@0.3", "stop", "@0.3", "back", "@0.3"),
                ["simbt mode 0", "screen off"], "Bluetooth audio: open, tap every row, borrow the knob, leave (one reconnect); Wired output: 20 taps on the ceiling with a 192k source playing (re-plans); the details page with the output block over the playing track; leave"),
    # fx apps T13: the untagged-level row (an axis: tap +1, knob +2 -2, net +1 a cycle, so the tap wraps at the top now and then) and the reboot row armed and let run out (never tapped twice)
    "setrows": (["screen on"], seq("setui", "@0.3", "settings key sleep", "knob 2", "@0.2", "knob -2", "@0.2", "@4.5", "settings key reboot", "@4.5", "back", "@0.3", "back", "@0.3"),
                ["screen off"], "settings: the sleep-timer row borrowed (tap, knob +2 -2, let the borrow run out); the reboot row armed once and let run out (4 s); leave"),
    # set time (2026-11-22): the page from Settings > System, a month axis stepped by tap and knob, Apply (the message page), then left with an axis still borrowed
    "timeset": (["screen on"], seq("setui", "@0.3", "settings key time", "@0.3", "timeset key month", "knob 2", "@0.2", "knob -1", "@0.2", "timeset key apply", "@3", "timeset key day", "knob 1", "@0.2", "back", "@0.3", "back", "@0.3", "back", "@0.3"),
                ["screen off"], "set time: month axis (tap, knob +2 -1), Apply and its message, day axis borrowed when the page is left"),
    # hires T11 (simulator: simdac is simulator-only): the rate test page opened from the settings row, a tap at tone 1's time (ignored) and then, 2 s later, the confirm of tier 1; left by Back in tier 2
    "ratetest": (["screen on", "simdac 0"], seq("setui", "@0.3", "settings key ratetest", "@0.3", "ratetest tap", "@2", "ratetest tap", "@0.3", "back", "@0.3", "back", "@0.3"),
                 ["simdac off", "screen off"], "settings -> rate test page (the test starts, tones play on the fake audio service), a tap too early, a tap after tone 2 (confirm), Back (leaves: nothing stored), Back"),
    # hires T14 (simulator: simaudiorate is simulator-only, the board has real files): rate changes on the wired output, a 44.1 -> 96k -> 48k -> 192k -> 384k -> 44.1 track sequence at dac_max_rate 384000,
    # a Bluetooth sink on and off during the 384k track (three halvings and back), stop
    "hires": (["screen on", "vol 0", "seti dac_max_rate 384000", "mp3ui"],
              seq("simaudiorate 44100 1", "mp3 1", "@0.3", "simaudiorate 96000 1", "mp3 2", "@0.3", "simaudiorate 48000 1", "mp3 3", "@0.3", "simaudiorate 192000 1", "mp3 4", "@0.3",
                  "simaudiorate 384000 1 4096", "mp3 5", "@0.3", "simsink 1", "@0.2", "simsink 0", "@0.2", "stop", "@0.3", "back", "@0.2"),
              ["stop", "simsink 0", "cfgdel dac_max_rate", "screen off"], "hires: wired track sequence 44.1/96/48/192/384 kHz (the fake audio service plans each), a sink on and off on the 384k track, stop"),
    # cardfmt (simulator: simsd is simulator-only): the card format flow without the format: settings row, the card page, the warning, the code pad (a wrong tap, then the
    # whole code), the final row armed once (no second tap: nothing formats), Back three times; the 說明 page of the card page and Back
    "cardfmt": (["screen on", "simsd fail gpt"], seq("setui", "@0.3", "settings key card", "@0.3", "cardfmt row", "@0.3", "cardfmt continue", "@0.3", "codepad tap u", "codepad tap a",
                                                      *[f"codepad tap {k}" for k in "uuddlrlrba"], "@0.3", "cardfmt arm", "@0.2", "back", "@0.3", "help tap", "@0.3", "back", "@0.3",
                                                      "back", "@0.3", "back", "@0.3"),
                ["simsd ok", "screen off"], "記憶卡: settings row -> card page -> warning -> code pad (wrong tap, the code) -> final row armed once -> Back (nothing formats); 說明; Back to the menu"),
    # cardtest: the card diagnostics page: a raw read to its end, the file test armed and run to its end (the temp file made and removed), then Back mid-test (cancelled), the 說明 page
    "cardtest": (["screen on"], seq("setui", "@0.3", "settings key cardtest", "@0.3", "cardtest tap raw", "@6", "cardtest tap file", "cardtest tap file", "@0.3", "back", "@0.3",
                                    "settings key cardtest", "@0.3", "cardtest tap file", "cardtest tap file", "@10", "help tap", "@0.3", "back", "@0.3", "back", "@0.3"),
                 ["screen off"], "記憶卡診斷: raw read finished, a file test cancelled by Back, a file test finished, 說明; Back"),
    # night3 E: pages the 2026-11-21 board night visited that no cycle covered. lab: the console's list / set / refused value / reset (each stores and re-reads the card config);
    # nowstyles: the three 播放頁樣式 values through the settings row (a switch while a track plays, the sharp cover swapped in each time), the page opened and left in each;
    # libwalk: the folder list (the _test_hires folder is the last of the list on the board), a folder opened and left, paged by the knob both ways
    "lab": (["screen on"], seq("lab", "lab flick_ms 400", "lab flick_ms", "lab lcd_chunk_rows 48", "lab lcd_chunk_rows 20", "lab key_dbl_ms 500", "lab reset", "lab bogus 1", "lab"),
            ["lab reset", "screen off"], "lab: the console's list, a set, a refused value, an unknown key, reset (config writes)"),
    "nowstyles": (["screen on", "vol 0"], seq("cfgset now_fp 2", "track 3", "@1.5", "back", "@0.3", "cfgset now_fp 0", "track 3", "@1.5", "back", "@0.3", "cfgset now_fp 1", "track 3", "@1.5",
                                              "cfgset now_fp 0", "@0.5", "cfgset now_fp 1", "@0.5", "back", "@0.3"),
                  ["stop", "cfgdel now_fp", "cfgdel mp3_order", "screen off"], "the three now-playing styles switched while the page is open and left each time (the sharp cover decoded for A and B)"),
    "libwalk": (["screen on", "vol 0"], seq("mp3ui", "@0.3", "knob 3", "@0.2", "knob 40", "@0.2", "knob -40", "@0.2", "simtap 230 230", "@0.4", "knob 5", "@0.2", "knob -5", "@0.2", "back", "@0.3", "back", "@0.3"),
                ["stop", "screen off"], "the folder list: scrolled to its end and back, a folder opened and its track list scrolled, Back twice"),
    "msgpage": (["screen on", "mem", "vol 0", "track 3", "@2"], seq("track 3", "@0.5", "uipush message", "@3"),
                ["stop", "@0.5", "screen off"], "a message page over the cover page (the song restarted each cycle so that it never ends), left to go by itself"),
    "homering": (["screen on", "mem", "vol 0"], seq("track 3", "@0.5", "nowmenu -2", "@0.3", "simpress 180 320 3100", "@0.3",
                                                    "track 3", "@0.5", "nowmenu -2", "nowmenu 2", "@0.3", "simpress 180 318 3100", "@0.3"),
                 ["stop", "@0.5", "screen off"], "long press on Back = home: with the playing ring open (its Back cell), and with the song list open from the ring (its Back button)"),
    # --- end final-review fix round ---
    # instrument app (enter with "inst" first; its console takes "inst ..." and mem); --kit NAME from "inst kits"
    "inst_pages": ([], seq(*[f"inst page {p}" for p in range(6)], "inst page 0", "popring -2", "popring 3", "inst knob -1", "@4.5",
                           "popring -2", "popring 5", "popring -3", "inst page 3", "inst row 5", "@0.3", "inst page 0"), [],
                   "instrument: every page (UI unify T10: pushed over the play page), the play ring (chip axis stepped and given back, 4 s timeout; exit armed, Back cell), the kits list"),
    # UI unify T11: every instrument ring opened, left to time out (4.5 s), opened again and left by its Back cell; the file ring (on the first loop file: more-list row 2,
    # after save and export MOD) the same, closed by its Back cell, then the more list left by Back; the pads ring is opened by "popring -2" (the hub's tap).
    # Batch B fix S-m7: a settings axis row borrowed (+1, knob -1) and left to time out; the pads ring's kit cell (the kits list over the pads, Back); the metronome
    # ring's clock, click and centre each toggled twice (the starting state again)
    "inst_rings": ([], seq("inst page 0", "popring -2", "@4.5", "popring -2", "popring -3",
                           "inst page 1", "popring -2", "@4.5", "popring -2", "popring 1", "inst knob -1", "@4.5", "popring -2", "popring 0", "@0.3", "back", "@0.3",
                           "popring -2", "popring -3",
                           "inst page 3", "inst row 1", "inst knob -1", "@4.5",
                           "inst page 2", "popring -2", "@4.5", "popring -2", "popring 0", "inst knob 1", "inst knob -2", "@0.2",
                           "popring 3", "popring 3", "popring 4", "popring 4", "popring -1", "popring -1", "@0.2", "popring -3",
                           "inst page 5", "popring -2", "@4.5", "popring -2", "popring 2", "popring -3",
                           "inst page 5", "popring -2", "popring 4", "@0.2", "inst row 2", "@4.5", "inst row 2", "popring 1", "popring -3", "back", "@0.3", "inst page 0"), [],
                   "instrument rings: play, pads (layer axis tapped +1, knob -1; kit cell -> kits list -> back), a settings axis row (+1 -1, timeout), metronome (tempo axis tapped +1, knob +1 -2; clock, click, centre twice each), looper (clear armed), each timed out and left by Back; the file ring (delete armed) timed out and closed"),
    # 外接模式 T2 (sim --mode ext): keys of the media layer and of a three-layer config (layer cells rebuild the ring), the 設定 page and its 說明, 重讀設定, the exit armed and let run out,
    # the cable out and in (the reload after the eject rebuilds the cells again), the settings address typed
    "ext_ring": (["simusb mount", "ext cfg @layers", "ext eject", "@0.5"], seq("ext key 0", "@0.2", "ext layer 1", "@0.4", "ext key 4", "@0.3", "ext layer 2", "@0.4", "ext layer 0", "@0.4",
                                       "ext errors", "@0.2", "help tap", "@0.2", "back", "back", "@0.3", "ext reload", "ext arm", "@4.5", "ext disarm", "ext url", "@0.3",
                                       "ext cfg @bad", "simusb unplug", "simusb mount", "ext eject", "@0.6", "ext cfg @layers", "simusb unplug", "simusb mount", "ext eject", "@0.6"), [],
                 "external mode: key sends, layer cells (ring rebuilt), the 設定 page with its 說明, reload, exit armed and timed out, unplug / replug with a reload (good and rejected file)"),
    # 外接模式 T3 (sim --mode ext): unplug -> the question page -> yes -> advertise, connect, a key, disconnect, replug -> the question -> yes (back on USB); then unplug -> no, the 藍牙 cell, 清除配對
    "ext_ble": (["simusb mount", "ext cfg @layers", "ext eject", "@0.5"], seq("simusb unplug", "@0.6", "ext ask yes", "@0.4", "ext ble conn", "@0.3", "ext key 0", "@0.3", "ext ble disc", "@0.3", "ext ble adv",
                                       "simusb mount", "@0.6", "ext ask yes", "@0.4", "simusb unplug", "@0.6", "ext ask no", "@0.3", "ext bt", "@0.6", "ext ask no", "@0.3", "ext unpair", "@0.3", "simusb mount", "@0.5"), [],
                "external mode Bluetooth: the question page both ways (yes / no / the 藍牙 cell), advertising, a connection, a key over BLE, disconnect, unpair"),
    # 外接模式 T4 (sim --mode ext): the descriptor walked and mounted, alternate settings, the USB mix cell armed and let run out, then the mix cycled three times (drive + keys, sound card + keys, back to all three: the ring is rebuilt each time)
    "ext_usb": (["ext usb plug", "@0.5"], seq("ext usb alt 1", "ext usb alt 3", "ext usb alt 0", "ext usb dump", "ext page", "@0.3", "ext funcarm", "@4.5", "ext page", "ext funcarm", "ext funcarm", "@0.5", "ext usb dump",
                                     "ext page", "ext funcarm", "ext funcarm", "@0.5", "ext usb alt 2", "ext page", "ext funcarm", "ext funcarm", "@0.5", "ext usb dump"), [],
                "external mode USB mix: enumeration walk, alternate settings, the mix cell (armed, timeout, three switches)"),
    # 外接模式 T5 (sim --mode ext): the sound card's stream: a rate / depth, the status line on the page, stop, another rate (the mixer is started again), the volume turned while it streams, mute and the feature unit, then stop
    "ext_uac": (["ext usb plug", "@0.5"], seq("ext uac play 44100 16 1", "@0.3", "knob 1", "@0.3", "ext uac fu -1536", "ext uac mute 1", "ext uac mute 0", "ext uac play 48000 24 1", "@0.3", "ext uac stop", "@0.3", "ext uac status"), [],
                "external mode sound card: a stream (two formats), the status on the page, the knob = device volume while streaming, the feature unit, stop"),
    "inst_chips": ([], seq(lambda i: f"inst chip {i % 11}", "inst note 60 100", "@0.3", "inst alloff"), ["inst chip 0"],
                   "instrument: chip change (11 chips) + a note"),
    "inst_kits": ([], seq(lambda i: f"inst kit {KIT[0]}", "@2.5", "inst kitstat", "inst kit none", "@0.5"), [],
                  "instrument: kit load (TF -> PSRAM) / unload"),
    "inst_loop": ([], seq("inst page 5", "inst loop rec", "@2.4", "inst note 60 100", "inst note 64 100", "inst note 67 100",
                          "@0.5", "inst loop rec", "@0.8", "inst loop clear 0", "inst loop reset", "inst page 0"), [],
                  "instrument: looper record (after the 1-bar count-in) a few notes, overdub, clear, reset"),
    "btpeer": ([], peer_reset, [], "headphones mode: peer reset -> disconnect, reconnect (no audio playing)"),
    "btpeer_play": (["mem", "vol 0", "mp3 all", "@4"], peer_reset, ["stop", "back"],
                    "headphones mode: peer reset while music plays -> reconnect, the stream starts again"),
    "idle": ([], seq("@2"), [], "nothing (baseline noise)"),
    # simulator checker self-tests (tests of the checks: each is expected to FAIL under --port sim; make sim-test runs them)
    "uileak": ([], seq("uileak", "back"), ["uileak clean"], "sim self-test: a screen that leaves a timer and an object behind on purpose"),
    "simleaktimer": ([], seq("simleaktimer"), [], "sim self-test: an esp_timer that is created and never deleted every cycle"),
}


# default warm-up cycles per action (--warm overrides). next: see the doc string; viz: the FreeType glyph caches (LRU, 256 glyphs, glyph-info 2x256, bitmap 256)
# take one new kind/variant/ramp label combination per cycle and are full after ~45 cycles
# arena (G1): the same glyph-cache fill from rare game events (a cleared level's 過關！, a power-up's name, the card's best line): constant from cycle ~45 to 160 (measured)
WARM = {"dial": 10, "arena": 60, "next": 120, "viz": 100, "vizring": 300, "vizpage": 150, "vizkinds": 150, "vizring_leave": 150, "vizplay": 150, "vizown": 300}
# simulator verdict for lv_bytes: default = exact equality after every measured cycle. A band rule (max_spread, max_trend) for actions whose LVGL heap
# legitimately wobbles once the caches are full: accepted when (max - min) of the measured samples <= max_spread bytes AND
# mean(last quarter of the samples) - mean(the quarter before it) <= max_trend bytes (no upward trend). All of it is PSRAM on the device.
LV_BAND = {"viz": (512, 64), "vizring": (512, 64), "vizkinds": (512, 64), "vizplay": (512, 64), "vizown": (512, 64)}   # vizring: the same glyph-cache fill (kind/ramp labels); vizplay: the next song every cycle (another title, +-160 B that returns, no trend)

def lv_verdict(name, lv):
    """(ok, rule text) for the measured lv_bytes samples (lv[0] is taken before the first measured cycle and left out)."""
    v = list(lv[1:])
    if name not in LV_BAND:
        return len(set(v)) <= 1, "rule exact: constant after every measured cycle"
    spread_max, trend_max = LV_BAND[name]
    q = max(1, len(v) // 4)
    last, prev = statistics.mean(v[-q:]), statistics.mean(v[-2 * q:-q] or v[-q:])
    spread, trend = max(v) - min(v), last - prev
    ok = spread <= spread_max and trend <= trend_max
    return ok, f"rule band: spread {spread} <= {spread_max} and trend (mean last quarter - mean quarter before) {trend:+.0f} <= {trend_max}"


def run(a, b):
    setup, fn, teardown, doc = ACTIONS[a.action]
    if a.warm is None: a.warm = WARM.get(a.action, 5)    # see the doc string
    print(f"== {a.action}: {doc}; warm {a.warm}, cycles {a.cycles}, sample every {a.every}, settle {a.settle} s", flush=True)
    for c in setup: do(b, c)
    lit = "screen on" in setup
    for i in range(a.warm):
        if lit: b.cmd("screen on", 0.1)
        fn(b, i)
    xs, fr, lg, ps, mn, lv = [], [], [], [], [], []
    t0 = time.time()
    for i in range(a.cycles + 1):
        if i % a.every == 0:
            f, m, l, p, v = b.settled(a.settle)
            xs.append(i); fr.append(f); lg.append(l); ps.append(p); mn.append(m); lv.append(v)
            print(f"   cycle {i:4d} free={f} largest={l} min={m} psram={p} lv={v} top={b.top}  t={time.time() - t0:.0f}s", flush=True)
        if i < a.cycles:
            if lit: b.cmd("screen on", 0.1)   # counts as activity: the 60 s backlight timeout must not darken the test
            fn(b, a.warm + i)
    for c in teardown: do(b, c)
    mode = getattr(b, "mode", None)
    for c in (("inst page 0",) if mode == "inst" else () if mode == "ext" else ("mp3ui", "back")): do(b, c)   # cfg last_screen back to JC's "mp3" (screens that open set it); the instrument app: its play page; the USB mode: its one page
    h = len(xs) // 2
    s_all, s_half = slope(xs, fr), slope(xs[h:], fr[h:])
    s_l, s_p, s_v = slope(xs, lg), slope(xs, ps), slope(xs, lv)
    print(f"RESULT {a.action} cycles={a.cycles} ui_leak_lines={b.leaks} free {fr[0]} -> {fr[-1]} (min sample {min(fr)}) slope={s_all:+.1f} B/cycle "
          f"2nd-half={s_half:+.1f} largest {lg[0]} -> {lg[-1]} ({s_l:+.1f}) psram {ps[0]} -> {ps[-1]} ({s_p:+.1f} B/cycle) lv {lv[0]} -> {lv[-1]} ({s_v:+.1f} B/cycle)", flush=True)
    return xs, lv


def sim_main(a):
    """Every action in the list on one simulator process; ok = exit code 0, no sanitizer lines, no 'APP ui leak', lv_bytes and the esp_timer count the same after every cycle."""
    from sim_con import SimCrash
    b, bad = None, False
    KIT[0] = a.kit
    try:
        b = SimBoard(a.root, None if a.mode == "player" else a.mode)
        for spec in a.action.split(","):
            name, _, n = spec.partition(":")
            a2 = argparse.Namespace(**vars(a)); a2.action = name
            if n: a2.cycles = int(n)
            leaks0, tm0 = b.leaks, len(b.timers)
            xs, lv = run(a2, b)
            tm = b.timers[tm0:]
            flat, rule = lv_verdict(name, lv)         # sample 0 is taken before the first measured cycle and left out
            tflat = len(set(tm[1:])) <= 1            # the same rule for the live esp_timer count (constant, not necessarily 0: a screen may keep one for its lifetime)
            at = {c: lv[xs.index(c)] for c in (1, 10, 50, 100) if c in xs}
            print(f"SIM {name} cycles={a2.cycles} lv_bytes at cycle {at} -> {'ok' if flat else 'FAIL'} ({rule}), esp_timers {tm[0]} -> {tm[-1]} "
                  f"{'constant' if tflat else 'CHANGES'}, ui_leak_lines={b.leaks - leaks0}", flush=True)
            bad |= not flat or not tflat or b.leaks != leaks0
        rc, san = b.close()
    except SimCrash as e:
        print(f"FAILED: {e}", flush=True)
        return 1
    finally:
        if b: b.close()
    if rc or san:
        print(f"FAILED: simulator exit code {rc}" + "".join("\n  " + l for l in san[:10]), flush=True)
        return 1
    print(f"SIM exit code 0, no sanitizer output; lv_bytes (per-action rule) and esp_timers {'ok after every cycle in every action' if not bad else 'NOT ok, or ui leak lines: see above'}", flush=True)
    return 1 if bad else 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("action", nargs="?")
    ap.add_argument("--cycles", type=int, default=60); ap.add_argument("--warm", type=int, default=None)
    ap.add_argument("--every", type=int, default=5); ap.add_argument("--settle", type=float, default=1.5)
    ap.add_argument("--echo", action="store_true"); ap.add_argument("--list", action="store_true")
    ap.add_argument("--port", default=PORT, help="the main board by-id path, or sim"); ap.add_argument("--root", help="sim only: data root to copy (default sim/fixture)"); ap.add_argument("--kit", default="demo")
    ap.add_argument("--mode", choices=["player", "inst", "ext"], default="player", help="sim only: the simulator's boot mode (inst: the instrument app, for the inst_* actions; usb: the USB drive mode, usb_ring)")
    a = ap.parse_args()
    if a.list or not a.action:
        for k, v in ACTIONS.items(): print(f"{k:12s} {v[3]}")
        sys.exit(0)
    if a.port == "sim":
        sys.exit(sim_main(a))
    if not a.port.startswith("/dev/serial/by-id/"): sys.exit("by-id ports only")
    if any(x.split(":")[0] == "fxpages" for x in a.action.split(",")):
        sys.exit("fxpages is simulator only (--port sim): it imports a preset, turns effects on and off and ends with every effect off, which would overwrite the card's saved sound (fx.txt)")
    KIT[0] = a.kit
    b = Board(a.port, a.echo)
    for spec in a.action.split(","):           # name[:cycles], comma separated
        name, _, n = spec.partition(":")
        a2 = argparse.Namespace(**vars(a)); a2.action = name
        if n: a2.cycles = int(n)
        run(a2, b)
