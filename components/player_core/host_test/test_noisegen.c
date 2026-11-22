// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_noisegen.h"
#include "pc_test.h"

#define N (PC_NOISE_RATE * 2)
static float buf[2 * N];

/* rms of L, how much of L survives a 32-sample moving average (low-frequency share), L/R correlation */
static void measure(int color, double *rms, double *slope, double *corr)
{
    static pc_noisegen_t g;
    pc_noisegen_start(&g, color);
    pc_noisegen_render(&g, buf, N);
    double s = 0, d = 0, lr = 0, rr = 0, acc = 0;
    for (int i = 1; i < N; i++) {
        double l = buf[2 * i], r = buf[2 * i + 1];
        acc += l - (i >= 32 ? buf[2 * (i - 32)] : 0);
        double lp = acc / 32;
        s += l * l; d += lp * lp; lr += l * r; rr += r * r;
        CHECK(l >= -1.0 && l <= 1.0 && r >= -1.0 && r <= 1.0);
    }
    *rms = sqrt(s / N);
    *slope = sqrt(d / N) / *rms;
    *corr = lr / sqrt(s * rr);
}

static void test_levels_and_stereo(void)
{
    for (int c = 0; c < PC_NOISE_COLORS; c++) {
        double rms, slope, corr;
        measure(c, &rms, &slope, &corr);
        printf("  %-14s rms=%.3f slope=%.3f corr=%.3f\n", pc_noise_name(c, 0), rms, slope, corr);
        CHECK(rms > 0.08 && rms < 0.25);          /* calibrated ~0.15 full scale */
        CHECK(fabs(corr) < 0.1);                   /* L and R are different noise */
    }
}

static void test_spectral_order(void)
{
    int order[] = { PC_NOISE_BROWN, PC_NOISE_PINK, PC_NOISE_WHITE, PC_NOISE_BLUE, PC_NOISE_VIOLET };
    double prev = 0;
    for (int i = 0; i < 5; i++) {
        double rms, slope, corr;
        measure(order[i], &rms, &slope, &corr);
        CHECK(i == 0 || slope < prev);             /* less low-frequency share, colour by colour */
        prev = slope;
    }
}

static void test_repeatable(void)
{
    static pc_noisegen_t g;
    float a[64], b[64];
    pc_noisegen_start(&g, PC_NOISE_PINK);
    pc_noisegen_render(&g, a, 32);
    pc_noisegen_start(&g, PC_NOISE_PINK);
    pc_noisegen_render(&g, b, 32);
    CHECK(!memcmp(a, b, sizeof a));
    CHECK_STR(pc_noise_name(PC_NOISE_BROWN, 1), "棕噪音");
}

int main(void)
{
    RUN(test_levels_and_stereo);
    RUN(test_spectral_order);
    RUN(test_repeatable);
    return 0;
}
