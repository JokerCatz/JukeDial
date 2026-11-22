// SPDX-License-Identifier: LGPL-2.1-or-later
/* The tuner's pure parts (pc_tuner): the pitch detector's accuracy, the note and cents arithmetic, the reference tone. */
#include "pc/pc_tuner.h"
#include "pc_test.h"

#define RATE 16000
static int16_t s_buf[2048];
static float s_scr[PC_PITCH_SCRATCH];

static double cents_of(double a, double b) { return 1200.0 * log2(a / b); }
static void gen(double hz, double amp, int harmonics, unsigned seed, double noise)
{
    double ph = 0.37;
    unsigned r = seed;
    for (int i = 0; i < 2048; i++) {
        double y = 0;
        for (int h = 1; h <= harmonics; h++) y += sin(2 * M_PI * ph * h) / h;
        r = r * 1664525u + 1013904223u;
        y = y * amp + noise * ((double)(r >> 8) / 8388608.0 - 1.0);
        s_buf[i] = (int16_t)lrint(y);
        ph += hz / RATE;
        if (ph >= 1) ph -= 1;
    }
}
static float detect(void)
{
    pc_pitch_t p;
    return pc_pitch_detect(s_buf, 2048, RATE, s_scr, &p) ? p.hz : 0.0f;
}
static void test_sines(void)
{
    static const double hz[] = { 41.2, 55.0, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 440.0, 523.25, 880.0, 1046.5, 1760.0, 1975.5 };
    double worst = 0;
    for (unsigned k = 0; k < sizeof hz / sizeof *hz; k++) {
        gen(hz[k], 8000, 1, 1, 0);
        float f = detect();
        CHECK(f > 0);
        double c = fabs(cents_of(f, hz[k]));
        if (c > worst) worst = c;
        if (c > 3.0) fprintf(stderr, "sine %g -> %g (%.2f cents)\n", hz[k], f, c);
        CHECK(c <= 3.0);
    }
    printf("worst sine error %.2f cents\n", worst);
}
static void test_offsets(void)
{
    const double base = 440.0;
    static const double off[] = { 7, -12, 30, -45, 3, -3 };
    for (unsigned k = 0; k < sizeof off / sizeof *off; k++) {
        double hz = base * pow(2.0, off[k] / 1200.0);
        gen(hz, 6000, 1, 2, 0);
        float f = detect();
        float cents;
        int n = pc_note_from_hz(f, 440.0f, &cents);
        CHECK_EQ_INT(n, 69);
        CHECK_NEAR(cents, off[k], 2.0);
    }
}
static void test_guitar_harmonics(void)       /* a plucked string: strong overtones, the fundamental still wins */
{
    static const double hz[] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63 };
    for (unsigned k = 0; k < sizeof hz / sizeof *hz; k++) {
        gen(hz[k], 4000, 8, 3, 150);
        float f = detect();
        CHECK(f > 0);
        CHECK(fabs(cents_of(f, hz[k])) <= 4.0);
    }
}
static void test_noise_and_silence(void)
{
    for (unsigned seed = 1; seed < 20; seed++) {
        gen(100, 0, 1, seed, 6000);
        CHECK(detect() == 0.0f);
    }
    memset(s_buf, 0, sizeof s_buf);
    CHECK(detect() == 0.0f);
    gen(440, 20, 1, 5, 4);                     /* below the gate */
    CHECK(detect() == 0.0f);
}
static void test_notes(void)
{
    char b[8];
    float c;
    CHECK_EQ_INT(pc_note_from_hz(440.0f, 440.0f, &c), 69);
    CHECK_NEAR(c, 0, 0.01);
    CHECK_STR(pc_note_name(69, b, sizeof b), "A4");
    CHECK_STR(pc_note_name(40, b, sizeof b), "E2");
    CHECK_STR(pc_note_name(61, b, sizeof b), "C#4");
    CHECK_EQ_INT(pc_note_from_hz(82.41f, 440.0f, &c), 40);
    CHECK_NEAR(pc_note_hz(40, 440.0f), 82.407, 0.01);
    CHECK_NEAR(pc_note_hz(69, 432.0f), 432.0, 1e-3);
    CHECK_EQ_INT(pc_note_from_hz(440.0f, 432.0f, &c), 69);   /* the reference moves the cents: 440 against 432 is +31.8 */
    CHECK_NEAR(c, 31.77, 0.1);
    CHECK(pc_in_tune(5.0f) && pc_in_tune(-5.0f) && !pc_in_tune(5.5f) && !pc_in_tune(-6.0f));
}
static void test_tone(void)
{
    static float blk[2 * 4800];
    pc_tone_t t;
    memset(&t, 0, sizeof t);
    CHECK(!pc_tone_active(&t));
    CHECK(!pc_tone_mix(&t, blk, 100, 48000, 1.0f));
    for (int w = 0; w < PC_TONE_WAVES; w++) {
        memset(&t, 0, sizeof t);
        pc_tone_set(&t, w, 220.0f, true);
        memset(blk, 0, sizeof blk);
        CHECK(pc_tone_mix(&t, blk, 4800, 48000, 1.0f));
        float pk = 0;
        for (int i = 0; i < 9600; i++) { float a = fabsf(blk[i]); if (a > pk) pk = a; }
        CHECK(pk > 0.15f && pk <= PC_TONE_PEAK * 1.1f);
        CHECK(fabsf(blk[0]) < 0.002f);                          /* the ramp: no click */
        /* pitch: count rising zero crossings of the second half */
        int cr = 0;
        for (int i = 2401; i < 4800; i++) if (blk[2 * (i - 1)] < 0.0f && blk[2 * i] >= 0.0f) cr++;
        CHECK(cr >= 10 && cr <= 12);                      /* 0.05 s of 220 Hz = 11 */
        pc_tone_set(&t, w, 220.0f, false);
        pc_tone_mix(&t, blk, 4800, 48000, 1.0f);
        CHECK(!pc_tone_active(&t));
    }
    /* music at full scale is never pushed beyond itself */
    memset(&t, 0, sizeof t);
    pc_tone_set(&t, PC_TONE_SQ50, 100.0f, true);
    for (int i = 0; i < 2 * 4800; i++) blk[i] = 1.0f;
    pc_tone_mix(&t, blk, 4800, 48000, 1.0f);
    for (int i = 0; i < 2 * 4800; i++) CHECK(blk[i] <= 1.0f);
    /* every wave read back by the detector: the tone is exact within a cent */
    for (int w = 0; w < PC_TONE_WAVES; w++) {
        static const float hz[] = { 110.0f, 261.63f, 880.0f };
        for (unsigned k = 0; k < 3; k++) {
            memset(&t, 0, sizeof t);
            pc_tone_set(&t, w, hz[k], true);
            for (int i = 0; i < 2048; i++) {
                float f[2] = { 0, 0 };
                pc_tone_mix(&t, f, 1, RATE, 1.0f);
                s_buf[i] = (int16_t)lrintf(f[0] * 32767.0f * 2.0f);
            }
            float f = detect();
            CHECK(f > 0);
            CHECK(fabs(cents_of(f, hz[k])) <= 3.0);
        }
    }
}
int main(void)
{
    RUN(test_notes);
    RUN(test_sines);
    RUN(test_offsets);
    RUN(test_guitar_harmonics);
    RUN(test_noise_and_silence);
    RUN(test_tone);
    return 0;
}
