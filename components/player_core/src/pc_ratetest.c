// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_ratetest.h"
#include <math.h>

#define TONE2_MS (PC_RT_TONE_MS + PC_RT_GAP_MS)
#define LEVEL 0.0316f             /* -30 dBFS */
#define RAMP_MS 20

void pc_ratetest_init(pc_ratetest_t *t, uint32_t max_rate, int32_t now_ms)
{
    static const uint32_t ALL[4] = { 48000, 96000, 192000, 384000 };
    *t = (pc_ratetest_t){ .t0_ms = now_ms };
    for (int i = 0; i < 4; i++)
        if (ALL[i] <= max_rate) t->tiers[t->ntiers++] = ALL[i];
    if (!t->ntiers) { t->state = PC_RT_DONE; t->end = PC_RT_END_TOP; }
}

uint32_t pc_ratetest_rate_now(const pc_ratetest_t *t, int32_t now_ms)
{
    if (t->state != PC_RT_RUN) return 0;
    int32_t d = now_ms - t->t0_ms;
    uint32_t tier = t->tiers[t->tier];
    if (d >= 0 && d < PC_RT_TONE_MS) return tier / 160 * 147;      /* 48000 -> 44100 (every tier is a multiple of 160) */
    if (d >= TONE2_MS && d < TONE2_MS + PC_RT_TONE_MS) return tier;
    return 0;
}

uint32_t pc_ratetest_tone_hz(const pc_ratetest_t *t, int32_t now_ms)
{
    uint32_t r = pc_ratetest_rate_now(t, now_ms);
    return !r ? 0 : now_ms - t->t0_ms < PC_RT_TONE_MS ? 880 : 1320;
}

static void finish(pc_ratetest_t *t, pc_rt_end_t why)
{
    t->state = PC_RT_DONE;
    t->end = (uint8_t)why;
}

void pc_ratetest_tick(pc_ratetest_t *t, int32_t now_ms)
{
    if (t->state == PC_RT_RUN && now_ms - t->t0_ms >= PC_RT_TIMEOUT_MS) finish(t, PC_RT_END_TIMEOUT);
}

bool pc_ratetest_tap(pc_ratetest_t *t, int32_t now_ms)
{
    if (t->state != PC_RT_RUN || now_ms - t->t0_ms < TONE2_MS) return false;
    t->confirmed = t->tiers[t->tier];
    if (++t->tier >= t->ntiers) { t->tier = t->ntiers - 1; finish(t, PC_RT_END_TOP); }
    else { t->t0_ms = now_ms; t->base_done = 0; }
    return true;
}

bool pc_ratetest_baseline_at(pc_ratetest_t *t, int32_t now_ms)
{
    if (t->state != PC_RT_RUN || t->base_done || now_ms - t->t0_ms < PC_RT_BASELINE_MS) return false;
    t->base_done = 1;
    return true;
}

void pc_ratetest_fault(pc_ratetest_t *t) { if (t->state == PC_RT_RUN) finish(t, PC_RT_END_FAULT); }
void pc_ratetest_leave(pc_ratetest_t *t) { if (t->state == PC_RT_RUN) finish(t, PC_RT_END_LEFT); }

/* the envelope: 0.5 (1 - cos(pi d / n)) over the first and the last n frames (d = the distance to the nearest end, 0 at the first and the last frame) */
static float env(uint32_t pos, uint32_t len, uint32_t n)
{
    uint32_t d = pos < len - 1 - pos ? pos : len - 1 - pos;
    return d >= n ? 1.0f : 0.5f * (1.0f - cosf((float)M_PI * (float)d / (float)n));
}

void pc_ratetest_render(float *lr, int frames, uint32_t rate, double *phase, uint32_t *pos, uint32_t len, double hz)
{
    /* a unit phasor turned by one step per frame (float; the double maths is once per call), back to |1| at the end of the call */
    double w = 2.0 * M_PI * hz / (double)rate, p0 = 2.0 * M_PI * *phase;
    float s = (float)sin(p0), c = (float)cos(p0), sr = (float)sin(w), cr = (float)cos(w);
    uint32_t n = (uint32_t)((uint64_t)rate * RAMP_MS / 1000);
    for (int i = 0; i < frames; i++) {
        float v = 0.0f;
        if (*pos < len) { v = LEVEL * s * env(*pos, len, n); (*pos)++; }
        lr[2 * i] = lr[2 * i + 1] = v;
        float c2 = c * cr - s * sr;
        s = s * cr + c * sr;
        c = c2;
    }
    double ph = *phase + (double)frames * hz / (double)rate;
    *phase = ph - floor(ph);
}
