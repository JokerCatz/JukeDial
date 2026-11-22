// SPDX-License-Identifier: LGPL-2.1-or-later
#include "chipbench_ctl.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "audio_svc.h"
#include "backend_gme.h"
#include "pc/pc_chipseq.h"
#include "pc/pc_chipbench.h"
#include "pc/pc_zip.h"

#define MAX_FILE (6u * 1024 * 1024)

typedef struct { char path[128]; int secs, sub; } job_t;

static volatile bool s_busy;
static uint32_t s_free_min, s_big_min;
static int s_tick;

static uint64_t now_us(void) { return (uint64_t)esp_timer_get_time(); }

/* after every block, outside the timed part: the lowest internal free size, and (every 20th block, a heap walk) the smallest largest-free-block */
static void sample(void *u)
{
    (void)u;
    uint32_t f = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (f < s_free_min) s_free_min = f;
    if (++s_tick % 20 == 0) {
        uint32_t b = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (b < s_big_min) s_big_min = b;
    }
}

/* the backend a file's extension picks: the module player, the three audio formats of the library (FMT), else Game_Music_Emu */
static const pc_backend_t *backend_of(const char *p, const char **name)
{
    const char *d = strrchr(p, '.');
    size_t pl = strlen(p);
    *name = "gme";
    if (pl > 7 && !strcasecmp(p + pl - 7, ".p8.png")) { *name = "p8"; return &pc_backend_p8; }
    if (!d) return &backend_gme;
    if (!strcasecmp(d, ".p8")) { *name = "p8"; return &pc_backend_p8; }
    if (!strcasecmp(d, ".aif") || !strcasecmp(d, ".aiff") || !strcasecmp(d, ".aifc")) { *name = "aiff"; return &pc_backend_aiff; }
    if (!strcasecmp(d, ".mpc")) { *name = "mpc"; return &pc_backend_mpc; }
    if (!strcasecmp(d, ".tta")) { *name = "tta"; return &pc_backend_tta; }
    if (!strcasecmp(d, ".ahx") || !strcasecmp(d, ".hvl")) { *name = "hvl"; return &pc_backend_hvl; }
    if (!strcasecmp(d, ".ym")) { *name = "ym"; return &pc_backend_ym; }
    if (!strcasecmp(d, ".sid")) { *name = "sid"; return &pc_backend_sid; }
    if (pc_opl_ext(d)) { *name = "opl"; return &pc_backend_opl; }
    if (!strcasecmp(d, ".adx")) { *name = "adx"; return &pc_backend_adx; }
    if (!strcasecmp(d, ".vag")) { *name = "vag"; return &pc_backend_vag; }
    if (!strcasecmp(d, ".brstm")) { *name = "brstm"; return &pc_backend_brstm; }
    return &backend_gme;
}

static bool is_xmp(const char *p)
{
    const char *d = strrchr(p, '.');
    return d && !pc_opl_ext(d) && pc_xmp_ext(d);
}

static bool playable(const char *p)
{
    static const char *const ext[] = { ".spc", ".nsf", ".nsfe", ".gbs", ".hes", ".kss", ".sap", ".ay", ".vgm", ".vgz", ".gym", ".aif", ".aiff", ".aifc", ".mpc", ".tta", ".ahx", ".hvl", ".p8", ".ym", ".sid", ".rad", ".hsc", ".imf", ".dro", ".cmf", ".a2m", ".a2t", ".d00", ".sop", ".xad", ".mtr", ".lds", ".plx", ".rol", ".ksm", ".mus", ".ims", ".mdi", ".sci", ".laa", ".amd", ".bam", ".msc", ".rix", ".got", ".dfm", ".dtm", ".sat", ".sa2", ".hsq", ".sqx", ".sdb", ".agd", ".ha2", ".sng", ".bmf", ".wlf", ".adx", ".vag", ".brstm" };
    const char *d = strrchr(p, '.');
    size_t pl = strlen(p);
    if (pl > 7 && !strcasecmp(p + pl - 7, ".p8.png")) return true;
    if (is_xmp(p)) return true;
    for (size_t i = 0; d && i < sizeof ext / sizeof *ext; i++) if (!strcasecmp(d, ext[i])) return true;
    return false;
}

