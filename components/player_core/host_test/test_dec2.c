// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdlib.h>
#include <time.h>
#include "pc/pc_dec2.h"
#include "pc_test.h"
#include "dsp_meas.h"

#define N 9600

static int run_blocks(const float *x, int n, int blk, float *out)
{
    pc_dec2_t *d = malloc(pc_dec2_size());
    pc_dec2_init(d);
    float *tmp = malloc(sizeof(float) * 2 * (size_t)blk);
    int done = 0;
    for (int i = 0; i < n; i += blk) {
        int k = n - i < blk ? n - i : blk;
        memcpy(tmp, x + 2 * (size_t)i, sizeof(float) * 2 * (size_t)k);
        done += pc_dec2_run(d, tmp, k, out + 2 * (size_t)done);
    }
    free(tmp);
    free(d);
    return done;
}

/* the final tap count of tools/gen/gen_halfband.py (49..61 cannot hold the ripple, 97 gives 87.1 dB, 101 gives 92.2 dB: 20 kHz must also pass at 88.2 kHz in, which makes the transition band 4.1 kHz wide) */
#define PINNED_TAPS 101

static void make(float *x, int n)
{
    for (int i = 0; i < n; i++) {
        float v = 0.4f * sinf(2 * (float)M_PI * 1000 * i / 96000) + 0.4f * sinf(2 * (float)M_PI * 30000 * i / 96000);
        x[2 * i] = v;
        x[2 * i + 1] = -0.5f * v;
    }
}

/* stereo sine of amplitude amp at hz, n frames at rate (right = left) */
static float *sine(int n, double rate, double hz, double amp)
{
    float *x = malloc(sizeof(float) * 2 * (size_t)n);
    for (int i = 0; i < n; i++) x[2 * i] = x[2 * i + 1] = (float)(amp * sin(2 * M_PI * hz * i / rate));
    return x;
}

/* in_rate -> in_rate/2 through one dec2; returns the output frames, caller frees *out */
static int dec_all(const float *x, int n, float **out)
{
    *out = malloc(sizeof(float) * 2 * (size_t)n);
    return run_blocks(x, n, 960, *out);
}

/* 1.1 s in, the first 100 ms of the output is the settle, the rest is measured */
static double dec_level(double rate, double tone_hz, double amp, double meas_hz)
{
    int n = (int)(rate * 1.1);
    float *x = sine(n, rate, tone_hz, amp), *o;
    int m = dec_all(x, n, &o), settle = (int)(rate / 2 * 0.1);
    double db = meas_tone_db(o + 2 * (size_t)settle, m - settle, (uint32_t)(rate / 2), meas_hz);
    free(x);
    free(o);
    return db;
}
static void test_dec2_taps(void)
{
    CHECK_EQ_INT(PC_DEC2_TAPS, PINNED_TAPS);
}

static void test_dec2_passband(void)
{
    static const double hz[] = { 20, 1000, 10000, 15000, 20000 };
    for (int i = 0; i < 5; i++) {
        double db = dec_level(96000, hz[i], 0.5, hz[i]);
        CHECK_NEAR(db, 20 * log10(0.5), 0.05);
    }
    CHECK_NEAR(dec_level(88200, 20000, 0.5, 20000), 20 * log10(0.5), 0.05);
}

static void test_dec2_stopband(void)
{
    static const double hz[] = { 28000, 30000, 40000, 47000 };
    for (int i = 0; i < 4; i++) {
        double alias = fabs(48000 - hz[i]);   /* where the unfiltered tone would fold to at 48 kHz */
        double peak = -300;   /* the worst of +-3 bins around the alias (bin = 48 kHz / 48000 measured frames = 1 Hz) */
        for (int j = -3; j <= 3; j++) {
            double db = dec_level(96000, hz[i], 1.0, alias + j * 48000.0 / 48000);
            if (db > peak) peak = db;
        }
        CHECK(peak <= -90);
    }
}

static void test_dec2_thdn(void)
{
    double amp = pow(10, -1.0 / 20);
    int n = 96000;
    float *x = sine(n, 96000, 1000, amp), *o;
    int m = dec_all(x, n, &o);
    double d = meas_thdn_db(o + 2 * 4800, m - 4800, 48000, 1000);
    CHECK(d <= -120);
    free(x);
    free(o);
}

static void test_dec2_blocks(void)
{
    static float x[2 * N], ref[2 * N], o[2 * N];
    make(x, N);
    int nr = run_blocks(x, N, N, ref);
    CHECK_EQ_INT(nr, N / 2);
    static const int blks[] = { 960, 7, 1, 961 };
    for (int b = 0; b < 4; b++) {
        int n = run_blocks(x, N, blks[b], o);
        CHECK_EQ_INT(n, nr);
        for (int i = 0; i < 2 * nr; i++) CHECK_NEAR(o[i], ref[i], 1e-7);
    }
    /* in place, odd blocks 961 then 959 */
    pc_dec2_t *d = malloc(pc_dec2_size());
    pc_dec2_init(d);
    static float y[2 * N];
    memcpy(y, x, sizeof(float) * 2 * 1920);
    int n1 = pc_dec2_run(d, y, 961, y);
    int n2 = pc_dec2_run(d, y + 2 * 961, 959, y + 2 * n1);
    CHECK_EQ_INT(n1 + n2, 960);
    for (int i = 0; i < 2 * 960; i++) CHECK_NEAR(y[i], ref[i], 1e-7);
    free(d);
}

