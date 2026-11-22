#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""refactor_snapshot: a safety net for behaviour-preserving refactors (file moves, file splits, Makefile clean-ups).

  refactor_snapshot.py snap --out DIR [--root DIR]
  refactor_snapshot.py diff BASE NEW [--allow console|cfg|make|sources]... [--max-data-delta BYTES] [--require-fresh] [-v]
  refactor_snapshot.py --selftest

snap records, from what is ALREADY built (it never builds anything):
  * per firmware ELF (the Makefile's variant build dirs: app/build, build-ldac, build-peer, build-fakepod; build-trace only when it was built: an on-demand diagnostic) and
    per simulator binary (sim/build, sim/build-fp): section sizes and the defined symbols (name, type, size), read with the
    toolchain nm/size the build used (project_description.json c_compiler; PATH, then host nm, as fallbacks);
  * inventories of the source tree: console commands, settings keys with defaults, `make -n` of every safe target,
    and the source files each firmware build and each simulator build compiles (compile_commands.json; the simulator's is exported by
    sim/CMakeLists.txt, so a simulator built before that change has none until it is re-configured).
A binary is STALE when a file ITS OWN build uses is newer: the sources, the headers in its -I dirs and the CMakeLists.txt above them from its
compile_commands.json (+ every tracked sim/ file for a simulator), not any firmware source; no compile_commands.json = the old every-source rule.
diff compares two snapshots. Strict: the multiset of code symbols (nm types t T w W) by (name, size) per binary, and the four
inventories (exact; --allow lets one through). Informational: data / rodata / bss by symbol with totals (file-path strings from
asserts change when files move); --max-data-delta turns a total change above BYTES into a failure.
Symbol names have numeric suffix segments removed (name.constprop.0, s_x.1234 -> name.constprop, s_x) because the compiler's
counters shift when code moves between files; same-named statics in different files are compared as multisets.
Exit status 1 on any strict failure. Standard library only."""
import argparse
import collections
import difflib
import glob
import json
import os
import re
import subprocess
import sys
import tempfile

SCHEMA = 1
# on-demand diagnostic builds (make build-trace: heap tracing for leak hunts): not built is normal, listed as "optional, not built", not as a fault
OPTIONAL = {"elf-build-trace"}
CODE_TYPES = set("tTwW")
DATA_TYPES = set("dDvV")
RODATA_TYPES = set("rR")
BSS_TYPES = set("bBsScC")
SRC_EXT = {".c", ".cc", ".cpp", ".h", ".hpp", ".S", ".def", ".cmake", ".csv"}
PLACEHOLDER = "<ROOT>"


# ----------------------------------------------------------------------------- helpers

def run(cmd, cwd=None, env=None, check=True):
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, errors="replace")
    if check and p.returncode != 0:
        raise RuntimeError("%s failed (%d): %s" % (" ".join(cmd[:3]), p.returncode, p.stderr.strip()[:300]))
    return p


def main_checkout(root):
    """The main checkout's root (a worktree shares its .git directory), or root itself."""
    p = run(["git", "-C", root, "rev-parse", "--path-format=absolute", "--git-common-dir"], check=False)
    if p.returncode == 0 and p.stdout.strip():
        return os.path.dirname(p.stdout.strip())
    return root


def variants(root):
    """Firmware build dirs and simulator build dirs named by the Makefile (app/build is the default build)."""
    mk = open(os.path.join(root, "Makefile"), encoding="utf-8", errors="replace").read()
    fw = ["build"] + sorted(set(re.findall(r"-B (build[\w-]*)", mk)))
    sim = sorted(set(re.findall(r"\bsim/(build[\w-]*)", mk)))
    return fw, sim


def tool_for(build_dir, name):
    """nm / size of the toolchain a firmware build used; falls back to PATH, then the host tool."""
    pd = os.path.join(build_dir, "project_description.json")
    cands = []
    try:
        cc = json.load(open(pd)).get("c_compiler", "")
        if cc:
            d, b = os.path.split(cc)
            cands.append(os.path.join(d, b[:b.rfind("gcc")] + name if "gcc" in b else name))
    except (OSError, ValueError):
        pass
    try:
        cc = json.load(open(os.path.join(build_dir, "compile_commands.json")))[0]["command"].split()[0]
        d, b = os.path.split(cc)
        if "gcc" in b:
            cands.append(os.path.join(d, b[:b.rfind("gcc")] + name))
    except (OSError, ValueError, IndexError, KeyError):
        pass
    for c in cands:
        if os.path.isfile(c):
            return c
    for pre in ("riscv32-esp-elf-", ""):
        for d in os.environ.get("PATH", "").split(os.pathsep):
            c = os.path.join(d, pre + name)
            if os.path.isfile(c):
                return c
    return name


SUFFIX_RE = re.compile(r"\.\d+(?=\.|$)")


def norm_name(n):
    return SUFFIX_RE.sub("", n)


def classify(t):
    if t in CODE_TYPES:
        return "code"
    if t in DATA_TYPES:
        return "data"
    if t in RODATA_TYPES:
        return "rodata"
    if t in BSS_TYPES:
        return "bss"
    return None


def read_binary(path, nm, size):
    """{'sections': {name: size}, 'symbols': [[name, type, size], ...]} for one binary."""
    out = run([nm, "-S", "-P", "--defined-only", path]).stdout
    syms = []
    for line in out.splitlines():
        f = line.split()
        if len(f) < 4:          # no size field (labels, absolute symbols): not a sized object
            continue
        name, typ, _addr, sz = f[0], f[1], f[2], f[3]
        if classify(typ) is None:
            continue
        try:
            syms.append([norm_name(name), typ, int(sz, 16)])
        except ValueError:
            continue
    syms.sort()
    secs = {}
    for line in run([size, "-A", path]).stdout.splitlines():
        f = line.split()
        if len(f) == 3 and f[0].startswith(".") and f[1].isdigit():
            secs[f[0]] = secs.get(f[0], 0) + int(f[1])
    return {"sections": secs, "symbols": syms}


# ----------------------------------------------------------------------------- C scanning

def strip_comments(src):
    """The source with comments blanked (newlines kept) and string / char literals left intact."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


STR_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def split_args(text):
    """Split the text after '(' of a call into top-level arguments; returns (args, end index)."""
    args, depth, cur, i, n = [], 0, [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            cur.append(text[i:j + 1])
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                args.append("".join(cur).strip())
                return args, i
            depth -= 1
        elif c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    return args, n


def source_files(root, dirs):
    res = []
    for d in dirs:
        for base, subs, files in os.walk(os.path.join(root, d)):
            subs[:] = [s for s in subs if not s.startswith("build") and s not in ("host_test", "managed_components", "third_party")]
            for f in files:
                if os.path.splitext(f)[1] in (".c", ".cpp"):
                    res.append(os.path.join(base, f))
    return sorted(res)


def function_spans(code):
    """[(name, body_text)] for the top-level function definitions of comment-stripped C."""
    spans, depth, start, i, n = [], 0, 0, 0, len(code)
    head_start = 0
    while i < n:
        c = code[i]
        if c in "\"'":
            j = i + 1
            while j < n and code[j] != c and code[j] != "\n":
                j += 2 if code[j] == "\\" else 1
            i = j + 1
            continue
        if c == "{":
            if depth == 0:
                start = i
                m = re.search(r"(\w+)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*$", code[head_start:i])
                name = m.group(1) if m else None
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                if name:
                    spans.append((name, code[start:i + 1]))
                head_start = i + 1
        elif c == ";" and depth == 0:
            head_start = i + 1
        i += 1
    return spans


CMP_RE = re.compile(r"\b(strcmp|strncmp|strcasecmp|strncasecmp|sscanf|memcmp)\s*\(")
CMD_FUNC_RE = re.compile(r"(_cmd|_cmds|_console|_command)$|^console_|^handle_cmd")


def literals_of_calls(code):
    """String literals inside strcmp / strncmp / sscanf calls of comment-stripped code."""
    lits = []
    for m in CMP_RE.finditer(code):
        args, _ = split_args(code[m.end():])
        for a in args:
            lits.extend(STR_RE.findall(a))
    return lits


def console_inventory(root):
    """Multiset of the string literals the console dispatch compares / scans: all of console*.c, plus the bodies of every
    command handler function (name ends _cmd / _console ...) in other files. No file or function names, so moving code
    between files does not change it."""
    cnt = collections.Counter()
    files = source_files(root, ["app/main", "components"])
    for f in files:
        code = strip_comments(open(f, encoding="utf-8", errors="replace").read())
        if os.path.basename(f).startswith("console"):
            cnt.update(literals_of_calls(code))
            continue
        if not CMP_RE.search(code):
            continue
        for name, body in function_spans(code):
            if CMD_FUNC_RE.search(name):
                cnt.update(literals_of_calls(body))
    return cnt


CFG_RE = re.compile(r"\b(cfg_int|cfg_str|cfg_set_int|cfg_set_str|cfg_has|cfg_del)\s*\(")
CFG_FNS = {"cfg_int": 2, "cfg_str": 2}     # functions whose 2nd argument is a default to record


def norm_ws(s):
    return re.sub(r"\s+", " ", s)


def cfg_inventory(root):
    """Multiset of 'function key=<arg> [default=<arg>]' over every settings call site (no file names)."""
    cnt = collections.Counter()
    for f in source_files(root, ["app/main", "components"]):
        code = strip_comments(open(f, encoding="utf-8", errors="replace").read())
        for m in CFG_RE.finditer(code):
            args, _ = split_args(code[m.end():])
            if not args or not args[0] or "const char" in args[0]:       # a declaration / definition, not a call
                continue
            fn = m.group(1)
            line = "%s key=%s" % (fn, norm_ws(args[0]))
            if fn in CFG_FNS and len(args) >= 2:
                line += " default=%s" % norm_ws(args[1])
            cnt[line] += 1
    return cnt


def fmt_counter(cnt):
    return "".join("%d\t%s\n" % (n, k) for k, n in sorted(cnt.items()))


# ----------------------------------------------------------------------------- make -n inventory

def make_targets(root):
    names = []
    for line in open(os.path.join(root, "Makefile"), encoding="utf-8", errors="replace"):
        m = re.match(r"^([A-Za-z0-9_][A-Za-z0-9_.-]*(?: [A-Za-z0-9_][A-Za-z0-9_.-]*)*)\s*:(?![=:])", line)
        if m:
            names.extend(m.group(1).split())
    seen, res = set(), []
    for n in names:
        if n not in seen:
            seen.add(n)
            res.append(n)
    return sorted(res)


SELF_TARGETS = {"refactor-base", "refactor-diff"}        # they run this tool: no point recording their own dry run
SUBMAKE_RE = re.compile(r"make\[\d+\]: (Entering|Leaving) directory")


def make_inventory(root):
    """(text, excluded {target: reason}). The dry run runs with -n; a recipe line that contains $(MAKE) is still executed by
    make (as a sub-make that inherits -n), so targets that descend into a generated build tree (their dry run depends on
    the state of that tree, not on the Makefile) are left out."""
    mk = open(os.path.join(root, "Makefile"), encoding="utf-8", errors="replace").read()
    if re.search(r"^\s*\+", mk, re.M):
        plus = "a recipe has a + prefix (runs under -n)"
    else:
        plus = None
    main = main_checkout(root)
    # a snap started from `make refactor-*` inherits MAKELEVEL / MAKEFLAGS: clear them, or every dry run prints "Entering directory"
    env = {k: v for k, v in os.environ.items() if k not in ("MAKELEVEL", "MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES")}
    env["LC_ALL"] = "C"
    excluded, blocks = {}, []
    for t in make_targets(root):
        if t in SELF_TARGETS:
            excluded[t] = "runs this tool (self-referential)"
            continue
        p = run(["make", "-n", "-f", "Makefile", t], cwd=root, env=env, check=False)
        text = p.stdout
        if SUBMAKE_RE.search(text):
            excluded[t] = "recurses into a generated build tree (make -C ...); the dry run depends on that tree's state"
            continue
        if plus and re.search(r"^\+", text, re.M):
            excluded[t] = plus
            continue
        text = text.replace(root, PLACEHOLDER)
        if main != root:
            text = text.replace(main, PLACEHOLDER)
        text = text.replace(os.path.expanduser("~"), "<HOME>")
        err = [l for l in p.stderr.replace(root, PLACEHOLDER).splitlines() if l.strip()]
        blocks.append("### %s (rc=%d)\n%s%s" % (t, p.returncode, text if text.endswith("\n") or not text else text + "\n",
                                                "".join("stderr: %s\n" % l for l in err)))
    return "".join(blocks), excluded


# ----------------------------------------------------------------------------- source list

def sources_inventory(root, build_dir):
    cc = os.path.join(build_dir, "compile_commands.json")
    try:
        data = json.load(open(cc))
    except (OSError, ValueError):
        return None
    idf = None
    try:
        idf = json.load(open(os.path.join(build_dir, "project_description.json"))).get("idf_path")
    except (OSError, ValueError):
        pass
    main = main_checkout(root)
    res = set()
    for e in data:
        f = e.get("file", "")
        if not os.path.isabs(f):
            f = os.path.normpath(os.path.join(e.get("directory", ""), f))
        f = os.path.realpath(f)                       # a build configured through a symlinked path (~/self -> ~/self_remote) lists the other spelling
        for pre, ph in ((os.path.realpath(root), ""), (os.path.realpath(main), "")):
            if f.startswith(pre + os.sep):
                f = f[len(pre) + 1:]
                break
        else:
            if idf and f.startswith(os.path.realpath(idf) + os.sep):
                f = "<IDF>/" + f[len(os.path.realpath(idf)) + 1:]
        res.add(f)
    return sorted(res)


# ----------------------------------------------------------------------------- freshness

def newest_source(root):
    """(mtime, path) of the newest tracked source file a build depends on."""
    p = run(["git", "-C", root, "ls-files", "-z", "app", "components", "sim"], check=False)
    files = [f for f in p.stdout.split("\0") if f] if p.returncode == 0 else []
    best = (0.0, "")
    for f in files:
        base = os.path.basename(f)
        ext = os.path.splitext(base)[1]
        if "/host_test/" in f or "/third_party/" in f:
            continue
        if not (ext in SRC_EXT or base == "CMakeLists.txt" or base.startswith("sdkconfig")):
            continue
        try:
            m = os.stat(os.path.join(root, f)).st_mtime
        except OSError:
            continue
        if m > best[0]:
            best = (m, f)
    return best


HDR_EXT = {".h", ".hpp", ".def", ".cmake", ".csv"}


def compile_entries(build_dir):
    """The compile_commands.json entries of a build dir, or None (not built / not exported)."""
    try:
        return json.load(open(os.path.join(build_dir, "compile_commands.json")))
    except (OSError, ValueError):
        return None


def include_dirs(entry):
    """Absolute -I / -isystem directories of one compile command."""
    import shlex
    args = entry.get("arguments") or shlex.split(entry.get("command", ""))
    res, i = [], 0
    while i < len(args):
        a = args[i]
        for flag in ("-I", "-isystem"):
            if a == flag and i + 1 < len(args):
                i += 1
                res.append(args[i])
            elif a.startswith(flag) and a != flag:
                res.append(a[len(flag):])
        i += 1
    return [os.path.normpath(d if os.path.isabs(d) else os.path.join(entry.get("directory", ""), d)) for d in res]


def binary_newest(root, build_dir, extra_prefix=()):
    """(mtime, path, how): the newest file THIS binary's own build depends on: the sources of its compile_commands.json, the headers /
    .def / .cmake files in its include directories, the CMakeLists.txt above each source, and (a simulator) every tracked file under
    extra_prefix. how is "compile_commands" or "all-sources" (no compile_commands.json: the old rule, every firmware source)."""
    entries = compile_entries(build_dir)
    if entries is None:
        m, f = newest_source(root)
        return m, f, "all-sources"
    p = run(["git", "-C", root, "ls-files", "-z"], check=False)
    tracked = [f for f in p.stdout.split("\0") if f] if p.returncode == 0 else []
    inc, srcs, cdirs = set(), set(), set()
    for e in entries:
        f = e.get("file", "")
        f = os.path.realpath(f if os.path.isabs(f) else os.path.join(e.get("directory", ""), f))   # realpath: see sources_inventory
        srcs.add(f)
        d = os.path.dirname(f)
        while d.startswith(root + os.sep):
            cdirs.add(d)
            d = os.path.dirname(d)
        inc.update(os.path.realpath(i) for i in include_dirs(e))
    cand = set(srcs)
    for f in tracked:
        a = os.path.join(root, f)
        base = os.path.basename(f)
        dn = os.path.dirname(a)
        if (dn in inc and os.path.splitext(base)[1] in HDR_EXT) or (dn in cdirs and base == "CMakeLists.txt") \
                or (extra_prefix and f.startswith(extra_prefix)) or (not extra_prefix and base.startswith("sdkconfig") and f.startswith("app/")):
            cand.add(a)
    best = (0.0, "")
    for a in cand:
        try:
            m = os.stat(a).st_mtime
        except OSError:
            continue
        if m > best[0]:
            best = (m, os.path.relpath(a, root) if a.startswith(root + os.sep) else a)
    return best[0], best[1], "compile_commands"


# ----------------------------------------------------------------------------- snap

def snap(root, out):
    root = os.path.realpath(root)
    os.makedirs(out, exist_ok=True)
    fw, sims = variants(root)
    newest_m, newest_f = newest_source(root)
    meta = {"schema": SCHEMA, "newest_source": newest_f, "newest_source_mtime": newest_m, "binaries": {}, "missing": [], "optional": [],
            "make_excluded": {}}
    for v in fw:
        bd = os.path.join(root, "app", v)
        elfs = [e for e in glob.glob(os.path.join(bd, "*.elf")) if not os.path.basename(e).startswith("bootloader")]
        pn = None
        try:
            pn = json.load(open(os.path.join(bd, "project_description.json"))).get("project_name")
        except (OSError, ValueError):
            pass
        elf = os.path.join(bd, (pn or "jc_player") + ".elf")
        label = "elf-" + v
        if not os.path.isfile(elf):
            meta["optional" if label in OPTIONAL else "missing"].append(label)
            continue
        nm, size = tool_for(bd, "nm"), tool_for(bd, "size")
        d = read_binary(elf, nm, size)
        mt = os.stat(elf).st_mtime
        bm, bf, how = binary_newest(root, bd)
        d["tool"] = nm
        json.dump(d, open(os.path.join(out, label + ".json"), "w"), separators=(",", ":"))
        meta["binaries"][label] = {"path": os.path.relpath(elf, root), "mtime": mt, "stale": mt < bm, "newest_source": bf, "freshness": how,
                                   "symbols": len(d["symbols"]), "tool": nm}
        src = sources_inventory(root, bd)
        if src is not None:
            open(os.path.join(out, "sources-%s.txt" % v), "w").write("\n".join(src) + "\n")
            meta["binaries"][label]["sources"] = len(src)
    for v in sims:
        b = os.path.join(root, "sim", v, "sim")
        label = "sim-" + v
        if not os.path.isfile(b):
            meta["missing"].append(label)
            continue
        d = read_binary(b, "nm", "size")
        d["tool"] = "nm"
        mt = os.stat(b).st_mtime
        bm, bf, how = binary_newest(root, os.path.join(root, "sim", v), ("sim/",))
        json.dump(d, open(os.path.join(out, label + ".json"), "w"), separators=(",", ":"))
        meta["binaries"][label] = {"path": os.path.relpath(b, root), "mtime": mt, "stale": mt < bm, "newest_source": bf, "freshness": how,
                                   "symbols": len(d["symbols"]), "tool": "nm"}
        src = sources_inventory(root, os.path.join(root, "sim", v))
        if src is not None:
            open(os.path.join(out, "sources-%s.txt" % label), "w").write("\n".join(src) + "\n")
            meta["binaries"][label]["sources"] = len(src)
    con, cfg = console_inventory(root), cfg_inventory(root)
    open(os.path.join(out, "inv-console.txt"), "w").write(fmt_counter(con))
    open(os.path.join(out, "inv-cfg.txt"), "w").write(fmt_counter(cfg))
    text, excl = make_inventory(root)
    open(os.path.join(out, "inv-make.txt"), "w").write(text)
    meta["make_excluded"] = excl
    meta["counts"] = {"console_literals": len(con), "cfg_sites": len(cfg), "make_targets": text.count("### "),
                      "make_excluded": len(excl)}
    json.dump(meta, open(os.path.join(out, "meta.json"), "w"), indent=1, sort_keys=True)
    print("snapshot %s: %d binaries (%s), %d missing%s" % (out, len(meta["binaries"]), ", ".join(sorted(meta["binaries"])),
                                                           len(meta["missing"]), (": " + ", ".join(meta["missing"])) if meta["missing"] else ""))
    if meta["optional"]:
        print("optional, not built: %s (on-demand diagnostic build; not a fault)" % ", ".join(meta["optional"]))
    for k, b in sorted(meta["binaries"].items()):
        if b["stale"]:
            print("STALE %s (older than %s, a file its own build uses%s)" % (k, b["newest_source"],
                  "" if b["freshness"] == "compile_commands" else "; no compile_commands.json, so every source counts"))
    print("console literals %d, cfg call-site shapes %d, make targets %d (excluded %d)" %
          (len(con), len(cfg), meta["counts"]["make_targets"], len(excl)))
    for t, why in sorted(excl.items()):
        print("  make -n excluded: %s: %s" % (t, why))
    return 0


# ----------------------------------------------------------------------------- diff

def multiset(syms, types):
    c = collections.Counter()
    for n, t, s in syms:
        if classify(t) in types:
            c[(n, s)] += 1
    return c


def code_diff(a, b):
    """(lines, ok) comparing two code-symbol multisets of (name, size)."""
    ca, cb = multiset(a, {"code"}), multiset(b, {"code"})
    gone, new = ca - cb, cb - ca
    if not gone and not new:
        return [], True
    out = []
    gn, nn = collections.defaultdict(list), collections.defaultdict(list)
    for (n, s), k in gone.items():
        gn[n] += [s] * k
    for (n, s), k in new.items():
        nn[n] += [s] * k
    for n in sorted(set(gn) | set(nn)):
        o, w = sorted(gn.get(n, [])), sorted(nn.get(n, []))
        if o and w:
            out.append("resized  %s: %s -> %s" % (n, o, w))
        elif o:
            out.append("removed  %s %s" % (n, o))
        else:
            out.append("added    %s %s" % (n, w))
    return out, False


def class_totals(syms):
    tot = collections.Counter()
    for n, t, s in syms:
        c = classify(t)
        if c:
            tot[c] += s
    return tot


def data_diff(a, b, max_delta):
    """(summary lines, failures) for data / rodata / bss, symbol multisets plus totals."""
    lines, fails = [], []
    ta, tb = class_totals(a), class_totals(b)
    for cls, types in (("data", {"data"}), ("rodata", {"rodata"}), ("bss", {"bss"})):
        ca, cb = multiset(a, types), multiset(b, types)
        add, rem = sum((cb - ca).values()), sum((ca - cb).values())
        delta = tb[cls] - ta[cls]
        lines.append("%s %d -> %d (%+d), %d symbols added, %d removed" % (cls, ta[cls], tb[cls], delta, add, rem))
        if max_delta is not None and abs(delta) > max_delta:
            fails.append("%s total changed by %+d bytes (limit %d)" % (cls, delta, max_delta))
    return lines, fails


def unified(a_text, b_text, name):
    return "".join(difflib.unified_diff(a_text.splitlines(True), b_text.splitlines(True), "base/" + name, "new/" + name, n=1))


def read(path):
    try:
        return open(path, encoding="utf-8").read()
    except OSError:
        return None


def diff(base, new, allow, max_delta, require_fresh, verbose, out=print):
    fails, info = [], []
    mb, mn = json.load(open(os.path.join(base, "meta.json"))), json.load(open(os.path.join(new, "meta.json")))
    for label in sorted(mb["binaries"]):
        if label not in mn["binaries"]:
            fails.append("%s: present in the base but missing now (build it first)" % label)
            continue
        if require_fresh and mn["binaries"][label]["stale"]:
            fails.append("%s: older than %s (stale: rebuild before comparing)" % (label, mn["binaries"][label].get("newest_source", mn["newest_source"])))
        da = json.load(open(os.path.join(base, label + ".json")))
        db = json.load(open(os.path.join(new, label + ".json")))
        lines, ok = code_diff(da["symbols"], db["symbols"])
        ncode = sum(multiset(da["symbols"], {"code"}).values())
        dl, df = data_diff(da["symbols"], db["symbols"], max_delta)
        tail = "; ".join(x.split(" (")[0].replace(" -> ", ">") + x[x.index("("):x.index(")") + 1] for x in dl)
        if ok:
            out("%-18s code ok (%d fns); %s" % (label, ncode, tail))
        else:
            fails.append("%s: code symbols differ (%d)" % (label, len(lines)))
            out("%-18s CODE DIFFERS; %s" % (label, tail))
            for l in lines[:None if verbose else 30]:
                out("    " + l)
            if not verbose and len(lines) > 30:
                out("    ... %d more (-v)" % (len(lines) - 30))
        if verbose:
            for l in dl:
                out("    info: " + l)
        fails.extend("%s: %s" % (label, f) for f in df)
        sa, sb = da["sections"], db["sections"]
        ch = ["%s %+d" % (s, sb.get(s, 0) - sa.get(s, 0)) for s in sorted(set(sa) | set(sb)) if sa.get(s, 0) != sb.get(s, 0)]
        if ch and verbose:
            out("    info: sections " + ", ".join(ch))
    for label in sorted(set(mn["binaries"]) - set(mb["binaries"])):
        out("%-18s new in this snapshot (not in the base, not compared)" % label)
    invs = [("console", "inv-console.txt"), ("cfg", "inv-cfg.txt"), ("make", "inv-make.txt")]
    invs += [("sources", os.path.basename(f)) for f in sorted(glob.glob(os.path.join(base, "sources-*.txt")) +
                                                              glob.glob(os.path.join(new, "sources-*.txt")))]
    done = set()
    for kind, fn in invs:
        if fn in done:
            continue
        done.add(fn)
        a, b = read(os.path.join(base, fn)), read(os.path.join(new, fn))
        if a == b:
            out("%-18s same" % fn)
            continue
        d = unified(a or "", b or "", fn)
        if kind in allow:
            out("%-18s differs, allowed (--allow %s)" % (fn, kind))
        else:
            fails.append("%s differs" % fn)
            out("%-18s DIFFERS" % fn)
        lines = d.splitlines()
        for l in lines[:None if verbose else 40]:
            out("    " + l.rstrip("\n"))
        if not verbose and len(lines) > 40:
            out("    ... %d more lines (-v)" % (len(lines) - 40))
    out("")
    out("RESULT: %s%s" % ("PASS" if not fails else "FAIL", "" if not fails else " (%d)" % len(fails)))
    for f in fails:
        out("  - " + f)
    return 1 if fails else 0


# ----------------------------------------------------------------------------- selftest

def selftest():
    # C scanning
    src = ('/* strcmp(c, "ghost") */ void x_cmd(const char *c) {\n if (!strcmp(c, "alpha")) a();\n'
           ' else if (sscanf(c, "beta %d", &n) == 1) b(); // strncmp(c,"no",2)\n else if (!strncmp(c, "gam", 3)) c(); }\n'
           'static int other(const char *s) { return !strcmp(s, "plain"); }\n')
    code = strip_comments(src)
    names = [n for n, _ in function_spans(code)]
    assert names == ["x_cmd", "other"], names
    assert literals_of_calls(code) == ["alpha", "beta %d", "gam", "plain"], literals_of_calls(code)
    assert [l for n, b in function_spans(code) if CMD_FUNC_RE.search(n) for l in literals_of_calls(b)] == ["alpha", "beta %d", "gam"]
    # cfg
    cs = strip_comments('int v = cfg_int("vol", 40); cfg_set_int("vol", cfg_int("x", (1+2))); cfg_str(k, ""); int cfg_int(const char *key, int def);')
    got = []
    for m in CFG_RE.finditer(cs):
        args, _ = split_args(cs[m.end():])
        got.append((m.group(1), args))
    assert got[0] == ("cfg_int", ['"vol"', "40"]) and got[2] == ("cfg_int", ['"x"', "(1+2)"]), got
    # names
    assert norm_name("s_x.1234") == "s_x" and norm_name("f.constprop.0") == "f.constprop" and norm_name("a.b") == "a.b"
    # diff logic
    A = [["f", "T", 10], ["f", "t", 20], ["f", "t", 20], ["g", "T", 5], ["tbl", "r", 100], ["v", "b", 8]]
    assert code_diff(A, list(A))[1]
    B = [["f", "T", 10], ["f", "t", 20], ["f", "t", 24], ["h", "t", 5], ["tbl", "r", 160], ["v", "b", 8]]
    lines, ok = code_diff(A, B)
    assert not ok and any(l.startswith("resized  f") for l in lines) and any("removed  g" in l for l in lines) \
        and any("added    h" in l for l in lines), lines
    _, fl = data_diff(A, B, 50)
    assert len(fl) == 1 and "rodata" in fl[0]
    assert not data_diff(A, B, None)[1]
    # a whole snapshot pair in a scratch dir
    with tempfile.TemporaryDirectory() as d:
        def mk(name, syms, con, stale=False):
            p = os.path.join(d, name)
            os.makedirs(p)
            json.dump({"sections": {".text": 10}, "symbols": syms}, open(os.path.join(p, "elf-build.json"), "w"))
            json.dump({"binaries": {"elf-build": {"stale": stale}}, "newest_source": "x.c"}, open(os.path.join(p, "meta.json"), "w"))
            for fn, t in (("inv-console.txt", con), ("inv-cfg.txt", "1\tcfg_int key=\"a\" default=1\n"), ("inv-make.txt", "### t\n")):
                open(os.path.join(p, fn), "w").write(t)
            return p
        sink = []
        p1, p2 = mk("a", A, "1\talpha\n1\tbeta\n"), mk("b", A, "1\talpha\n")
        assert diff(p1, p1, set(), None, True, False, sink.append) == 0
        assert diff(p1, p2, set(), None, False, False, sink.append) == 1 and any("-1\tbeta" in s for s in sink)
        assert diff(p1, p2, {"console"}, None, False, False, sink.append) == 0
        p3 = mk("c", B, "1\talpha\n1\tbeta\n")
        assert diff(p1, p3, set(), None, False, False, sink.append) == 1
        p4 = mk("e", A, "1\talpha\n1\tbeta\n", stale=True)
        assert diff(p1, p4, set(), None, True, False, sink.append) == 1
        assert diff(p1, p4, set(), None, False, False, sink.append) == 0
    # per-binary freshness: a file the binary does not compile must not make it stale
    with tempfile.TemporaryDirectory() as d:
        run(["git", "init", "-q", d])
        am, sm = "app/main/", "sim/"   # joined so check_paths does not take them for repo paths
        for f in (am + "a.c", am + "a.h", am + "only_fw.c", sm + "s.c"):
            os.makedirs(os.path.dirname(os.path.join(d, f)), exist_ok=True)
            open(os.path.join(d, f), "w").write("x")
            os.utime(os.path.join(d, f), (1000, 1000))
        run(["git", "-C", d, "add", "-A"])
        bd = os.path.join(d, "sim", "build")
        os.makedirs(bd)
        json.dump([{"directory": bd, "file": os.path.join(d, am + "a.c"), "command": "cc -I%s -c a.c" % os.path.join(d, am)},
                   {"directory": bd, "file": "../s.c", "arguments": ["cc", "-I", os.path.join(d, "sim")]}], open(os.path.join(bd, "compile_commands.json"), "w"))
        os.utime(os.path.join(d, am + "only_fw.c"), (5000, 5000))          # not compiled by this binary
        m, f, how = binary_newest(d, bd, ("sim/",))
        assert m == 1000 and how == "compile_commands", (m, f, how)
        os.utime(os.path.join(d, am + "a.h"), (3000, 3000))                # a header in an -I dir counts
        assert binary_newest(d, bd, ("sim/",))[:2] == (3000, am + "a.h")
        os.utime(os.path.join(d, sm + "s.c"), (4000, 4000))                     # sim/** counts
        assert binary_newest(d, bd, ("sim/",))[:2] == (4000, sm + "s.c")
        assert binary_newest(d, os.path.join(d, "nobuild"), ("sim/",))[2] == "all-sources" and binary_newest(d, os.path.join(d, "nobuild"))[0] == 5000
        assert sources_inventory(d, bd) == [am + "a.c", sm + "s.c"], sources_inventory(d, bd)
    # a real tiny binary, when a host compiler exists
    cc = None
    for c in ("cc", "gcc"):
        if run(["which", c], check=False).returncode == 0:
            cc = c
            break
    if cc:
        with tempfile.TemporaryDirectory() as d:
            c_ = os.path.join(d, "t.c")
            open(c_, "w").write("static int s(int x){return x*3;} static int tab[4]={1,2,3,4}; int main(int a,char**v){return s(a)+tab[a&3];}\n")
            exe = os.path.join(d, "t")
            run([cc, "-O0", "-o", exe, c_])
            r = read_binary(exe, "nm", "size")
            names = {(n, classify(t)) for n, t, s in r["symbols"]}
            assert ("main", "code") in names and ("s", "code") in names and ("tab", "data") in names and r["sections"].get(".text"), names
    print("refactor_snapshot selftest ok")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    s = sub.add_parser("snap")
    s.add_argument("--out", required=True)
    s.add_argument("--root", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    d = sub.add_parser("diff")
    d.add_argument("base")
    d.add_argument("new")
    d.add_argument("--allow", action="append", default=[], choices=["console", "cfg", "make", "sources"])
    d.add_argument("--max-data-delta", type=int, default=None, metavar="BYTES")
    d.add_argument("--require-fresh", action="store_true", help="fail when a binary in NEW is older than the newest source")
    d.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.cmd == "snap":
        return snap(a.root, a.out)
    if a.cmd == "diff":
        return diff(a.base, a.new, set(a.allow), a.max_data_delta, a.require_fresh, a.verbose)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
