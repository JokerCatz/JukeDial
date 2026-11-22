#!/usr/bin/env bash
# Patched Bluedroid for the AVRCP bench peer: a fresh copy of $IDF_PATH/components/bt in .local/bt_avrc/bt with
# avrc_bench.patch applied (only the SetAbsoluteVolume auto-answer in btc_avrc.c changes: a weak hook, see README.md).
# tools/bt_peer/avrc_peer uses the copy as its project component `bt`. The copy gets its own git repo (host/bluedroid
# tracked, pristine commit first), so after editing:  (cd .local/bt_avrc/bt && git diff) > tools/bt_peer/avrc_peer/avrc_bench.patch
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
dest="${BT_AVRC_DEST:-$root/.local/bt_avrc/bt}"   # override for a trial run
patch="$here/avrc_bench.patch"
IDF_PATH="${IDF_PATH:-$HOME/.espressif/v6.1/esp-idf}"   # this project: IDF v6.1
src="$IDF_PATH/components/bt"
[ -d "$src/host/bluedroid" ] || { echo "make_bt: no Bluedroid in $src" >&2; exit 1; }
rm -rf "$dest"
mkdir -p "$(dirname "$dest")"
cp -a "$src" "$dest"
cd "$dest"
git init -q
git add host/bluedroid
git -c user.name=make_bt -c user.email=make_bt@local commit -qm "pristine $(cd "$IDF_PATH" && git describe --always --dirty 2>/dev/null || echo IDF) of the IDF bt component"
if ! git apply --check "$patch"; then
    echo "make_bt: FAILED - avrc_bench.patch does not apply to $src (IDF changed?). See tools/bt_peer/avrc_peer/README.md" >&2
    exit 1
fi
git apply "$patch"
echo "make_bt: patched Bluedroid ready in $dest ($(git diff --shortstat))"
