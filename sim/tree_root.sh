#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# A simulator data root for the song tree (LIBTREE): sim/fixture plus root1.mp3 root2.mp3, deep/{a,b}/ (one song each, none of its own), many/ (100 songs and a sub-folder of 3)
# and the scanned library index. Folders in pre-order: deep 0, a 1, b 2, many 3, sub 4, then the fixture's three albums; 119 songs. Usage: sim/tree_root.sh <dir> (replaced if it exists).
# Run from the repository root; needs sim/build/sim.
set -e
d=$1; [ -n "$d" ] || { echo "usage: $0 <dir>" >&2; exit 2; }
rm -rf "$d"; mkdir -p "$d"; cp -r sim/fixture/. "$d/"
m="$d/export/紙飛機工廠/04_untitled_demo.mp3"; e="$d/export"
mkdir -p "$e/many/sub" "$e/deep/a" "$e/deep/b"
for ((i = 1; i <= 100; i++)); do cp "$m" "$e/many/s$(printf %03d $i).mp3"; done
for i in 1 2 3; do cp "$m" "$e/many/sub/t$i.mp3"; done
cp "$m" "$e/deep/a/x.mp3"; cp "$m" "$e/deep/b/y.mp3"; cp "$m" "$e/root1.mp3"; cp "$m" "$e/root2.mp3"
sim/build/sim --headless --root "$d" --cmd "scan; @0.5" | grep -q '^APP scan n=119 folders=8 rc=0 ' || { echo "tree_root: the scan did not give 119 songs in 8 folders" >&2; exit 1; }
