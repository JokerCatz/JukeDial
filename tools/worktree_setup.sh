#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Run from a git worktree: links the untracked inputs the simulator gates (make sim-test, sim-diff, ...) need from the MAIN checkout.
# usage: tools/worktree_setup.sh [name ...]      default names: app/sdkconfig app/managed_components .local/faces.zip .local/bt_ldac
# Only explicit file/dir names are linked (never .local as a whole). Names that look secret are refused.
set -e
cd "$(git rev-parse --show-toplevel)"
main=$(cd "$(git rev-parse --git-common-dir)/.." && pwd)
[ "$main" != "$PWD" ] || { echo "already the main checkout: nothing to link"; exit 0; }
ALLOW="app/sdkconfig app/managed_components .local/faces.zip .local/bt_ldac"
[ $# -gt 0 ] || set -- $ALLOW
for n in "$@"; do
  case " $ALLOW " in *" $n "*) ;; *) echo "refused (not in the allow-list): $n" >&2; exit 1;; esac
  if printf '%s' "$n" | grep -qiE 'wifi|gitee|ssid|passw|\.env$'; then echo "refused (secret-looking name): $n" >&2; exit 1; fi
  if [ ! -e "$main/$n" ]; then echo "skip (missing in main checkout): $n"; continue; fi
  if [ -e "$n" ] || [ -L "$n" ]; then echo "keep (already there): $n"; continue; fi
  mkdir -p "$(dirname "$n")"
  ln -s "$main/$n" "$n"
  echo "linked: $n -> $main/$n"
done
