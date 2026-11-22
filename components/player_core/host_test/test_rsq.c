// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdlib.h>
#include "pc/pc_rsq.h"
#include "pc_test.h"
#include "dsp_meas.h"

/* the tap count the targets need (56 misses the 44.1 -> 48 image target at -68.8 dB, 64 misses the 48 -> 24 alias at -75.1 dB); raising it is a decision, not a drift */
#define PINNED_TAPS 68

static float *sine(int n, double rate, double hz, double amp)
{
    float *x = malloc(sizeof(float) * 2 * (size_t)n);
    for (int i = 0; i < n; i++) x[2 * i] = x[2 * i + 1] = (float)(amp * sin(2 * M_PI * hz * i / rate));
    return x;
}

static float *two(int n, double rate, double h1, double a1, double h2, double a2)
{
    float *x = malloc(sizeof(float) * 2 * (size_t)n);
    for (int i = 0; i < n; i++) x[2 * i] = x[2 * i + 1] = (float)(a1 * sin(2 * M_PI * h1 * i / rate) + a2 * sin(2 * M_PI * h2 * i / rate));
    return x;
}

/* the whole input through the resampler in blocks of blk (the loop hp_sink runs); returns the output frames, *used_total = the input consumed */
static int run(uint32_t in_rate, uint32_t out_rate, const float *x, int n, int blk, float **out, int *used_total)
{
    pc_rsq_t *q = malloc(pc_rsq_size());
    CHECK_EQ_INT(pc_rsq_init(q, in_rate, out_rate), 0);
    int cap = (int)((double)n * out_rate / in_rate) + 64, m = 0, ut = 0;
    *out = malloc(sizeof(float) * 2 * (size_t)cap);
    for (int i = 0; i < n; i += blk) {
        int k = n - i < blk ? n - i : blk, done = 0;
        while (done < k) {
            int used = 0;
            int r = pc_rsq_process(q, x + 2 * (size_t)(i + done), k - done, *out + 2 * (size_t)m, cap - m, &used);
            m += r;
            done += used;
            ut += used;
            if (!used && !r) break;
        }
    }
    free(q);
    if (used_total) *used_total = ut;
    return m;
}

/* level (dBFS) of the tone at meas_hz after a sine of amp at tone_hz went in_rate -> out_rate; 1.1 s in, the first 100 ms out is the settle */
static double lvl(uint32_t in_rate, uint32_t out_rate, double tone_hz, double amp, double meas_hz)
{
    int n = (int)(in_rate * 1.1);
    float *x = sine(n, in_rate, tone_hz, amp), *o;
    int m = run(in_rate, out_rate, x, n, 480, &o, NULL), settle = (int)(out_rate * 0.1);
    double db = meas_tone_db(o + 2 * (size_t)settle, m - settle, out_rate, meas_hz);
    free(x);
    free(o);
    return db;
}

static void test_rsq_taps(void) { CHECK_EQ_INT(PC_RSQ_TAPS, PINNED_TAPS); CHECK_EQ_INT(PC_RSQ_PHASES, 256); }

static void test_rsq_passband(void)
{
    static const double hz[] = { 20, 1000, 10000, 20000 };
    double worst = 0;
    for (int d = 0; d < 2; d++) {
        uint32_t a = d ? 48000 : 44100, b = d ? 44100 : 48000;
        for (int i = 0; i < 4; i++) {
            double e = lvl(a, b, hz[i], 0.5, hz[i]) - 20 * log10(0.5);
            if (fabs(e) > worst) worst = fabs(e);
            CHECK_NEAR(e, 0.0, 0.1);
        }
    }
    printf("rsq passband: worst %.4f dB (44.1<->48, 20..20000 Hz)\n", worst);
}

#define FFT_N 16384

static void fft(double *re, double *im, int n)               /* in-place radix-2 */
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) {
                double wr = cos(a * k), wi = sin(a * k);
                int u = i + k, v = i + k + len / 2;
                double xr = re[v] * wr - im[v] * wi, xi = re[v] * wi + im[v] * wr;
                re[v] = re[u] - xr; im[v] = im[u] - xi;
                re[u] += xr; im[u] += xi;
            }
    }
}

/* the largest component of the output spectrum in [lo, hi] Hz outside tone +- guard: a Hann-windowed FFT of FFT_N frames of the left channel,
 * EVERY bin looked at (bins of rate / FFT_N Hz), level in dBFS as meas_tone_db reads it */
