#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Guard (DACFS review I4): every FakePod translation unit that tests BSP_DAC_CS43131 / BSP_FAKEPOD_BOARD must get them on its compile line.

The macro used to live only in the FakePod's bsp_pins.h, so a file that did not include that header (inst_engine.c, scr_settings.c) silently built the round board's code and only the simulator
(SIM_FP) showed the difference. The FakePod BSP component now defines both PUBLIC (components/fakepod_bsp/CMakeLists.txt). This reads app/build-fakepod/compile_commands.json (when the
FakePod has been built; --require makes a missing file an error) and checks that each source of ours that mentions the macros is compiled with -DBSP_DAC_CS43131 (and -DBSP_FAKEPOD_BOARD).
  check_fakepod_macro.py [--require] [--db FILE]     --selftest: the checker on a tiny fake database"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MACROS = ("BSP_DAC_CS43131", "BSP_FAKEPOD_BOARD")
OURS = ("app/main/", "components/jc3636f118_bsp/", "components/fakepod_bsp/", "components/player_core/")


def check(entries, root):
    bad = []
    for e in entries:
        f = e["file"]
        rel = os.path.relpath(f, root) if os.path.isabs(f) else f
        if not rel.startswith(OURS) or not rel.endswith((".c", ".cpp")):
            continue
        try:
            text = open(os.path.join(root, rel), errors="replace").read()
        except OSError:
            continue
        cmd = e.get("command") or " ".join(e.get("arguments", []))
        for m in MACROS:
            if re.search(r"\b%s\b" % m, text) and not re.search(r"-D%s(=|\s|$)" % m, cmd):
                if rel.startswith("components/fakepod_bsp/") and m == "BSP_DAC_CS43131":
                    continue   # the component's own files include its bsp_pins.h
                bad.append((rel, m))
    return bad


def selftest():
    import tempfile
    d = tempfile.mkdtemp()
    os.makedirs(os.path.join(d, "app/main"))
    open(os.path.join(d, "app/main/a.c"), "w").write("#ifdef BSP_DAC_CS43131\n#endif\n")
    ok = [{"file": os.path.join(d, "app/main/a.c"), "command": "cc -DBSP_DAC_CS43131=1 -c a.c"}]
    no = [{"file": os.path.join(d, "app/main/a.c"), "command": "cc -c a.c"}]
    assert check(ok, d) == [] and check(no, d) == [("app/main/a.c", "BSP_DAC_CS43131")]
    print("check_fakepod_macro: selftest ok")


def main():
    a = sys.argv[1:]
    if "--selftest" in a:
        return selftest()
    db = os.path.join(ROOT, "app/build-fakepod/compile_commands.json")
    if "--db" in a:
        db = a[a.index("--db") + 1]
    if not os.path.exists(db):
        if "--require" in a:
            print("check_fakepod_macro: %s is missing" % db)
            sys.exit(1)
        print("check_fakepod_macro: no FakePod build here, skipped")
        return
    bad = check(json.load(open(db)), ROOT)
    for rel, m in bad:
        print("check_fakepod_macro: %s tests %s but its compile line has no -D%s" % (rel, m, m))
    if bad:
        sys.exit(1)
    print("check_fakepod_macro: ok")


main()
