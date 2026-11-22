// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Console `chipbench <path|dir> [seconds=30] [sub=0]` (CHIPEXP Phase 0): the host bench core (pc_chipbench.c) on the board. Reads the file
 * into PSRAM, runs the backend (GME, or libxmp for .mod .xm .s3m .it) for N seconds in 10 ms blocks on a task pinned to core 1 at the
 * audio task's priority, and prints one "APP chipbench ..." line per file. Refused while a source plays. Never touches the audio output. */
void chipbench_cmd(const char *args);