static double spur_max(const float *o, int m, uint32_t rate, double lo, double hi, double tone, double guard, double *at)
{
    CHECK(m >= FFT_N);
    static double re[FFT_N], im[FFT_N];
    double ws = 0, worst = -300;
    for (int i = 0; i < FFT_N; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * (i + 0.5) / FFT_N);
        re[i] = o[2 * (size_t)i] * w;
        im[i] = 0;
        ws += w;
    }
    fft(re, im, FFT_N);
    for (int k = 1; k < FFT_N / 2; k++) {
        double f = (double)k * rate / FFT_N;
        if (f < lo || f > hi || fabs(f - tone) < guard) continue;
        double d = 20 * log10(2 * sqrt(re[k] * re[k] + im[k] * im[k]) / ws + 1e-30);
        if (d > worst) { worst = d; if (at) *at = f; }
    }
    return worst;
}

/* a double-precision reference: output k is the windowed sinc (the same Kaiser beta 8.6 kernel, normalised over its taps) evaluated at the EXACT
 * fractional position k * in / out (no table, no phase quantisation). pc_rsq must follow it closely, which only the linear interpolation between the
 * 256 phases achieves: reading the nearest phase gives an error about 1e-3 of the signal slope at 20 kHz (measured gap printed below). */
static double bessel_i0(double x) { double t = 1, s = 1; for (int k = 1; k < 60; k++) { t *= x * x / 4 / ((double)k * k); s += t; } return s; }

static double ref_out(const float *x, int n, uint32_t in_rate, uint32_t out_rate, int k)
{
    double pos = (double)k * in_rate / out_rate, r = (double)(in_rate < out_rate ? in_rate : out_rate) / in_rate;
    int idx = (int)floor(pos);
    double f = pos - idx, sum = 0, acc = 0, i0b = bessel_i0(8.6);
    for (int j = 0; j < PC_RSQ_TAPS; j++) {
        int m = idx - PC_RSQ_TAPS + 1 + j;
        double tau = f + PC_RSQ_TAPS / 2 - 1 - j;
        double h = tau == 0 ? r : sin(M_PI * r * tau) / (M_PI * tau);
        double u = tau / (PC_RSQ_TAPS / 2), w = bessel_i0(8.6 * sqrt(1 - u * u)) / i0b;
        h *= w;
        sum += h;
        if (m >= 0 && m < n) acc += h * x[2 * (size_t)m];
    }
    return acc / sum;
}

static void test_rsq_reference(void)
{
    static const uint32_t pair[][2] = { { 44100, 48000 }, { 48000, 44100 }, { 48000, 24000 } };
    for (int p = 0; p < 3; p++) {
        uint32_t a = pair[p][0], b = pair[p][1];
        int n = (int)a / 5;
        float *x = two(n, a, 1000, 0.4, a == 48000 && b == 24000 ? 9500 : 17500, 0.4), *o;
        int m = run(a, b, x, n, 441, &o, NULL);
        double worst = 0;
        for (int k = 100; k < m - 100; k++) {
            double d = fabs(ref_out(x, n, a, b, k) - o[2 * (size_t)k]);
            if (d > worst) worst = d;
        }
        printf("rsq reference %u->%u: worst gap to the exact-position double reference %.2e\n", (unsigned)a, (unsigned)b, worst);
        CHECK(worst < 1e-5);                            /* the linear interpolation leaves ~5e-6 at 17.5 kHz; nearest-of-256 phases leaves ~2e-3 (see the mutant run in the report) */
        free(x);
        free(o);
    }
}

/* every row sums to exactly 1 (to float precision): a constant stays constant at every fractional position, and a 1 kHz sine keeps its level to 0.001 dB */
static void test_rsq_unity(void)
{
    static const uint32_t pair[][2] = { { 44100, 48000 }, { 48000, 44100 }, { 48000, 24000 }, { 32000, 48000 }, { 22050, 48000 } };
    for (int p = 0; p < 5; p++) {
        uint32_t a = pair[p][0], b = pair[p][1];
        int n = (int)a / 4;
        float *x = malloc(sizeof(float) * 2 * (size_t)n), *o;
        for (int i = 0; i < 2 * n; i++) x[i] = 1.0f;
        int m = run(a, b, x, n, 480, &o, NULL);
        double worst = 0;
        for (int k = 0; k < 2 * m; k++) {
            int fr = k / 2;
            if (fr < PC_RSQ_TAPS * 3) continue;            /* the start-up ramp of the delayed history */
            if (fabs(o[k] - 1.0) > worst) worst = fabs(o[k] - 1.0);
        }
        printf("rsq unity %u->%u: DC error %.2e\n", (unsigned)a, (unsigned)b, worst);
        CHECK(worst < 1e-6);
        free(x);
        free(o);
        double e = lvl(a, b, 1000, 0.5, 1000) - 20 * log10(0.5);
        CHECK_NEAR(e, 0.0, 0.001);
    }
}

