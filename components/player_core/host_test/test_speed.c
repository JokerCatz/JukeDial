// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdbool.h>
#include <time.h>
#include "pc/pc_speed.h"
#include "pc_test.h"

typedef struct { uint32_t rate; long pos, len; double f1, f2; } gen_t;   /* two-tone "music" with a slow amplitude beat */
static int gen_feed(void *u, float *dst, int n)
{
    gen_t *g = u;
    int k = 0;
    for (; k < n && g->pos < g->len; k++, g->pos++) {
        double t = (double)g->pos / g->rate;
        float v = (float)(0.3 * sin(2 * M_PI * g->f1 * t) + 0.2 * sin(2 * M_PI * g->f2 * t)) * (float)(0.7 + 0.3 * sin(2 * M_PI * 3 * t));
        dst[2 * k] = v;
        dst[2 * k + 1] = 0.8f * v;
    }
    return k;
}

static pc_speed_t S;   /* big: static */

/* run the whole source through the stage in 10 ms blocks; returns the output frame count; zc = zero-crossings of the left channel, maxd = largest sample step */
static long run_all(uint32_t rate, float speed, double f1, double f2, long srcframes, long *zc, float *maxd, float *peak)
{
    gen_t g = { rate, 0, srcframes, f1, f2 };
    pc_speed_init(&S, rate, speed);
    static float blk[2 * 480], prev;
    long out = 0, z = 0;
    float md = 0, pk = 0;
    bool end = false;
    int bf = (int)(rate / 100);
    for (int guard = 0; !end && guard < 100000; guard++) {
        int n = pc_speed_run(&S, blk, bf, gen_feed, &g, &end);
        for (int i = 0; i < n; i++) {
            float v = blk[2 * i];
            if (out + i > 0) { if ((prev < 0) != (v < 0)) z++; float d = fabsf(v - prev); if (d > md) md = d; }
            if (fabsf(v) > pk) pk = fabsf(v);
            prev = v;
        }
        out += n;
    }
    CHECK(end);
    if (zc) *zc = z;
    if (maxd) *maxd = md;
    if (peak) *peak = pk;
    return out;
}

