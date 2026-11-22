// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extstream.h"
#include "pc/pc_extuac.h"
#include "pc_test.h"
#include <stdint.h>

static uint8_t ring[PC_EXTSTREAM_RING_BYTES];
static pc_extstream_t S;

static int put16(uint8_t *d, int16_t l, int16_t r) { d[0] = (uint8_t)l; d[1] = (uint8_t)(l >> 8); d[2] = (uint8_t)r; d[3] = (uint8_t)(r >> 8); return 4; }
static void fill_to_target(int bytes, int16_t l, int16_t r)   /* 20 ms of one constant frame */
{
    static uint8_t b[8 * 4000];
    uint32_t n = pc_extstream_target_frames(&S);
    int fb = 2 * bytes;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *p = b + i * fb;
        if (bytes == 2) put16(p, l, r);
        else if (bytes == 3) { p[0] = 0; p[1] = (uint8_t)l; p[2] = (uint8_t)(l >> 8); p[3] = 0; p[4] = (uint8_t)r; p[5] = (uint8_t)(r >> 8); }   /* the 16 bit value in the top two bytes */
        else { p[0] = p[1] = 0; p[2] = (uint8_t)l; p[3] = (uint8_t)(l >> 8); p[4] = p[5] = 0; p[6] = (uint8_t)r; p[7] = (uint8_t)(r >> 8); }
    }
    CHECK_EQ_INT(pc_extstream_write(&S, b, n * fb), n * fb);
}

static void test_start_volume(void)
{
    CHECK_EQ_INT(pc_extstream_start_volume(100), 20);
    CHECK_EQ_INT(pc_extstream_start_volume(40), 20);
    CHECK_EQ_INT(pc_extstream_start_volume(20), 20);
    CHECK_EQ_INT(pc_extstream_start_volume(7), 7);
    CHECK_EQ_INT(pc_extstream_start_volume(0), 0);
    CHECK_EQ_INT(pc_extstream_start_volume(-5), 0);
    CHECK_EQ_INT(PC_EXTSTREAM_RING_BYTES % 24, 0);
    CHECK_EQ_INT(PC_EXTSTREAM_RING_BYTES, 384000 * 40 / 1000 * 2 * 4);
}

