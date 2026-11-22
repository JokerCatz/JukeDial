// SPDX-License-Identifier: LGPL-2.1-or-later
#include "inst_loopfile.h"
#include "pc/pc_paths.h"
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "inst_engine.h"
#include "pc/pc_looper.h"

static int name_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

int inst_loop_list(char names[][INST_LOOP_NAME], int max)
{
    DIR *d = opendir(INST_LOOP_DIR);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        size_t l = strlen(e->d_name);
        if (l > 4 && l - 4 < INST_LOOP_NAME && !strcasecmp(e->d_name + l - 4, ".jcl")) {
            memcpy(names[n], e->d_name, l - 4);
            names[n][l - 4] = 0;
            n++;
        }
    }
    closedir(d);
    qsort(names, n, INST_LOOP_NAME, name_cmp);
    return n;
}

/* loop_NNN after the highest number on the card */
static void next_name(char *out)
{
    static char names[INST_LOOP_FILES][INST_LOOP_NAME];
    int n = inst_loop_list(names, INST_LOOP_FILES), hi = 0;
    for (int i = 0; i < n; i++) { int k; if (sscanf(names[i], "loop_%d", &k) == 1 && k > hi) hi = k; }
    snprintf(out, INST_LOOP_NAME, "loop_%03d", hi + 1);
}

static int write_file(const char *path, const uint8_t *b, size_t n)
{
    char tmp[96];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return INST_LF_IO;
    size_t w = fwrite(b, 1, n, f);
    if (fclose(f) != 0 || w != n) { remove(tmp); return INST_LF_IO; }
    remove(path);
    return rename(tmp, path) == 0 ? INST_LF_OK : INST_LF_IO;
}

static void meta_of(const pc_looper_t *lp, pc_lp_meta_t *m)
{
    memset(m, 0, sizeof *m);
    m->rate = INST_RATE;
    m->len = lp->len;
    m->bpm_x100 = (uint16_t)lround(60.0 * INST_RATE * 100.0 / lp->beat);
    m->bpb = (uint8_t)(lp->beat ? lp->bar / lp->beat : 4);
    m->bars = (uint8_t)(lp->bar ? lp->len / lp->bar : 0);
    m->quant = (uint8_t)(lp->grid ? (lp->grid == lp->beat / 2 ? 1 : 2) : 0);
    m->mute = lp->mute;
    const pc_synth_t *s = inst_engine_synth();
    snprintf(m->chip, sizeof m->chip, "%s", s ? s->id : "");
}

static bool ready(const pc_looper_t *lp) { return (lp->state == PC_LP_STOP || lp->state == PC_LP_PLAY) && !lp->rec && lp->len; }

int inst_loop_save(char *name_out)
{
    const pc_looper_t *lp = inst_engine_looper();
    if (!ready(lp)) return lp->rec ? INST_LF_BUSY : INST_LF_EMPTY;
    if (!pc_lp_has_content(lp)) return INST_LF_EMPTY;
    pc_int_mkdir(INST_LOOP_DIR);
    char name[INST_LOOP_NAME];
    next_name(name);
    size_t cap = pc_lp_save_size(lp);
    uint8_t *b = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!b) return INST_LF_MEMORY;
    pc_lp_meta_t m;
    meta_of(lp, &m);
    int64_t t0 = esp_timer_get_time();
    size_t n = pc_lp_save(lp, &m, b, cap);
    char path[96];
    snprintf(path, sizeof path, INST_LOOP_DIR "/%s.jcl", name);
    int r = n ? write_file(path, b, n) : INST_LF_EMPTY;
    heap_caps_free(b);
    printf("INST loop save %s bytes=%u rc=%d ms=%d\n", name, (unsigned)n, r, (int)((esp_timer_get_time() - t0) / 1000));
    if (r == INST_LF_OK && name_out) snprintf(name_out, INST_LOOP_NAME, "%s", name);
    return r;
}

int inst_loop_load(const char *name)
{
    char path[96];
    snprintf(path, sizeof path, INST_LOOP_DIR "/%s.jcl", name);
    FILE *f = fopen(path, "rb");
    if (!f) return INST_LF_IO;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) { fclose(f); return INST_LF_FORMAT; }
    uint8_t *b = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM);
    if (!b) { fclose(f); return INST_LF_MEMORY; }
    size_t got = fread(b, 1, (size_t)sz, f);
    fclose(f);
    pc_looper_t *lp = inst_engine_loop_park(true);
    int r = INST_LF_BUSY;
    if (lp) {
        pc_lp_meta_t m;
        r = pc_lp_load(lp, &m, b, got) == 0 ? INST_LF_OK : INST_LF_FORMAT;
        if (r != INST_LF_OK) pc_lp_reset(lp);
        else printf("INST loop load %s events=%u len=%u bpm=%.2f bpb=%d chip=%s\n", name, (unsigned)lp->n, (unsigned)lp->len,
                    m.bpm_x100 / 100.0, m.bpb, m.chip);
    }
    inst_engine_loop_park(false);
    heap_caps_free(b);
    return r;
}

int inst_loop_export(const char *name, int *channels)
{
    const pc_looper_t *lp = inst_engine_looper();
    if (!ready(lp)) return lp->rec ? INST_LF_BUSY : INST_LF_EMPTY;
    size_t cap = 1084 + 64 * 64 * 8 * 4 + 512;
    uint8_t *b = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!b) return INST_LF_MEMORY;
    pc_lp_mod_info_t in;
    size_t n = pc_lp_mod(lp, 60.0 * INST_RATE / lp->beat, (int)(lp->bar / lp->beat), b, cap, &in);
    pc_int_mkdir(INST_LOOP_DIR);
    char path[96];
    snprintf(path, sizeof path, INST_LOOP_DIR "/%s.mod", name);
    int r = n ? write_file(path, b, n) : INST_LF_EMPTY;
    heap_caps_free(b);
    printf("INST loop export %s.mod bytes=%u channels=%d rows=%d patterns=%d notes=%d dropped=%d folded=%d rc=%d\n", name,
           (unsigned)n, in.channels, in.rows, in.patterns, in.notes, in.dropped, in.folded, r);
    if (channels) *channels = in.channels;
    return r;
}

int inst_loop_remove(const char *name)
{
    char path[96];
    snprintf(path, sizeof path, INST_LOOP_DIR "/%s.jcl", name);
    int a = remove(path);
    snprintf(path, sizeof path, INST_LOOP_DIR "/%s.mod", name);
    remove(path);
    printf("INST loop rm %s rc=%d\n", name, a);
    return a == 0 ? INST_LF_OK : INST_LF_IO;
}
