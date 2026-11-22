#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Guard (zipguard): a pc_zip_open() entry array is never indexed with a number that came from outside.

Board run 4: console `chip zzr4 10 0` indexed a 10-entry zip at 10 and crashed. Code takes entries through
pc_zip_at(ents, n, idx) (NULL when out of range). A direct `ents[...]` index is allowed only on a line that carries
the marker `zip-index-ok` (a loop bounded by the entry count)."""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PAT = re.compile(r"\b(?:ents|s_ents|zip_ents)\s*\[")
SKIP = ("host_test", "build", "managed_components")
bad = []
for d in ("app/main", "components/player_core/src"):
    for f in (ROOT / d).rglob("*.c"):
        if any(s in f.parts for s in SKIP):
            continue
        for n, line in enumerate(f.read_text(errors="replace").splitlines(), 1):
            if PAT.search(line) and "zip-index-ok" not in line:
                bad.append(f"{f.relative_to(ROOT)}:{n}: {line.strip()[:110]}")
if bad:
    print("check_zip_index: direct entry indexing (use pc_zip_at, or mark a count-bounded loop with /* zip-index-ok */):")
    print("\n".join(bad))
    sys.exit(1)
print("check_zip_index ok")