static void test_convert(void)
{
    static float o[2 * 960];
    for (int bytes = 2; bytes <= 4; bytes++) {
        pc_extstream_init(&S, ring, sizeof ring);
        pc_extstream_set_format(&S, 48000, bytes);
        fill_to_target(bytes, 16384, -16384);                           /* +-0.5 */
        pc_extstream_render(&S, o, 100, 1.0f, 1.0f);                    /* the first block after the start ramps up from silence */
        CHECK(fabsf(o[0]) < 0.01f && fabsf(o[2 * 99]) > 0.49f);
        CHECK_EQ_INT(pc_extstream_render(&S, o, 480, 1.0f, 1.0f), 480);
        for (int i = 0; i < 480; i++) { CHECK_NEAR(o[2 * i], 0.5, 1e-6); CHECK_NEAR(o[2 * i + 1], -0.5, 1e-6); }
        CHECK_EQ_INT(pc_extstream_render(&S, o, 380, 0.5f, 0.25f), 380);   /* the gain ramps from 1: the end of the block is at the new gain */
        CHECK_NEAR(o[2 * 379], 0.25, 1e-6); CHECK_NEAR(o[2 * 379 + 1], -0.125, 1e-6);
        CHECK(o[0] < 0.5f && o[0] > 0.25f);
        CHECK_EQ_INT(S.underruns, 0);
    }
}
static void test_full_scale(void)
{
    static float o[2 * 960];
    static uint8_t b[8 * 960];
    pc_extstream_init(&S, ring, sizeof ring);
    pc_extstream_set_format(&S, 48000, 4);
    for (int i = 0; i < 960; i++) {                                    /* INT32_MIN / MAX, and everything between from a counter */
        uint32_t l = i & 1 ? 0x7FFFFFFFu : 0x80000000u, r = i & 1 ? 0x80000000u : 0x7FFFFFFFu;
        if (i > 8) { l = (uint32_t)i * 0x9E3779B9u; r = ~l; }
        memcpy(b + 8 * i, &l, 4); memcpy(b + 8 * i + 4, &r, 4);
    }
    CHECK_EQ_INT(pc_extstream_write(&S, b, sizeof b), sizeof b);
    pc_extstream_render(&S, o, 960, 1.0f, 1.0f);
    for (int i = 0; i < 2 * 960; i++) CHECK(o[i] <= 1.0f && o[i] >= -1.0f);
    CHECK(o[2] > 0.0f && o[0] < 0.0f);                              /* (the first block ramps up from 0: the sign of INT32_MIN / MAX is what is left to see) */
}
static void test_gain_clamped(void)                                     /* a gain above 1, NaN or negative can never be asked for */
{
    static float o[2 * 480];
    float bad[] = { 2.0f, 1e30f, INFINITY, NAN, -1.0f, -INFINITY, 1.0000001f };
    for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        pc_extstream_init(&S, ring, sizeof ring);
        pc_extstream_set_format(&S, 48000, 2);
        fill_to_target(2, 32767, -32768);
        pc_extstream_render(&S, o, 480, bad[k], bad[k]);                  /* the first block of the stream: the gain is taken as is (clamped), no ramp from anything */
        for (int i = 0; i < 2 * 480; i++) CHECK(fabsf(o[i]) <= 1.0f);
        if (!(bad[k] > 1.0f)) { for (int i = 0; i < 2 * 480; i++) CHECK(!isnan(o[i])); }
    }
}
static void test_prime_underrun(void)
{
    static float o[2 * 480];
    static uint8_t b[4 * 480];
    pc_extstream_init(&S, ring, sizeof ring);
    pc_extstream_set_format(&S, 48000, 2);
    for (int i = 0; i < 480; i++) put16(b + 4 * i, 1000, 1000);
    pc_extstream_write(&S, b, sizeof b);                               /* 10 ms: below the 20 ms water level */
    CHECK_EQ_INT(pc_extstream_render(&S, o, 480, 1, 1), 480);
    for (int i = 0; i < 960; i++) CHECK(o[i] == 0.0f);                 /* waiting for the level: silence, not an underrun */
    CHECK_EQ_INT(S.underruns, 0);
    CHECK_EQ_INT(pc_extstream_fill_frames(&S), 480);                   /* nothing was consumed */
    pc_extstream_write(&S, b, sizeof b);
    CHECK_EQ_INT(pc_extstream_render(&S, o, 480, 1, 1), 480);          /* 20 ms there: plays */
    CHECK(o[0] > 0.0f);
    CHECK_EQ_INT(pc_extstream_render(&S, o, 480, 1, 1), 480);
    CHECK(o[0] > 0.0f);
    CHECK_EQ_INT(S.underruns, 0);
    pc_extstream_write(&S, b, 4 * 100);                                /* 100 frames left for a 480 block: an underrun, the rest silent, the full count returned */
    CHECK_EQ_INT(pc_extstream_render(&S, o, 480, 1, 1), 480);
    CHECK_EQ_INT(S.underruns, 1);
    CHECK(o[2 * 98] > 0.0f && o[2 * 99] == 0.0f);   /* the last 64 frames fade out to zero, no cut */
    CHECK(o[2 * 100] == 0.0f && o[2 * 479] == 0.0f);
    CHECK_EQ_INT(pc_extstream_render(&S, o, 480, 1, 1), 480);          /* and the level is waited for again: silence, still one underrun */
    CHECK_EQ_INT(S.underruns, 1); CHECK(o[0] == 0.0f);
    CHECK_EQ_INT(pc_extstream_render(&S, NULL, 0, 1, 1), 0);
}
static void test_overrun_and_wrap(void)
{
    static float o[2 * 3840];
    static uint8_t b[4 * 1200];
    pc_extstream_init(&S, ring, sizeof ring);
    pc_extstream_set_format(&S, 48000, 2);
    for (int i = 0; i < 1200; i++) put16(b + 4 * i, (int16_t)(i + 1), (int16_t)(-i - 1));
    for (int k = 0; k < 3; k++) pc_extstream_write(&S, b, sizeof b);   /* 3600 frames > 35 ms (1680): above the mark, nothing is playing yet: dropped, not counted */
    CHECK(pc_extstream_fill_frames(&S) <= 1680);
    CHECK_EQ_INT(S.overruns, 0);
    pc_extstream_render(&S, o, 480, 1, 1);                             /* primes */
    pc_extstream_write(&S, b, sizeof b);
    CHECK(S.overruns > 0); CHECK(S.drops > 0);
    CHECK(pc_extstream_fill_frames(&S) <= 1680);
    /* a rate change while playing (48k -> 44.1k, the mixer restarting, the reader not rendering): the writer fills past the mark before the reader has taken the flush: waiting, not an overrun */
    pc_extstream_set_format(&S, 44100, 2);
    for (int k = 0; k < 4; k++) pc_extstream_write(&S, b, sizeof b);
    CHECK_EQ_INT(S.overruns, 0 + S.overruns); { uint32_t ov = S.overruns, dr = S.drops; for (int k = 0; k < 4; k++) pc_extstream_write(&S, b, sizeof b); CHECK_EQ_INT(S.overruns, ov); CHECK_EQ_INT(S.drops, dr); }
    /* order across the ring's end: a long run in odd chunk sizes, every frame read back in sequence */
    pc_extstream_init(&S, ring, sizeof ring);
    pc_extstream_set_format(&S, 44100, 3);
    uint32_t next = 0, want = 0;
    uint8_t c[6 * 333];
    for (int round = 0; round < 400; round++) {
        for (int f = 0; f < 333; f++, next++) { uint8_t *p = c + 6 * f; p[0] = (uint8_t)next; p[1] = (uint8_t)(next >> 8); p[2] = 0; p[3] = (uint8_t)next; p[4] = (uint8_t)(next >> 8); p[5] = 0; }
        CHECK_EQ_INT(pc_extstream_write(&S, c, sizeof c), sizeof c);
        while ((S.primed && pc_extstream_fill_frames(&S) >= 441) || (!S.primed && pc_extstream_fill_frames(&S) >= 882)) {
            bool first = !S.primed;                                     /* the first block after a start ramps up from 0: only the order of the later ones is checked */
            pc_extstream_render(&S, o, 441, 1, 1);
            for (int i = 0; i < 441; i++, want++) {
                if (first) continue;
                int32_t v = (int32_t)(want & 0xFFFFu);                    /* the 24 bit sample: the counter's low 16 bits, the top byte 0 */
                CHECK_NEAR(o[2 * i], (double)v / 8388608.0, 1e-9);
            }
        }
    }
    CHECK(want > 100000);
    CHECK_EQ_INT(S.underruns, 0);
}
static void test_format_switch_wrap(void)              /* 24 bit -> 16 bit -> 32 bit with the ring's indexes far from a frame boundary of the new format: a sample straddles the end, ASan must see no read past it */
{
    static float o[2 * 4000];
    static uint8_t b[8 * 4000];
    pc_extstream_init(&S, ring, sizeof ring);
    static const int fmts[3] = { 3, 2, 4 };
    uint32_t seq = 0;
    for (int round = 0; round < 6; round++) {
        int by = fmts[round % 3];
        pc_extstream_set_format(&S, 96000, by);
        for (int blk = 0; blk < 700; blk++) {                          /* enough to go round the ring several times */
            int fr = 960;
            for (int i = 0; i < fr; i++) for (int c = 0; c < 2; c++) { uint8_t *p = b + (i * 2 + c) * by; for (int k = 0; k < by; k++) p[k] = (uint8_t)(seq++ * 7 + k); }
            pc_extstream_write(&S, b, (uint32_t)fr * 2u * (uint32_t)by);
            pc_extstream_render(&S, o, 960, 1.0f, 1.0f);
            for (int i = 0; i < 2 * 960; i++) CHECK(o[i] <= 1.0f && o[i] >= -1.0f);
        }
        pc_extstream_write(&S, b, 7u * 2u * (uint32_t)by);               /* an odd number of frames before the switch: the read index is not a multiple of the next format's frame size */
        pc_extstream_render(&S, o, 7, 1.0f, 1.0f);
    }
}
static void test_format_change(void)
{
    static float o[2 * 960];
    pc_extstream_init(&S, ring, sizeof ring);
    pc_extstream_set_format(&S, 48000, 2);
    fill_to_target(2, 8000, 8000);
    pc_extstream_render(&S, o, 480, 1, 1);
    CHECK(o[0] > 0);
    pc_extstream_set_format(&S, 96000, 4);                             /* a new format: what is in the ring is the old format's, the reader drops it */
    pc_extstream_render(&S, o, 960, 1, 1);
    for (int i = 0; i < 1920; i++) CHECK(o[i] == 0.0f);
    CHECK_EQ_INT(pc_extstream_fill_frames(&S), 0);
    pc_extstream_set_format(&S, 0, 0);                                 /* no stream */
    CHECK_EQ_INT(pc_extstream_write(&S, (const uint8_t *)"01234567", 8), 0);
    CHECK_EQ_INT(pc_extstream_render(&S, o, 480, 1, 1), 480); CHECK(o[0] == 0.0f);
    pc_extstream_set_format(&S, 48000, 5);                             /* not a format */
    CHECK_EQ_INT(pc_extstream_rate(&S), 0);
    pc_extstream_set_format(&S, 384001, 4);
    CHECK_EQ_INT(pc_extstream_rate(&S), 0);
    pc_extstream_set_format(&S, 384000, 4);                            /* the largest: 35 ms fits the ring */
    static uint8_t big[8 * 13440];
    CHECK_EQ_INT(pc_extstream_write(&S, big, sizeof big), sizeof big);
    CHECK_EQ_INT(pc_extstream_write(&S, big, sizeof big), 0);          /* the mark */
    pc_extstream_flush(&S);
    pc_extstream_render(&S, o, 100, 1, 1);
    CHECK_EQ_INT(pc_extstream_fill_frames(&S), 0);
}

