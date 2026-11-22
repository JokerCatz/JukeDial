// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_paths.h"
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_kit.h"
#include "pc/pc_sampler.h"
/* Sample kits on the TF card (stage 4): /sdcard/instrument/kits/<folder>/ with kit.txt (kit "<folder>") and/or
 * .sfz files (kit "<folder>/<name>"). Loading reads every WAV into PSRAM (cap INST_KIT_CAP for the kit, all
 * or nothing) on a worker task, then hands the kit to the engine; the previous kit is dropped and freed
 * first (two 8 MB kits would not fit). */
#define INST_KIT_DIR PC_KITS_DIR
#define INST_KIT_CAP (8u * 1024 * 1024)
#define INST_KIT_NAME 48

typedef struct inst_kit {
    pc_kit_def_t *def;
    int16_t smp_of[PC_KIT_REGIONS];       /* region -> smp, -1: its file did not load */
    pc_sample_t smp[PC_KIT_REGIONS];      /* one per distinct file */
    int nsmp;
    size_t bytes;
    char name[INST_KIT_NAME];
} inst_kit_t;

typedef enum { INST_KIT_NONE, INST_KIT_LOADING, INST_KIT_READY, INST_KIT_ERROR } inst_kit_state_t;
enum { INST_KIT_E_NOTFOUND = 1, INST_KIT_E_EMPTY, INST_KIT_E_TOOBIG, INST_KIT_E_MEMORY, INST_KIT_E_FORMAT };

int  inst_kit_list(char (*names)[INST_KIT_NAME], int max);   /* sorted */
void inst_kit_load(const char *name);   /* async; "" unloads */
inst_kit_state_t inst_kit_state(int *err, size_t *bytes, int *skipped);
const inst_kit_t *inst_kit_current(void);   /* the loader task and the engine only */
/* for the screen: a copy, safe from any task; gen changes with every kit change */
typedef struct { uint32_t gen; char name[INST_KIT_NAME]; int16_t note[8]; char label[8][16]; } inst_kit_info_t;
void inst_kit_info(inst_kit_info_t *out);