static void bench_file(const char *path, int secs, int sub)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("APP chipbench file=%s err=open\n", path); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || (unsigned long)sz > MAX_FILE) { fclose(f); printf("APP chipbench file=%s err=size %ld\n", path, sz); return; }
    uint8_t *buf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); heap_caps_free(buf); printf("APP chipbench file=%s err=read\n", path); return; }
    fclose(f);
    if (sz > 18 && buf[0] == 0x1f && buf[1] == 0x8b) {          /* .vgz: gunzipped at open like chip_ctl.c load() does (to PSRAM: pc_gunzip mallocs big) */
        uint8_t *raw;
        uint32_t rl;
        int grc = pc_gunzip(buf, (uint32_t)sz, &raw, &rl);
        heap_caps_free(buf);
        if (grc) { printf("APP chipbench file=%s err=gunzip\n", path); return; }
        buf = raw;
        sz = (long)rl;
    }
    pc_chipbench_env_t env = { .now_us = now_us, .block_hook = sample, .ext = strrchr(path, '.') };
    pc_chipbench_t r;
    const char *bname;
    const pc_backend_t *be = is_xmp(path) ? (bname = "xmp", &pc_backend_xmp) : backend_of(path, &bname);
    size_t ps0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t min0 = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    s_free_min = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_big_min = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    s_tick = 0;
    pc_chipbench_run(be, buf, (uint32_t)sz, sub, 2, &env, &r);   /* warm-up (caches, first-open allocations) outside the numbers */
    s_free_min = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    pc_chipbench_run(be, buf, (uint32_t)sz, sub, secs, &env, &r);
    size_t ps1 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t min1 = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    char line[320];
    pc_chipbench_fmt(&r, line, sizeof line);
    const char *base = strrchr(path, '/');
    printf("APP chipbench be=%s file=%s secs=%d %s int_free_min=%u int_big_min=%u int_min_before=%u int_min_after=%u psram_used=%d stack_hw=%u\n",
           bname, base ? base + 1 : path, secs, line, (unsigned)s_free_min, (unsigned)s_big_min, (unsigned)min0, (unsigned)min1,
           (int)ps0 - (int)ps1, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    heap_caps_free(buf);
}

static void task(void *arg)
{
    job_t *j = arg;
    DIR *d = opendir(j->path);               /* a folder opens, a file does not */
    if (d) {
        struct dirent *e;
        int n = 0;
        while ((e = readdir(d)) != NULL) {          /* one folder level, 20 s each unless seconds were given */
            if (e->d_name[0] == '.' || !playable(e->d_name)) continue;
            char p[256];
            snprintf(p, sizeof p, "%s/%s", j->path, e->d_name);
            bench_file(p, j->secs, j->sub);
            n++;
        }
        closedir(d);
        printf("APP chipbench dir=%s files=%d done\n", j->path, n);
    } else {
        bench_file(j->path, j->secs, j->sub);
    }
    free(j);
    s_busy = false;
    vTaskDeleteWithCaps(NULL);
}

void chipbench_cmd(const char *args)
{
    aud_status_t st;
    audio_svc_status(&st);
    if (s_busy) { printf("APP chipbench busy\n"); return; }
    if (st.state == AUD_PLAYING || st.state == AUD_PAUSED || st.state == AUD_NEAR || st.state == AUD_NEXT || audio_svc_live_gen() != 0) {
        printf("APP chipbench refused: a source is playing (stop first)\n");
        return;
    }
    job_t *j = calloc(1, sizeof *j);
    if (!j) { printf("APP chipbench err=oom\n"); return; }
    j->secs = 30;
    char *a = strdup(args ? args : ""), *save = NULL;
    char *p = a ? strtok_r(a, " ", &save) : NULL;
    if (!p) { printf("APP chipbench usage: chipbench <file|dir> [seconds=30] [sub=0]\n"); free(a); free(j); return; }
    snprintf(j->path, sizeof j->path, "%s", p);
    char *s = strtok_r(NULL, " ", &save);
    if (s) j->secs = atoi(s);
    s = strtok_r(NULL, " ", &save);
    if (s) j->sub = atoi(s);
    free(a);
    if (j->secs < 1 || j->secs > 600) j->secs = 30;
    s_busy = true;
    /* core 1, the audio task's priority: with nothing playing the audio task sleeps, so this measures a block as the audio task would see it */
    if (xTaskCreatePinnedToCoreWithCaps(task, "chipbench", 32 * 1024, j, configMAX_PRIORITIES - 2, NULL, 1, MALLOC_CAP_SPIRAM) != pdPASS) {
        s_busy = false;
        free(j);
        printf("APP chipbench err=task\n");
        return;
    }
    printf("APP chipbench started\n");
}