/* ---- the feedback controller against a model: a host that follows the last value it read (5 ms late, whole frames per millisecond), a device whose clock is off by ppm
 * and pulls 10 ms blocks, a pump that runs about every millisecond with +-0.3 ms of jitter ---- */
typedef struct { double fill_min, fill_max, fill_mean, fb_pp, fb_mean, fb_sec_dev, fill_sec_dev; uint32_t under, over; } fbres_t;   /* *_sec_dev: the largest deviation of a one second mean from the overall mean (slow swing) */
static fbres_t run_fb(uint32_t rate, double ppm, int secs, int stall_at_ms, int stall_ms)
{
    static uint8_t zeros[8 * 4000];
    static float o[2 * 3840];
    pc_extstream_t s;
    pc_extfb_t f;
    pc_extstream_init(&s, ring, sizeof ring);
    pc_extstream_set_format(&s, rate, 4);
    pc_extfb_init(&f, rate);
    double acc = 0, blk_period = 10000.0 / (1.0 + ppm * 1e-6), next_block = blk_period;   /* us of the host's clock between two block reads of the device */
    uint32_t seed = 12345;
    fbres_t r = { 1e30, -1e30, 0, 0, 0, 0, 0, 0, 0 };
    double secfb[10] = { 0 }, secfill[10] = { 0 };
    double sum = 0, fbsum = 0, fbmin = 1e30, fbmax = -1e30;
    long cnt = 0;
    int blk = (int)(rate / 100);
    uint32_t value = f.nominal, held[40];                              /* the value the host acts on is the pump's value 5 ms ago: 40 micro-frames */
    for (int i = 0; i < 40; i++) held[i] = f.nominal;
    uint64_t next_pump = 500;
    for (long uf = 0; uf < (long)secs * 8000; uf++) {                  /* one step = one micro-frame of 125 us */
        uint64_t t = (uint64_t)uf * 125u;
        uint32_t fbv = held[uf % 40];
        if (!(t / 1000 >= (uint64_t)stall_at_ms && t / 1000 < (uint64_t)(stall_at_ms + stall_ms))) {
            acc += (double)fbv / 65536.0;
            uint32_t n = (uint32_t)acc;
            acc -= n;
            if (n) pc_extstream_write(&s, zeros, n * 8);
        }
        while (next_block <= (double)t + 125.0) { pc_extstream_render(&s, o, blk, 1.0f, 1.0f); next_block += blk_period; }
        if (t + 125 > next_pump) {
            seed = seed * 1664525u + 1013904223u;
            value = pc_extfb_update(&f, next_pump, pc_extstream_fill_frames(&s), pc_extstream_playing(&s));
            next_pump += 1000u + ((seed >> 16) % 600u) - 300u;          /* about every millisecond, +-0.3 ms */
        }
        held[uf % 40] = value;
        if (uf >= (long)(secs - 10) * 8000 && uf % 8 == 0) {            /* the last 10 s, sampled every ms */
            double fr = pc_extstream_fill_frames(&s);
            if (fr < r.fill_min) r.fill_min = fr;
            if (fr > r.fill_max) r.fill_max = fr;
            sum += fr; fbsum += value; cnt++;
            secfb[(uf - (long)(secs - 10) * 8000) / 8000] += value / 1000.0; secfill[(uf - (long)(secs - 10) * 8000) / 8000] += fr / 1000.0;
            if (value < fbmin) fbmin = value;
            if (value > fbmax) fbmax = value;
        }
    }
    r.fill_mean = sum / (double)cnt;
    r.fb_mean = fbsum / (double)cnt;
    r.fb_pp = fbmax - fbmin;
    for (int i = 0; i < 10; i++) {
        if (fabs(secfb[i] - r.fb_mean) > r.fb_sec_dev) r.fb_sec_dev = fabs(secfb[i] - r.fb_mean);
        if (fabs(secfill[i] - r.fill_mean) > r.fill_sec_dev) r.fill_sec_dev = fabs(secfill[i] - r.fill_mean);
    }
    r.under = s.underruns;
    r.over = s.overruns;
    return r;
}
static void test_feedback(void)
{
    static const uint32_t rates[] = { 44100, 48000, 96000, 192000, 384000 };
    static const double ppms[] = { 0, 500, -500 };
    for (unsigned a = 0; a < sizeof rates / sizeof rates[0]; a++)
        for (unsigned b = 0; b < sizeof ppms / sizeof ppms[0]; b++) {
            uint32_t rate = rates[a];
            fbres_t r = run_fb(rate, ppms[b], 40, -1, 0);
            double target = rate * 0.020, blkf = rate / 100.0;
            double nom = (double)pc_extfb_nominal(rate);
            printf("fb rate=%u ppm=%+.0f fill min=%.0f mean=%.0f max=%.0f (target %.0f) fb mean=%.1f nominal=%.0f pp=%.1f sec_dev=%.1f fill_sec_dev=%.1f under=%u over=%u\n", (unsigned)rate, ppms[b], r.fill_min,
                   r.fill_mean, r.fill_max, target, r.fb_mean, nom, r.fb_pp, r.fb_sec_dev, r.fill_sec_dev, r.under, r.over);
            CHECK_EQ_INT(r.under, 0);
            CHECK_EQ_INT(r.over, 0);
            CHECK(fabs(r.fill_mean - target) < 0.02 * target);   /* converged: the pump averages the fill over exactly one block, so the sawtooth of the 10 ms reads averages to the level */
            CHECK(r.fill_max - r.fill_min < 1.2 * blkf + 0.05 * target);       /* the spread is the sawtooth of the block reads, nothing wandering */
            CHECK(fabs(r.fb_mean - nom * (1.0 + ppms[b] * 1e-6)) < nom * 60e-6);   /* the value the host is told follows the device clock (500 ppm off nominal) to 60 ppm */
            CHECK(r.fb_pp < nom * 0.006);                                       /* the ripple (jitter of the pump's samples of a sawtooth) stays under 0.6 % of nominal */
            CHECK(r.fb_sec_dev < nom * 150e-6);                                 /* no slow swing: the one second means of the last 10 s sit within 150 ppm of each other */
            CHECK(r.fill_sec_dev < 0.01 * target);                              /* and the level itself does not wander: within 1 % */
        }
}
static void test_feedback_recovery(void)
{
    fbres_t r = run_fb(48000, 300, 40, 12000, 80);                      /* the host stops for 80 ms at 12 s: an underrun is counted, and the loop is back within seconds */
    printf("fb stall under=%u over=%u fill mean=%.0f\n", r.under, r.over, r.fill_mean);
    CHECK(r.under >= 1 && r.under <= 3);
    CHECK(fabs(r.fill_mean - 960) < 0.04 * 960);
    r = run_fb(48000, 300, 40, 12000, 6000);                            /* a 6 s stall (a host that paused the stream): no wind-up, so no flood and no overrun train when it comes back */
    printf("fb stall6 under=%u over=%u fill mean=%.0f\n", r.under, r.over, r.fill_mean);
    CHECK(r.over <= 3);
    CHECK(fabs(r.fill_mean - 960) < 0.04 * 960);
    CHECK(pc_extfb_nominal(44100) == 361267 && pc_extfb_nominal(48000) == 393216);
}
static void test_feedback_clamp(void)
{
    pc_extfb_t f;
    pc_extfb_init(&f, 48000);
    uint64_t t = 1000;
    for (int i = 0; i < 12000; i++) { t += 1000; pc_extfb_update(&f, t, 0, false); }                 /* a ring that stays empty for 12 s, nothing playing: no wind-up, only the P part (0.24 sample), the I part is still 0 */
    CHECK(f.value > f.nominal && f.value < f.nominal + 20000 && f.integ == 0.0f);
    for (int i = 0; i < 4000; i++) { t += 1000; pc_extfb_update(&f, t, 960, true); }                  /* at the level: back to nominal at once */
    CHECK(llabs((long long)f.value - (long long)f.nominal) < 100);
    for (int i = 0; i < 4000; i++) { t += 1000; pc_extfb_update(&f, t, 5000, true); }
    CHECK(f.value >= f.nominal - 65536 && f.value <= f.nominal + 65536);
    for (int i = 0; i < 20000; i++) { t += 1000; pc_extfb_update(&f, t, 5000, true); }
    CHECK_EQ_INT(f.value, f.nominal - 65536);
}
int main(void)
{
    RUN(test_start_volume); RUN(test_convert); RUN(test_full_scale); RUN(test_gain_clamped); RUN(test_prime_underrun); RUN(test_overrun_and_wrap);
    RUN(test_format_switch_wrap); RUN(test_format_change); RUN(test_feedback_clamp); RUN(test_feedback); RUN(test_feedback_recovery);
    return 0;
}
