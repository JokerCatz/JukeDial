#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Timer races (spec 2026-11-11-ui-kit-diagnostics 3.2, review focus 1 and 2): leave a page, or rebuild every page (theme
next), while one of its timers is running or just about to fire.

usage: sim_timer_races.py [--port sim|BY-ID-PATH] [--reps 10] [--only a,b,...]

  (a) tools dice, tools roll, theme next within 0.3 s           (dice animation during a rebuild)
  (b) fortune wheel, fortune draw, theme next, back at once     (wheel animation, rebuild, leave)
  (c) tools lunch / ideas, tools spin, back                     (reel animation; the ideas card one-shot, 450 ms after the reels stop)
  (d) vocab start, vocab right, back (or theme next)            (next-question one-shot, 900 ms)
  (e) noiseui, sceneui 0, a tap on the first layer row -> its ring, back / theme   (the ring's 4 s one-shot; simnoise and simtap: simulator only)
  (f) now playing menu / album menu open, back / theme next     (4 s menu timeout one-shot)
Each case runs --reps times at every delay in its list; the delays sit around the one-shot's due time (just before, at,
just after). In the simulator time is virtual (@d), so a delay is exact and a use-after-free is reported by ASan.
Checks: the page on top afterwards (uitop), no "APP ui leak" / unknown command (SimCon strict), simulator exit code 0
and no sanitizer output; on the board no panic / reset / ui leak in the output. Ends on the main menu, theme monokai.
A rebuild case (theme next) must really hit its timer: in the simulator `simtimers` right before the theme change has
the timer (by period: 33 = an animation frame, next to LVGL's own 33 ms timers; 450x1 / 900x1 / 1500x1 / 4000x1 = the
one-shots) and right after it there are fewer of that period; otherwise the case fails as "missed the window". (The red test that a timer surviving a rebuild
is reported is in sim/test.sh.)
On the board the delays are real seconds (approximate), every byte read is checked, and the (d) cases run only when the
open vocabulary deck is the built-in sample (answers write the deck's progress; the sample's is reset afterwards).
Exit code 0 = all passed."""
import argparse, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

RING_TAP = "simtap 180 98"         # the scene page's first layer row ("simnoise 4 4": four layers) -> that layer's ring (UI unify T6; the 1.5 s hint it replaced is gone)
BAD_BOARD = re.compile(rb"APP ui leak|Guru Meditation|abort\(\)|panic|rst:0x|APP cmd unknown|Backtrace", re.M)
LV = re.compile(rb"lv_bytes=(\d+)")
PERIODS = re.compile(rb"^SIM timers n=\d+ forever=\d+ periods=(\S*)$", re.M)

# name: (setup, [(cmd, wait)...] with "D" = the delay under test, expected top afterwards, delays, sim only)
CASES = {
    "a_dice_theme": ([], [("tools dice", 0.5), ("tools roll", "D"), ("theme next", 0.6)], "tools_dice", [0.0, 0.1, 0.2, 0.3], False),
    "b_wheel_theme_back": ([], [("fortune wheel", 0.5), ("fortune draw", "D"), ("theme next", 0), ("back", 0.5)], "fortune",
                           [0.0, 0.1, 0.5], False),
    "c_lunch_back": ([], [("tools lunch", 0.5), ("tools spin", "D"), ("back", 0.5)], "tools", [0.0, 0.1, 1.0], False),
    "c_ideas_card": ([], [("tools ideas", 0.5), ("tools spin", "D"), ("back", 0.5)], "tools", [2.0, 2.5, 2.8, 3.0, 3.2, 3.25, 3.3, 3.5],
                     False),   # the reels stop at ~2.8 s, the card is due ~3.25 s
    "c_ideas_theme": ([], [("tools ideas", 0.5), ("tools spin", "D"), ("theme next", 0.6)], "tools_list", [2.5, 3.0], False),
    "d_vocab_back": ([], [("vocab start", 0.8), ("vocab right", "D"), ("back", 0.5)], "vocab", [0.0, 0.2, 0.85, 0.9, 0.95], False),
    "d_vocab_theme": ([], [("vocab start", 0.8), ("vocab right", "D"), ("theme next", 0.6)], "vocab_quiz", [0.2, 0.85], False),
    "e_ring_back": (["simnoise 4 4"], [("noiseui", 0.3), ("sceneui 0", 0.3), (RING_TAP, "D"), ("back", 0.5)], "noise", [0.5, 3.9, 4.0, 4.1], True),
    "e_ring_theme": (["simnoise 4 4"], [("noiseui", 0.3), ("sceneui 0", 0.3), (RING_TAP, "D"), ("theme next", 0.6)], "scene", [0.5, 3.9], True),
    "f_now_menu_back": (["vol 0", ("track 1", 3)], [("nowmenu -2", "D"), ("back", 0.5)], "menu", [0.5, 3.9, 4.0, 4.1], False),
    "f_now_menu_theme": (["vol 0", ("track 1", 3)], [("nowmenu -2", "D"), ("theme next", 0.6)], "now", [0.5, 3.9], False),
    "f_album_menu_back": ([("album", 1.0)], [("albummenu -2", "D"), ("back", 0.5)], "menu", [0.5, 3.9, 4.0, 4.1], False),
}
# rebuild cases: the timer (simtimers period key) that must be running right before "theme next", by delay
NEED = {
    "a_dice_theme": lambda d: "33",
    "b_wheel_theme_back": lambda d: "33",
    "c_ideas_theme": lambda d: "33" if d < 2.8 else "450x1",
    "d_vocab_theme": lambda d: "900x1",
    "e_ring_theme": lambda d: "4000x1",
    "f_now_menu_theme": lambda d: "4000x1",
}


def periods(c):
    m = PERIODS.search(c.cmd("simtimers", 0))
    return [x.rstrip("p") for x in m.group(1).decode().split(",")] if m else []   # paused or not: the display refresh pauses its own 33


def run(c, board, reps, only):
    fails, n, hits = [], 0, 0
    builtin = True
    if board:
        builtin = b"APP vocab stat deck=builtin:vocab " in c.cmd("vocab", 1.0) + c.cmd("vocab save", 1.0)
        c.home("menu")
    for name, (setup, steps, want, delays, sim_only) in CASES.items():
        if only and not any(name.startswith(o) for o in only): continue
        if sim_only and board:
            print(f"SKIP {name}: needs simtap (no tap command on the board)", flush=True)
            continue
        if name.startswith("d_") and not builtin:
            print(f"SKIP {name}: the open vocabulary deck is not the built-in sample (answers would change its progress)", flush=True)
            continue
        lv = []
        for r in range(reps):
            for d in delays:
                c.cmd("screen on", 0)               # the idle timeout must not stop the animations (screen off: no animation)
                for s in setup:
                    cmd, w = (s, 0.3) if isinstance(s, str) else s
                    c.cmd(cmd, w)
                for cmd, w in steps:
                    w = d if w == "D" else w
                    if cmd == "theme next" and name in NEED and not board:
                        key = NEED[name](d)
                        before = periods(c).count(key)
                        c.cmd(cmd, 0)
                        after = periods(c).count(key)
                        if after < before: hits += 1
                        else: fails.append(f"{name} rep {r} d={d}: missed the window (timer {key}: {before} before, {after} after the rebuild)")
                        c.drain(w)
                    else:
                        c.cmd(cmd, w)
                top = c.top()
                n += 1
                if top != want: fails.append(f"{name} rep {r} d={d}: top={top}, want {want}")
                if name.startswith("f_now"): c.cmd("stop", 0.3)
                c.home("menu")
                m = LV.search(c.cmd("mem", 0.3 if board else 0))
                if m: lv.append(int(m.group(1)))
                if board:
                    bad = BAD_BOARD.search(c.seen.replace(b"\r", b""))
                    if bad: fails.append(f"{name} rep {r} d={d}: {bad.group(0).decode()}")
                    c.seen = b""
        print(f"{name}: {reps} x {delays} ok={not any(f.startswith(name) for f in fails)} lv_bytes first/last {lv[:1]}/{lv[-1:]}", flush=True)
    if board and builtin and (not only or any(o.startswith("d") for o in only)):
        c.cmd("vocab", 1.0); c.cmd("vocab reset", 1.0); c.home("menu")      # the sample deck's progress is test data
    for _ in range(30):                              # back to JC's theme (21 themes)
        if b"theme=monokai" in c.cmd("theme next", 0.6): break
    else: fails.append("theme: monokai not reached")
    c.cmd("screen off", 0.3)
    if not board: print(f"rebuilds that hit their running timer: {hits}", flush=True)
    return fails, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="sim", help="sim, or the main board's by-id path")
    ap.add_argument("--reps", type=int, default=10)
    ap.add_argument("--only", help="comma separated case name prefixes (a, b, c_ideas, ...)")
    a = ap.parse_args()
    only = a.only.split(",") if a.only else None
    board = a.port != "sim"
    if board:
        if not a.port.startswith("/dev/serial/by-id/"): sys.exit("by-id ports only")
        from ui_baseline import Con

        class SeenCon(Con):                          # keeps every byte read, so lines that arrive late are checked too
            seen = b""
            def drain(self, secs):
                out = super().drain(secs); self.seen += out; return out
        c = SeenCon(a.port)
    else:
        from sim_con import SimCon
        c = SimCon(strict=True)
    t0 = time.time()
    try:
        fails, n = run(c, board, a.reps, only)
    except Exception as e:                           # SimCrash: died, ASan, "APP ui leak" (strict)
        if not board: c.close(); print(getattr(c, "stderr", "")[-3000:])
        print(f"FAILED: {e}"); sys.exit(1)
    if board:
        c.s.close()
        san, rc = [], 0
    else:
        rc = c.close()
        san = c.sanitizer_output()
    print(f"RESULT runs={n} fails={len(fails)} exit={rc} sanitizer_lines={len(san)} real_s={time.time() - t0:.1f}")
    for f in fails + san: print("  " + f)
    sys.exit(0 if not fails and not san and rc == 0 else 1)


if __name__ == "__main__":
    main()
