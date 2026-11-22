// SPDX-License-Identifier: LGPL-2.1-or-later
/* Host harness for `make lib-stress` (no board, no simulator): measures the library index code on a fake index and counts the small allocations of
 * pc_library_scan. Built by tools/bench/lib_stress.py with -Wl,--wrap=malloc,... so every allocation made by the player_core objects is seen.
 *   lib_stress load  LIBRARY.BIN   -> key=value lines: load time, struct sizes, PSRAM estimate, path-order time, shuffle pick times
 *   lib_stress scan  EXPORT_DIR    -> key=value lines: allocation counts of pc_library_scan (blocks below 1 KiB go to internal RAM on the board)
 * Nothing here is firmware code; it only reads pc_library.h / pc_shuffle.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pc/pc_library.h"
#include "pc/pc_shuffle.h"
#include "pc/pc_stats.h"

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

/* ---- allocation accounting: a 16-byte header keeps the size ---- */
typedef struct { size_t size; size_t pad; } hdr_t;
static int g_on;
static long a_small, a_big, live_small, peak_small, live_all, peak_all, live_small_blocks, peak_small_blocks, small_total_bytes, small_total_al;
static long small_live_al, small_peak_al;
#define SMALL 1024
static size_t al16(size_t n) { return (n + 15) & ~(size_t)15; }
static void note_alloc(size_t n)
{
    if (!g_on) return;
    live_all += n; if (live_all > peak_all) peak_all = live_all;
    if (n < SMALL) {
        a_small++; small_total_bytes += n; small_total_al += al16(n);
        live_small += n; live_small_blocks++; small_live_al += al16(n);
        if (live_small > peak_small) peak_small = live_small;
        if (live_small_blocks > peak_small_blocks) peak_small_blocks = live_small_blocks;
        if (small_live_al > small_peak_al) small_peak_al = small_live_al;
    } else a_big++;
}
static void note_free(size_t n)
{
    if (!g_on) return;
    live_all -= n;
    if (n < SMALL) { live_small -= n; live_small_blocks--; small_live_al -= al16(n); }
}
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);
void *__real_calloc(size_t, size_t);
void __real_free(void *);
char *__real_strdup(const char *);
void *__wrap_malloc(size_t n)
{
    hdr_t *h = __real_malloc(n + sizeof *h);
    if (!h) return NULL;
    h->size = n; note_alloc(n);
    return h + 1;
}
void *__wrap_calloc(size_t a, size_t b)
{
    void *p = __wrap_malloc(a * b);
    if (p) memset(p, 0, a * b);
    return p;
}
void __wrap_free(void *p)
{
    if (!p) return;
    hdr_t *h = (hdr_t *)p - 1;
    note_free(h->size);
    __real_free(h);
}
void *__wrap_realloc(void *p, size_t n)
{
    if (!p) return __wrap_malloc(n);
    hdr_t *h = (hdr_t *)p - 1;
    size_t old = h->size;
    note_free(old);
    hdr_t *q = __real_realloc(h, n + sizeof *h);
    if (!q) { note_alloc(old); return NULL; }
    q->size = n; note_alloc(n);
    return q + 1;
}
char *__wrap_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = __wrap_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static uint32_t rng_s = 12345;
static uint32_t rnd(void) { rng_s = rng_s * 1664525u + 1013904223u; return rng_s >> 8; }

