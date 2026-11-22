#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# "Source list changed -> reconfigure": the components collect their sources with file(GLOB), so a new or removed file needs a CMake reconfigure.
# Usage: tools/src_stamp.sh <build-dir> '<reconfigure command>'   (build-dir relative to the repo root, e.g. app/build-stock)
# Stamps a hash of the file list in <build-dir>/.srclist; when the list differs (or there is no stamp) it prints "source list changed: reconfigure",
# runs the command with bash -c, and stamps only if the command succeeded (its exit code is passed on). No change: silent, exit 0.
# Keep the listing in step with every file(GLOB) of app/main/CMakeLists.txt and components/*/CMakeLists.txt
# (check: grep -rn 'file(GLOB' app/main components/*/CMakeLists.txt).
set -u
cd "$(dirname "$0")/.." || exit 2
[ $# -eq 2 ] || { echo "usage: tools/src_stamp.sh <build-dir> '<reconfigure command>'" >&2; exit 2; }
D=$1; CMD=$2
mkdir -p "$D"
ls app/main/*.c app/main/*.cpp app/main/ui/*.c components/*/src/*.c components/libadplug/adplug/*.c components/libadplug/adplug/*.cpp 2>/dev/null | md5sum >"$D/.srclist.new"
if cmp -s "$D/.srclist.new" "$D/.srclist"; then rm -f "$D/.srclist.new"; exit 0; fi
echo "source list changed: reconfigure"
if bash -c "$CMD"; then mv "$D/.srclist.new" "$D/.srclist"; else rc=$?; rm -f "$D/.srclist.new"; exit $rc; fi
