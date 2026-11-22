// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_paths.h"
#include "pc/pc_library.h"
/* The MP3 library, owned by the LVGL task (read it only from LVGL context). */
#define LIB_EXPORT PC_EXPORT_DIR
#define LIB_INDEX PC_LIB_INDEX
extern pc_library_t g_lib;
void library_svc_load(void);          /* boot: index; a missing/damaged index with songs on the card starts a rescan */
unsigned library_gen(void);           /* counts every index load / swap of g_lib (a page compares it instead of pointers) */
bool library_scanning(void);
int  library_scan_done(void);         /* files scanned so far */
bool library_rescan(void);            /* false: no card or already scanning; else a core-0 task swaps g_lib in LVGL context */
extern void (*library_on_scanned)(void);   /* LVGL context, after the scan (swapped or not) */
