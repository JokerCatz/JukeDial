#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The last crash line of the board's /sdcard/_player/logs/crash.txt with its pc / ra turned into function and line.
Usage: crash_decode.py PORT [--elf app/build/x.elf]   (make crash: the IDF environment provides riscv32-esp-elf-addr2line)
       crash_decode.py --input FILE [--elf ...]       (a saved console capture instead of the board)
Sends the console command `crashlog`, prints what comes back, takes pc=0x... ra=0x... of the last crash line that has them
and says so when that is not the newest crash (the newest one had no pc: a watchdog reset, or no wrap)."""
import argparse, glob, os, re, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PC = re.compile(r"\bpc=(0x[0-9a-f]+) ra=(0x[0-9a-f]+)")

def addr2line():
    t = shutil.which("riscv32-esp-elf-addr2line")
    if t: return t
    c = sorted(glob.glob(os.path.expanduser("~/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-addr2line")))
    return c[-1] if c else None

def read_port(port, secs):
    from boardport import open_port
    s = open_port(port, unopened=True)         # DTR/RTS left as they are: opening never resets the board
    s.write(b"crashlog\n")
    lines, buf, end = [], b"", time.time() + secs
    while time.time() < end:
        buf += s.read(4096)
        *got, buf = buf.split(b"\n")
        for ln in got:
            t = ln.decode("utf-8", "replace").rstrip("\r")
            if t.startswith("APP crashlog"): print(t); lines.append(t)
        if any(t.startswith("APP crashlog end") for t in lines): break
    s.close()
    return lines

def read_file(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return [ln.rstrip("\r\n") for ln in f]

def is_crash(t):                               # "APP crashlog <line>" (console) or "APP crash <line>" (boot)
    return (t.startswith("APP crashlog ") and not t.startswith("APP crashlog end")) or (t.startswith("APP crash ") and not t.startswith("APP crash log "))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?"); ap.add_argument("--input"); ap.add_argument("--elf"); ap.add_argument("--secs", type=float, default=4)
    a = ap.parse_args()
    if not a.input and not (a.port or "").startswith("/dev/serial/by-id/"): sys.exit("STOP: PORT must be a /dev/serial/by-id path")
    elfs = [a.elf] if a.elf else sorted(glob.glob(os.path.join(ROOT, "app", "build", "*.elf")))
    if not elfs or not os.path.exists(elfs[0]): sys.exit("no app/build/*.elf: run make build first")
    crashes = [t for t in (read_file(a.input) if a.input else read_port(a.port, a.secs)) if is_crash(t)]
    hit = [i for i, t in enumerate(crashes) if PC.search(t)]
    if not hit: sys.exit("no crash line with pc=0x... (wrap off, or no crash logged)")
    line = crashes[hit[-1]]
    print(f"decoding: {line}")
    if hit[-1] != len(crashes) - 1:
        print("WARNING: the newest crash has no pc (watchdog reset, or no wrap); decoding an older crash")
    m = PC.search(line)
    tool = addr2line()
    if not tool: sys.exit("riscv32-esp-elf-addr2line not found (make crash sources the IDF environment)")
    print(f"decode {os.path.relpath(elfs[0], ROOT)} pc={m.group(1)} ra={m.group(2)}")
    print("(the ELF must be the build that crashed: fw= in the line names its version)")
    r = subprocess.run([tool, "-pfiaC", "-e", elfs[0], m.group(1), m.group(2)], capture_output=True, text=True)
    sys.stdout.write(r.stdout); sys.stderr.write(r.stderr)
    sys.exit(r.returncode)

if __name__ == "__main__":
    main()
