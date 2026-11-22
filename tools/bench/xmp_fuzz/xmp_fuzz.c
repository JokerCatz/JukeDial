// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fuzz / regression harness for the vendored libxmp (components/libxmp): every module loader, ProWizard and the Amiga depackers,
 * through the same callback loader the firmware uses (pc_backend_xmp.c), then the player for a bounded number of frames.
 * Built by tools/bench/xmp_fuzz/run.sh with ASan + UBSan (findings abort), against the counting heap of fz_alloc.c.
 *   xmp_fuzz --run FILE...            load + play each file once, print "type chn len frames" (exit 1 if any fails to load)
 *   xmp_fuzz --fuzz SEEDDIR N SEED    N mutated executions per seed file, PRNG seed SEED; the current input is kept in $FZ_CUR
 *   xmp_fuzz --time FILE...           per-file load ms and the slowest frame ms (build without sanitizers)
 * An execution that runs longer than FZ_ALARM seconds (default 20) aborts with "HANG": the hang bound. A leak after xmp_free_context
 * aborts with "LEAK". */
#define _GNU_SOURCE
#include <dirent.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xmp.h>

long long fz_live(void);
unsigned long long fz_failed(void);
void fz_reset(void);

typedef struct { const uint8_t *p; size_t n, off; } mem_t;
static unsigned long mem_read(void *d, unsigned long size, unsigned long nmemb, void *priv)
{
    mem_t *m = priv;
    if (!size || !nmemb) return 0;
    unsigned long want = size * nmemb;
    if (m->off >= m->n) return 0;
    if (want > m->n - m->off) want = m->n - m->off;
    memcpy(d, m->p + m->off, want);
    m->off += want;
    return want / size;
}
static int mem_seek(void *priv, long off, int whence)
{
    mem_t *m = priv;
    long to = whence == SEEK_SET ? off : whence == SEEK_CUR ? (long)m->off + off : (long)m->n + off;
    if (to < 0) return -1;
    m->off = (size_t)to;
    return 0;
}
static long mem_tell(void *priv) { return (long)((mem_t *)priv)->off; }

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static int g_frames = 400;                 /* ~8 s of music at 125 BPM */
static double g_load_ms, g_frame_ms;
static char g_type[64];
static int g_chn, g_len;

static const char *g_cur;
static void on_alarm(int s)
{
    (void)s;
    static const char m[] = "HANG: execution exceeded the alarm bound\n";
    write(2, m, sizeof m - 1);
    abort();
}

/* one execution: 0 = loaded and played, -1 = refused */
static int exec_one(const uint8_t *buf, size_t n, int frames)
{
    mem_t m = { buf, n, 0 };
    struct xmp_callbacks cb = { .read_func = mem_read, .seek_func = mem_seek, .tell_func = mem_tell, .close_func = NULL };
    long long base = fz_live();
    xmp_context c = xmp_create_context();
    double t0 = now_ms();
    int rc = c ? xmp_load_module_from_callbacks(c, &m, cb) : -1;
    g_load_ms = now_ms() - t0;
    g_frame_ms = 0;
    g_type[0] = 0;
    if (rc == 0) {
        struct xmp_module_info mi;
        xmp_get_module_info(c, &mi);
        snprintf(g_type, sizeof g_type, "%s", mi.mod->type);
        g_chn = mi.mod->chn;
        g_len = mi.mod->len;
        if (xmp_start_player(c, 44100, 0) == 0) {
            for (int i = 0; i < g_chn && i < XMP_MAX_CHANNELS; i++) xmp_channel_mute(c, i, i & 1);
            struct xmp_frame_info fi;
            int k;
            for (k = 0; k < frames; k++) {
                double f0 = now_ms();
                if (xmp_play_frame(c) != 0) break;
                double f1 = now_ms() - f0;
                if (f1 > g_frame_ms) g_frame_ms = f1;
                xmp_get_frame_info(c, &fi);
                if (fi.buffer_size < 0 || fi.buffer_size > 44100 * 4) { fprintf(stderr, "BAD buffer_size %d\n", fi.buffer_size); abort(); }
                if (fi.loop_count > 0) break;
            }
            if (k == frames / 2 && g_len > 1) xmp_set_position(c, g_len / 2);
            xmp_end_player(c);
        }
    }
    xmp_free_context(c);
    if (fz_live() != base) { fprintf(stderr, "LEAK: %lld bytes live after free_context (%s)\n", fz_live() - base, g_cur ? g_cur : "?"); abort(); }
    return rc == 0 ? 0 : -1;
}

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)l + 1);
    *n = fread(b, 1, (size_t)l, f);
    fclose(f);
    return b;
}

static uint64_t rs;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }
static const uint32_t INTERESTING[] = { 0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 129, 255, 256, 511, 512, 1023, 1024, 4095, 4096, 0x7fff, 0x8000, 0xffff, 0x10000,
    0x7fffff, 0x800000, 0xffffff, 0x1000000, 0x7fffffff, 0x80000000u, 0xffffffffu, 0xfffffffeu, 0xfffffff0u, 0x40000000, 0x3fffffff, 0x20000000 };

