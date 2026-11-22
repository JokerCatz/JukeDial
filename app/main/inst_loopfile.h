// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_paths.h"
/* Looper files on the card (stage 6): /sdcard/_player/loops/loop_NNN.jcl (pc_lp_save) and loop_NNN.mod
 * (ProTracker export). Call from the UI / console task, never the audio task. */
#define INST_LOOP_DIR PC_LOOPS_DIR
#define INST_LOOP_NAME 24
#define INST_LOOP_FILES 64
enum { INST_LF_OK = 0, INST_LF_EMPTY = -1, INST_LF_BUSY = -2, INST_LF_IO = -3, INST_LF_FORMAT = -4, INST_LF_MEMORY = -5 };

int inst_loop_list(char names[][INST_LOOP_NAME], int max);   /* .jcl names without the extension, sorted */
int inst_loop_save(char *name_out);                         /* a new loop_NNN; not while recording */
int inst_loop_load(const char *name);                       /* stops the looper; sets its tempo */
int inst_loop_export(const char *name, int *channels);      /* NAME.mod from the current loop */
int inst_loop_remove(const char *name);                     /* NAME.jcl and NAME.mod */
