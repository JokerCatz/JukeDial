// SPDX-License-Identifier: LGPL-2.1-or-later
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "bsp/bsp_math.h"

static void test_sine_1khz_44k1_amplitude_and_phase(void)
{
    int16_t b[2 * 441];
    float ph = 0;
    bsp_math_fill_sine_s16_stereo(b, 441, 44100, 1000, 16000, &ph);
    assert(b[0] == 0 && b[1] == 0);
    int16_t pk = 0; uint16_t rms = 0;
    bsp_math_pcm_level_s16(b, 2 * 441, &pk, &rms);
    assert(pk >= 15900 && pk <= 16000);
    assert(rms > 11000 && rms < 11600);
    assert(fabsf(ph - 10.0f) < 1e-3f); /* 441 frames of 1 kHz @ 44.1 kHz = 10 cycles */
}

static void test_level_of_silence_is_zero(void)
{
    int16_t z[64] = {0}; int16_t pk = 1; uint16_t rms = 1;
    bsp_math_pcm_level_s16(z, 64, &pk, &rms);
    assert(pk == 0 && rms == 0);
}

static void test_level_peak_handles_int16_min(void)
{
    int16_t v[2] = {-32768, 0}; int16_t pk = 0; uint16_t rms = 0;
    bsp_math_pcm_level_s16(v, 2, &pk, &rms);
    assert(pk == 32767);
}

static void test_rail_from_pin_divider(void)
{
    assert(bsp_math_rail_mv_from_pin_mv(1573) == 3932);
    assert(bsp_math_rail_mv_from_pin_mv(0) == 0);
}

static void test_format_scan(void)
{
    char o[32]; uint8_t a[] = {0x15, 0x5A};
    bsp_math_format_scan(o, sizeof o, a, 2); assert(!strcmp(o, "0x15,0x5A"));
    bsp_math_format_scan(o, sizeof o, a, 0); assert(!strcmp(o, "none"));
    char tiny[6];
    bsp_math_format_scan(tiny, sizeof tiny, a, 2); assert(strlen(tiny) < sizeof tiny); /* truncates, never overflows */
}

static void test_floor_div_steps_evenly_through_zero(void)
{
    /* 4 encoder edges per detent: every detent is exactly 4 edges wide, including around 0 */
    assert(bsp_math_floor_div(0, 4) == 0);
    assert(bsp_math_floor_div(3, 4) == 0);
    assert(bsp_math_floor_div(4, 4) == 1);
    assert(bsp_math_floor_div(-1, 4) == -1);
    assert(bsp_math_floor_div(-4, 4) == -1);
    assert(bsp_math_floor_div(-5, 4) == -2);
    assert(bsp_math_floor_div(7, 1) == 7);
}

static void test_has_addr(void)
{
    uint8_t a[] = {0x15, 0x5A};
    assert(bsp_math_has_addr(a, 2, 0x15));
    assert(bsp_math_has_addr(a, 2, 0x5A));
    assert(!bsp_math_has_addr(a, 2, 0x6B));
    assert(!bsp_math_has_addr(a, 0, 0x15));
    assert(!bsp_math_has_addr(a, -1, 0x15));
}

int main(void)
{
    test_floor_div_steps_evenly_through_zero();
    test_has_addr();
    test_sine_1khz_44k1_amplitude_and_phase();
    test_level_of_silence_is_zero();
    test_level_peak_handles_int16_min();
    test_rail_from_pin_divider();
    test_format_scan();
    puts("bsp_math: all tests passed");
    return 0;
}
