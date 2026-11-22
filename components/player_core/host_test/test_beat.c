// SPDX-License-Identifier: LGPL-2.1-or-later
/* pc_beat (ideas G3): synthetic spectra: a click track at 90 / 120 / 150 BPM, silence, noise, a tempo change, jitter, a lost signal, the phase and the CPU. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "pc/pc_beat.h"
#include "pc_test.h"

static uint32_t s_rng = 12345;
static float rnd(void) { s_rng = s_rng * 1664525u + 1013904223u; return (float)(s_rng >> 8) / 16777216.0f; }

/* the bars of a click track at time t: a floor of 0.05 + noise; every beat a burst on the low bars (full) and the mid ones (half) that decays in ~60 ms */
static void bars_click(float bars[64], uint32_t t, float bpm, uint32_t offset_ms, float jitter_ms)
{
    float P = 60000.0f / bpm, since = fmodf((float)(t - offset_ms) + jitter_ms, P);
    float env = since >= 0 ? expf(-since / 60.0f) : 0;
    for (int i = 0; i < 64; i++) {
        float v = 0.05f + 0.02f * rnd();
        if (i < 16) v += 0.85f * env; else if (i < 40) v += 0.5f * env;
        bars[i] = v > 1 ? 1 : v;
    }
}
static float s_jit;
/* run for ms at dt; the click track of `bpm`; returns the events seen */
static int s_onsets, s_ticks;
static void run(pc_beat_t *b, uint32_t from, uint32_t ms, uint32_t dt, float bpm, uint32_t off, float jitter)
{
    float bars[64];
    s_jit = jitter;
    for (uint32_t t = from; t < from + ms; t += dt) {
        float j = jitter > 0 ? (rnd() * 2 - 1) * jitter : 0;
        bars_click(bars, t + dt, bpm, off, j);   /* the frame that ends at the detector's new clock */
        uint32_t ev = pc_beat_step(b, bars, dt);
        s_onsets += (ev & PC_BEAT_ONSET) != 0;
        s_ticks += (ev & PC_BEAT_TICK) != 0;
    }
}
static void silence(pc_beat_t *b, uint32_t ms, uint32_t dt)
{
    float bars[64] = { 0 };
    for (uint32_t t = 0; t < ms; t += dt) { uint32_t ev = pc_beat_step(b, bars, dt); s_onsets += (ev & PC_BEAT_ONSET) != 0; }
}

