// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_failstreak.h"
#include "pc/pc_mix.h"
#include "pc_test.h"

static void test_mix(void)
{
    float in[4] = { 1.5f, -1.5f, 0.5f, 0.0f };
    int16_t out[4];
    pc_f32_to_s16(in, out, 4);
    CHECK_EQ_INT(out[0], 32767);
    CHECK_EQ_INT(out[1], -32768);
    CHECK_NEAR(out[2], 16384, 1);
    CHECK_EQ_INT(out[3], 0);
    float acc[2] = { 0.1f, 0.2f }, src[2] = { 1.0f, -1.0f };
    pc_mix_add(acc, src, 2, 0.5f);
    CHECK_NEAR(acc[0], 0.6, 1e-6);
    CHECK_NEAR(acc[1], -0.3, 1e-6);
    pc_gain(acc, 2, 2.0f);
    CHECK_NEAR(acc[0], 1.2, 1e-6);
}

static void test_s32_round_clip(void)
{
    const float in[8] = { 0.0f, 1.0f, -1.0f, 0.5f, 2.0f, -2.0f, 1.0f / 8388608.0f, 0.6f / 8388608.0f };
    const int32_t want[8] = { 0, 0x7FFFFF00, (int32_t)0x80000000, 0x40000000, 0x7FFFFF00, (int32_t)0x80000000, 0x100, 0x100 };
    int32_t out[8];
    pc_f32_to_s32(in, out, 8);
    for (int i = 0; i < 8; i++) {
        CHECK_EQ_INT(out[i], want[i]);
        CHECK_EQ_INT(out[i] & 0xFF, 0);
    }
}

static void test_tpdf_bounds(void)
{
    enum { N = 100000 };
    static float in[N];
    static int16_t out[N];
    for (int i = 0; i < N; i++) in[i] = 0.25f;
    pc_dither_t d;
    pc_dither_init(&d, 0);
    pc_f32_to_s16_tpdf(&d, in, out, N);
    double sum = 0;
    int n8191 = 0, n8193 = 0, bad = 0;
    for (int i = 0; i < N; i++) {
        if (out[i] < 8191 || out[i] > 8193) bad++;
        if (out[i] == 8191) n8191++;
        if (out[i] == 8193) n8193++;
        sum += out[i] - 8191.75;
    }
    CHECK_EQ_INT(bad, 0);
    CHECK(n8191 > 0);
    CHECK(n8193 > 0);
    CHECK_NEAR(sum / N, 0.0, 0.05);
}

static void test_tpdf_bounds_neg(void)
{
    enum { N = 100000 };
    static float in[N];
    static int16_t out[N];
    for (int i = 0; i < N; i++) in[i] = -0.25f;
    pc_dither_t d;
    pc_dither_init(&d, 0);
    pc_f32_to_s16_tpdf(&d, in, out, N);
    double sum = 0;
    int n8191 = 0, n8193 = 0, bad = 0;
    for (int i = 0; i < N; i++) {
        if (out[i] < -8193 || out[i] > -8191) bad++;
        if (out[i] == -8191) n8191++;
        if (out[i] == -8193) n8193++;
        sum += out[i] + 8191.75;
    }
    CHECK_EQ_INT(bad, 0);
    CHECK(n8191 > 0);
    CHECK(n8193 > 0);
    CHECK_NEAR(sum / N, 0.0, 0.05);
}

static void test_tpdf_silence(void)
{
    float in[960] = { 0 };
    int16_t out[960];
    for (int i = 0; i < 960; i++) out[i] = 77;
    pc_dither_t d;
    pc_dither_init(&d, 0);
    uint32_t s0 = d.s;
    pc_f32_to_s16_tpdf(&d, in, out, 960);
    int nz = 0;
    for (int i = 0; i < 960; i++) nz += out[i] != 0;
    CHECK_EQ_INT(nz, 0);
    CHECK(d.s == s0);
    in[500] = 0.25f;
    pc_f32_to_s16_tpdf(&d, in, out, 960);
    CHECK(d.s != s0);
}

static void test_tpdf_clip(void)
{
    const float in[3] = { 1.5f, -1.5f, -1.0f };
    int16_t out[3];
    pc_dither_t d;
    pc_dither_init(&d, 1);
    pc_f32_to_s16_tpdf(&d, in, out, 3);
    CHECK_EQ_INT(out[0], 32767);
    CHECK_EQ_INT(out[1], -32768);
    CHECK(out[2] == -32767 || out[2] == -32768);
}

static void test_rs_ratio(void)
{
    pc_rs_t rs;
    pc_rs_init(&rs, 44100, 48000);
    static float in[2 * 4410], out[2 * 6000];
    for (int i = 0; i < 4410; i++) in[2 * i] = in[2 * i + 1] = (float)i / 4410.0f;
    int consumed = 0;
    int n = pc_rs_process(&rs, in, 4410, out, 6000, &consumed);
    CHECK_EQ_INT(consumed, 4410);
    CHECK_NEAR(n, 4800, 1);
    for (int i = 1; i < n; i++) CHECK(out[2 * i] >= out[2 * i - 2] - 1e-6f);   /* a ramp stays a ramp */
    pc_rs_init(&rs, 48000, 48000);
    n = pc_rs_process(&rs, in, 100, out, 6000, &consumed);
    CHECK_NEAR(n, 100, 1);
}

static void test_rs_output_limit(void)
{
    pc_rs_t rs;
    pc_rs_init(&rs, 44100, 48000);
    static float in[2 * 1000], out[2 * 100];
    int consumed = 0;
    int n = pc_rs_process(&rs, in, 1000, out, 100, &consumed);
    CHECK_EQ_INT(n, 100);
    CHECK(consumed < 1000 && consumed > 80);
}

static void test_failstreak(void)
{
    pc_failstreak_t f = { 0 };
    for (int i = 0; i < 4; i++) CHECK(!pc_failstreak_record(&f, false));
    CHECK(pc_failstreak_record(&f, false));
    pc_failstreak_record(&f, true);
    for (int i = 0; i < 4; i++) CHECK(!pc_failstreak_record(&f, false));
    pc_failstreak_record(&f, true);
    CHECK(!pc_failstreak_record(&f, false));
}

int main(void)
{
    RUN(test_mix);
    RUN(test_rs_ratio);
    RUN(test_rs_output_limit);
    RUN(test_failstreak);
    RUN(test_s32_round_clip);
    RUN(test_tpdf_bounds);
    RUN(test_tpdf_bounds_neg);
    RUN(test_tpdf_silence);
    RUN(test_tpdf_clip);
    return 0;
}