static void test_rsq_images(void)
{
    /* 44.1 -> 48: 20 kHz in, its image 44.1 - 20 = 24.1 kHz folds to 23.9 kHz */
    double img = lvl(44100, 48000, 20000, 1.0, 23900);
    printf("rsq image: 20 kHz tone -> 23.9 kHz image %.1f dBFS\n", img);
    CHECK(img <= -80.0);
    /* 1 kHz in: nothing else above -80 dB anywhere */
    int n = 44100 + 4410;
    float *x = sine(n, 44100, 1000, 1.0), *o;
    int m = run(44100, 48000, x, n, 441, &o, NULL), s = 4800;
    double at = 0, w = spur_max(o + 2 * (size_t)s, m - s, 48000, 50, 23950, 1000, 300, &at);
    printf("rsq spurs: 1 kHz tone, worst other component %.1f dBFS at %.0f Hz (every bin)\n", w, at);
    CHECK(w <= -80.0);
    free(x);
    free(o);
    x = sine(n, 44100, 17000, 1.0);
    m = run(44100, 48000, x, n, 441, &o, NULL);
    w = spur_max(o + 2 * (size_t)s, m - s, 48000, 50, 23950, 17000, 300, &at);
    printf("rsq spurs: 17 kHz tone, worst other component %.1f dBFS at %.0f Hz (every bin)\n", w, at);
    CHECK(w <= -80.0);
    free(x);
    free(o);
    /* 48 -> 44.1: 1 kHz + 23.9 kHz; nothing below 20 kHz except the 1 kHz tone */
    n = 48000 + 4800;
    x = two(n, 48000, 1000, 0.5, 23900, 0.5);
    m = run(48000, 44100, x, n, 480, &o, NULL);
    s = 4410;
    double base = meas_tone_db(o + 2 * (size_t)s, m - s, 44100, 1000);
    w = spur_max(o + 2 * (size_t)s, m - s, 44100, 50, 19900, 1000, 300, &at);
    printf("rsq 48->44.1: 1 kHz at %.1f dBFS, worst other component below 20 kHz %.1f dBFS at %.0f Hz (every bin); the 20.2 kHz alias %.1f dBFS\n",
           base, w, at, meas_tone_db(o + 2 * (size_t)s, m - s, 44100, 20200));
    CHECK(w <= -80.0);
    free(x);
    free(o);
}

static void test_rsq_48_to_24(void)
{
    double e = lvl(48000, 24000, 10000, 0.5, 10000) - 20 * log10(0.5);
    double al = lvl(48000, 24000, 14000, 1.0, 10000);
    printf("rsq 48->24: 10 kHz %.4f dB, 14 kHz alias at 10 kHz %.1f dBFS\n", e, al);
    CHECK_NEAR(e, 0.0, 0.1);
    CHECK(al <= -80.0);
    {   /* 3 kHz in: every bin of the 24 kHz output */
        int n = 48000 + 4800;
        float *x = sine(n, 48000, 3000, 1.0), *o;
        int m = run(48000, 24000, x, n, 480, &o, NULL);
        double at = 0, w = spur_max(o + 2 * 2400, m - 2400, 24000, 50, 11950, 3000, 300, &at);
        printf("rsq 48->24: 3 kHz tone, worst other component %.1f dBFS at %.0f Hz (every bin)\n", w, at);
        CHECK(w <= -80.0);
        free(x);
        free(o);
    }
    for (double f = 100; f <= 10000; f += 900) CHECK_NEAR(lvl(48000, 24000, f, 0.5, f) - 20 * log10(0.5), 0.0, 0.1);
    /* every input from 14 kHz up aliases into 1..10 kHz of the output: at least 80 dB down */
    double worst = -300;
    for (double f = 14000; f <= 23000; f += 1000) {
        double d = lvl(48000, 24000, f, 1.0, 24000 - f);
        if (d > worst) worst = d;
    }
    printf("rsq 48->24: worst alias of 14..23 kHz inputs %.1f dBFS\n", worst);
    CHECK(worst <= -80.0);
}

