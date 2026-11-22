#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Keeps every script on one way to open the board's port (tools/boardport.py). A hand-made open skips the drain of the stale
log fragment (the internal notes, Task 6b). Over the whole repo (not the paths build, managed_components, .worktrees, .local, .git; not
the two files tools/boardport.py and tools/check_boardport.py):
  .py            fails on  Serial(  (any spelling of the class, also "from serial import Serial" and a space before the paren),
                 serial_for_url, "from serial import", "import serial as", and any "import serial" line (pyserial is only
                 imported by boardport; a tool needing SerialException takes boardport.SerialException)
  .sh .js .mjs .html   fails on the literal call text. Web Serial (navigator.serial) is not that and stays allowed.
A comment may not spell the call either (it is a text scan): describe it in other words.
  check_boardport.py [--root DIR] [--selftest]"""
import os
import re
import sys

SKIP = {"build", "managed_components", ".worktrees", ".local", ".git"}     # whole path components
OWN = {os.path.join("tools", "boardport.py"), os.path.join("tools", "check_boardport.py")}   # exact paths (this file spells the patterns)
PY = [re.compile(p) for p in (r"\bSerial\s*\(", r"serial_for_url", r"\bfrom\s+serial\b", r"\bimport\s+serial\s+as\b",
                              r"^\s*import\s+(?:[\w.]+\s*,\s*)*serial\b(?!\s*\.)")]
OTHER = re.compile(r"serial\.Serial\(")


def scan(root):
    """[(path, line number)] of every hand-made open under root."""
    bad = []
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if x not in SKIP]
        for f in sorted(files):
            ext = os.path.splitext(f)[1]
            if ext not in (".py", ".sh", ".js", ".mjs", ".html"):
                continue
            p = os.path.join(d, f)
            if os.path.relpath(p, root) in OWN:
                continue
            pats = PY if ext == ".py" else [OTHER]
            for n, ln in enumerate(open(p, encoding="utf-8", errors="replace"), 1):
                if any(r.search(ln) for r in pats):
                    bad.append((p, n))
    return bad


def message(p, n):
    return "%s:%d: opens a serial port by hand (or imports pyserial): open the port with boardport.open_port" % (p, n)


def selftest():
    import tempfile

    def tree(files):
        t = tempfile.mkdtemp()
        for name, text in files.items():
            path = os.path.join(t, name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "w").write(text)
        return t

    def hits(files):
        t = tree(files)
        return sorted(os.path.relpath(p, t) for p, _ in scan(t))

    bad = []
    CALL = "s = serial.Serial(p, 115200)\n"
    cases = [   # name, files, expected files reported
        ("clean tree: boardport itself may open, a tool using it passes",
         {"tools/boardport.py": "import serial\n" + CALL, "tools/good.py": "from boardport import open_port\ns = open_port(p)\n"}, []),
        ("hand-made open in a tool", {"tools/bad.py": CALL}, ["tools/bad.py"]),
        ("alias import", {"tools/a.py": "import serial as sp\n"}, ["tools/a.py"]),
        ("alias use", {"tools/a.py": "s = sp.Serial(p)\n"}, ["tools/a.py"]),
        ("from serial import Serial", {"tools/a.py": "from serial import Serial\n"}, ["tools/a.py"]),
        ("space before the parenthesis", {"tools/a.py": "s = serial.Serial (p)\n"}, ["tools/a.py"]),
        ("serial_for_url", {"tools/a.py": "s = serial.serial_for_url(u)\n"}, ["tools/a.py"]),
        ("a bare import serial", {"tools/a.py": "    import serial\n"}, ["tools/a.py"]),
        ("a file outside tools/", {"sim/x.py": CALL}, ["sim/x.py"]),
        ("tools/build_x is not skipped", {"tools/build_x/a.py": CALL}, ["tools/build_x/a.py"]),
        ("a boardport.py in a subdirectory is not exempt", {"tools/sub/boardport.py": CALL}, ["tools/sub/boardport.py"]),
        ("the checker itself (it spells the patterns)", {"tools/check_boardport.py": CALL}, []),
        ("a skipped path component", {"app/build/a.py": CALL, "x/.worktrees/b/a.py": CALL, "managed_components/m/a.py": CALL}, []),
        ("shell and web files: the literal call", {"tools/a.sh": "python -c 'serial.Serial(p)'\n", "tools/w.mjs": CALL}, ["tools/a.sh", "tools/w.mjs"]),
        ("Web Serial stays allowed", {"tools/web/index.html": "navigator.serial.requestPort({})\n"}, []),
        ("SerialException by itself is fine", {"tools/a.py": "from boardport import SerialException\nraise SerialException()\n"}, []),
    ]
    for name, files, want in cases:
        got = hits(files)
        if got != want:
            bad.append("%s: got %s, wanted %s" % (name, got, want))
    t = tree({"tools/bad.py": "x\n" + CALL})
    (p, n), = scan(t)
    if n != 2 or "bad.py" not in message(p, n) or "open the port with boardport.open_port" not in message(p, n):
        bad.append("message or line number")
    for b in bad:
        print("check_boardport selftest FAIL:", b)
    if bad:
        return 1
    print("check_boardport selftest: ok (%d cases)" % (len(cases) + 1))
    return 0


def main():
    a = sys.argv[1:]
    if a == ["--selftest"]:
        return selftest()
    root = a[a.index("--root") + 1] if "--root" in a else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    root = os.path.normpath(root)
    bad = scan(root)
    for p, n in bad:
        print(message(p, n))
    print("check_boardport: %s" % ("FAILED (%d)" % len(bad) if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
