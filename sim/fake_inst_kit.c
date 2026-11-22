// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes inst_kit (sample kits read from the TF card into PSRAM by a worker task). */
/* Behaviour: the kit names are the folders of /sdcard/instrument/kits in the simulator root; a load is ready 50 ms of virtual time later, with 8 pads on notes 36..43. */
/* Empty: the WAV reading, the sampler, the memory cap (inst_kit_current is NULL). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "inst_kit.h"
#include "sim.h"

static char s_name[INST_KIT_NAME];
static int64_t s_t0;
static uint32_t s_gen;
static int s_err;
static inst_kit_state_t s_st = INST_KIT_NONE;

static int cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
int inst_kit_list(char (*names)[INST_KIT_NAME], int max)
{
    DIR *d = opendir(INST_KIT_DIR);
    int n = 0;
    if (!d) return 0;
    for (struct dirent *e; (e = readdir(d)) && n < max; )
        if (e->d_name[0] != '.' && strlen(e->d_name) < INST_KIT_NAME) memcpy(names[n++], e->d_name, strlen(e->d_name) + 1);
    closedir(d);
    qsort(names, (size_t)n, INST_KIT_NAME, cmp);
    return n;
}

void inst_kit_load(const char *name)
{
    char path[160];
    struct stat st;
    s_gen++;
    s_err = 0;
    snprintf(s_name, sizeof s_name, "%s", name);
    if (!*name) { s_st = INST_KIT_NONE; return; }
    snprintf(path, sizeof path, INST_KIT_DIR "/%s", name);
    if (stat(path, &st)) { s_st = INST_KIT_ERROR; s_err = INST_KIT_E_NOTFOUND; return; }
    s_st = INST_KIT_LOADING;
    s_t0 = sim_now_us();
}

inst_kit_state_t inst_kit_state(int *err, size_t *bytes, int *skipped)
{
    if (s_st == INST_KIT_LOADING && sim_now_us() - s_t0 >= 50000) s_st = INST_KIT_READY;
    if (err) *err = s_err;
    if (bytes) *bytes = s_st == INST_KIT_READY ? 262144 : 0;
    if (skipped) *skipped = 0;
    return s_st;
}

const inst_kit_t *inst_kit_current(void) { return NULL; }

void inst_kit_info(inst_kit_info_t *out)
{
    static const char *const lab[8] = { "kick", "snare", "hat closed", "hat open", "clap", "rim", "tom low", "tom high" };
    memset(out, 0, sizeof *out);
    out->gen = s_gen;
    snprintf(out->name, sizeof out->name, "%s", s_name);
    inst_kit_state(NULL, NULL, NULL);
    for (int p = 0; p < 8; p++) {
        out->note[p] = s_st == INST_KIT_READY ? (int16_t)(36 + p) : -1;
        if (s_st == INST_KIT_READY) snprintf(out->label[p], sizeof out->label[p], "%s", lab[p]);
    }
}
