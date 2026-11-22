#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""check_cfg_keys: every settings key the firmware names must be in tools/check/cfg_keys.txt (a typo cannot quietly create a key).

  check_cfg_keys.py [--root DIR] [--keys tools/check/cfg_keys.txt] [--selftest]

Scans app/main/**/*.c|h|cpp (not third_party) for a string-literal first argument of the accessors declared in app/main/cfg.h
(cfg_has cfg_int cfg_str cfg_set_int cfg_set_str cfg_del). Rules:
  1. every literal key is in the list, and every listed key is still used (the list is sorted, one key per line, '#' comments);
  2. the number of listed keys is <= CFG_MAX (parsed from cfg.h): the table must be able to hold all of them;
  3. one key read (cfg_int / cfg_str, each by itself) with two different default texts is reported, unless it is listed after the
     line '# allowed-default-mismatch' (a ratchet: a listed key that no longer mismatches must be removed).
Keys the web tool sets go through ctl.c commands that call these accessors; the console's cfgset / seti / unset take a runtime name
(test use) and cannot be scanned. A call whose key is not a literal is not checked. Standard library only."""
import argparse
import os
import re
import sys
import tempfile

ACCESSORS = ("cfg_has", "cfg_int", "cfg_str", "cfg_set_int", "cfg_set_str", "cfg_del")
CALL_RE = re.compile(r"(?<![\w.>])(" + "|".join(ACCESSORS) + r")\s*\(")
MARK = "# allowed-default-mismatch"


def strip_comments(s):
    """Source without /* */ and // comments (string and char literals kept); newlines kept so line numbers stay."""
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c in "\"'":
            j = i + 1
            while j < n and s[j] != c:
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1]); i = j + 1
        elif s.startswith("/*", i):
            j = s.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", s[i:j])); i = j
        elif s.startswith("//", i):
            j = s.find("\n", i)
            i = n if j < 0 else j
        else:
            out.append(c); i += 1
    return "".join(out)


def split_args(s, i):
    """Arguments of the call whose '(' is at s[i-1]: list of texts up to the matching ')'."""
    depth, args, cur, n = 1, [], [], len(s)
    while i < n and depth:
        c = s[i]
        if c in "\"'":
            j = i + 1
            while j < n and s[j] != c:
                j += 2 if s[j] == "\\" else 1
            cur.append(s[i:j + 1]); i = j + 1; continue
        if c in "([{": depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0: break
        if c == "," and depth == 1:
            args.append("".join(cur)); cur = []
        else:
            cur.append(c)
        i += 1
    args.append("".join(cur))
    return [a.strip() for a in args]


def scan_text(text, path, uses, defaults):
    """uses: key -> [path:line]; defaults: (accessor, key) -> {default text: [path:line]}."""
    s = strip_comments(text)
    for m in CALL_RE.finditer(s):
        args = split_args(s, m.end())
        km = re.fullmatch(r'"([^"\\]*)"', args[0]) if args else None
        if not km:
            continue
        key, where = km.group(1), "%s:%d" % (path, s.count("\n", 0, m.start()) + 1)
        uses.setdefault(key, []).append(where)
        if m.group(1) in ("cfg_int", "cfg_str") and len(args) >= 2:
            d = re.sub(r"\s+", "", args[1])
            defaults.setdefault((m.group(1), key), {}).setdefault(d, []).append(where)


def scan_tree(root):
    uses, defaults = {}, {}
    base = os.path.join(root, "app", "main")
    for d, dirs, files in os.walk(base):
        dirs[:] = [x for x in dirs if x != "third_party"]
        for f in sorted(files):
            if f.endswith((".c", ".h", ".cpp")):
                p = os.path.join(d, f)
                with open(p, encoding="utf-8", errors="replace") as fh:
                    scan_text(fh.read(), os.path.relpath(p, root).replace(os.sep, "/"), uses, defaults)
    lab = os.path.join(root, "components", "player_core", "src", "pc_lab.c")   # the LAB run-time knobs (pc_lab.h): the table names the keys, lab.c reads them by name
    if os.path.exists(lab):
        with open(lab, encoding="utf-8", errors="replace") as fh:
            for i, ln in enumerate(fh, 1):
                for k in re.findall(r'\{\s*"(lab_[a-z0-9_]+)"', ln):
                    uses.setdefault(k, []).append("components/player_core/src/pc_lab.c:%d" % i)
    return uses, defaults


