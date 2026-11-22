// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_spectrum.h"
#include "pc_test.h"

static void test_sine_peak(void)
{
    static float mono[1024];
    for (int i = 0; i < 1024; i++) mono[i] = 0.5f * sinf(2.0f * 3.14159265f * 1000.0f * (float)i / 48000.0f);
    float bars[64];
    pc_spectrum_bars(mono, 1024, 48000, bars);
    int best = 0;
    for (int i = 1; i < 64; i++) if (bars[i] > bars[best]) best = i;
    float lo, hi;
    pc_spectrum_band(best, &lo, &hi);
    CHECK(lo <= 1000.0f * 1.06f && hi >= 1000.0f / 1.06f);   /* the peak band holds 1 kHz (one-bin leakage allowed) */
    CHECK(bars[best] > 0.7f);
    for (int i = 0; i < 64; i++) CHECK(bars[i] >= 0.0f && bars[i] <= 1.0f);
}

static void test_silence(void)
{
    static float mono[1024];
    float bars[64];
    pc_spectrum_bars(mono, 1024, 44100, bars);
    for (int i = 0; i < 64; i++) CHECK(bars[i] < 0.01f);
}

static void test_db_floor(void)
{
    static float mono[1024];
    for (int i = 0; i < 1024; i++) mono[i] = 0.03f * sinf(2.0f * 3.14159265f * 1000.0f * (float)i / 48000.0f);   /* about -30 dBFS */
    float b60[64], b48[64], b36[64], bd[64];
    pc_spectrum_bars(mono, 1024, 48000, b60);
    pc_spectrum_bars_db(mono, 1024, 48000, -60.0f, bd);
    /* golden values: the output of pc_spectrum_bars before the _db refactor (commit 8edeb12), same signal, printed with %a */
    static const float golden[64] = {
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x1.ed5e0ap-2f, 0x1.cc5b4p-2f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f,
        0x0p+0f, 0x0p+0f, 0x0p+0f, 0x0p+0f
    };
    /* a tolerance, not bit-equality: last-bit results of sinf/log10f depend on the host libm and on FMA contraction */
    for (int i = 0; i < 64; i++) CHECK_NEAR(b60[i], golden[i], 1e-6);
    for (int i = 0; i < 64; i++) CHECK(b60[i] == bd[i]);
    pc_spectrum_bars_db(mono, 1024, 48000, -48.0f, b48);
    pc_spectrum_bars_db(mono, 1024, 48000, -36.0f, b36);
    int best = 0;
    for (int i = 1; i < 64; i++) if (b60[i] > b60[best]) best = i;
    CHECK(b60[best] > 0.4f && b60[best] < 0.6f);
    CHECK(b48[best] < b60[best]);
    CHECK(b36[best] < b48[best]);
    float e[2] = { 0.0f, -200.0f };
    for (int k = 0; k < 2; k++) {
        pc_spectrum_bars_db(mono, 1024, 48000, e[k], bd);
        for (int i = 0; i < 64; i++) CHECK(bd[i] >= 0.0f && bd[i] <= 1.0f);
    }
}

/* the contract is n = 1024 (a power of two): anything else gives all-zero bars and never touches the FFT buffers */
static void test_invalid_n(void)
{
    static float mono[1024];
    for (int i = 0; i < 1024; i++) mono[i] = 0.5f * sinf(0.3f * (float)i);
    const int bad[] = { 0, 1, 3, 1000, -4 };
    for (size_t t = 0; t < sizeof bad / sizeof *bad; t++) {
        float bars[64];
        for (int i = 0; i < 64; i++) bars[i] = 0.5f;
        pc_spectrum_bars(mono, bad[t], 48000, bars);
        for (int i = 0; i < 64; i++) CHECK(bars[i] == 0.0f);
    }
}

int main(void)
{
    RUN(test_db_floor);
    RUN(test_sine_peak);
    RUN(test_silence);
    RUN(test_invalid_n);
    return 0;
}
