#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""check_vendor: vendored (external) code must stay comparable with upstream (the project rules).

  check_vendor.py [--root DIR] [--manifest tools/check/vendor_hashes.txt] [--update] [--provenance] [--selftest]

tools/check/vendor_hashes.txt holds one line per tracked file of the vendored trees: "<sha256>  <path>". The check fails and names every
file that changed, appeared or disappeared against it. A necessary fix must be small and recorded in that library's patches/ (or UPSTREAM.txt /
SOURCE.md); then run --update, which rewrites the manifest. Files that are plainly ours inside a vendored tree (port/ patches/ folders, the
tree-root CMakeLists.txt, SOURCE*, UPSTREAM*, *.lf, *.cmake) are marked "(our glue)" in the output; everything else is treated as upstream.
--provenance lists the trees (per library inside a multi-library tree) that have no note saying where they came from; a note counts at the tree root or one or two levels down. Standard library only."""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile

TREES = ["components/emu2413", "components/gme", "components/libadplug", "components/libcrsid", "components/libfreeaptx",
         "components/libhvl", "components/libldac", "components/libmpcdec", "components/libtta", "components/libxmp",
         "components/player_core/third_party", "app/main/third_party", "app/components/esp_tinyusb", "tools/web/vendor",
         "tools/fatfs_exfat"]
OUR_DIRS = {"port", "patches"}
NOTE_PREFIX = ("SOURCE", "UPSTREAM", "README", "PROVENANCE", "VENDOR")


def tracked(root):
    out = subprocess.run(["git", "-C", root, "ls-files", "-z", "--"] + TREES, capture_output=True, check=True).stdout
    return sorted(p for p in out.decode("utf-8", "surrogateescape").split("\0") if p)


def tree_of(path):
    return next((t for t in TREES if path.startswith(t + "/")), "")


def is_ours(path):
    parts = path.split("/")
    base = parts[-1]
    if OUR_DIRS & set(parts[:-1]):
        return True
    if tree_of(path) and len(parts) == len(tree_of(path).split("/")) + 1:  # directly in the tree root
        return base == "CMakeLists.txt" or base.startswith(("SOURCE", "UPSTREAM")) or base.endswith((".lf", ".cmake"))
    return False


def digest(root, path):
    h = hashlib.sha256()
    with open(os.path.join(root, path), "rb") as fh:
        for blk in iter(lambda: fh.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()


def current(root):
    return {p: digest(root, p) for p in tracked(root)}


def read_manifest(path):
    res = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8", errors="surrogateescape") as fh:
            for l in fh:
                l = l.rstrip("\n")
                if l and not l.startswith("#"):
                    h, _, p = l.partition("  ")
                    res[p] = h
    return res


HEADER = ("# sha256 of every tracked file in the vendored trees (tools/check/check_vendor.py). Vendored code is not tidied: a change here must be\n"
          "# small and recorded in that library's patches/ (or UPSTREAM.txt / SOURCE.md); then `python3 tools/check/check_vendor.py --update`.\n")


def write_manifest(path, cur):
    with open(path, "w", encoding="utf-8", errors="surrogateescape") as fh:
        fh.write(HEADER)
        for p in sorted(cur):
            fh.write("%s  %s\n" % (cur[p], p))


def diff(cur, man):
    """list of problem lines."""
    errs = []
    for p in sorted(set(cur) | set(man)):
        tag = " (our glue)" if is_ours(p) else ""
        if p not in man:
            errs.append("added:   %s%s" % (p, tag))
        elif p not in cur:
            errs.append("removed: %s%s" % (p, tag))
        elif cur[p] != man[p]:
            errs.append("changed: %s%s" % (p, tag))
    return errs


def provenance(root):
    """trees (or, for a tree of several libraries, the libraries) with no note file or patches/ folder saying where they came from.
    A note counts at the tree root, or one or two levels down inside a library folder (lib/SOURCE.md, lib/<version>/VENDOR.md)."""
    files = tracked(root)
    res = []

    def noted(names):  # names: paths relative to a folder; a note or patches/ in the folder itself
        return any(n.split("/")[0].startswith(NOTE_PREFIX) or n.split("/")[0] == "patches" for n in names)

    for t in TREES:
        names = [p[len(t) + 1:] for p in files if p.startswith(t + "/")]
        if noted(names):
            continue
        subs = sorted({n.split("/")[0] for n in names if "/" in n})
        bad = []
        for s in subs:
            inner = [n[len(s) + 1:] for n in names if n.startswith(s + "/")]
            subs2 = sorted({n.split("/")[0] for n in inner if "/" in n})
            if noted(inner) or any(noted([n[len(s2) + 1:] for n in inner if n.startswith(s2 + "/")]) for s2 in subs2):
                continue
            bad.append(t + "/" + s)
        if not subs:
            res.append(t)
        else:
            res.extend(bad)
    return res


def run(root, manifest, update, prov):
    if prov:
        for t in provenance(root):
            print("no provenance note: " + t)
        return 0
    cur = current(root)
    if update:
        write_manifest(manifest, cur)
        print("check_vendor: manifest rewritten (%d files). Record the change in that library's patches/ or UPSTREAM.txt/SOURCE.md." % len(cur))
        return 0
    errs = diff(cur, read_manifest(manifest))
    if errs:
        print("check_vendor: %d vendored file(s) differ from tools/check/vendor_hashes.txt" % len(errs))
        print("\n".join(errs))
        print("Vendored code stays comparable with upstream: undo the edit, or keep it small, record it in the library's patches/ (or UPSTREAM.txt),"
              " then run python3 tools/check/check_vendor.py --update")
        return 1
    print("check_vendor ok (%d files in %d trees)" % (len(cur), len(TREES)))
    return 0


def selftest():
    c = "components/"
    assert is_ours(c + "libxmp/port/a.c") and is_ours(c + "libtta/patches/x.patch")
    assert is_ours(c + "libxmp/CMakeLists.txt") and not is_ours(c + "libxmp/libxmp/CMakeLists.txt")
    assert not is_ours(c + "emu2413/emu2413.c") and is_ours(c + "emu2413/SOURCE.md")
    cur = {"a": "1", "b": "2", c + "libtta/port/c": "3"}
    assert not diff(cur, dict(cur))
    e = diff({"a": "1", "n": "9"}, {"a": "x", "b": "2"})
    assert e == ["changed: a", "removed: b", "added:   n"], e
    with tempfile.TemporaryDirectory() as d:
        m = os.path.join(d, "m.txt")
        write_manifest(m, cur)
        assert read_manifest(m) == cur
    print("check_vendor selftest ok")
    return 0


def main():
    here = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=here)
    ap.add_argument("--manifest", default=None)
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--provenance", action="store_true", help="list the trees without a where-from note")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    return run(a.root, a.manifest or os.path.join(a.root, "tools", "check", "vendor_hashes.txt"), a.update, a.provenance)


if __name__ == "__main__":
    sys.exit(main())