static void test_rsq_accounting(void)
{
    static const int blk[] = { 441, 480, 882, 960 };
    static const uint32_t pair[][2] = { { 44100, 48000 }, { 48000, 44100 }, { 48000, 24000 }, { 44100, 96000 } };
    for (int p = 0; p < 4; p++) {
        uint32_t a = pair[p][0], b = pair[p][1];
        int n = (int)a * 10;
        float *x = sine(n, a, 440, 0.5);
        for (int k = 0; k < 4; k++) {
            float *o;
            int used = 0, m = run(a, b, x, n, blk[k], &o, &used);
            long want = (long)llround((double)n * b / a);
            CHECK(labs((long)m - want) <= 1);
            CHECK_EQ_INT(used, n);
            free(o);
        }
        free(x);
    }
}

static void test_rsq_continuity(void)
{
    int n = 44100;
    float *x = two(n, 44100, 1000, 0.4, 7777, 0.3), *a, *b, *c;
    int ma = run(44100, 48000, x, n, 1, &a, NULL), mb = run(44100, 48000, x, n, 7, &b, NULL), mc = run(44100, 48000, x, n, 441, &c, NULL);
    CHECK_EQ_INT(ma, mc);
    CHECK_EQ_INT(mb, mc);
    double worst = 0;
    for (int i = 0; i < 2 * mc; i++) {
        double d = fabs((double)a[i] - c[i]), e = fabs((double)b[i] - c[i]);
        if (d > worst) worst = d;
        if (e > worst) worst = e;
    }
    printf("rsq continuity: blocks 1/7/441 differ by %g\n", worst);
    CHECK(worst < 1e-6);
    free(x); free(a); free(b); free(c);
}

static void test_rsq_latency(void)
{
    int n = 2000;
    float *x = calloc(2 * (size_t)n, sizeof(float)), *o;
    x[0] = x[1] = 1.0f;
    int m = run(44100, 48000, x, n, 441, &o, NULL), pk = 0;
    for (int i = 0; i < m; i++) if (fabsf(o[2 * i]) > fabsf(o[2 * pk])) pk = i;
    double ms = pk * 1000.0 / 48000;
    printf("rsq latency: impulse peak at output frame %d = %.3f ms\n", pk, ms);
    CHECK(ms <= 1.0);
    CHECK(o[2 * pk] > 0.5f);
    free(x); free(o);
}

static void test_rsq_bad(void)
{
    pc_rsq_t *q = malloc(pc_rsq_size());
    CHECK_EQ_INT(pc_rsq_init(q, 0, 48000), -1);
    CHECK_EQ_INT(pc_rsq_init(q, 48000, 0), -1);
    CHECK_EQ_INT(pc_rsq_init(q, 8000, 48000), -1);
    CHECK_EQ_INT(pc_rsq_init(q, 48000, 8000), -1);
    CHECK_EQ_INT(pc_rsq_init(q, 12000, 48000), 0);
    CHECK_EQ_INT(pc_rsq_init(q, 48000, 12000), 0);
    CHECK_EQ_INT(pc_rsq_init(q, 48000, 48000), 0);
    free(q);
}

/* a small cap stops the call without losing a frame: consumed + the next call carry on to the same output */
static void test_rsq_cap(void)
{
    int n = 4410;
    float *x = two(n, 44100, 1000, 0.4, 7777, 0.3), *ref;
    int mr = run(44100, 48000, x, n, 441, &ref, NULL);
    pc_rsq_t *q = malloc(pc_rsq_size());
    pc_rsq_init(q, 44100, 48000);
    float *o = malloc(sizeof(float) * 2 * 6000);
    int m = 0, done = 0;
    while (done < n) {
        int used = 0, r = pc_rsq_process(q, x + 2 * (size_t)done, n - done, o + 2 * (size_t)m, m + 7 > 5000 ? 5000 - m : 7, &used);
        m += r;
        done += used;
        if (!r && !used) break;
    }
    CHECK_EQ_INT(done, n);
    CHECK_EQ_INT(m, mr);
    for (int i = 0; i < 2 * m; i++) CHECK(o[i] == ref[i]);
    free(x); free(ref); free(o); free(q);
}

int main(void)
{
    RUN(test_rsq_taps);
    RUN(test_rsq_bad);
    RUN(test_rsq_passband);
    RUN(test_rsq_reference);
    RUN(test_rsq_unity);
    RUN(test_rsq_images);
    RUN(test_rsq_48_to_24);
    RUN(test_rsq_accounting);
    RUN(test_rsq_continuity);
    RUN(test_rsq_latency);
    RUN(test_rsq_cap);
    return 0;
}