static void test_tempos(void)
{
    static const float T[] = { 90, 120, 150, 60, 180, 100, 133 };
    for (unsigned k = 0; k < sizeof T / sizeof T[0]; k++) {
        pc_beat_t b;
        pc_beat_init(&b);
        s_onsets = s_ticks = 0;
        run(&b, 0, 12000, 33, T[k], 100, 0);
        CHECK(pc_beat_locked(&b));
        CHECK_NEAR(b.bpm, T[k], 2.0);
        CHECK(b.confidence >= 0.8f);
        CHECK(s_ticks >= (int)(6.0f * T[k] / 60.0f));                        /* the grid ticks about once a beat while locked */
        /* the phase: the next predicted beat is on a true click (offset 100 + k P), +- 45 ms */
        float P = 60000.0f / T[k], nx = (float)pc_beat_next_ms(&b), r = fmodf(nx - 100.0f, P);
        if (r > P / 2) r -= P;
        CHECK(fabsf(r) <= 45.0f);
        CHECK(nx > (float)b.now_ms && nx <= (float)b.now_ms + P + 1.0f);
    }
}
static void test_rates(void)                                                   /* the frame rate may be 20 Hz or 50 Hz too */
{
    static const uint32_t DT[] = { 20, 50 };
    for (int k = 0; k < 2; k++) {
        pc_beat_t b;
        pc_beat_init(&b);
        run(&b, 0, 14000, DT[k], 120, 0, 0);
        CHECK(pc_beat_locked(&b));
        CHECK_NEAR(b.bpm, 120, 3.0);
    }
}
static void test_jitter(void)                                                  /* +-20 ms of jitter on every click */
{
    pc_beat_t b;
    pc_beat_init(&b);
    run(&b, 0, 16000, 33, 110, 0, 20.0f);
    CHECK(pc_beat_locked(&b));
    CHECK_NEAR(b.bpm, 110, 3.0);
}
static void test_silence(void)
{
    pc_beat_t b;
    pc_beat_init(&b);
    s_onsets = 0;
    silence(&b, 20000, 33);
    CHECK_EQ_INT(s_onsets, 0);
    CHECK(!pc_beat_locked(&b));
    CHECK(b.bpm == 0 && b.confidence == 0);
    CHECK_EQ_INT(pc_beat_next_ms(&b), 0);
}
static void test_noise(void)                                                   /* steady random bars: no tempo, never locked */
{
    pc_beat_t b;
    pc_beat_init(&b);
    float bars[64];
    int locked = 0;
    for (int t = 0; t < 60000; t += 33) {
        for (int i = 0; i < 64; i++) bars[i] = 0.3f + 0.3f * rnd();
        pc_beat_step(&b, bars, 33);
        locked += pc_beat_locked(&b);
    }
    CHECK_EQ_INT(locked, 0);
}
static void test_random_hits(void)                                             /* random hits (no pulse): the confidence stays under the bar */
{
    pc_beat_t b;
    pc_beat_init(&b);
    float bars[64];
    uint32_t next = 0;
    int locked = 0, frames = 0;
    for (uint32_t t = 0; t < 120000; t += 33) {
        if (t >= next) { next = t + 200 + (uint32_t)(rnd() * 900); for (int i = 0; i < 64; i++) bars[i] = 0.9f; }
        else for (int i = 0; i < 64; i++) bars[i] = 0.05f + 0.02f * rnd();
        pc_beat_step(&b, bars, 33);
        if (t > 10000) { locked += pc_beat_locked(&b); frames++; }
    }
    CHECK(locked * 12 < frames);                                                 /* random 200..1100 ms hits are the worst case there is: a false lock under 8 % of the time */
}
static void test_tempo_change(void)
{
    pc_beat_t b;
    pc_beat_init(&b);
    run(&b, 0, 10000, 33, 100, 0, 0);
    CHECK_NEAR(b.bpm, 100, 2.0);
    run(&b, 10000, 10000, 33, 140, 0, 0);
    CHECK(pc_beat_locked(&b));
    CHECK_NEAR(b.bpm, 140, 3.0);
}
static void test_lost(void)                                                    /* the music stops: within 4 s no tempo, no lock, no ticks */
{
    pc_beat_t b;
    pc_beat_init(&b);
    run(&b, 0, 10000, 33, 120, 0, 0);
    CHECK(pc_beat_locked(&b));
    float bars[64] = { 0 };
    for (int t = 0; t < 4000; t += 33) pc_beat_step(&b, bars, 33);
    CHECK(!pc_beat_locked(&b));
    CHECK(b.bpm == 0);
    CHECK_EQ_INT(pc_beat_phase(&b) == 0.0f, 1);
}
static void test_latency(void)                                                 /* the grid moves with the feed's latency */
{
    pc_beat_t a, c;
    pc_beat_init(&a); pc_beat_init(&c);
    pc_beat_latency(&c, 80);
    run(&a, 0, 12000, 33, 120, 0, 0);
    run(&c, 0, 12000, 33, 120, 0, 0);
    float d = (float)pc_beat_next_ms(&c) - (float)pc_beat_next_ms(&a);
    d = fmodf(d, 500.0f);
    if (d < 0) d += 500.0f;
    CHECK(fabsf(d - 80.0f) < 2.0f || fabsf(d - 420.0f) < 2.0f);
}
static void test_cpu(void)
{
    pc_beat_t b;
    pc_beat_init(&b);
    float bars[64];
    s_jit = 0;
    const int N = 300000;
    clock_t c0 = clock();
    for (int i = 0; i < N; i++) { bars_click(bars, (uint32_t)i * 33, 120, 0, 0); pc_beat_step(&b, bars, 33); }
    double total = (double)(clock() - c0) / CLOCKS_PER_SEC;
    c0 = clock();
    for (int i = 0; i < N; i++) bars_click(bars, (uint32_t)i * 33, 120, 0, 0);
    double gen = (double)(clock() - c0) / CLOCKS_PER_SEC;
    double ns = (total - gen) / N * 1e9;
    printf("pc_beat step: %.0f ns/step on the host (bars generator %.0f ns excluded); locked=%d bpm=%.1f\n", ns, gen / N * 1e9, pc_beat_locked(&b), b.bpm);
    CHECK(ns < 20000.0);
}

int main(void)
{
    test_tempos();
    test_rates();
    test_jitter();
    test_silence();
    test_noise();
    test_random_hits();
    test_tempo_change();
    test_lost();
    test_latency();
    test_cpu();
    printf("test_beat ok\n");
    return 0;
}
