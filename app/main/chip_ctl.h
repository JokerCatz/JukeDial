// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_paths.h"
#include <stddef.h>
#include "pc/pc_chipseq.h"
/* Chiptune: /sdcard/chiptune/<system>.zip -> game (zip entry) -> sub-track (spec 4.3). LVGL context only. */
#define CHIP_DIR PC_CHIP_DIR
int  chip_list_systems(char (*names)[64], int max);   /* zip names without .zip, sorted */
int  chip_open_system(const char *name);              /* file count, <0 error */
int  chip_file_count(void);
void chip_file_label(int file, char *out, size_t cap);   /* entry base name without extension */
int  chip_track_names(int file, char (*names)[64], int max);   /* extracts the entry; count, <0 error */
void chip_play(int file, int sub);
void chip_next(void);
void chip_stop(void);
pc_seq_t chip_mode(void);
void chip_set_mode(pc_seq_t m);                       /* stored as cfg chip_mode */
bool chip_now(char *sys, size_t cap, int *file, int *sub);   /* the playing system/file/sub-track; false when idle */