/* x2 interpolator: in_rate -> 2 * in_rate */
static float *int_all(const float *x, int n, int *m)
{
    pc_int2_t *s = malloc(pc_int2_size());
    pc_int2_init(s);
    float *o = malloc(sizeof(float) * 4 * (size_t)n);
    *m = pc_int2_run(s, x, n, o);
    free(s);
    return o;
}

static double int_level(double tone_hz, double amp, double meas_hz)
{
    int n = 48000 * 11 / 10, m;
    float *x = sine(n, 48000, tone_hz, amp), *o = int_all(x, n, &m);
    int settle = 9600;   /* 100 ms at 96 kHz */
    double db = meas_tone_db(o + 2 * (size_t)settle, m - settle, 96000, meas_hz);
    free(x);
    free(o);
    return db;
}

static void test_int2_images(void)
{
    CHECK(int_level(1000, 1.0, 47000) <= -90);
    CHECK(int_level(18000, 1.0, 30000) <= -90);
    CHECK(int_level(20000, 1.0, 28000) <= -90);
    static const double hz[] = { 20, 1000, 10000, 15000, 20000 };
    for (int i = 0; i < 5; i++) CHECK_NEAR(int_level(hz[i], 0.5, hz[i]), 20 * log10(0.5), 0.05);
}

static void test_int2_frames(void)
{
    static float x[2 * 480], o[2 * 960];
    pc_int2_t *s = malloc(pc_int2_size());
    pc_int2_init(s);
    for (int i = 0; i < 2 * 480; i++) x[i] = 0.25f * (float)sin(i * 0.1);
    CHECK_EQ_INT(pc_int2_run(s, x, 480, o), 960);
    CHECK_EQ_INT(pc_int2_run(s, x, 1, o), 2);
    CHECK_EQ_INT(pc_int2_run(s, x, 0, o), 0);
    CHECK_EQ_INT(pc_int2_size() % 8, 0);
    free(s);
}

static void test_int2_blocks(void)
{
    static float x[2 * 480], ref[4 * 480], o[4 * 480];
    for (int i = 0; i < 2 * 480; i++) x[i] = 0.5f * (float)sin(i * 0.37) + 0.1f * (float)cos(i * 1.9);
    pc_int2_t *s = malloc(pc_int2_size());
    pc_int2_init(s);
    CHECK_EQ_INT(pc_int2_run(s, x, 480, ref), 960);
    static const int blks[] = { 7, 1, 61 };
    for (int b = 0; b < 3; b++) {
        pc_int2_init(s);
        int done = 0;
        for (int i = 0; i < 480; i += blks[b]) {
            int k = 480 - i < blks[b] ? 480 - i : blks[b];
            done += pc_int2_run(s, x + 2 * i, k, o + 2 * done);
        }
        CHECK_EQ_INT(done, 960);
        for (int i = 0; i < 4 * 480; i++) CHECK_NEAR(o[i], ref[i], 1e-7);
    }
    free(s);
}

/* host timing, not asserted: ns per output frame (both channels) of dec2 at 96k->48k and int2 at 48k->96k */
static void bench_halfband(void)
{
    enum { F = 96000, REP = 20 };
    float *x = sine(F, 96000, 1000, 0.5), *o = malloc(sizeof(float) * 4 * F);
    pc_dec2_t *d = malloc(pc_dec2_size());
    pc_int2_t *s = malloc(pc_int2_size());
    pc_dec2_init(d);
    pc_int2_init(s);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int m = 0;
    for (int r = 0; r < REP; r++) for (int i = 0; i < F; i += 480) m += pc_dec2_run(d, x + 2 * i, 480, o);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
    printf("halfband bench: dec2 %.1f ns per output frame (%d frames)", ns / m, m);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    m = 0;
    for (int r = 0; r < REP; r++) for (int i = 0; i < F; i += 480) m += pc_int2_run(s, x + 2 * i, 480, o);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
    printf(", int2 %.1f ns per output frame (%d frames), sink %g\n", ns / m, m, (double)o[7]);
    free(x); free(o); free(d); free(s);
}

int main(void)
{
    RUN(test_dec2_taps);
    RUN(test_dec2_passband);
    RUN(test_dec2_stopband);
    RUN(test_dec2_thdn);
    RUN(test_dec2_blocks);
    RUN(test_int2_images);
    RUN(test_int2_frames);
    RUN(test_int2_blocks);
    bench_halfband();
    return 0;
}
