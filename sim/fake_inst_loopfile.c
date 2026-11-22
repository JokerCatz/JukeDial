// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes inst_loopfile (the looper's files on the card: loop_NNN.jcl and the .mod export). */
/* Behaviour: names are the .jcl files of /sdcard/_player/loops in the simulator root; save and export refuse as the real ones (BUSY while recording, EMPTY without a loop or events), save makes loop_NNN.jcl, load stops the looper with a loop (fake engine), remove deletes and prints "SIM inst loopfile rm <name>". */
/* Empty: the looper's contents (nothing is written into the file, nothing is read back). */
#include <dirent.h>
#include "pc/pc_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "inst_loopfile.h"
#include "sim.h"

static int cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
int inst_loop_list(char names[][INST_LOOP_NAME], int max)
{
    DIR *d = opendir(INST_LOOP_DIR);
    int n = 0;
    if (!d) return 0;
    for (struct dirent *e; (e = readdir(d)) && n < max; ) {
        size_t l = strlen(e->d_name);
        if (l > 4 && l - 4 < INST_LOOP_NAME && !strcmp(e->d_name + l - 4, ".jcl")) { memcpy(names[n], e->d_name, l - 4); names[n++][l - 4] = 0; }
    }
    closedir(d);
    qsort(names, (size_t)n, INST_LOOP_NAME, cmp);
    return n;
}

int inst_loop_save(char *name_out)
{
    int ready = fake_inst_engine_loop_ready();
    if (ready) return ready;
    if (!fake_inst_engine_loop_content()) return INST_LF_EMPTY;
    static char names[INST_LOOP_FILES][INST_LOOP_NAME];
    int n = inst_loop_list(names, INST_LOOP_FILES), top = 0;
    for (int i = 0; i < n; i++) { int v; if (sscanf(names[i], "loop_%d", &v) == 1 && v > top) top = v; }
    char path[96];
    snprintf(name_out, INST_LOOP_NAME, "loop_%03d", top + 1);
    snprintf(path, sizeof path, INST_LOOP_DIR "/%s.jcl", name_out);
    pc_int_mkdir(INST_LOOP_DIR);
    FILE *f = fopen(path, "wb");
    if (!f) return INST_LF_IO;
    fputs("JCL fake\n", f);
    fclose(f);
    return INST_LF_OK;
}

int inst_loop_load(const char *name)
{
    char path[96];
    snprintf(path, sizeof path, INST_LOOP_DIR "/%s.jcl", name);
    if (access(path, 0)) return INST_LF_IO;
    fake_inst_engine_loop_loaded();
    return INST_LF_OK;
}
int inst_loop_export(const char *name, int *channels)
{
    (void)name;
    int ready = fake_inst_engine_loop_ready();
    if (ready) return ready;
    if (channels) *channels = 4;
    return INST_LF_OK;
}
int inst_loop_remove(const char *name)
{
    char path[96];
    printf("SIM inst loopfile rm %s\n", name);
    snprintf(path, sizeof path, INST_LOOP_DIR "/%s.jcl", name);
    unlink(path);
    snprintf(path, sizeof path, INST_LOOP_DIR "/%s.mod", name);
    unlink(path);
    return INST_LF_OK;
}
