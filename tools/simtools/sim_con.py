#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""SimCon: (mode="inst": sim --mode inst, the instrument app) the desktop simulator behind the same interface as ui_baseline.py's serial Con
(cmd, top, home, shot, drain), plus close() -> exit code. home() raises SimCrash when the root is not reached; strict=True also raises on
"APP cmd unknown", "APP uipush unknown", "APP ui leak" (a run whose pages are not the pages asked for must not pass), and (cfg_guard, on by default
with strict) records the settings (`simcfg`) before the first command and again in close(): `settings_changed` then lists every key the
run left different (board batch A F6: a shot or measure tool puts back what it moved; the caller fails on a non-empty list).

Starts `sim --headless --root <fresh copy of sim/fixture, or an empty directory>`, sends one command per line and reads the
reply up to the line "SIM ok". A wait of S seconds is a virtual-time "@S" (nothing really sleeps). If the simulator dies before
it answers (ASan, abort, ...), SimCrash says which command that was, its text, the exit code and the last stderr lines.
SIM_NOFONT=1 leaves the repo fonts/ out of the root (the no-CJK-font fallback). SIM_FONTS=<dir> links that directory (ui-tc.ttf ui-sc.ttf ui-ext.ttf, e.g.
.local/fontsets/out from make fonts-build) as <root>/_player/fonts instead of the repo fonts/."""
import os, re, select, shutil, subprocess, sys, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOP = re.compile(rb"APP uitop (\S+)")
OK = b"SIM ok\n"
# what a screenshot run must never see: the firmware's answers for an unknown command / unknown uipush page (app/main/console.c), a screen that leaves a
# timer or object behind (app/main/ui/ui.c), the simulator's own "no handler" line. Any of them means the shot that follows shows something else.
BAD = re.compile(rb"^(APP cmd unknown|APP uipush unknown|APP ui leak\b.*|SIM unknown\b.*)$", re.M)
CMD_TIMEOUT = 60.0          # real seconds per command (a loaded machine, e.g. gates of two worktrees at once, must not turn a slow answer into a failure)
CFG = re.compile(rb"^SIM cfg (?!end n=)([^=\n]+)=(.*)$", re.M)


class SimCrash(RuntimeError):
    pass


class SimCon:
    def __init__(self, binary=None, extra_args=(), src_root=None, times=None, strict=False, mode=None, cfg_guard=None):
        self.binary = binary or os.path.join(REPO, "sim", "build-fp" if os.environ.get("SIM_FP") == "1" else "build", "sim")   # SIM_FP=1: the square FakePod simulator (make sim-fp)
        self.work = tempfile.mkdtemp(prefix="sim_root_")
        fixture = src_root or os.path.join(REPO, "sim", "fixture")      # src_root: a data root to copy (.local/sim_root), symlinks kept
        self.root = os.path.join(self.work, "root")
        if os.path.isdir(fixture): shutil.copytree(fixture, self.root, symlinks=True)
        else: os.mkdir(self.root)
        fz = os.path.join(REPO, ".local", "faces.zip")                  # the HUD faces plug-in (make faces-zip; generated, never in git): /sdcard/_player/faces.zip
        if os.environ.get("SIM_NOFACES") != "1" and os.path.isfile(fz) and not os.path.exists(os.path.join(self.root, "_player", "faces.zip")):
            os.makedirs(os.path.join(self.root, "_player"), exist_ok=True)
            os.symlink(fz, os.path.join(self.root, "_player", "faces.zip"))
        fonts = os.path.abspath(os.environ.get("SIM_FONTS") or os.path.join(REPO, "fonts"))
        if os.environ.get("SIM_NOFONT") != "1" and os.path.isdir(fonts) and not os.path.exists(os.path.join(self.root, "_player", "fonts")):
            os.makedirs(os.path.join(self.root, "_player"), exist_ok=True)
            os.symlink(fonts, os.path.join(self.root, "_player", "fonts"))      # font.c reads /sdcard/_player/fonts/<name>; the simulator maps /sdcard to the root
        self.err = open(os.path.join(self.work, "stderr.txt"), "w+b")
        self.strict = strict                                          # raise SimCrash when a reply holds a BAD line (screenshot runs; leak_cycles counts them itself)
        self.times = times or {}                                      # page name -> UTC "YYYY-MM-DD HH:MM:SS[.mmm]" set right before its screenshot
        self.n = 0                                                    # commands sent so far
        self.last = ""
        self.rc = None
        self.cfg_guard = strict if cfg_guard is None else cfg_guard
        self.cfg0 = None                                              # the settings before the first command (cfg_guard)
        self.settings_changed = []                                    # "key: before -> after" lines, filled by close() (cfg_guard)
        self.p = subprocess.Popen([self.binary, "--headless", "--root", self.root, *(["--mode", mode] if mode else []), *extra_args], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=self.err, cwd=REPO)

    def _tail(self, lines=30):
        self.err.flush(); self.err.seek(0)
        return b"".join(self.err.read().splitlines(True)[-lines:]).decode(errors="replace")

    def _fail(self, why):
        if self.p.poll() is None:
            self.p.kill()
        rc = self.p.wait()
        raise SimCrash(f"simulator {why} at command #{self.n}: {self.last!r} (exit code {rc})\n--- stderr (last 30 lines) ---\n{self._tail()}")

    def _cfg(self):
        """the settings now ({key: value}); not counted as a command (crash reports name the caller's command numbers)"""
        n, last = self.n, self.last
        out = self._send("simcfg")
        self.n, self.last = n, last
        return {k.decode(errors="replace"): v.decode(errors="replace") for k, v in CFG.findall(out)}

    def _send(self, c):
        if self.cfg_guard and self.cfg0 is None and c != "simcfg":
            self.cfg0 = {}                                            # set first: _cfg() itself goes through here
            self.cfg0 = self._cfg()
        self.n += 1
        self.last = c
        try:
            self.p.stdin.write((c + "\n").encode()); self.p.stdin.flush()
        except BrokenPipeError:
            self._fail("died")
        out, end = b"", time.time() + CMD_TIMEOUT
        fd = self.p.stdout.fileno()
        while not out.endswith(OK):
            left = end - time.time()
            if left <= 0: self._fail(f"timed out after {CMD_TIMEOUT:.0f} s")
            if not select.select([fd], [], [], left)[0]: continue
            b = os.read(fd, 65536)
            if not b: self._fail("died")
            out += b
        out = out[:-len(OK)]
        if self.strict:
            m = BAD.search(out)
            if m: self._fail(f"answered {m.group(0).decode(errors='replace')!r}")
        return out

    def drain(self, secs):
        return self._send(f"@{secs}") if secs > 0 else b""

    def cmd(self, c, wait=1.0):
        out = self._send(c)
        return out + self.drain(wait)

    def top(self):
        m = TOP.search(self._send("uitop"))
        return m.group(1).decode() if m else "?"

    def home(self, root):
        for _ in range(10):
            if self.top() == root: return
            self._send("back"); self.drain(0.8)
        self._fail(f"could not get back to {root} (at {self.top()}) after 10 x back")

    def page_start(self, name):
        """Before a page is opened: set the clock for it (a page that draws once at creation, like the moon, sees the same instant as the board's)."""
        if name in self.times: self._send(f"simtime {self.times[name]}")

    def shot(self, path):
        path = os.path.abspath(path)
        t = self.times.get(os.path.basename(path)[:-4])
        if t: self._send(f"simtime {t}")
        out = self._send(f"shot {path}")
        if b"SIM shot " + path.encode() not in out or not os.path.getsize(path):
            raise SimCrash(f"no screenshot from command #{self.n}: {self.last!r}: {out.decode(errors='replace')}")

    def close(self):
        """Ends the simulator (stdin EOF, the same exit as every other path); returns its exit code."""
        if self.rc is None:
            if self.cfg_guard and self.cfg0 is not None and self.p.poll() is None:
                try:
                    now = self._cfg()
                    self.settings_changed = [f"{k}: {self.cfg0.get(k, '(none)')} -> {now.get(k, '(none)')}"
                                             for k in sorted(set(self.cfg0) | set(now)) if self.cfg0.get(k) != now.get(k)]
                except SimCrash:
                    pass                                              # the exit code tells
            try: self.p.stdin.close()
            except OSError: pass
            try: self.rc = self.p.wait(timeout=CMD_TIMEOUT)
            except subprocess.TimeoutExpired:
                self.p.kill(); self.rc = self.p.wait()
            self.stderr = self._tail(10 ** 6)
            self.err.close()
            shutil.rmtree(self.work, ignore_errors=True)
        return self.rc

    def sanitizer_output(self):
        """The stderr lines of AddressSanitizer / LeakSanitizer / UBSan (after close())."""
        return [l for l in self.stderr.splitlines() if "Sanitizer" in l or "runtime error:" in l]
