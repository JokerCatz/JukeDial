#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Card path guard (make lint-app, sim-test): the card's paths are spelled in ONE place, components/player_core/include/pc/pc_paths.h.
No other firmware or simulator source may hold a string literal that starts at the card ("/sdcard", "/usbcard") or names a device-owned file
or folder (_player, .crashtest, .soak, .app_risky, library.bin, thumbs.pak). Comments are ignored. Allowed: the generic file protocol of the
console (a "/sdcard/" prefix test), the simulator's /sdcard remap and its self test, the BSP mount point, the sim's own ext sample file.
usage: check_card_paths.py [--selftest]"""
import os, re, sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROOTS = ("app/main", "components/player_core/src", "components/player_core/include", "sim")
SKIP_DIRS = {"build", "build-host", "build-host-asan", "managed_components", "fixture", "third_party", "host_test"}
ALLOW = {"components/player_core/include/pc/pc_paths.h", "app/main/console.c", "sim/sim_fs.c", "sim/selftest.c", "sim/fake_usb_mode.c", "sim/sim.h"}
LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
BAD = re.compile(r'^/(sdcard|usbcard)\b|_player|\.crashtest|\.soak\b|\.app_risky|library\.bin|thumbs\.pak')

def strip_comments(s):
    out, i, n, q = [], 0, len(s), None
    while i < n:
        c = s[i]
        if q:
            out.append(c)
            if c == "\\" and i + 1 < n: out.append(s[i + 1]); i += 1
            elif c == q: q = None
        elif c in "\"'": q = c; out.append(c)
        elif s.startswith("/*", i):
            j = s.find("*/", i + 2); j = n if j < 0 else j + 2
            out.append("\n" * s.count("\n", i, j)); i = j; continue
        elif s.startswith("//", i):
            j = s.find("\n", i); i = n if j < 0 else j; continue
        else: out.append(c)
        i += 1
    return "".join(out)

def findings(rel, src):
    res = []
    for no, line in enumerate(strip_comments(src).split("\n"), 1):
        if line.lstrip().startswith("#include"): continue
        for m in LIT.finditer(line):
            if BAD.search(m.group(1)): res.append(f"{rel}:{no}: card path literal {m.group(0)} (use pc/pc_paths.h)")
    return res

def selftest():
    assert findings("x.c", 'fopen("/sdcard/a", "r");')
    assert findings("x.c", 'x = "dir/_player";')
    assert not findings("x.c", '/* "/sdcard/a" */ f(PC_CFG_DIR "/a.txt"); // "/sdcard"')
    assert not findings("x.c", '#include "pc/pc_paths.h"\nputs("a\\"b");')
    print("check_card_paths selftest ok")

def main():
    if "--selftest" in sys.argv: return selftest()
    bad = []
    for r in ROOTS:
        for dp, dn, fn in os.walk(os.path.join(REPO, r)):
            dn[:] = [d for d in dn if d not in SKIP_DIRS]
            for f in fn:
                if not f.endswith((".c", ".h", ".cpp")): continue
                rel = os.path.relpath(os.path.join(dp, f), REPO)
                if rel in ALLOW or rel.endswith("i18n_strings.c") or rel.endswith("i18n_data.c") or rel.endswith("ui_help_texts.c"): continue
                bad += findings(rel, open(os.path.join(dp, f), encoding="utf-8", errors="replace").read())
    if bad: sys.exit("check_card_paths: " + str(len(bad)) + " finding(s)\n" + "\n".join(bad))
    print("check_card_paths ok")

if __name__ == "__main__":
    main()
