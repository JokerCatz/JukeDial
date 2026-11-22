#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# A simulator data root for the four picker lists (Task 13): sim/fixture plus 20 stations (three with 25-character names), 12 chiptune systems
# (empty zips: listed, not playable), 12 more MP3 folders (15 in all) and the scanned library index. The 30 noise scenes are the console's "simnoise 30 3".
# Usage: sim/pickers_root.sh <dir> (replaced if it exists). Run from the repository root; needs sim/build/sim.
set -e
d=$1; [ -n "$d" ] || { echo "usage: $0 <dir>" >&2; exit 2; }
rm -rf "$d"; mkdir -p "$d"; cp -r sim/fixture/. "$d/"
{ echo "# 20 synthetic stations (nothing listens on these ports)"
  for ((i = 0; i < 20; i++)); do
      case $i in 4) n="Retro Lofi Chiptune Radio";; 9) n="SomaFM Groove Salad Prime";; 15) n="WQXR New York Classical 1";; *) n="Sim 電台 $(printf %02d $i)";; esac
      echo "$n|http://127.0.0.1:8000/s$i.mp3"
  done; } > "$d/_player/config/radio.txt"
mkdir -p "$d/chiptune"
for n in "Atari 8-bit" "Commodore 64 SID" "Game Boy" "MSX" "NEC PC-Engine" "Neo Geo Pocket" "Nintendo Entertainment System" "Sega Game Gear" \
         "Sega Master System" "Sega Mega Drive" "Super Nintendo" "ZX Spectrum"; do : > "$d/chiptune/$n.zip"; done
for ((i = 1; i <= 12; i++)); do f="$d/export/folder$(printf %02d $i)"; mkdir "$f"; cp "$d/export/紙飛機工廠/04_untitled_demo.mp3" "$f/song.mp3"; done
sim/build/sim --headless --root "$d" --cmd "scan; @0.5" | grep -q '^APP scan n=24 folders=15 rc=0 ' || { echo "pickers_root: the scan did not give 24 songs in 15 folders" >&2; exit 1; }
