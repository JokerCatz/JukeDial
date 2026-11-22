// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdlib.h>
#include "pc/pc_dec2.h"
#include "pc/pc_halve.h"
#include "pc_test.h"

static double ph;   /* 1 kHz sine phase, continuous across blocks */

static void sine(float *b, int n, double rate)
{
    for (int i = 0; i < n; i++) {
        float v = 0.5f * (float)sin(ph);
        b[2 * i] = b[2 * i + 1] = v;
        ph += 2 * M_PI * 1000 / rate;
    }
}

static int crossings(const float *b, int n)
{
    int z = 0;
    for (int i = 1; i < n; i++) if ((b[2 * (i - 1)] < 0) != (b[2 * i] < 0)) z++;
    return z;
}

static void test_halve_replan_mid_stream(void)
{
    pc_halve_t *h = malloc(pc_halve_size());
    pc_halve_init(h);
    float b[2 * 960], c[2 * 960];
    pc_pipe_t p = pc_halve_set(h, 96000, 96000, 0, PC_PIPE_CAP);
    CHECK_EQ_INT(p.pipe, 96000);
    CHECK_EQ_INT(pc_halve_in_frames(h, 480), 480);
    for (int k = 0; k < 50; k++) {
        sine(b, 480, 96000);
        memcpy(c, b, sizeof(float) * 960);
        CHECK_EQ_INT(pc_halve_run(h, b, 480), 480);
        CHECK(memcmp(b, c, sizeof(float) * 960) == 0);
    }
    p = pc_halve_set(h, 96000, 48000, 0, PC_PIPE_SINK);
    CHECK_EQ_INT(p.pipe, 48000);
    CHECK_EQ_INT(pc_halve_in_frames(h, 480), 960);
    for (int k = 0; k < 50; k++) {
        sine(b, 960, 96000);
        CHECK_EQ_INT(pc_halve_run(h, b, 960), 480);
        if (k > 0) CHECK_NEAR(crossings(b, 480), 20, 1);
    }
    p = pc_halve_set(h, 96000, 96000, 0, PC_PIPE_CAP);
    CHECK_EQ_INT(p.pipe, 96000);
    for (int k = 0; k < 5; k++) {
        sine(b, 480, 96000);
        CHECK_EQ_INT(pc_halve_run(h, b, 480), 480);
    }
    free(h);
}

static void test_halve_hwcap_drop(void)
{
    pc_halve_t *h = malloc(pc_halve_size());
    pc_dec2_t *ref0 = malloc(pc_dec2_size()), *ref1 = malloc(pc_dec2_size());   /* uninterrupted reference stages */
    pc_halve_init(h);
    ph = 0;
    static float b[2 * 4 * 480], r[2 * 4 * 480];
    pc_pipe_t p = pc_halve_set(h, 192000, 192000, 4096, PC_PIPE_CAP);
    CHECK_EQ_INT(p.pipe, 192000);
    for (int k = 0; k < 20; k++) { sine(b, 1920, 192000); CHECK_EQ_INT(pc_halve_run(h, b, 1920), 1920); }
    p = pc_halve_set(h, 192000, 96000, 4096, PC_PIPE_CLOCK);
    CHECK_EQ_INT(p.pipe, 96000);
    CHECK_EQ_INT(p.halvings, 1);
    CHECK_EQ_INT(p.why, PC_PIPE_CLOCK);
    pc_dec2_init(ref0);                                 /* stage 0 turns on here: same cleared start as the halving stage */
    for (int k = 0; k < 20; k++) {
        sine(b, 1920, 192000);
        memcpy(r, b, sizeof(float) * 2 * 1920);
        CHECK_EQ_INT(pc_halve_run(h, b, 1920), 960);
        CHECK_EQ_INT(pc_dec2_run(ref0, r, 1920, r), 960);
        for (int i = 0; i < 2 * 960; i++) CHECK_NEAR(b[i], r[i], 1e-6);
    }
    p = pc_halve_set(h, 192000, 48000, 4096, PC_PIPE_SINK);
    CHECK_EQ_INT(p.pipe, 48000);
    CHECK_EQ_INT(p.halvings, 2);
    pc_dec2_init(ref1);                                 /* only the second stage starts cleared; stage 0 must keep its history */
    float peak = 0;
    for (int k = 0; k < 20; k++) {
        sine(b, 1920, 192000);
        memcpy(r, b, sizeof(float) * 2 * 1920);
        CHECK_EQ_INT(pc_halve_run(h, b, 1920), 480);
        CHECK_EQ_INT(pc_dec2_run(ref0, r, 1920, r), 960);
        CHECK_EQ_INT(pc_dec2_run(ref1, r, 960, r), 480);
        for (int i = 0; i < 2 * 480; i++) CHECK_NEAR(b[i], r[i], 1e-6);   /* every frame after the re-plan, no skipped window */
        if (k >= 10) for (int i = 0; i < 480; i++) if (fabsf(b[2 * i]) > peak) peak = fabsf(b[2 * i]);
    }
    CHECK(peak > 0.4f);
    free(ref0);
    free(ref1);
    free(h);
}

int main(void)
{
    RUN(test_halve_replan_mid_stream);
    RUN(test_halve_hwcap_drop);
    return 0;
}
