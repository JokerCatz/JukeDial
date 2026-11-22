#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""check_paths: every repo-relative path a tracked text file mentions must exist (stale doc / script / comment paths).

  check_paths.py [--root DIR] [--known tools/check/paths_known_missing.txt] [--list] [--selftest]

Scans tracked *.md, Makefile, *.sh, *.py, *.c/*.h/*.cpp, *.def, CMake files, and *.txt under tools/ and sim/. A mention is
a token that starts with docs/ tools/ app/ components/ sim/ (not glued to a longer path like /x/app/...). "Exists" means
tracked in git (a file, or a directory holding tracked files): the answer does not depend on which build outputs
or generated files one checkout happens to have. Skipped as sources (SKIP_SOURCES): captured logs, vendored trees, checker selftest fixtures. Skipped: .superpowers/, docs/archive/, spikes/, vendored and third_party dirs,
build dirs, and any token with a glob character or placeholder (* ? < > { } $ % [ ] ...).

The known-missing list is a ratchet (same idea as tools/ui_lint.allow): a NEW missing path fails, and so does a listed path
that now exists or is no longer mentioned anywhere ("remove it from the list"). Standard library only."""
import argparse
import os
import re
import subprocess
import sys
import tempfile

PREFIXES = ("docs/", "tools/", "app/", "components/", "sim/")
SKIP_DIRS = (".superpowers/", "docs/archive/", "spikes/", "hwtest/", "JC3636F118_knob_CN/", "source_TF/")
# files whose own text is not ours to fix (not scanned as sources; a path they are the TARGET of is still checked):
# captured logs, vendored upstream trees (tools/check/check_vendor.py TREES), and the checkers' own selftest fixtures (made-up paths)
SKIP_SOURCES = ("docs/logs/", "components/emu2413/", "components/gme/", "components/libadplug/", "components/libcrsid/", "components/libfreeaptx/",
                "components/libhvl/", "components/libldac/", "components/libmpcdec/", "components/libtta/", "components/libxmp/",
                "components/player_core/third_party/", "app/main/third_party/", "app/components/esp_tinyusb/", "tools/web/vendor/",
                "tools/fatfs_exfat/", "tools/check_boardport.py", "tools/check/check_fakepod_macro.py", "sim/tests/12_checkers_fail.sh",
                "tools/release/test_export.py", "tools/release/export.py")
# build inputs that exist in every working checkout but are generated, so never tracked
UNTRACKED_OK = {"app/sdkconfig"}
SKIP_PARTS = {"third_party", "vendor", "managed_components", "node_modules", "__pycache__", ".git", ".local", ".worktrees"}
TEXT_NAMES = {"Makefile"}
TEXT_EXT = {".md", ".sh", ".py", ".c", ".h", ".cpp", ".def", ".cmake"}
# a token: starts at a path prefix not preceded by a path/identifier character, runs over path characters and glob/placeholder marks
TOKEN_RE = re.compile(r"(?<![\w./~$@%{}<>*-])((?:docs|tools|app|components|sim)/[\w./+*?<>{}$%\[\]~=,-]*)")
BAD_CHARS = set("*?<>{}$%[]~=,")
SELF = ("tools/check/check_paths.py", "tools/check/paths_known_missing.txt")


def tracked(root):
    """Set of tracked file paths (repo relative, '/'), or None when git is unavailable."""
    try:
        out = subprocess.run(["git", "-C", root, "ls-files", "-z"], capture_output=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return {p for p in out.decode("utf-8", "replace").split("\0") if p}


def walk_files(root):
    res = set()
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if x not in SKIP_PARTS and not x.startswith("build")]
        for f in files:
            res.add(os.path.relpath(os.path.join(d, f), root).replace(os.sep, "/"))
    return res


def is_scanned(p):
    if p in SELF or p.startswith(SKIP_DIRS) or p.startswith(SKIP_SOURCES) or SKIP_PARTS & set(p.split("/")):
        return False
    if any(x.startswith("build") for x in p.split("/")[:-1]):
        return False
    base = p.rsplit("/", 1)[-1]
    ext = os.path.splitext(base)[1]
    if base in TEXT_NAMES or base == "CMakeLists.txt" or ext in TEXT_EXT:
        return True
    return ext == ".txt" and p.startswith(("tools/", "sim/"))


def clean(tok):
    """The path inside a token, or None when it is a glob / placeholder / not a plain path."""
    if "..." in tok:
        return None
    tok = tok.rstrip(".,;:)'\"`")
    if not tok or BAD_CHARS & set(tok) or "..." in tok or ".." in tok.split("/"):
        return None
    if tok.endswith("/"):
        tok = tok[:-1]
    if not tok or "//" in tok:
        return None
    parts = tok.split("/")
    if len(parts) < 2 or SKIP_PARTS & set(parts) or any(x.startswith("build") for x in parts):
        return None
    if tok.startswith(SKIP_DIRS) or (tok + "/").startswith(SKIP_DIRS):
        return None
    return tok


def mentions(text):
    """{path: first line number} for the paths a text mentions."""
    res = {}
    for n, line in enumerate(text.splitlines(), 1):
        for m in TOKEN_RE.finditer(line):
            p = clean(m.group(1))
            if p and p not in res:
                res[p] = n
    return res


def scan(root, files=None):
    """(missing {path: 'file:line'}, mentioned set) over the tracked scanned files."""
    files = files if files is not None else (tracked(root) or walk_files(root))
    dirs = set()
    for f in files:
        parts = f.split("/")
        for i in range(1, len(parts)):
            dirs.add("/".join(parts[:i]))
    missing, seen = {}, set()
    for f in sorted(files):
        if not is_scanned(f):
            continue
        try:
            with open(os.path.join(root, f), encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError:
            continue
        for p, n in mentions(text).items():
            seen.add(p)
            if p not in files and p not in dirs and p not in missing and p not in UNTRACKED_OK:
                missing[p] = "%s:%d" % (f, n)
    return missing, seen


LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")


def check_links(root, files, known=()):
    """Failure messages for relative Markdown links (images and pages) in docs/guide/**/*.md and README*.md that point at no tracked file.
    Guide images shared by all three languages live in docs/guide/img/ (tools/simtools/guide_share.py), so a language page links ../img/x.png.
    A target listed in the known-missing file is skipped (the path check owns it)."""
    errs = []
    for f in sorted(files):
        if not (f.endswith(".md") and (f.startswith("docs/guide/") or f in ("README.md", "README.zh-TW.md", "README.zh-CN.md"))):
            continue
        try:
            with open(os.path.join(root, f), encoding="utf-8", errors="replace") as fh:
                lines = fh.read().splitlines()
        except OSError:
            continue
        for n, line in enumerate(lines, 1):
            for m in LINK_RE.finditer(line):
                t = m.group(1).split("#")[0]
                if not t or re.match(r"[a-z][a-z0-9+.-]*:", t) or t.startswith("/"):
                    continue
                tgt = os.path.normpath(os.path.join(os.path.dirname(f), t)).replace(os.sep, "/")
                if tgt not in files and tgt not in known and not any(x.startswith(tgt + "/") for x in files):
                    errs.append("%s:%d: broken link %s" % (f, n, t))
    return errs


def read_known(path):
    if not os.path.exists(path):
        return set()
    with open(path, encoding="utf-8") as fh:
        return {l.strip() for l in fh if l.strip() and not l.lstrip().startswith("#")}


HEADER = """# Paths that tracked text files mention but that do not exist (tools/check/check_paths.py ratchet). Today's debt, not a licence:
# a NEW missing path fails the check; a path listed here that now exists or is no longer mentioned fails too (delete its line).
# Fix a stale mention by correcting the file that holds it, then remove the line. Regenerate: python3 tools/check/check_paths.py --list
"""


def verdict(missing, seen, known):
    """List of failure messages for a scan against the known list."""
    errs = []
    for p in sorted(set(missing) - known):
        errs.append("%s: missing path %s (fix the mention, or if it is meant to be absent list it in tools/check/paths_known_missing.txt)"
                    % (missing[p], p))
    for p in sorted(known - set(missing)):
        why = "now exists" if p in seen else "no longer mentioned anywhere"
        errs.append("tools/check/paths_known_missing.txt: %s %s (remove the line)" % (p, why))
    return errs


def run(root, known_path, list_only):
    missing, seen = scan(root)
    if list_only:
        sys.stdout.write(HEADER + "".join(p + "\n" for p in sorted(missing)))
        return 0
    known = read_known(known_path)
    errs = verdict(missing, seen, known)
    errs += check_links(root, tracked(root) or walk_files(root), known)
    if errs:
        print("check_paths: %d problem(s)" % len(errs))
        print("\n".join(errs))
        return 1
    print("check_paths ok (%d mentioned paths, %d known missing)" % (len(seen), len(missing)))
    return 0


def selftest():
    # token extraction
    t = ("see docs/features/base.md and `tools/x.py:12`, app/main/*.c, components/<name>/src, app/build/x.elf, "
         "/abs/app/main/y.c, sim/test.sh. Also docs/a/b/ and app/main/... and ../tools/z.py\n")
    got = set(mentions(t))
    assert got == {"docs/features/base.md", "tools/x.py", "sim/test.sh", "docs/a/b"}, got
    assert not mentions("docs/archive/old.md spikes/x tools/web/vendor/a.js")
    # a scratch repo: a missing path, a known one, a fixed one
    with tempfile.TemporaryDirectory() as d:
        files = {"docs/real.md", "tools/a.py", "Makefile", "docs/archive/x.md"}
        for p, s in (("docs/real.md", "x"),
                     ("tools/a.py", "# uses docs/real.md and tools/gone.py and docs/archive/x.md\n"),
                     ("Makefile", "t:\n\tpython3 tools/a.py app/nothere/f.c\n"),
                     ("docs/archive/x.md", "app/zzz/y.c")):
            os.makedirs(os.path.dirname(os.path.join(d, p)), exist_ok=True)
            with open(os.path.join(d, p), "w") as fh:
                fh.write(s)
        missing, seen = scan(d, files)
        assert set(missing) == {"tools/gone.py", "app/nothere/f.c"}, missing
        assert not verdict(missing, seen, {"tools/gone.py", "app/nothere/f.c"})
        e = verdict(missing, seen, {"tools/gone.py"})
        assert len(e) == 1 and "app/nothere/f.c" in e[0], e
        e = verdict(missing, seen, {"tools/gone.py", "app/nothere/f.c", "docs/real.md"})
        assert len(e) == 1 and "now exists" in e[0], e
        e = verdict(missing, seen, {"tools/gone.py", "app/nothere/f.c", "tools/other.py"})
        assert len(e) == 1 and "no longer mentioned" in e[0], e
    L = {"docs/guide/en/a.md", "docs/guide/img/s.png", "docs/guide/en/img/e.png"}
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, "docs/guide/en"))
        with open(os.path.join(d, "docs/guide/en/a.md"), "w") as fh:
            fh.write("![x](../img/s.png) ![y](img/e.png) ![z](img/s.png) [w](https://x.y/z.png) [v](#top)\n")
        e = check_links(d, L)
        assert len(e) == 1 and "img/s.png" in e[0] and e[0].startswith("docs/guide/en/a.md:1"), e
    print("check_paths selftest ok")
    return 0


def main():
    here = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=here)
    ap.add_argument("--known", default=None)
    ap.add_argument("--list", action="store_true", help="print the missing paths as a known-missing file")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    return run(a.root, a.known or os.path.join(a.root, "tools", "check", "paths_known_missing.txt"), a.list)


if __name__ == "__main__":
    sys.exit(main())