static int cmd_load(const char *path)
{
    pc_library_t lib;
    double t0 = now_ms();
    int rc = pc_library_load(&lib, path);
    double t1 = now_ms();
    if (rc) { printf("error=load rc=%d\n", rc); return 1; }
    int bigf = 0;
    for (int i = 0; i < lib.nfolders; i++) if (lib.folders[i].count > lib.folders[bigf].count) bigf = i;
    size_t tb = (size_t)lib.n * sizeof(pc_track_t), fb = (size_t)lib.nfolders * sizeof(pc_folder_t);
    printf("load_ms=%.2f\nn=%d\nnfolders=%d\nbiggest_folder=%d\nbiggest_count=%d\n", t1 - t0, lib.n, lib.nfolders, bigf, lib.folders[bigf].count);
    printf("sizeof_track=%zu\nsizeof_folder=%zu\ntracks_bytes=%zu\nfolders_bytes=%zu\npool_bytes=%u\npsram_est=%zu\n", sizeof(pc_track_t), sizeof(pc_folder_t), tb, fb, lib.pool_len,
           tb + fb + lib.pool_len + 3 /* the loader's +1 per array */);
    uint32_t longest = 0;
    for (int i = 0; i < lib.nfolders; i++) { uint32_t l = (uint32_t)strlen(pc_str(&lib, lib.folders[i].name)); if (l > longest) longest = l; }
    printf("longest_folder_name=%u\n", longest);

    int *ix = malloc((size_t)lib.n * sizeof *ix + 1);
    t0 = now_ms(); if (lib.n) pc_library_path_order(&lib, ix); t1 = now_ms();
    printf("path_order_ms=%.2f\n", t1 - t0);
    free(ix);

    /* mp3_ctl: build_candidates (all) once, then pc_shuffle_pick per track change */
    int *cand = malloc((size_t)lib.n * sizeof *cand + 1);
    t0 = now_ms(); for (int i = 0; i < lib.n; i++) cand[i] = i; t1 = now_ms();
    printf("build_cand_ms=%.3f\n", t1 - t0);
    pc_stats_t *st = pc_stats_create();
    char id[300];
    for (int k = 0; k < 200 && lib.n; k++) { snprintf(id, sizeof id, "mp3:%s", pc_str(&lib, lib.tracks[rnd() % (uint32_t)lib.n].path)); pc_stats_toggle_fav(st, id); }
    for (int variant = 0; variant < 3; variant++) {
        const pc_stats_t *s = variant == 1 ? st : NULL;
        int *c = cand; int nc = lib.n;
        if (variant == 2) { c = cand + lib.folders[bigf].first; nc = lib.folders[bigf].count; s = st; }   /* the biggest folder of own songs */
        pc_recent_t rec; memset(&rec, 0, sizeof rec);
        t0 = now_ms();
        long sum = 0;
        for (int k = 0; k < 1000; k++) { int p = pc_shuffle_pick(&lib, c, nc, s, &rec, rnd()); pc_recent_push(&rec, p); sum += p; }
        t1 = now_ms();
        const char *nm = variant == 0 ? "_all_nostats" : variant == 1 ? "_all_stats" : "_bigfolder_stats";
        printf("pick%s_total_ms=%.2f\npick%s_per_ms=%.4f\n", nm, t1 - t0, nm, (t1 - t0) / 1000);
        /* the pool weights made once (pc_wscope), then the same 1000 picks over them: what mp3_ctl does now */
        pc_wscope_t ws; memset(&ws, 0, sizeof ws);
        const int first = variant == 2 ? lib.folders[bigf].first : 0;
        t0 = now_ms(); pc_wscope_build(&ws, &lib, first, nc, s); t1 = now_ms();
        printf("wsbuild%s_ms=%.3f\n", nm, t1 - t0);
        memset(&rec, 0, sizeof rec);
        t0 = now_ms();
        for (int k = 0; k < 1000; k++) { int p = pc_wscope_pick(&ws, &rec, rnd()); pc_recent_push(&rec, p); sum += p; }
        t1 = now_ms();
        printf("wspick%s_per_ms=%.5f\n", nm, (t1 - t0) / 1000);
        pc_wscope_free(&ws);
        if (sum == -1) puts("");
    }
    free(cand);
    pc_stats_destroy(st);
    pc_library_free(&lib);
    return 0;
}

static int cmd_scan(const char *dir)
{
    pc_library_t lib;
    g_on = 1;
    double t0 = now_ms();
    int rc = pc_library_scan(dir, &lib, NULL, NULL);
    double t1 = now_ms();
    g_on = 0;
    if (rc) { printf("error=scan rc=%d\n", rc); return 1; }
    printf("scan_ms=%.1f\nn=%d\nnfolders=%d\n", t1 - t0, lib.n, lib.nfolders);
    printf("small_allocs=%ld\nsmall_bytes_total=%ld\nsmall_bytes_total_16=%ld\nsmall_peak_live_bytes=%ld\nsmall_peak_live_blocks=%ld\nsmall_peak_live_16=%ld\n", a_small,
           small_total_bytes, small_total_al, peak_small, peak_small_blocks, small_peak_al);
    printf("big_allocs=%ld\npeak_live_all=%ld\nlive_small_blocks_after=%ld\n", a_big, peak_all, live_small_blocks);
    pc_library_free(&lib);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "load")) return cmd_load(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "scan")) return cmd_scan(argv[2]);
    fprintf(stderr, "usage: lib_stress load LIBRARY.BIN | scan EXPORT_DIR\n");
    return 2;
}
