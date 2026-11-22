#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Run a build command quietly: output goes to <logfile>; on success nothing is printed, on failure the last 60 lines and the log path.
# Usage: tools/quiet_build.sh <logfile> <command> [args...]
log=$1; shift
mkdir -p "$(dirname "$log")"
if "$@" >"$log" 2>&1; then exit 0; else rc=$?; tail -n 60 "$log"; echo "build FAILED (rc=$rc), full log: $log"; exit $rc; fi