/* mutate buf (cap bytes) in place; returns the new length */
static size_t mutate(uint8_t *b, size_t n, size_t cap)
{
    int rounds = 1 + (int)(rnd() % 4);
    while (rounds--) {
        size_t lim = (rnd() % 3 == 0 || n < 64) ? n : (n < 1200 ? n : 1200);   /* two thirds of the hits go to the first 1200 bytes (headers, tables) */
        if (!n) { b[0] = (uint8_t)rnd(); n = 1; }
        size_t at = rnd() % (lim ? lim : 1);
        switch (rnd() % 11) {
        case 0: b[at] ^= (uint8_t)(1u << (rnd() & 7)); break;
        case 1: b[at] = (uint8_t)rnd(); break;
        case 2: { uint32_t v = INTERESTING[rnd() % (sizeof INTERESTING / 4)]; int w = 1 << (rnd() % 3); for (int i = 0; i < w && at + i < n; i++) b[at + i] = (uint8_t)(v >> (8 * ((rnd() & 1) ? i : w - 1 - i))); break; }
        case 3: { size_t l = 1 + rnd() % 32; if (at + l > n) l = n - at; memset(b + at, (rnd() & 1) ? 0 : 0xff, l); break; }
        case 4: if (n > 8) { size_t cut = 1 + rnd() % (n / 2); n -= cut; } break;                   /* truncate */
        case 5: { size_t l = 1 + rnd() % 64; if (n + l <= cap && at < n) { memmove(b + at + l, b + at, n - at); for (size_t i = 0; i < l; i++) b[at + i] = (uint8_t)rnd(); n += l; } break; }
        case 6: { size_t l = 1 + rnd() % 64; if (at + l < n) { memmove(b + at, b + at + l, n - at - l); n -= l; } break; }
        case 7: { size_t l = 1 + rnd() % 64, from = rnd() % n; if (from + l > n) l = n - from; if (at + l > n) l = n - at; memmove(b + at, b + from, l); break; }
        case 8: if (at + 4 <= n) { uint32_t v; memcpy(&v, b + at, 4); v += (uint32_t)((int)(rnd() % 33) - 16); memcpy(b + at, &v, 4); } break;
        case 9: { size_t l = 1 + rnd() % 256; if (n + l <= cap) { memset(b + n, (int)(rnd() & 0xff), l); n += l; } break; }   /* grow with a run */
        default: b[at] += (uint8_t)((int)(rnd() % 9) - 4); break;
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    int mode = 0;
    if (argc < 3) { fprintf(stderr, "usage: see the header of xmp_fuzz.c\n"); return 2; }
    const char *a = getenv("FZ_ALARM");
    signal(SIGALRM, on_alarm);
    if (!strcmp(argv[1], "--run") || !strcmp(argv[1], "--time")) {
        mode = argv[1][2] == 't';
        int bad = 0;
        double worst_load = 0, worst_frame = 0;
        for (int i = 2; i < argc; i++) {
            size_t n;
            uint8_t *b = slurp(argv[i], &n);
            if (!b) { printf("%s: cannot read\n", argv[i]); bad++; continue; }
            g_cur = argv[i];
            alarm(a ? (unsigned)atoi(a) : 20);
            int rc = exec_one(b, n, g_frames);
            alarm(0);
            if (rc) { printf("%s: REFUSED\n", argv[i]); bad++; }
            else printf("%s: %s chn=%d len=%d load=%.1fms slowest_frame=%.2fms\n", argv[i], g_type, g_chn, g_len, g_load_ms, g_frame_ms);
            if (g_load_ms > worst_load) worst_load = g_load_ms;
            if (g_frame_ms > worst_frame) worst_frame = g_frame_ms;
            free(b);
        }
        if (mode) printf("TIME worst_load=%.1fms worst_frame=%.2fms\n", worst_load, worst_frame);
        return bad ? 1 : 0;
    }
    if (!strcmp(argv[1], "--fuzz") && argc >= 5) {
        const char *dir = argv[2];
        long iters = atol(argv[3]);
        rs = strtoull(argv[4], NULL, 0) * 0x9E3779B97F4A7C15ull + 1;
        const char *cur = getenv("FZ_CUR");
        if (!cur) cur = "/tmp/xmp_fuzz_cur.bin";
        DIR *d = opendir(dir);
        if (!d) { perror(dir); return 2; }
        struct dirent *e;
        long execs = 0, loaded = 0;
        double worst_load = 0, worst_frame = 0;
        char types[64][64]; int ntypes = 0;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            size_t n0;
            uint8_t *seed = slurp(path, &n0);
            if (!seed || !n0) continue;
            size_t cap = n0 + 4096;
            uint8_t *w = malloc(cap);
            for (long it = 0; it < iters; it++) {
                memcpy(w, seed, n0);
                size_t n = it ? mutate(w, n0, cap) : n0;
                FILE *f = fopen(cur, "wb");
                if (f) { fwrite(w, 1, n, f); fclose(f); }
                g_cur = path;
                alarm(a ? (unsigned)atoi(a) : 20);
                int rc = exec_one(w, n, 120);
                alarm(0);
                execs++;
                if (rc == 0) {
                    loaded++;
                    int k; for (k = 0; k < ntypes; k++) if (!strcmp(types[k], g_type)) break;
                    if (k == ntypes && ntypes < 64) snprintf(types[ntypes++], 64, "%s", g_type);
                }
                if (g_load_ms > worst_load) worst_load = g_load_ms;
                if (g_frame_ms > worst_frame) worst_frame = g_frame_ms;
            }
            free(w);
            free(seed);
        }
        closedir(d);
        printf("FUZZ execs=%ld loaded=%ld types=%d worst_load=%.1fms worst_frame=%.2fms\n", execs, loaded, ntypes, worst_load, worst_frame);
        for (int k = 0; k < ntypes; k++) printf("  type: %s\n", types[k]);
        return 0;
    }
    fprintf(stderr, "bad arguments\n");
    return 2;
}
