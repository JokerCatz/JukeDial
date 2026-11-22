// SPDX-License-Identifier: LGPL-2.1-or-later
/* DSP filter cost (hires_bench part "dsp", Task 10): the player's own sources (pc_dec2.c, pc_rsq.c, taken by path in main/CMakeLists.txt),
 * one 10 ms block of input per call, the cycle counter around the call. Buffers come from malloc (> 1 KB: PSRAM, as the player's).
 * Cases: pc_dec2_run at 192k (1920 frames in) and 96k (960), pc_int2_run at 48k (480 in), pc_rsq_process 44.1 -> 48 (441 in) and 48 -> 24 (480 in).
 * A warm-up pass of 50 blocks is discarded, then 200 blocks: min/avg/max. pct = average cycles / cycles in 10 ms at the measured CPU clock.
 * Output: HB dsp <name> pct=<%> us_per_block=<min>/<avg>/<max>; HB dsp rsq_init in=<hz> out=<hz> us=<n>. */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "pc/pc_dec2.h"
#include "pc/pc_rsq.h"

uint32_t hb_cpu_mhz(void);

#define WARM 50
#define REPS 200

static volatile float s_sink;

typedef int (*blk_fn)(void *ctx, const float *in, int n, float *out);

static void time_case(const char *name, int frames, blk_fn fn, void *ctx, float *in, float *out)
{
    uint32_t mn = UINT32_MAX, mx = 0;
    uint64_t sum = 0;
    int pos = 0;
    for (int r = 0; r < WARM + REPS; r++) {
        uint32_t t0 = esp_cpu_get_cycle_count();
        int m = fn(ctx, in + 2 * (size_t)pos, frames, out);
        uint32_t c = esp_cpu_get_cycle_count() - t0;
        s_sink = out[2 * (m > 0 ? m - 1 : 0)];
        pos = (pos + frames) % (frames * 8);
        if (r < WARM) continue;
        sum += c;
        if (c < mn) mn = c;
        if (c > mx) mx = c;
    }
    uint32_t mhz = hb_cpu_mhz();
    double avg = (double)sum / REPS;
    printf("HB dsp %s pct=%.2f us_per_block=%.0f/%.0f/%.0f\n", name, 100.0 * avg / ((double)mhz * 10000.0), (double)mn / mhz, avg / mhz, (double)mx / mhz);
}

static int fn_dec2(void *c, const float *in, int n, float *out) { return pc_dec2_run(c, in, n, out); }
static int fn_int2(void *c, const float *in, int n, float *out) { return pc_int2_run(c, in, n, out); }
static int fn_rsq(void *c, const float *in, int n, float *out)
{
    int used = 0, done = 0, m = 0;
    while (done < n) {                                   /* the loop hp_sink runs */
        int r = pc_rsq_process(c, in + 2 * (size_t)done, n - done, out + 2 * (size_t)m, 600 - m, &used);
        m += r;
        done += used;
        if (!used && !r) break;
    }
    return m;
}

static void rsq_case(const char *name, uint32_t a, uint32_t b, int frames, pc_rsq_t *q, float *in, float *out)
{
    uint32_t t0 = esp_cpu_get_cycle_count();
    int e = pc_rsq_init(q, a, b);
    uint32_t c = esp_cpu_get_cycle_count() - t0;
    printf("HB dsp rsq_init in=%" PRIu32 " out=%" PRIu32 " err=%d us=%.0f taps=%d\n", a, b, e, (double)c / hb_cpu_mhz(), PC_RSQ_TAPS);
    if (!e) time_case(name, frames, fn_rsq, q, in, out);
}

void dsp_part(void)
{
    size_t fr = 1920 * 8;                                /* 8 blocks of the largest case */
    float *in = heap_caps_malloc(sizeof(float) * 2 * fr, MALLOC_CAP_SPIRAM), *out = heap_caps_malloc(sizeof(float) * 2 * 4000, MALLOC_CAP_SPIRAM);
    pc_dec2_t *d = heap_caps_malloc(pc_dec2_size(), MALLOC_CAP_SPIRAM);
    pc_int2_t *s = heap_caps_malloc(pc_int2_size(), MALLOC_CAP_SPIRAM);
    pc_rsq_t *q = heap_caps_malloc(pc_rsq_size(), MALLOC_CAP_SPIRAM);
    if (!in || !out || !d || !s || !q) { printf("HB dsp alloc failed\n"); goto done; }
    for (size_t i = 0; i < fr; i++) in[2 * i] = in[2 * i + 1] = 0.4f * sinf(0.0713f * (float)i) + 0.3f * sinf(0.911f * (float)i);
    pc_dec2_init(d);
    time_case("dec2_192k", 1920, fn_dec2, d, in, out);
    pc_dec2_init(d);
    time_case("dec2_96k", 960, fn_dec2, d, in, out);
    pc_int2_init(s);
    time_case("int2_48k", 480, fn_int2, s, in, out);
    rsq_case("rsq_44k1_48k", 44100, 48000, 441, q, in, out);
    rsq_case("rsq_48k_24k", 48000, 24000, 480, q, in, out);
done:
    free(in); free(out); free(d); free(s); free(q);
}