def read_list(path):
    keys, allowed, sec = [], [], False
    with open(path, encoding="utf-8") as fh:
        for ln in fh:
            ln = ln.strip()
            if ln == MARK:
                sec = True; continue
            if not ln or ln.startswith("#"):
                continue
            (allowed if sec else keys).append(ln)
    return keys, allowed


def cfg_max(root):
    with open(os.path.join(root, "app", "main", "cfg.h"), encoding="utf-8") as fh:
        m = re.search(r"#define\s+CFG_MAX\s+(\d+)", fh.read())
    return int(m.group(1)) if m else None


def mismatches(defaults):
    res = {}
    for (acc, key), ds in defaults.items():
        if len(ds) > 1:
            res.setdefault(key, []).append("%s %s" % (acc, " vs ".join("%s (%s)" % (d, v[0]) for d, v in sorted(ds.items()))))
    return res


def check(root, keys_path):
    uses, defaults = scan_tree(root)
    keys, allowed = read_list(keys_path)
    cmax, errs = cfg_max(root), []
    if keys != sorted(keys): errs.append("%s: the key list is not sorted" % keys_path)
    if len(set(keys)) != len(keys): errs.append("%s: a key is listed twice" % keys_path)
    for k in sorted(set(uses) - set(keys)):
        errs.append("unlisted key '%s' (%s): a typo, or add it to the list" % (k, uses[k][0]))
    for k in sorted(set(keys) - set(uses)):
        errs.append("listed key '%s' is not used any more: remove it from the list" % k)
    if cmax is None: errs.append("CFG_MAX not found in app/main/cfg.h")
    elif len(keys) > cmax: errs.append("%d listed keys > CFG_MAX %d: raise CFG_MAX" % (len(keys), cmax))
    mm = mismatches(defaults)
    for k in sorted(set(mm) - set(allowed)):
        errs.append("key '%s' read with different defaults: %s" % (k, "; ".join(mm[k])))
    for k in sorted(set(allowed) - set(mm)):
        errs.append("'%s' is listed as an allowed default mismatch but no longer mismatches: remove it" % k)
    return errs, len(keys), cmax, len(mm)


def selftest():
    uses, defaults = {}, {}
    scan_text('/* cfg_int("c", 1) */ int a = cfg_int("a", 1); x = cfg_int("a", 2); cfg_set_str("b", f(1, 2)); cfg_str(k, "z");\n'
              '// cfg_del("d")\ncfg_str("s", "x"); cfg_str("s", "x"); cfg_int("s", 0); s = "cfg_int(\\"q\\", 1)"; xcfg_int("w", 1);', "t.c", uses, defaults)
    assert set(uses) == {"a", "b", "s"}, uses
    assert set(mismatches(defaults)) == {"a"}, mismatches(defaults)
    with tempfile.TemporaryDirectory() as t:
        os.makedirs(os.path.join(t, "app", "main", "third_party"))
        with open(os.path.join(t, "app", "main", "cfg.h"), "w") as f: f.write("#define CFG_MAX 2\n")
        with open(os.path.join(t, "app", "main", "x.c"), "w") as f: f.write('void f(void){cfg_int("a",0);cfg_set_int("b",1);}')
        with open(os.path.join(t, "app", "main", "third_party", "y.c"), "w") as f: f.write('cfg_int("zz",0);')
        kp = os.path.join(t, "keys.txt")
        def run(body):
            with open(kp, "w") as f: f.write(body)
            return check(t, kp)[0]
        assert run("# c\na\nb\n") == []
        assert any("unlisted key 'b'" in e for e in run("a\n"))
        assert any("not used" in e for e in run("a\nb\nc\n"))
        assert any("not sorted" in e for e in run("b\na\n"))
        assert any("CFG_MAX" in e for e in run("a\nb\nc\n"))
        assert any("no longer mismatches" in e for e in run("a\nb\n" + MARK + "\na\n"))
    print("check_cfg_keys selftest ok")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    ap.add_argument("--keys", default=None)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    errs, nk, cmax, nm = check(a.root, a.keys or os.path.join(a.root, "tools", "check", "cfg_keys.txt"))
    if errs:
        print("check_cfg_keys: %d problem(s)" % len(errs))
        print("\n".join(errs))
        return 1
    print("check_cfg_keys ok (%d keys, CFG_MAX %s, %d allowed default mismatches)" % (nk, cmax, nm))
    return 0


if __name__ == "__main__":
    sys.exit(main())