static void test_length_and_pitch(void)
{
    const float sp[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    for (int r = 0; r < 2; r++) {
        uint32_t rate = r ? 44100 : 48000;
        long src = (long)rate * 12;
        for (int i = 0; i < 5; i++) {
            long zc; float md, pk;
            long out = run_all(rate, sp[i], 440.0, 660.0, src, &zc, &md, &pk);
            CHECK_NEAR((double)out / ((double)src / sp[i]), 1.0, 0.03);                       /* duration = source / speed */
            /* pitch kept: the two-tone mix crosses zero about as often per second of OUTPUT as the source does per second of source */
            gen_t g = { rate, 0, src, 440.0, 660.0 };
            long z0 = 0; float p = 0;
            for (long k = 0; k < src; k++) { float w2[2]; gen_feed(&g, w2, 1); float v = w2[0]; if (k && (p < 0) != (v < 0)) z0++; p = v; }
            double rate_src = (double)z0 / src, rate_out = (double)zc / out;
            CHECK_NEAR(rate_out / rate_src, 1.0, 0.06);
            CHECK(pk < 0.62f && pk > 0.3f);                                                    /* no gain change, no overshoot from the overlap-add */
            CHECK(md < 0.12f);                                                                 /* no grain-join click: source max step is ~0.08 */
        }
    }
}

static void test_short_and_empty(void)
{
    uint32_t rate = 48000;
    CHECK_EQ_INT(run_all(rate, 1.5f, 440, 660, 0, NULL, NULL, NULL), 0);
    long o = run_all(rate, 2.0f, 440, 660, 300, NULL, NULL, NULL);                          /* shorter than a grain: passed through */
    CHECK_EQ_INT(o, 300);
    o = run_all(rate, 2.0f, 440, 660, 3000, NULL, NULL, NULL);
    CHECK(o > 1000 && o < 3000);
}

static void test_clamp_and_restart(void)
{
    pc_speed_init(&S, 96000, 9.0f);                                                          /* rate and speed clamp */
    CHECK_EQ_INT(S.ov, 480);
    CHECK_NEAR(S.speed, 2.0, 1e-6);
    pc_speed_init(&S, 48000, 0.1f);
    CHECK_NEAR(S.speed, 0.5, 1e-6);
}

static float *PRE; static long pre_n, pre_pos;
static int pre_feed(void *u, float *dst, int n)
{
    (void)u;
    int k = (int)(pre_n - pre_pos < n ? pre_n - pre_pos : n);
    memcpy(dst, PRE + 2 * pre_pos, (size_t)k * 2 * sizeof(float));
    pre_pos += k;
    return k;
}

static void test_bench(void)
{
    /* the DSP alone (the source pre-rendered), per second of 48 kHz stereo OUTPUT; reported, with a generous bound so a loaded host does not fail it */
    pre_n = 48000L * 120;
    PRE = malloc((size_t)pre_n * 2 * sizeof(float));
    gen_t g = { 48000, 0, pre_n, 440, 660 };
    gen_feed(&g, PRE, (int)pre_n);
    const float sp[] = { 0.75f, 1.25f, 1.5f, 2.0f };
    for (int i = 0; i < 4; i++) {
        struct timespec a, b;
        static float blk[2 * 480];
        bool end = false;
        long out = 0;
        pre_pos = 0;
        pc_speed_init(&S, 48000, sp[i]);
        clock_gettime(CLOCK_MONOTONIC, &a);
        while (!end) out += pc_speed_run(&S, blk, 480, pre_feed, NULL, &end);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double s = (double)(b.tv_sec - a.tv_sec) + 1e-9 * (double)(b.tv_nsec - a.tv_nsec), sec = (double)out / 48000;
        printf("bench speed %.2f: %.3f ms host per output second (%.2f %% of one host core)\n", sp[i], s * 1000 / sec, s * 100 / sec);
        CHECK(s / sec < 0.2);
    }
    free(PRE);
}

static void test_policy(void)
{
    CHECK_EQ_INT(pc_speed_norm(125), 125);
    CHECK_EQ_INT(pc_speed_norm(110), 100);
    CHECK_EQ_INT(pc_speed_norm(0), 100);
    CHECK_EQ_INT(pc_speed_step(200, 1), 75);
    CHECK_EQ_INT(pc_speed_step(75, -1), 200);
    CHECK_EQ_INT(pc_speed_step(100, 1), 125);
    CHECK_EQ_INT(pc_xfade_norm(5), 0);
    CHECK_EQ_INT(pc_xfade_step(6, 1), 0);
    CHECK_EQ_INT(pc_xfade_step(0, -1), 6);
    CHECK(pc_speed_ok(0) && pc_speed_ok(1 | 64));                       /* battery and a plain Bluetooth sink do not matter */
    CHECK(!pc_speed_ok(2) && !pc_speed_ok(4) && !pc_speed_ok(8) && !pc_speed_ok(16));
}

static void test_xfade(void)
{
    enum { N = 4800 };
    static float a[2 * N], b[2 * N], w[2 * N];
    /* equal power: two uncorrelated-looking signals (a constant and its quarter-phase partner) keep the power of the sum at 1 */
    for (int i = 0; i < N; i++) { a[2 * i] = a[2 * i + 1] = 0.5f; b[2 * i] = b[2 * i + 1] = 0.5f; w[2 * i] = w[2 * i + 1] = 0.5f; }
    pc_xfade_mix(a, b, N, 0.0f, 1.0f);
    CHECK_NEAR(a[0], 0.5, 1e-3);                                         /* starts as the old track */
    CHECK_NEAR(a[2 * (N - 1)], 0.5, 2e-3);                               /* a == b: cos + sin at the end is 1 again */
    float mn = 9;
    for (int i = 0; i < N; i++) if (a[2 * i] < mn) mn = a[2 * i];
    CHECK_NEAR(mn, 0.5, 1e-3);                                           /* equal power: the sum of two equal signals swells to 1.41 x mid-way, it never dips */
    for (int i = 0; i < N; i++) { a[2 * i] = a[2 * i + 1] = 1.0f; b[2 * i] = b[2 * i + 1] = 1.0f; }
    pc_xfade_mix(a, b, N, 0.0f, 1.0f);                                   /* M9: the correlated sum (+3 dB mid-way) never goes past full scale */
    float mx = 0;
    for (int i = 0; i < 2 * N; i++) if (fabsf(a[i]) > mx) mx = fabsf(a[i]);
    CHECK(mx <= 1.0f);
    for (int i = 0; i < N; i++) { a[2 * i] = a[2 * i + 1] = 0.5f; b[2 * i] = b[2 * i + 1] = 0.0f; }
    pc_xfade_mix(a, b, N, 0.0f, 1.0f);                                   /* old only fades out to 0 */
    CHECK_NEAR(a[0], 0.5, 1e-3);
    CHECK_NEAR(a[2 * (N - 1)], 0.0, 2e-3);
    for (int i = 0; i < N; i++) { a[2 * i] = a[2 * i + 1] = 0.0f; b[2 * i] = b[2 * i + 1] = 0.5f; }
    pc_xfade_mix(a, b, N, 0.0f, 1.0f);                                   /* new only fades in to 0.5 */
    CHECK_NEAR(a[0], 0.0, 1e-3);
    CHECK_NEAR(a[2 * (N - 1)], 0.5, 2e-3);
    float p = 0.5f;                                                      /* power law: cos^2 + sin^2 at the middle */
    for (int i = 0; i < N; i++) { a[2 * i] = a[2 * i + 1] = 1.0f; b[2 * i] = b[2 * i + 1] = 0.0f; }
    pc_xfade_mix(a, b, 1, p, p);
    CHECK_NEAR(a[0], cos(M_PI / 4), 1e-5);
    for (int i = 0; i < N; i++) b[2 * i] = b[2 * i + 1] = 1.0f;
    pc_xfade_finish(b, N, 0.5f);                                         /* from sin(45 deg) up to 1 */
    CHECK(b[0] > 0.70f && b[0] < 0.72f);
    CHECK_NEAR(b[2 * (N - 1)], 1.0, 1e-3);
    CHECK_NEAR(pc_xfade_progress(100, 100), 0.0, 1e-6);
    CHECK_NEAR(pc_xfade_progress(25, 100), 0.75, 1e-6);
    CHECK_NEAR(pc_xfade_progress(0, 100), 1.0, 1e-6);
    CHECK_NEAR(pc_xfade_progress(500, 100), 0.0, 1e-6);
    CHECK_NEAR(pc_xfade_progress(5, 0), 1.0, 1e-6);
}

static void test_run_big(void)   /* M3: a call far over the contract is clamped (no overflow of out[]), also with the end-of-source flush */
{
    static float big[2 * 4000];
    for (int len = 100; len < 4000; len += 777) {
        gen_t g = { 48000, 0, len, 440, 660 };
        pc_speed_init(&S, 48000, 2.0f);
        bool end = false;
        int n = pc_speed_run(&S, big, 4000, gen_feed, &g, &end);
        CHECK(n <= PC_SPEED_RUN_MAX);
        CHECK(S.out_n <= PC_SPEED_OUT_MAX);
    }
}

int main(void)
{
    RUN(test_run_big);
    RUN(test_policy);
    RUN(test_xfade);
    RUN(test_length_and_pitch);
    RUN(test_short_and_empty);
    RUN(test_clamp_and_restart);
    RUN(test_bench);
    return 0;
}
